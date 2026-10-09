// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <format>
#include <span>
#include <cmath>
#include <cstring>
#include <utility>
#include "ggml-cuda.h"
#include "llama.h"

#include "llama_sb.h"
#include "llama-ext.h"
#include "logging.h"
#include "string_utils.h"

constexpr int MAX_REPEAT = 50;

static bool read_vram(size_t &used, size_t &total) {
  size_t free = 0;
  total = 0;
#ifdef GGML_USE_CUDA
  ggml_backend_cuda_get_device_memory(0, &free, &total);
  if (total > 0) {
    used = total - free;
    return true;
  }
#endif
  return false;
}

LlamaIter::LlamaIter() :
  _llama(nullptr),
  _repetition_count(0),
  _tokens_generated(0),
  _has_next(false) {
}

LlamaIter::LlamaIter(LlamaIter &&other) noexcept
  : _llama(std::exchange(other._llama, nullptr))
  , _last_word(std::move(other._last_word))
  , _t_start(std::move(other._t_start))
  , _repetition_count(other._repetition_count)
  , _tokens_generated(other._tokens_generated)
  , _has_next(other._has_next) {
}

Llama::Llama() :
  _model(nullptr),
  _ctx(nullptr),
  _sampler(nullptr),
  _vocab(nullptr),
  _penalty_last_n(0),
  _penalty_repeat(0),
  _penalty_freq(0.0f),
  _penalty_present(0.0f),
  _temperature(0),
  _top_p(0),
  _min_p(0),
  _top_k(0),
  _max_tokens(0),
  _log_level(GGML_LOG_LEVEL_CONT),
  _n_gpu_layers(0),
  _n_system_tokens(0),
  _tokens_physically_used(0),
  _is_gemma4(false),
  _sampler_dirty(false),
  _can_shift(false),
  _memory_flush(false),
  _seed(LLAMA_DEFAULT_SEED),
  _ctx_mtp(nullptr),
  _n_mtp_layers(0),
  _mtp_enabled(false),
  _mtp_n_max(3),
  _mtp_n_min(1),
  _mtp_p_min(0.9f),
  _n_embd(0),
  _verify_h_rows(0),
  _batch_mtp(nullptr),
  _sampler_mtp(nullptr) {
  llama_log_set([](enum ggml_log_level level, const char *text, void *user_data) {
    Llama *llama = static_cast<Llama *>(user_data);
    if (level == GGML_LOG_LEVEL_ERROR && llama->_last_error.empty()) {
      // remember the first error message
      llama->_last_error = text;
    }
    if (level > llama->_log_level) {
      std::string log_text(text);
      log_write(LEVEL_INFO, "LLAMA: %s", utils::trim(text).c_str());
    }
  }, this);
  reset();
  llama_backend_init();
}

Llama::Llama(Llama &&other) noexcept
  : _model(std::exchange(other._model, nullptr))
  , _ctx(std::exchange(other._ctx, nullptr))
  , _sampler(std::exchange(other._sampler, nullptr))
  , _vocab(std::exchange(other._vocab, nullptr))
  , _stop_sequences(std::move(other._stop_sequences))
  , _grammar_src(std::move(other._grammar_src))
  , _grammar_root(std::move(other._grammar_root))
  , _last_error(std::move(other._last_error))
  , _template(std::move(other._template))
  , _penalty_last_n(other._penalty_last_n)
  , _penalty_repeat(other._penalty_repeat)
  , _penalty_freq(other._penalty_freq)
  , _penalty_present(other._penalty_present)
  , _temperature(other._temperature)
  , _top_p(other._top_p)
  , _min_p(other._min_p)
  , _top_k(other._top_k)
  , _max_tokens(other._max_tokens)
  , _log_level(other._log_level)
  , _n_gpu_layers(other._n_gpu_layers)
  , _n_system_tokens(other._n_system_tokens)
  , _tokens_physically_used(other._tokens_physically_used)
  , _is_gemma4(other._is_gemma4)
  , _sampler_dirty(other._sampler_dirty)
  , _can_shift(other._can_shift)
  , _memory_flush(other._memory_flush)
  , _seed(other._seed)
  , _ctx_mtp(std::exchange(other._ctx_mtp, nullptr))
  , _n_mtp_layers(other._n_mtp_layers)
  , _mtp_enabled(other._mtp_enabled)
  , _mtp_n_max(other._mtp_n_max)
  , _mtp_n_min(other._mtp_n_min)
  , _mtp_p_min(other._mtp_p_min)
  , _n_embd(other._n_embd)
  , _pending_h(std::move(other._pending_h))
  , _verify_h(std::move(other._verify_h))
  , _verify_h_rows(other._verify_h_rows)
  , _batch_mtp(std::exchange(other._batch_mtp, nullptr))
  , _sampler_mtp(std::exchange(other._sampler_mtp, nullptr))
  , _mtp_buffer(std::move(other._mtp_buffer)) {
}

