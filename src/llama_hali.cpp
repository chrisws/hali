// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <format>
#include <cmath>
#include <cstring>
#include <utility>

#include "ggml-cuda.h"
#include "llama.h"
#include "log.h"
#include "common.h"

#include "server-context.h"
#include "server-task.h"
#include "server-queue.h"

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

// ---------------------------------------------------------------------------
// LlamaIter
// ---------------------------------------------------------------------------

LlamaIter::LlamaIter() :
  _llama(nullptr),
  _repetition_count(0),
  _tokens_generated(0),
  _has_next(false) {
}

LlamaIter::~LlamaIter() {
  // _reader is auto-cleaned by unique_ptr
}

LlamaIter::LlamaIter(LlamaIter &&other) noexcept
  : _llama(std::exchange(other._llama, nullptr))
  , _last_word(std::move(other._last_word))
  , _tail(std::move(other._tail))
  , _t_start(std::move(other._t_start))
  , _repetition_count(other._repetition_count)
  , _tokens_generated(other._tokens_generated)
  , _has_next(other._has_next)
  , _reader(std::move(other._reader)) {
}

// ---------------------------------------------------------------------------
// Llama
// ---------------------------------------------------------------------------

Llama::Llama() :
  _emb_model(nullptr),
  _emb_ctx(nullptr),
  _server_running(false),
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
  _is_gemma4(false),
  _sampler_dirty(false),
  _can_shift(false),
  _memory_flush(false),
  _seed(LLAMA_DEFAULT_SEED),
  _n_mtp_layers(0),
  _mtp_enabled(false),
  _mtp_n_max(1),
  _mtp_n_min(1),
  _mtp_p_min(0.0f) {
  llama_log_set([](enum ggml_log_level level, const char *text, void *user_data) {
    Llama *llama = static_cast<Llama *>(user_data);
    if (level == GGML_LOG_LEVEL_ERROR && llama->_last_error.empty()) {
      llama->_last_error = text;
    }
    if (level == GGML_LOG_LEVEL_ERROR || level > llama->_log_level) {
      log_write(LEVEL_INFO, "LLAMA: %s", utils::trim(text).c_str());
    }
  }, this);

  const char *home = getenv("HOME");
  const auto path = std::string(home ? home : ".") + "/.config/hali/hali-common.log";
  common_log_set_file(common_log_main(), path.c_str());

  reset();
  llama_backend_init();
}

Llama::Llama(Llama &&other) noexcept
  : _server_ctx(std::exchange(other._server_ctx, nullptr))
  , _server_thread(std::move(other._server_thread))
  , _server_running(other._server_running)
  , _emb_model(std::exchange(other._emb_model, nullptr))
  , _emb_ctx(std::exchange(other._emb_ctx, nullptr))
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
  , _is_gemma4(other._is_gemma4)
  , _sampler_dirty(other._sampler_dirty)
  , _can_shift(other._can_shift)
  , _memory_flush(other._memory_flush)
  , _seed(other._seed)
  , _n_mtp_layers(other._n_mtp_layers)
  , _mtp_enabled(other._mtp_enabled)
  , _mtp_n_max(other._mtp_n_max)
  , _mtp_n_min(other._mtp_n_min)
  , _mtp_p_min(other._mtp_p_min) {
  other._server_running = false;
}

Llama::~Llama() {
  stop_server();
  if (_emb_ctx) {
    llama_free(_emb_ctx);
    _emb_ctx = nullptr;
  }
  if (_emb_model) {
    llama_model_free(_emb_model);
    _emb_model = nullptr;
  }
  llama_backend_free();
}

void Llama::stop_server() {
  if (_server_ctx) {
    _server_ctx->terminate();
  }
  if (_server_thread.joinable()) {
    _server_thread.join();
  }
  _server_running = false;
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
  _seed = LLAMA_DEFAULT_SEED;
  _sampler_dirty = true;
  _memory_flush = false;
}

bool Llama::is_memory_flush() {
  auto result = _memory_flush;
  if (result) {
    _memory_flush = false;
  }
  return result;
}

