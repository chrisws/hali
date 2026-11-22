// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

// basalt.cpp - Basalt in-process engine backend

#ifdef HALI_ENGINE_BASALT

#include "basalt.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <cuda_runtime.h>

#include "logging.h"
#include "util/json_escape.h"

// Redirect basalt console I/O into hali's logging system.
// The #define renames fwrite/printf/fprintf in all basalt headers
// (inline + template code) to our wrappers.  Undef'd after the last
// basalt header so the rest of this translation unit is unaffected.
#include "basalt_printf_redirect.h"

#include "basalt/model/adapter.hpp"
#include "basalt/models/builtin.hpp"
#include "basalt/kernels/sampler.hpp"
#include "basalt/program/gpu_select.hpp"
#include "basalt/program/tokenizer.hpp"
#include "basalt/core/model_file.hpp"
#include "basalt/artifact/gguf_reader.hpp"

#undef fwrite
#undef printf
#undef fprintf

#include "gpu_info.h"

using namespace basalt::model;
using namespace basalt::kernels;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Basalt::Basalt() {
  basalt_printf_redirect_set_sink([](const char *data, size_t len) {
    std::string s(data, len);
    log_write(LEVEL_INFO, "basalt", "%s", s.c_str());
  });
}

Basalt::~Basalt() = default;

//
// Global abort callback (engine.h interface) — no-op for basalt
//
void engine_set_abort_callback(void (*cb)(const char *msg)) {
  (void)cb;
}

// ---------------------------------------------------------------------------
// Error tracking
// ---------------------------------------------------------------------------

void Basalt::set_error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(_last_error, sizeof(_last_error), fmt, ap);
  va_end(ap);
}