Llama::~Llama() {
  if (_sampler_mtp) {
    llama_sampler_free(_sampler_mtp);
  }
  if (_batch_mtp) {
    llama_batch_ext_free(_batch_mtp);
  }
  if (_sampler) {
    llama_sampler_free(_sampler);
  }
  if (_ctx_mtp) {
    llama_free(_ctx_mtp);
  }
  if (_ctx) {
    llama_free(_ctx);
  }
  if (_model) {
    llama_model_free(_model);
  }
  llama_backend_free();
}

void Llama::reset() {
  _stop_sequences.clear();
  _last_error.clear();
  _penalty_last_n = 64;
  _penalty_repeat = 1.1f;
  _penalty_freq = 0.0f;
  _penalty_present = 0.0f;
  _temperature = 0;
  _top_k = 0;
  _top_p = 1.0f;
  _min_p = 0.0f;
  _max_tokens = 150;
  _n_system_tokens = 0;
  _tokens_physically_used = 0;
  _seed = LLAMA_DEFAULT_SEED;
  _sampler_dirty = true;
  _pending_h.clear();
  _verify_h.clear();
  _verify_h_rows = 0;
  _mtp_buffer.clear();
  if (_ctx) {
    llama_memory_clear(llama_get_memory(_ctx), true);
  }
  if (_ctx_mtp) {
    llama_memory_clear(llama_get_memory(_ctx_mtp), true);
  }
}

bool Llama::is_memory_flush() {
  auto result = _memory_flush;
  if (result) {
    _memory_flush = false;
  }
  return result;
}