// ---------------------------------------------------------------------------
// Model loading
// ---------------------------------------------------------------------------

bool Llama::load_model(const LlamaLoad &load) {
  // stop any previous server
  stop_server();
  _server_ctx.reset();

  // free any previous embedding model
  if (_emb_ctx) {
    llama_free(_emb_ctx);
    _emb_ctx = nullptr;
  }
  if (_emb_model) {
    llama_model_free(_emb_model);
    _emb_model = nullptr;
  }

  _vocab = nullptr;
  _n_mtp_layers = 0;
  _mtp_enabled = false;
  _last_error.clear();
  _log_level = load.log_level;
  _n_gpu_layers = load.n_gpu_layers;

  // map LlamaLoad → common_params
  common_params params;
  params.model.path = load.model_path;
  params.n_ctx = load.n_ctx;
  params.n_batch = load.n_batch;
  params.n_gpu_layers = load.n_gpu_layers;
  params.cpuparams.n_threads = load.n_threads > 0 ? load.n_threads : 1;
  params.cpuparams_batch.n_threads = load.n_threads_batch > 0 ? load.n_threads_batch : 1;
  params.rope_freq_scale = load.rope_freq_scale;

  switch (load.kv_cache_preset) {
    case KVCachePreset::F16:
      log_write(LEVEL_INFO, "HALI: kv_cache_preset=f16");
      params.cache_type_k = GGML_TYPE_F16;
      params.cache_type_v = GGML_TYPE_F16;
      break;
    case KVCachePreset::Balanced:
      log_write(LEVEL_INFO, "HALI: kv_cache_preset=balanced");
      params.cache_type_k = GGML_TYPE_Q8_0;
      params.cache_type_v = GGML_TYPE_Q8_0;
      break;
    case KVCachePreset::Compact:
      log_write(LEVEL_INFO, "HALI: kv_cache_preset=compact");
      params.cache_type_k = GGML_TYPE_Q4_0;
      params.cache_type_v = GGML_TYPE_Q4_0;
      break;
  }

  // MTP speculative decoding
  if (load.mtp_enabled) {
    params.speculative.types = {COMMON_SPECULATIVE_TYPE_DRAFT_MTP};
    params.speculative.draft.n_max = load.mtp_n_max;
    params.speculative.draft.n_min = load.mtp_n_min;
    params.speculative.draft.p_min = load.mtp_p_min;
  }

  // create server context
  _server_ctx = std::make_unique<server_context>();
  if (!_server_ctx->load_model(params)) {
    set_last_error("Load model via server_context");
    _server_ctx.reset();
    return false;
  }

  // extract metadata
  auto meta = _server_ctx->get_meta();
  _template = ""; // server handles template internally
  _is_gemma4 = false;

  // get raw context for memory queries
  llama_context *ctx = _server_ctx->get_llama_context();
  if (ctx) {
    _vocab = llama_model_get_vocab(llama_get_model(ctx));
    _can_shift = llama_memory_can_shift(llama_get_memory(ctx));
  }

  // MTP detection from meta
  _n_mtp_layers = 0;
  if (load.mtp_enabled) {
    // server_context handles MTP internally; we just track the config
    _mtp_enabled = true;
    _mtp_n_max = load.mtp_n_max;
    _mtp_n_min = load.mtp_n_min;
    _mtp_p_min = load.mtp_p_min;
  }

  log_write(LEVEL_INFO, "HALI: server_context loaded, can_shift=%d", _can_shift);

  // start server loop in a background thread
  _server_thread = std::thread([this]() {
    _server_ctx->start_loop();
  });
  _server_running = true;

  return _last_error.empty();
}