const char *Basalt::last_error() const {
  return _last_error;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool Basalt::load_model(const ModelLoad &load) {
  _last_error[0] = '\0';

  std::vector<std::string> model_paths;
  if (!resolve_adapter(load, model_paths)) return false;
  if (!start_model(load, model_paths)) return false;

  _model_path = load.model_path;
  log_write(LEVEL_INFO, "basalt", "model: %s", _model_path.c_str());
  _tokenizer = std::make_unique<basalt::program::Tokenizer>();
  _tokenizer_loaded = load_tokenizer();
  if (!_tokenizer_loaded) {
    set_error("tokenizer failed to load");
    return false;
  }

  resolve_eos_tokens();
  report_vram();

  // Reset token state.
  _tokens.clear();
  _pos = 0;
  _prompt_done = false;
  _finished = false;
  _tokens_generated = 0;

  check_cuda_errors();

  log_write(LEVEL_INFO, "basalt", "load_model: complete, session=%p", (void *) _session);
  return true;
}

bool Basalt::resolve_adapter(const ModelLoad &load,
                             std::vector<std::string> &model_paths) {
  // Register all built-in model adapters (idempotent).
  basalt::models::register_builtin_models();

  std::string err;
  const ModelAdapter *adapter = nullptr;

  {
    basalt::core::ModelFile file;
    if (file.open(load.model_path, err) && file.load_meta(err)) {
      // .basalt path: read architecture from the meta JSON.
      const basalt::core::Json *kv = file.meta().get("gguf_kv");
      const basalt::core::Json *arch = kv ? kv->get("general.architecture") : nullptr;
      const std::string arch_name = arch && arch->is_string() ? arch->s : std::string();
      adapter = find_adapter(arch_name);
      if (!adapter) {
        set_error("no basalt adapter for architecture '%s': %s",
                  arch_name.c_str(),
                  unavailable_architecture(arch_name).c_str());
        return false;
      }
      model_paths.push_back(load.model_path);
    } else {
      // GGUF path: handles single-file and multi-shard models.
      try {
        basalt::GgufModel model = basalt::GgufModel::open(load.model_path);
        adapter = adapter_for(model.meta(), err);
        if (!adapter) {
          set_error("no basalt adapter for GGUF model: %s", err.c_str());
          return false;
        }
        // Pre-validate token_embd for a clearer diagnostic than basalt's generic message.
        // Accept BF16 (30), F16 (1), Q4_K (12), and Q8_0 (8) — the latter two dequantized to F16 at load time.
        if (const auto *te = model.find("token_embd.weight");
            te && te->type != 30 && te->type != 1 && te->type != 12 && te->type != 8) {
          set_error("token_embd.weight is %s (%lld elements); basalt requires bf16, f16, q4_k or q8_0",
                    te->type_name(), (long long)te->elements());
          return false;
        }
        for (size_t i = 0; i < model.size(); ++i)
          model_paths.push_back(model.shard(i).path());
      } catch (const std::exception &e) {
        set_error("cannot open model file (neither .basalt nor GGUF): %s",
                  e.what());
        return false;
      }
    }
  }

  _adapter = adapter;
  return true;
}

bool Basalt::start_model(const ModelLoad &load,
                         const std::vector<std::string> &model_paths) {
  // Check that the adapter supports the standard in-process start path.
  // Some models (e.g. qwen4exp) run their own start via run() and do not
  // expose a LoadedModel, so make_model() returns nullptr and start()
  // refuses.
  if (!_adapter->make_model()) {
    set_error("model '%s' does not support in-process loading",
              _adapter->arch());
    return false;
  }

  // Build the LoadRequest.
  LoadRequest req;
  for (const auto &p : model_paths)
    req.paths.push_back(p);
  req.max_context = load.n_ctx > 0 ? load.n_ctx : 0;
  req.sessions = 1;
  req.vision = false;
  req.draft = false;

  // Start the model.
  Started started;
  Result r = _adapter->start(req, started);
  if (!r.ok()) {
    set_error("basalt start failed: %s", r.message.c_str());
    return false;
  }

  _started = std::make_unique<Started>(std::move(started));
  _session = _started->sessions.empty() ? nullptr
                                         : _started->sessions[0].get();
  if (!_session) {
    set_error("basalt start produced no sessions");
    return false;
  }
  return true;
}

void Basalt::resolve_eos_tokens() {
  _eos_ids.clear();
  if (_tokenizer_loaded && _tokenizer) {
    const TokenNames names = _adapter->token_names();
    for (const auto &eos_name : names.eos) {
      std::vector<int32_t> ids;
      std::string terr;
      if (_tokenizer->encode(eos_name, true, {}, ids, terr) && !ids.empty()) {
        _eos_ids.insert(_eos_ids.end(), ids.begin(), ids.end());
      }
    }
    log_write(LEVEL_INFO, "basalt", "load_model: %zu EOS token(s) resolved", _eos_ids.size());
  }
}

void Basalt::report_vram() {
  bool known = false;
  auto cards = basalt::program::visible_gpus(known);
  if (!cards.empty() && cards[0].vram > 0) {
    gpu_info::set_vram_bytes(cards[0].vram);
  }
}

void Basalt::check_cuda_errors() {
  int ce = cudaGetLastError();
  if (ce != 0) {
    log_write(LEVEL_INFO, "basalt", "load_model: CUDA error after load: %s",
              cudaGetErrorString((cudaError_t) ce));
  }
  ce = cudaDeviceSynchronize();
  if (ce != 0) {
    log_write(LEVEL_INFO, "basalt", "load_model: CUDA device sync after load: %s",
              cudaGetErrorString((cudaError_t) ce));
  }
}

void Basalt::reset() {
  _tokens.clear();
  _pos = 0;
  _prompt_done = false;
  _finished = false;
  _tokens_generated = 0;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void Basalt::add_stop(const char *stop) {
  _stops.push_back(stop);
}

void Basalt::set_max_tokens(int n) { _max_tokens = n; }
void Basalt::set_temperature(float v) { _sampling.temperature = v; }
void Basalt::set_top_k(int v) { _sampling.top_k = v; }
void Basalt::set_top_p(float v) { _sampling.top_p = v; }
void Basalt::set_min_p(float v) { _sampling.min_p = v; }
void Basalt::set_penalty_repeat(float v) { _sampling.penalty_repeat = v; }
void Basalt::set_penalty_freq(float v) { _sampling.penalty_freq = v; }
void Basalt::set_penalty_present(float v) { _sampling.penalty_present = v; }
void Basalt::set_penalty_last_n(int32_t v) { _sampling.penalty_last_n = v; }
void Basalt::set_seed(unsigned int v) { _sampling.seed = v; }
void Basalt::set_log_level(int) { /* no-op */ }

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

bool Basalt::add_message(ModelIter &iter, const string &role,
                         const string &content) {
  (void)role;

  // Tokenize the content.
  std::vector<int32_t> new_tokens;
  if (_tokenizer_loaded && _tokenizer) {
    std::string terr;
    if (!_tokenizer->encode(content, true, {}, new_tokens, terr)) {
      set_error("tokenization failed: %s", terr.c_str());
      return false;
    }
  } else {
    // Fallback: byte-level (experimental, produces garbage for the model).
    for (unsigned char c : content) {
      new_tokens.push_back(static_cast<int32_t>(c));
    }
  }

  _tokens.insert(_tokens.end(), new_tokens.begin(), new_tokens.end());

  // Run prefill on the accumulated tokens.
  if (!do_prefill()) {
    return false;
  }
  _prompt_done = true;
  _finished = false;
  _tokens_generated = 0;
  iter._has_next = true;
  return true;
}

string Basalt::next(ModelIter &iter) {
  if (_finished || !_session) {
    log_write(LEVEL_DEBUG, "basalt", "next: early exit (finished=%d, session=%p)",
              (int) _finished, (void *) _session);
    iter._has_next = false;
    return "";
  }

  int32_t tok = do_decode();
  if (tok < 0) {
    log_write(LEVEL_ERROR, "basalt", "next: do_decode returned %d", tok);
    _finished = true;
    iter._has_next = false;
    return "";
  }

  _tokens.push_back(tok);
  _pos++;
  _tokens_generated++;

  // Check EOS.
  if (std::find(_eos_ids.begin(), _eos_ids.end(), tok) != _eos_ids.end()) {
    log_write(LEVEL_INFO, "basalt", "next: EOS token %d at pos %zu", tok, _pos);
    _finished = true;
    iter._has_next = false;
    return "";
  }

  // Check max tokens.
  if (_tokens_generated >= _max_tokens) {
    _finished = true;
    iter._has_next = false;
  }

  // Decode token to string via the vocabulary.
  string result;
  if (_tokenizer_loaded && _tokenizer) {
    const std::string &bytes = _tokenizer->token_bytes(tok);
    result = bytes;
  } else {
    // Fallback: single byte (experimental).
    result = string(1, static_cast<char>(tok));
  }
  iter._last_word = result;
  return result;
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

ModelMemoryInfo Basalt::memory_info() const {
  ModelMemoryInfo info = {};
  if (_started && _started->model) {
    const ModelDesc &d = _started->model->desc();
    info.n_layers_total = static_cast<int>(d.n_layers);
    info.model_native_max_ctx = static_cast<int>(d.context_length);
  }
  return info;
}

float Basalt::memory_kv_percent() const {
  // TODO: derive from session->running_state() or expert_counts()
  return 0.0f;
}

bool Basalt::is_memory_flush() const {
  return false;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

bool Basalt::load_tokenizer() {
  if (!_adapter || _model_path.empty()) return false;

  std::string err;

  // --- Path 1: .basalt pack ---
  {
    basalt::core::ModelFile file;
    if (file.open(_model_path, err) && file.load_meta(err)) {
      std::vector<uint8_t> vj, mj, tj;
      if (basalt::core::read_range(file.range("tokenizer/vocab.json"), vj, err) &&
          basalt::core::read_range(file.range("tokenizer/merges.txt"), mj, err) &&
          basalt::core::read_range(file.range("tokenizer/token_type.json"), tj, err)) {
        auto pre = _adapter->pretokenizer();
        _tokenizer->set_pretokenize(pre.split);
        if (_tokenizer->load(std::string_view((const char *)vj.data(), vj.size()),
                             std::string_view((const char *)mj.data(), mj.size()),
                             std::string_view((const char *)tj.data(), tj.size()), err)) {
          log_write(LEVEL_INFO, "basalt", "tokenizer: loaded %zu tokens from .basalt", _tokenizer->vocab_size());
          return true;
        }
        log_write(LEVEL_INFO, "basalt", "tokenizer: .basalt load failed: %s", err.c_str());
        return false;
      }
      log_write(LEVEL_INFO, "basalt", "tokenizer: .basalt missing sections (%s)", err.c_str());
      // Fall through to GGUF.
    }
  }

  // --- Path 2: GGUF ---
  try {
    basalt::GgufModel model = basalt::GgufModel::open(_model_path);
    const auto &meta = model.meta();

    const basalt::MetaValue *vocab = meta.get("tokenizer.ggml.tokens");
    const basalt::MetaValue *merges = meta.get("tokenizer.ggml.merges");
    const basalt::MetaValue *tok_type = meta.get("tokenizer.ggml.token_type");
    log_write(LEVEL_INFO, "basalt", "tokenizer: GGUF keys — tokens=%s(%zu) merges=%s(%zu) token_type=%s(%zu)",
              vocab ? "yes" : "no", vocab ? vocab->count : 0,
              merges ? "yes" : "no", merges ? merges->count : 0,
              tok_type ? "yes" : "no", tok_type ? tok_type->count : 0);
    if (!vocab || vocab->type != basalt::MetaType::ARRAY ||
        !merges || merges->type != basalt::MetaType::ARRAY) {
      log_write(LEVEL_INFO, "basalt", "tokenizer: GGUF has no tokenizer data");
      return false;
    }

    // Diagnostic (added 2026-11-22): inspect raw merge items to diagnose
    // "merges.txt: line 0 is not a pair" failure seen with Qwen3.5-9B-Q8_0.
    //
    // Root cause hypothesis: the GGUF spec stores tokenizer.ggml.merges as
    // an array where EACH item is a complete "left right" merge pair string
    // (e.g. "Ġ a"). The code below assumes a flat array of individual tokens
    // (items[2i]=left, items[2i+1]=right), which is wrong for this format.
    //
    // What to look for in the log:
    //   - If merges[0] contains a space (e.g. "Ġ a"), items are complete pairs
    //     and the pairing loop below is the bug.
    //   - If merges[0] has no space (e.g. "Ġ"), items are individual tokens
    //     and the pairing loop is correct.
    //   - ODD count strongly suggests complete-pair format (no unpaired token).
    {
      size_t n_show = merges->count < 4 ? merges->count : 4;
      for (size_t i = 0; i < n_show; ++i) {
        log_write(LEVEL_INFO, "basalt", "tokenizer: merges[%zu] = \"%s\" (len=%zu)",
                  i, merges->items[i].s.c_str(), merges->items[i].s.size());
      }
      log_write(LEVEL_INFO, "basalt", "tokenizer: merges count=%zu parity=%s",
                merges->count, (merges->count % 2 == 0) ? "even" : "ODD");
    }

    // Build vocab_json: {"token": id, ...}
    std::string vocab_json = "{";
    for (size_t i = 0; i < vocab->count; ++i) {
      if (i > 0) vocab_json += ",";
      vocab_json += "\"";
      const std::string &tok = vocab->items[i].s;
      json_escape_utf8(vocab_json, tok);
      vocab_json += "\":";
      vocab_json += std::to_string(i);
    }
    vocab_json += "}";
    log_write(LEVEL_INFO, "basalt", "tokenizer: vocab_json built (%zu bytes, %zu tokens)",
              vocab_json.size(), vocab->count);

    // Diagnostic: log first 3 token names to verify the vocab array is being
    // read correctly. Token 0 should typically be a special token like
    // "<s>" or "!" — if it looks like a merge pair, the arrays are misaligned.
    {
      size_t n_show = vocab->count < 3 ? vocab->count : 3;
      for (size_t i = 0; i < n_show; ++i) {
        log_write(LEVEL_INFO, "basalt", "tokenizer: vocab[%zu] = \"%s\"",
                  i, vocab->items[i].s.c_str());
      }
    }

    // Build merges_txt: one "left right" per line.
    //
    // Format detection: do the raw GGUF merge items already contain a
    // space (i.e. they are complete "left right" pair strings)?
    bool merges_are_pairs = false;
    {
      size_t n_check = merges->count < 8 ? merges->count : 8;
      for (size_t i = 0; i < n_check; ++i) {
        if (merges->items[i].s.find(' ') != std::string::npos) {
          merges_are_pairs = true;
          break;
        }
      }
      log_write(LEVEL_INFO, "basalt",
                "tokenizer: merges format: items %s contain a space (checked %zu items)",
                merges_are_pairs ? "DO" : "do NOT", n_check);
    }

    std::string merges_txt;
    if (merges_are_pairs) {
      // Each item is already a complete "left right" pair string.
      for (size_t i = 0; i < merges->count; ++i) {
        merges_txt += merges->items[i].s;
        merges_txt += "\n";
      }
      log_write(LEVEL_INFO, "basalt",
                "tokenizer: using direct-pair format (%zu lines)", merges->count);
    } else {
      // Flat array of individual tokens: pair them up.
      for (size_t i = 0; i + 1 < merges->count; i += 2) {
        merges_txt += merges->items[i].s;
        merges_txt += " ";
        merges_txt += merges->items[i + 1].s;
        merges_txt += "\n";
      }
      if (merges->count % 2 != 0) {
        log_write(LEVEL_INFO, "basalt",
                  "tokenizer: WARNING: merges count %zu is ODD; last item \"%s\" is dropped",
                  merges->count, merges->items[merges->count - 1].s.c_str());
      }
    }

    // 2) Actual line count from the constructed string (not count/2).
    {
      size_t line_count = 0;
      for (size_t i = 0; i < merges_txt.size(); ++i) {
        if (merges_txt[i] == '\n') line_count++;
      }
      log_write(LEVEL_INFO, "basalt",
                "tokenizer: merges_txt actual line count = %zu (count/2 = %zu)",
                line_count, merges->count / 2);
    }

    // 3) Token count in the first line (split by space).
    {
      size_t first_nl = merges_txt.find('\n');
      std::string first_line = (first_nl != std::string::npos)
                                 ? merges_txt.substr(0, first_nl)
                                 : merges_txt;
      size_t tok_count = 1;
      for (char c : first_line) {
        if (c == ' ') tok_count++;
      }
      log_write(LEVEL_INFO, "basalt",
                "tokenizer: merges_txt first line = \"%s\" (%zu tokens, expected 2)",
                first_line.c_str(), tok_count);
    }

    // 4) Sample middle and last lines to check for consistency.
    {
      size_t last_nl = merges_txt.rfind('\n');
      size_t prev_nl = (last_nl > 0) ? merges_txt.rfind('\n', last_nl - 1) : std::string::npos;
      std::string last_line = (prev_nl != std::string::npos)
                                ? merges_txt.substr(prev_nl + 1, last_nl - prev_nl - 1)
                                : merges_txt.substr(0, last_nl);
      log_write(LEVEL_INFO, "basalt", "tokenizer: merges_txt last line = \"%s\"",
                last_line.c_str());

      size_t mid_pos = merges_txt.size() / 2;
      size_t mid_before = merges_txt.rfind('\n', mid_pos);
      size_t mid_after = merges_txt.find('\n', mid_pos);
      if (mid_before != std::string::npos && mid_after != std::string::npos) {
        std::string mid_line = merges_txt.substr(mid_before + 1, mid_after - mid_before - 1);
        log_write(LEVEL_INFO, "basalt", "tokenizer: merges_txt middle line = \"%s\"",
                  mid_line.c_str());
      }
    }

    // Build types_json from the GGUF token_type array, defaulting to 4.
    std::string types_json = "[";
    for (size_t i = 0; i < vocab->count; ++i) {
      if (i > 0) types_json += ",";
      if (tok_type && tok_type->type == basalt::MetaType::ARRAY && i < tok_type->count) {
        types_json += std::to_string((int) tok_type->items[i].u);
      } else {
        types_json += "4";
      }
    }
    types_json += "]";

    log_write(LEVEL_INFO, "basalt", "tokenizer: types_json built (%zu bytes, %zu entries)",
              types_json.size(), vocab->count);

    // Diagnostic: final summary of all three inputs before the load call.
    // If load fails, this line plus the error message above it give the full
    // picture of what was passed in. Cross-reference with the merges/vocab
    // diagnostics earlier in this function to pinpoint which input is bad.
    auto pre = _adapter->pretokenizer();
    _tokenizer->set_pretokenize(pre.split);
    log_write(LEVEL_INFO, "basalt", "tokenizer: calling load(vocab=%zuB, merges=%zuB, types=%zuB)",
              vocab_json.size(), merges_txt.size(), types_json.size());
    if (!_tokenizer->load(vocab_json, merges_txt, types_json, err)) {
      log_write(LEVEL_INFO, "basalt", "tokenizer: GGUF load failed: %s", err.c_str());
      return false;
    }
    log_write(LEVEL_INFO, "basalt", "tokenizer: loaded %zu tokens from GGUF", _tokenizer->vocab_size());
    return true;
  } catch (const std::exception &e) {
    log_write(LEVEL_INFO, "basalt", "tokenizer: GGUF open failed: %s", e.what());
    return false;
  }
}

bool Basalt::do_prefill() {
  if (_tokens.empty() || !_session) return true;

  log_write(LEVEL_INFO, "basalt", "do_prefill: %zu tokens, pos=0", _tokens.size());

  PrefillRequest pr;
  pr.tokens = _tokens.data();
  pr.n_tokens = static_cast<int>(_tokens.size());
  pr.pos = 0;
  pr.total_tokens = pr.n_tokens;
  pr.more = false;

  Result r = _session->prefill_chunk(pr);
  if (!r.ok()) {
    set_error("prefill failed: %s", r.message.c_str());
    return false;
  }

  log_write(LEVEL_INFO, "basalt", "do_prefill: prefill_chunk ok, calling end_prompt");
  _pos = _tokens.size();
  _session->end_prompt();
  log_write(LEVEL_INFO, "basalt", "do_prefill: done, pos=%zu", _pos);
  return true;
}

int32_t Basalt::do_decode() {
  if (!_session) return -1;

  // Build the sampling params.
  SamplerParams sp;
  sp.top_k = _sampling.top_k;
  sp.top_p = _sampling.top_p;
  sp.min_p = _sampling.min_p;
  sp.temperature = _sampling.temperature;
  sp.penalty_last_n = _sampling.penalty_last_n;
  sp.penalty_repeat = _sampling.penalty_repeat;
  sp.penalty_freq = _sampling.penalty_freq;
  sp.penalty_present = _sampling.penalty_present;
  sp.seed = _sampling.seed;
  sp.counter = static_cast<uint64_t>(_pos);

  // Build the decode request.
  int32_t next_tok = 0;
  DecodeRequest dr;
  dr.tokens = _tokens.data() + _pos - 1;  // last token as row 0
  dr.n_tokens = 1;
  dr.pos = _pos - 1;
  dr.next_token = &next_tok;
  dr.sampling = &sp;

  // Pass recent token history so the sampler can apply penalties.
  int hist_len = _sampling.penalty_last_n;
  if (hist_len > 0 && _pos > 0) {
    if (hist_len > (int) _pos) hist_len = (int) _pos;
    dr.history = _tokens.data() + _pos - hist_len;
    dr.history_len = hist_len;
  }

  Result r = _session->decode_window(dr);
  if (!r.ok()) {
    set_error("decode failed: %s", r.message.c_str());
    log_write(LEVEL_ERROR, "basalt", "do_decode: %s", r.message.c_str());
    return -1;
  }

  log_write(LEVEL_DEBUG, "basalt", "do_decode: ok, token=%d pos=%zu", next_tok, _pos);
  return next_tok;
}

#endif // HALI_ENGINE_BASALT