bool Llama::load_model(const LlamaLoad &load) {
  ggml_backend_load_all();

  llama_model_params mparams = llama_model_default_params();
  if (load.n_gpu_layers >= 0) {
    mparams.n_gpu_layers = load.n_gpu_layers;
  }
  if (load.mtp_enabled) {
    mparams.load_mtp = true;
  }

  _last_error.clear();
  _log_level = load.log_level;
  _n_gpu_layers = load.n_gpu_layers;
  _model = llama_model_load_from_file(load.model_path.c_str(), mparams);
  if (!_model) {
    set_last_error("Load model");
  } else {
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx   = load.n_ctx;
    cparams.n_batch = load.n_batch;
    cparams.n_ubatch = load.n_batch;
    cparams.no_perf = true;
    cparams.attention_type = LLAMA_ATTENTION_TYPE_UNSPECIFIED;
    cparams.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;

    switch (load.kv_cache_preset) {
      case KVCachePreset::F16:
        log_write(LEVEL_INFO, "HALI: kv_cache_preset=f16");
        cparams.type_k = GGML_TYPE_F16;
        cparams.type_v = GGML_TYPE_F16;
        break;
      case KVCachePreset::Balanced:
        log_write(LEVEL_INFO, "HALI: kv_cache_preset=balanced");
        cparams.type_k = GGML_TYPE_Q8_0;
        cparams.type_v = GGML_TYPE_Q8_0;
        break;
      case KVCachePreset::Compact:
        log_write(LEVEL_INFO, "HALI: kv_cache_preset=compact");
        cparams.type_k = GGML_TYPE_Q4_0;
        cparams.type_v = GGML_TYPE_Q4_0;
        break;
    }

    // keep KV cache on GPU
    cparams.offload_kqv = load.offload_kqv;

    // use full-size SWA cache
    cparams.swa_full = true;

    if (load.n_threads > 0) {
      log_write(LEVEL_INFO, "HALI: n_threads: %d", load.n_threads);
      cparams.n_threads = load.n_threads;
    }

    if (load.n_threads_batch > 0) {
      log_write(LEVEL_INFO, "HALI: n_threads_batch: %d", load.n_threads_batch);
      cparams.n_threads_batch = load.n_threads_batch;
    }

    _ctx = llama_init_from_model(_model, cparams);
    if (!_ctx) {
      set_last_error("Create context");
    } else {
      _vocab = llama_model_get_vocab(_model);
      _template = llama_model_chat_template(_model, nullptr);
      _is_gemma4 = (_template.find("<|turn>model") != string::npos);
      _can_shift = llama_memory_can_shift(llama_get_memory(_ctx));
      _n_embd = llama_model_n_embd(_model);
      log_write(LEVEL_INFO, "HALI: can_shift=%d, is_gemma4=%d n_swa=%d", _can_shift, _is_gemma4, llama_model_n_swa(_model));

      // MTP detection & context setup
      _n_mtp_layers = llama_model_n_layer_nextn(_model);
      if (load.mtp_enabled && _n_mtp_layers > 0) {
        log_write(LEVEL_INFO, "HALI: MTP detected: %d nextn layers", _n_mtp_layers);

        // create MTP draft context (reuses the same model)
        llama_context_params mtp_cparams = cparams;
        mtp_cparams.ctx_type = LLAMA_CONTEXT_TYPE_MTP;
        mtp_cparams.n_ctx = llama_n_ctx(_ctx);
        _ctx_mtp = llama_init_from_model(_model, mtp_cparams);
        if (_ctx_mtp) {
          _mtp_enabled = true;
          _mtp_n_max = std::min(load.mtp_n_max, _n_mtp_layers);
          _mtp_n_min = load.mtp_n_min;
          _mtp_p_min = load.mtp_p_min;

          // enable nextn hidden-state output on both contexts
          // target: masked=false → embeddings for ALL tokens in the batch
          // draft:  masked=true  → embeddings only for tokens with logits!=0
          llama_set_embeddings_nextn(_ctx, true, false);
          llama_set_embeddings_nextn(_ctx_mtp, true, true);

          // create the MTP batch and sampler
          _batch_mtp = llama_batch_ext_init(_ctx_mtp);
          auto sparams = llama_sampler_chain_default_params();
          sparams.no_perf = true;
          _sampler_mtp = llama_sampler_chain_init(sparams);
          llama_sampler_chain_add(_sampler_mtp, llama_sampler_init_top_k(10));

          log_write(LEVEL_INFO, "HALI: MTP ready: n_max=%d n_min=%d p_min=%.2f", _mtp_n_max, _mtp_n_min, _mtp_p_min);
        } else {
          log_write(LEVEL_INFO, "HALI: MTP context creation failed, continuing without speculative decoding");
          _n_mtp_layers = 0;
        }
      } else if (_n_mtp_layers > 0) {
        log_write(LEVEL_INFO, "HALI: MTP available (%d layers) but disabled", _n_mtp_layers);
        _n_mtp_layers = 0;
      }
    }
  }

  return _last_error.empty();
}

bool Llama::load_embedding_model(const string &model_path) {
  ggml_backend_load_all();

  llama_model_params mparams = llama_model_default_params();
  mparams.n_gpu_layers = 99;

  _last_error.clear();
  _model = llama_model_load_from_file(model_path.c_str(), mparams);
  if (!_model) {
    set_last_error("Load model");
  } else {
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx        = 512;
    cparams.n_batch      = 512;
    cparams.embeddings   = true;
    cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;

    _ctx = llama_init_from_model(_model, cparams);
    if (!_ctx) {
      set_last_error("Create context");
    } else {
      _vocab = llama_model_get_vocab(_model);
    }
  }

  return _last_error.empty();
}

void Llama::set_grammar(const string &src, const string &root) {
  _grammar_src = src;
  _grammar_root = root;
  dirty();
}