bool Llama::load_embedding_model(const string &model_path) {
  ggml_backend_load_all();

  // free any previous embedding model
  if (_emb_ctx) {
    llama_free(_emb_ctx);
    _emb_ctx = nullptr;
  }
  if (_emb_model) {
    llama_model_free(_emb_model);
    _emb_model = nullptr;
  }

  llama_model_params mparams = llama_model_default_params();
  mparams.n_gpu_layers = 99;

  _last_error.clear();
  _emb_model = llama_model_load_from_file(model_path.c_str(), mparams);
  if (!_emb_model) {
    set_last_error("Load embedding model");
  } else {
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx        = 512;
    cparams.n_batch      = 512;
    cparams.embeddings   = true;
    cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;

    _emb_ctx = llama_init_from_model(_emb_model, cparams);
    if (!_emb_ctx) {
      set_last_error("Create embedding context");
    } else {
      _vocab = llama_model_get_vocab(_emb_model);
    }
  }

  return _last_error.empty();
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

void Llama::set_grammar(const string &src, const string &root) {
  _grammar_src = src;
  _grammar_root = root;
  dirty();
}

bool Llama::add_message(LlamaIter &iter, const string &role, const string &content) {
  _last_error.clear();

  if (!_server_ctx || !_server_running) {
    set_last_error("Server not running");
    return false;
  }

  // render chat template
  string prompt;
  if (_is_gemma4) {
    if (role == "system") {
      prompt = "<|turn>system\n<|think|>" + content + "<turn|>\n";
    } else {
      prompt = "<|turn>" + role + "\n" + content + "<turn|>\n";
    }
  } else if (!_template.empty()) {
    llama_chat_message message = {role.c_str(), content.c_str()};
    int buf_size = 2 * (int)(role.size() + content.size() + 64);
    vector<char> buf(buf_size);
    bool add_ass = (role == "user" || role == "tool" || role == "tool_result");
    int32_t n = llama_chat_apply_template(_template.c_str(), &message, 1, add_ass, buf.data(), buf_size);
    if (n < 0) {
      set_last_error("Chat template support test");
      return false;
    }
    if (n > (int32_t)buf.size()) {
      buf.resize(n);
      llama_chat_apply_template(_template.c_str(), &message, 1, add_ass, buf.data(), buf.size());
    }
    prompt = string(buf.data(), n);
  } else {
    // no template: use raw content
    prompt = content;
  }

  // tokenize
  vector<llama_token> tokens = tokenize(prompt);
  if (tokens.empty()) {
    return false;
  }

  // build server_task
  server_task task(SERVER_TASK_TYPE_COMPLETION);
  task.tokens = server_tokens(tokens, false);

  // map sampling params
  task.params.stream = true;
  task.params.n_predict = _max_tokens > 0 ? _max_tokens : -1;
  task.params.sampling.temp = _temperature;
  task.params.sampling.top_k = _top_k;
  task.params.sampling.top_p = _top_p;
  task.params.sampling.min_p = _min_p;
  task.params.sampling.n_prev = _penalty_last_n;
  task.params.sampling.penalty_repeat = _penalty_repeat;
  task.params.sampling.penalty_freq = _penalty_freq;
  task.params.sampling.penalty_present = _penalty_present;
  task.params.sampling.seed = _seed;

  // stop sequences
  task.params.antiprompt = _stop_sequences;

  // grammar
  if (!_grammar_src.empty()) {
    task.params.sampling.grammar = common_grammar(COMMON_GRAMMAR_TYPE_USER, _grammar_src, _grammar_root);
  }

  // create reader and post task
  iter._reader = std::make_unique<server_response_reader>(_server_ctx->get_response_reader());
  iter._reader->post_task(std::move(task));

  // reset iter state
  iter._tokens_generated = 0;
  iter._tail.clear();
  iter._last_word.clear();
  iter._repetition_count = 0;
  iter._t_start = std::chrono::high_resolution_clock::now();
  iter._llama = this;
  iter._has_next = true;

  _sampler_dirty = false;
  return true;
}

string Llama::next(LlamaIter &iter) {
  _last_error.clear();
  if (!iter._has_next || !iter._reader) {
    set_last_error("Iteration beyond end of stream");
    return "";
  }

  auto result = iter._reader->next([]() { return false; });
  if (!result) {
    iter._has_next = false;
    return "";
  }

  // error result
  if (result->is_error()) {
    auto *err = dynamic_cast<server_task_result_error *>(result.get());
    if (err) {
      set_last_error(err->err_msg);
    }
    iter._has_next = false;
    return "";
  }

  // final result (stream ended)
  if (result->is_stop()) {
    iter._has_next = false;
    auto *final = dynamic_cast<server_task_result_cmpl_final *>(result.get());
    if (final && !final->content.empty()) {
      return process_text_chunk(iter, final->content);
    }
    return "";
  }

  // partial result (streaming chunk)
  auto *partial = dynamic_cast<server_task_result_cmpl_partial *>(result.get());
  if (partial) {
    if (!partial->content.empty()) {
      return process_text_chunk(iter, partial->content);
    }
    return "";
  }

  return "";
}

string Llama::process_text_chunk(LlamaIter &iter, const string &text) {
  string result = text;

  // repetition detection
  // check the last word in the chunk
  size_t last_space = result.find_last_of(" \t\n\r");
  string last_word = (last_space != string::npos) ? result.substr(last_space + 1) : result;
  bool is_trivial = last_word.find_first_not_of(" \t\n\r") == string::npos;
  if (!is_trivial && !last_word.empty()) {
    if (iter._last_word == last_word) {
      if (++iter._repetition_count >= MAX_REPEAT) {
        iter._has_next = false;
      }
    } else {
      iter._repetition_count = 0;
      iter._last_word = last_word;
    }
  }

  // max tokens check (approximate: count words/tokens in chunk)
  iter._tokens_generated += (int)text.size(); // rough estimate
  if (iter._tokens_generated >= _max_tokens * 4) {
    iter._has_next = false;
  }

  // stop sequence detection
  if (iter._has_next && !_stop_sequences.empty()) {
    iter._tail.append(text);

    size_t max_stop_len = 0;
    for (const auto &stop : _stop_sequences) {
      if (stop.size() > max_stop_len) {
        max_stop_len = stop.size();
      }
    }

    for (const auto &stop : _stop_sequences) {
      size_t pos = iter._tail.find(stop);
      if (pos != std::string::npos) {
        size_t chunk_start = iter._tail.size() - text.size();
        if (pos >= chunk_start) {
          result = result.substr(0, pos - chunk_start);
        } else {
          result.clear();
        }
        iter._has_next = false;
        break;
      }
    }

    // trim tail
    if (max_stop_len > 0 && iter._tail.size() > max_stop_len) {
      iter._tail = iter._tail.substr(iter._tail.size() - max_stop_len);
    }
  }

  return result;
}

string Llama::all(LlamaIter &iter) {
  string out;
  _last_error.clear();
  while (iter._has_next) {
    string tok = next(iter);
    if (tok.empty() && !_last_error.empty()) {
      break;
    }
    out.append(tok);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Memory info
// ---------------------------------------------------------------------------

float Llama::memory_kv_percent() const {
  if (!_server_ctx) {
    return 0.0f;
  }
  llama_context *ctx = _server_ctx->get_llama_context();
  if (!ctx) {
    return 0.0f;
  }
  llama_memory_t mem = llama_get_memory(ctx);
  llama_pos pos_max = llama_memory_seq_pos_max(mem, 0);
  int n_ctx = llama_n_ctx(ctx);
  int kv_used = (pos_max < 0) ? 0 : (int)pos_max + 1;
  return 100.0f * kv_used / n_ctx;
}

LlamaMemoryInfo Llama::memory_info() const {
  LlamaMemoryInfo info = {};

  if (!_server_ctx) {
    return info;
  }
  llama_context *ctx = _server_ctx->get_llama_context();
  if (!ctx) {
    return info;
  }

  // KV cache usage
  llama_memory_t mem = llama_get_memory(ctx);
  llama_pos pos_max = llama_memory_seq_pos_max(mem, 0);
  int n_ctx = llama_n_ctx(ctx);
  info.kv_total = n_ctx;
  info.kv_used = (pos_max < 0) ? 0 : (int)pos_max + 1;
  info.kv_percent = 100.0f * info.kv_used / info.kv_total;

  // Model layers
  auto n_gpu_layers = std::max(0, _n_gpu_layers);
  const llama_model *model = llama_get_model(ctx);
  if (model) {
    info.n_layers_total = llama_model_n_layer(model);
    info.n_layers_gpu = std::min(info.n_layers_total, n_gpu_layers);
    info.n_layers_cpu = info.n_layers_total - info.n_layers_gpu;
    info.model_native_max_ctx = llama_model_n_ctx_train(model);
  }

  // VRAM
  if (read_vram(info.vram_used, info.vram_total)) {
    info.vram_percent = 100.0f * info.vram_used / info.vram_total;
  }

  // Advice
  ostringstream advice;
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

// ---------------------------------------------------------------------------
// Embeddings
// ---------------------------------------------------------------------------

bool Llama::embed_text(const std::string &text, std::vector<float> &out, int embed_dim) {
  if (!_emb_ctx || !_emb_model) {
    set_last_error("No embedding model loaded");
    return false;
  }

  const llama_vocab *emb_vocab = llama_model_get_vocab(_emb_model);
  vector<llama_token> tokens;
  int n_prompt = -llama_tokenize(emb_vocab, text.c_str(), text.size(), nullptr, 0, true, true);
  if (n_prompt <= 0) {
    set_last_error("Failed to tokenize embedding text");
    return false;
  }
  tokens.resize(n_prompt);
  llama_tokenize(emb_vocab, text.c_str(), text.size(), tokens.data(), n_prompt, true, true);

  // truncate to context window
  int n_ctx = llama_n_ctx(_emb_ctx);
  if ((int)tokens.size() > n_ctx) {
    tokens.resize(n_ctx);
  }

  llama_memory_clear(llama_get_memory(_emb_ctx), true);

  // decode in batches
  uint32_t n_batch = llama_n_batch(_emb_ctx);
  for (size_t i = 0; i < tokens.size(); i += n_batch) {
    size_t batch_size = std::min((size_t)n_batch, tokens.size() - i);
    llama_batch batch = llama_batch_get_one(tokens.data() + i, batch_size);
    if (llama_decode(_emb_ctx, batch) != 0) {
      set_last_error("Failed to decode embedding batch");
      return false;
    }
  }

  float *emb = llama_get_embeddings_seq(_emb_ctx, 0);
  if (!emb) {
    emb = llama_get_embeddings_ith(_emb_ctx, (int)tokens.size() - 1);
  }
  if (!emb) {
    set_last_error("No embedding returned");
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

// ---------------------------------------------------------------------------
// Tokenization utilities
// ---------------------------------------------------------------------------

vector<llama_token> Llama::tokenize(const string &prompt) {
  vector<llama_token> result;
  if (!_vocab) {
    set_last_error("No vocab available");
    return result;
  }

  int n_prompt = -llama_tokenize(_vocab, prompt.c_str(), prompt.size(), nullptr, 0, true, true);
  if (n_prompt <= 0) {
    set_last_error("Failed to tokenize prompt");
  } else {
    result.reserve(n_prompt);
    result.resize(n_prompt);
    if (llama_tokenize(_vocab, prompt.c_str(), prompt.size(),
                       result.data(), n_prompt, true, true) < 0) {
      set_last_error("Failed to tokenize prompt");
      result.clear();
    }
  }
  return result;
}

string Llama::token_to_string(LlamaIter &iter, llama_token tok) const {
  string result;
  char buf[512];
  int n = llama_token_to_piece(_vocab, tok, buf, sizeof(buf), 0, false);
  if (n > 0) {
    result.append(buf, n);
  }
  return result;
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

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
    set_last_error(std::format("KV exhausted. Reduce batch or context sizes. batchNo:{} requested:{}",
                               index, num_tokens));
  } else {
    auto message = error == 2 ? "abort" : error == -1 ? "invalid" : "fatal";
    set_last_error(std::format("Failed to decode batch. batchNo:{} error:'{}'", index, message));
  }
}

