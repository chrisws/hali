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

#include <cstdarg>
#include <cstdio>
#include <string>

#include "logging.h"

// Redirect basalt console I/O into hali's logging system.
// The #define renames fwrite/printf/fprintf in all basalt headers
// (inline + template code) to our wrappers.  Undef'd after the last
// basalt header so the rest of this translation unit is unaffected.
#include "basalt_printf_redirect.h"

#include "basalt/model/adapter.hpp"
#include "basalt/models/builtin.hpp"
#include "basalt/kernels/sampler.hpp"
#include "basalt/program/gpu_select.hpp"
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

  // Register all built-in model adapters (idempotent).
  basalt::models::register_builtin_models();

  // Open the model file and resolve the adapter.
  // Try .basalt first, then fall back to GGUF (single or multi-shard).
  std::string err;
  const ModelAdapter *adapter = nullptr;
  std::vector<std::string> model_paths;

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

  // Check that the adapter supports the standard in-process start path.
  // Some models (e.g. qwen4exp) run their own start via run() and do not
  // expose a LoadedModel, so make_model() returns nullptr and start()
  // refuses.
  if (!_adapter->make_model()) {
    set_error("model '%s' does not support in-process loading ",
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

  // Report VRAM to the gpu_info module.
  {
    bool known = false;
    auto cards = basalt::program::visible_gpus(known);
    if (!cards.empty() && cards[0].vram > 0) {
      gpu_info::set_vram_bytes(cards[0].vram);
    }
  }

  // Reset token state.
  _tokens.clear();
  _pos = 0;
  _prompt_done = false;
  _finished = false;
  _tokens_generated = 0;

  return true;
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
  // Tokenize the content using the adapter's pretokenizer.
  // For the experimental build we use a simple byte-to-token mapping;
  // a production build would call the pretokenizer's split function.
  //
  // TODO: use _adapter->pretokenizer().split() for real tokenization.
  // For now, treat each byte as a token id (experimental only).
  for (unsigned char c : content) {
    _tokens.push_back(static_cast<int32_t>(c));
  }

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
    iter._has_next = false;
    return "";
  }

  int32_t tok = do_decode();
  if (tok < 0) {
    _finished = true;
    iter._has_next = false;
    return "";
  }

  _tokens.push_back(tok);
  _pos++;
  _tokens_generated++;

  // Check max tokens.
  if (_tokens_generated >= _max_tokens) {
    _finished = true;
    iter._has_next = false;
  }

  // Decode token to string (experimental: single byte).
  // TODO: use proper token-to-string via the model's vocabulary.
  string result(1, static_cast<char>(tok));
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

bool Basalt::do_prefill() {
  if (_tokens.empty() || !_session) return true;

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

  _pos = _tokens.size();
  _session->end_prompt();
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

  Result r = _session->decode_window(dr);
  if (!r.ok()) {
    set_error("decode failed: %s", r.message.c_str());
    return -1;
  }

  return next_tok;
}

#endif // HALI_ENGINE_BASALT