bool Llama::add_message(LlamaIter &iter, const string &role, const string &content) {
  llama_chat_message message = {role.c_str(), content.c_str()};
  int buf_size = 2 * (int)(role.size() + content.size() + 64);
  vector<char> buf(buf_size);
  int32_t n = 0;

  _last_error.clear();
  if (_template.empty()) {
    set_last_error("Chat template availability test");
    return false;
  }

  if (_is_gemma4) {
    // see: https://ai.google.dev/gemma/docs/core/prompt-formatting-gemma4
    string str;
    if (role == "system") {
      str = "<|turn>system\n<|think|>" + content + "<turn|>\n";
    } else {
      str = "<|turn>" + role + "\n" + content + "<turn|>\n";
    }
    n = str.size();
    buf.assign(str.begin(), str.end());
    buf.push_back('\0');
  } else {
    bool add_ass = (role == "user" || role == "tool" || role == "tool_result");
    n = llama_chat_apply_template(_template.c_str(), &message, 1, add_ass, buf.data(), buf_size);
    if (n < 0) {
      log_write(LEVEL_INFO, "HALI: unsupported template: %s", _template.c_str());
      set_last_error("Chat template support test");
      return false;
    } else if (n > (int32_t)buf.size()) {
      buf.resize(n);
      llama_chat_apply_template(_template.c_str(), &message, 1, add_ass, buf.data(), buf.size());
    }
  }
  string prompt(buf.data(), n);

  if (_sampler_dirty) {
    // avoid wasteful rebuild
    if (!configure_sampler()) {
      return false;
    }
    _sampler_dirty = false;
  }

  vector<llama_token> prompt_tokens = tokenize(prompt);
  if (prompt_tokens.size() == 0) {
    return false;
  }

  if (role == "system") {
    // always retain system tokens
    _n_system_tokens = prompt_tokens.size();
  }

  if (!make_space_for_tokens(prompt_tokens.size())) {
    return false;
  }

  // batch decode tokens
  if (!batch_decode_tokens(prompt_tokens)) {
    return false;
  }

  // handle encoder models
  if (llama_model_has_encoder(_model)) {
    // for example: T5, BART, and mBART.
    // Used for translation, summarization, text-to-text, paraphrasing, question answering
    llama_token decoder_start_token_id = llama_model_decoder_start_token(_model);
    if (decoder_start_token_id == LLAMA_TOKEN_NULL) {
      decoder_start_token_id = llama_vocab_bos(_vocab);
    }

    llama_batch decoder_batch = llama_batch_get_one(&decoder_start_token_id, 1);
    if (llama_decode(_ctx, decoder_batch)) {
      set_last_error("Failed to evaluate decoder start token");
      return false;
    }
    _tokens_physically_used += 1;
  }

  iter._tokens_generated = 0;
  iter._t_start = std::chrono::high_resolution_clock::now();
  iter._llama = this;
  iter._has_next = true;
  return true;
}

string Llama::next(LlamaIter &iter) {
  _last_error.clear();
  if (!iter._has_next) {
    set_last_error("Iteration beyond end of stream");
    return "";
  }

  // if we have buffered tokens from speculative decoding, return the next one
  if (!_mtp_buffer.empty()) {
    llama_token tok = _mtp_buffer.front();
    _mtp_buffer.erase(_mtp_buffer.begin());

    if (llama_vocab_is_eog(_vocab, tok)) {
      iter._has_next = false;
      _mtp_buffer.clear();
      return "";
    }

    string result = token_to_string(iter, tok);
    if (!iter._has_next) {
      _mtp_buffer.clear();
    }
    return result;
  }

  // sample the anchor token from the current logits
  llama_token tok = llama_sampler_sample(_sampler, _ctx, -1);

  // end-of-generation check
  if (llama_vocab_is_eog(_vocab, tok)) {
    iter._has_next = false;
    return "";
  }

  // decode the anchor token on the target
  llama_batch batch = llama_batch_get_one(&tok, 1);
  if (llama_decode(_ctx, batch)) {
    iter._has_next = false;
    set_last_error("Failed to evaluate token during generation");
    return "";
  }

  _tokens_physically_used += 1;

  // MTP speculative decoding
  if (_mtp_enabled) {
    sync_and_capture_mtp(&tok, 1, (llama_pos)(_tokens_physically_used - 1));
    vector<llama_token> drafts = generate_draft_tokens(tok, (llama_pos)_tokens_physically_used);
    vector<llama_token> accepted = verify_and_accept(drafts);
    for (auto t : accepted) {
      _mtp_buffer.push_back(t);
    }
  }

  string result = token_to_string(iter, tok);
  if (!iter._has_next) {
    _mtp_buffer.clear();
  }
  return result;
}

string Llama::all(LlamaIter &iter) {
  string out;

  vector<llama_token> decoded;
  decoded.reserve(_max_tokens);

  int generated = 0;

  _last_error.clear();
  while (generated < _max_tokens) {
    // sample the anchor token from the current logits
    llama_token tok = llama_sampler_sample(_sampler, _ctx, -1);

    // end-of-generation check
    if (llama_vocab_is_eog(_vocab, tok)) {
      break;
    }

    // append anchor to decoded list
    decoded.push_back(tok);
    ++generated;

    // decode the anchor token on the target
    llama_batch batch = llama_batch_get_one(&tok, 1);
    if (llama_decode(_ctx, batch)) {
      set_last_error("Failed to evaluate token during generation");
      break;
    }

    _tokens_physically_used += 1;

    // MTP speculative decoding
    if (_mtp_enabled) {
      sync_and_capture_mtp(&tok, 1, (llama_pos)(_tokens_physically_used - 1));
      vector<llama_token> drafts = generate_draft_tokens(tok, (llama_pos)_tokens_physically_used);
      vector<llama_token> accepted = verify_and_accept(drafts);

      for (auto t : accepted) {
        if (llama_vocab_is_eog(_vocab, t)) {
          break;
        }
        decoded.push_back(t);
        ++generated;
        if (generated >= _max_tokens) {
          break;
        }
      }
    }
  }

  // tokens exhausted - call add_message to continue
  iter._has_next = false;

  // detokenize sequentially
  if (!decoded.empty()) {
    for (llama_token tok : decoded) {
      out.append(token_to_string(iter, tok));
    }
  }

  return out;
}

float Llama::memory_kv_percent() const {
  llama_memory_t mem = llama_get_memory(_ctx);
  llama_pos pos_max  = llama_memory_seq_pos_max(mem, 0);
  int n_ctx          = llama_n_ctx(_ctx);
  int kv_used        = (pos_max < 0) ? 0 : (int)pos_max + 1;
  return 100.0f * kv_used / n_ctx;
}

LlamaMemoryInfo Llama::memory_info() const {
  LlamaMemoryInfo info = {};

  // KV cache usage
  llama_memory_t mem = llama_get_memory(_ctx);
  llama_pos pos_max  = llama_memory_seq_pos_max(mem, 0);
  int n_ctx          = llama_n_ctx(_ctx);
  info.kv_total      = n_ctx;
  info.kv_used       = (pos_max < 0) ? 0 : (int)pos_max + 1;
  info.kv_percent    = 100.0f * info.kv_used / info.kv_total;

  // Model layers
  auto n_gpu_layers = std::max(0, _n_gpu_layers);
  info.n_layers_total = llama_model_n_layer(_model);
  info.n_layers_gpu   = std::min(info.n_layers_total, n_gpu_layers);
  info.n_layers_cpu   = info.n_layers_total - info.n_layers_gpu;

  // ram
  if (read_vram(info.vram_used, info.vram_total)) {
    info.vram_percent = 100.0f * info.vram_used / info.vram_total;
  }

  info.model_native_max_ctx = llama_model_n_ctx_train(_model);

  // Advice
  ostringstream advice;

  // Check structural limits & model configuration quirks
  if (info.kv_total > info.model_native_max_ctx) {
    advice << "WARNING: Configured context size (" << info.kv_total
           << ") exceeds model native training length (" << info.model_native_max_ctx
           << "). Logic flaws or repetition bugs will occur unless RoPE scaling options are enabled. ";
  }

  if (n_gpu_layers < info.n_layers_total) {
    advice << "Only " << n_gpu_layers << "/" << info.n_layers_total
           << " layers on GPU - increase n_gpu_layers if VRAM allows. ";
  } else {
    advice << "All " << info.n_layers_total << " layers on GPU. ";
  }
  if (info.n_layers_cpu > 0) {
    advice << "CPU offload active (" << info.n_layers_cpu
           << " layers on CPU) - increase n_gpu_layers if VRAM allows. ";
  }
  if (info.vram_percent > 90.0f) {
    advice << "VRAM >90% - reduce n_ctx or use Q4_0 KV cache. ";
  } else if (info.vram_percent < 60.0f && info.n_layers_cpu > 0) {
    advice << "VRAM headroom available - try adding more GPU layers. ";
  }
  if (info.kv_percent > 80.0f) {
    advice << "Context >80% full - consider calling clear_history(). ";
  }
  info.advice = advice.str();

  return info;
}

bool Llama::embed_text(const std::string &text, std::vector<float> &out, int embed_dim) {
  vector<llama_token> tokens = tokenize(text);
  if (tokens.size() == 0) {
    return false;
  }

  // truncate to context window
  int n_ctx = llama_n_ctx(_ctx);
  int n = tokens.size();
  if (n > n_ctx) {
    set_last_error(std::format("warning: chunk truncated {} -> {} tokens ", n, n_ctx));
    n = n_ctx;
    tokens.resize(n);
  }

  llama_memory_clear(llama_get_memory(_ctx), true);

  if (!batch_decode_tokens(tokens)) {
    return false;
  }

  float *emb = llama_get_embeddings_seq(_ctx, 0);
  if (!emb) {
    emb = llama_get_embeddings_ith(_ctx, n - 1);
  }

  if (!emb) {
    set_last_error("no embedding returned");
    return false;
  }

  out.assign(emb, emb + embed_dim);

  // L2 normalize
  float norm = 0.0f;
  for (float v : out) {
    norm += v * v;
  }
  norm = std::sqrt(norm);
  if (norm > 1e-9f) {
    for (float &v : out) {
      v /= norm;
    }
  }

  return true;
}

bool Llama::batch_decode_tokens(vector<llama_token> &tokens) {
  uint32_t n_batch = llama_n_batch(_ctx);
  for (size_t i = 0; i < tokens.size(); i += n_batch) {
    size_t batch_size = std::min((size_t)n_batch, tokens.size() - i);
    llama_batch batch = llama_batch_get_one(tokens.data() + i, batch_size);
    int result = llama_decode(_ctx, batch);
    if (result == 1) {
      // KV full - make_space_for_tokens will either confirm there's room,
      // or force a full reset (clearing system tokens too) and signal
      // _memory_flush so the caller knows to replay the system prompt.
      if (!make_space_for_tokens((int)batch_size)) {
        set_decode_error(result, (int)i, (int)tokens.size());
        return false;
      }
      result = llama_decode(_ctx, batch);
    }
    if (result != 0) {
      // No more fallback: if this still fails after a confirmed-clear or
      // confirmed-room retry, it's a genuine, unexpected failure, not
      // something a second flush would fix.
      set_decode_error(result, (int)i, (int)tokens.size());
      return false;
    }
    _tokens_physically_used += batch_size;
    sync_and_capture_mtp(tokens.data() + i, (int)batch_size, (llama_pos)(_tokens_physically_used - batch_size));
  }
  return true;
}

bool Llama::configure_sampler() {
  auto sparams = llama_sampler_chain_default_params();
  sparams.no_perf = false;
  llama_sampler *chain = llama_sampler_chain_init(sparams);

  if (!_grammar_src.empty()) {
    llama_sampler *grammar = llama_sampler_init_grammar(_vocab, _grammar_src.c_str(), _grammar_root.c_str());
    if (!grammar) {
      set_last_error("failed to initialize grammar sampler");
      return false;
    }
    llama_sampler_chain_add(chain, grammar);
  }
  if (_penalty_last_n != 0 && _penalty_repeat != 1.0f) {
    auto n_vocab = llama_vocab_n_tokens(_vocab);
    auto penalties = llama_sampler_init_penalties(n_vocab, _penalty_last_n, _penalty_repeat, _penalty_freq, _penalty_present);
    llama_sampler_chain_add(chain, penalties);
  }
  if (_temperature <= 0.0f) {
    llama_sampler_chain_add(chain, llama_sampler_init_greedy());
  } else {
    if (_top_k > 0) {
      llama_sampler_chain_add(chain, llama_sampler_init_top_k(_top_k));
    }
    if (_top_p < 1.0f || _min_p > 0.0f) {
      llama_sampler_chain_add(chain, llama_sampler_init_top_p(_top_p, 1));
    }
    if (_min_p > 0.0f) {
      llama_sampler_chain_add(chain, llama_sampler_init_min_p(_min_p, 1));
    }
    llama_sampler_chain_add(chain, llama_sampler_init_temp(_temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(_seed));
  }
  if (_sampler) {
    llama_sampler_free(_sampler);
  }
  _sampler = chain;
  return true;
}

bool Llama::full_flush_except_system() {
  llama_memory_t mem = llama_get_memory(_ctx);
  llama_pos pos_min = llama_memory_seq_pos_min(mem, 0);
  if (pos_min < 0) {
    return true; // already empty
  }
  llama_pos flush_start = pos_min + _n_system_tokens;
  bool ok = llama_memory_seq_rm(mem, 0, flush_start, -1);
  if (!ok) {
    set_last_error("Failed to flush memory past system tokens");
    return false;
  }
  return true;
}

bool Llama::make_space_for_tokens(int n_tokens) {
  int n_ctx = llama_n_ctx(_ctx);
  if (n_tokens > n_ctx) {
    set_last_error("Too many tokens, increase context size (n_ctx)");
    return false;
  }

  if (_tokens_physically_used + (size_t)n_tokens <= (size_t)n_ctx) {
    return true;
  }

  log_write(LEVEL_DEBUG,
            "HALI: capacity exhausted, forcing full reset: "
            "used=%zu requested=%d n_ctx=%d can_shift=%d",
            _tokens_physically_used, n_tokens, n_ctx, _can_shift ? 1 : 0);

  llama_memory_clear(llama_get_memory(_ctx), true);
  _tokens_physically_used = 0;
  _n_system_tokens = 0;
  _memory_flush = true;
  return false;
}

vector<llama_token> Llama::tokenize(const string &prompt) {
  vector<llama_token> result;

  int n_prompt = -llama_tokenize(_vocab, prompt.c_str(), prompt.size(), nullptr, 0, true, true);
  if (n_prompt <= 0) {
    set_last_error("Failed to tokenize prompt");
  } else {
    result.reserve(n_prompt);
    result.resize(n_prompt);
    if (llama_tokenize(_vocab, prompt.c_str(), prompt.size(),
                       result.data(), n_prompt, true, true) < 0) {
      set_last_error("Failed to tokenize prompt");
    }
  }
  return result;
}

string Llama::token_to_string(LlamaIter &iter, llama_token tok) const {
  string result;
  char buf[512];
  int n = llama_token_to_piece(_vocab, tok, buf, sizeof(buf), 0, false);
  if (n > 0) {
    // detect repetition - only on non-whitespace tokens, otherwise
    // spaces/newlines trigger false positives almost immediately.
    string piece(buf, n);
    bool is_trivial = piece.find_first_not_of(" \t\n\r") == string::npos;
    if (!is_trivial) {
      if (iter._last_word == piece) {
        if (++iter._repetition_count >= MAX_REPEAT) {
          iter._has_next = false;
        }
      } else {
        iter._repetition_count = 0;
        iter._last_word = piece;
      }
    }

    result.append(buf, n);

    // detect end of max-tokens
    if (++iter._tokens_generated > _max_tokens) {
      iter._has_next = false;
    }

    // detect stop words
    if (iter._has_next) {
      for (const auto &stop : _stop_sequences) {
        size_t pos = result.find(stop);
        if (pos != std::string::npos) {
          // found stop sequence - truncate and signal end
          result = result.substr(0, pos);
          iter._has_next = false;
          break;
        }
      }
    }
  }
  return result;
}

void Llama::set_last_error(const string &message) {
  if (!_last_error.empty()) {
    if (_last_error.back() == '\n') {
      _last_error.pop_back();
    }
    _last_error = std::format("{}: {}", message, _last_error);
  } else {
    _last_error = std::format("{} failed", message);
  }
}

void Llama::set_decode_error(int32_t error, int index, int num_tokens) {
  if (error == 1) {
    llama_memory_t mem = llama_get_memory(_ctx);
    llama_pos pos_min = llama_memory_seq_pos_min(mem, 0);
    llama_pos pos_max = llama_memory_seq_pos_max(mem, 0);
    int n_ctx = llama_n_ctx(_ctx);
    int current_used = pos_max - pos_min + 1;
    int space_needed = num_tokens;
    int space_available = n_ctx - current_used;
    set_last_error(std::format("KV exhausted. Reduce batch or context sizes. batchNo:{} requested:{} available:{}",
                               index, space_needed, space_available));
  } else {
    auto message = error == 2 ? "abort" : error == -1 ? "invalid" : "fatal";
    set_last_error(std::format("Failed to decode batch. batchNo:{} error:'{}'", index, message));
  }
}
