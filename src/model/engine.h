// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

// engine.h - abstract inference engine interface (dependency inversion)
//
// agent.cpp depends on Engine, not on Llama or Basalt directly.
// Each backend (llama.cpp, basalt) provides a concrete implementation.

#ifndef ENGINE_H
#define ENGINE_H

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

using namespace std;

// ---------------------------------------------------------------------------
// Shared types
// ---------------------------------------------------------------------------

enum class KVCachePreset {
  F16,       // max quality, most VRAM
  Balanced,  // good middle ground
  Compact,   // max memory savings
};

struct ModelMemoryInfo {
  // KV cache / expert cache
  int     kv_used;
  int     kv_total;
  float   kv_percent;

  // GPU VRAM
  size_t  vram_used;
  size_t  vram_total;
  float   vram_percent;

  // Model layers (llama.cpp only; basalt leaves these 0)
  int     n_layers_total;
  int     n_layers_gpu;
  int     n_layers_cpu;
  int     model_native_max_ctx;

  // Advice string for the UI
  string  advice;
};

struct ModelLoad {
  string model_path;
  int n_ctx;
  int n_batch;
  int n_ubatch;
  int n_gpu_layers;
  int log_level;
  bool offload_kqv;
  int n_threads;
  int n_threads_batch;
  // Context extension (llama.cpp only; basalt ignores)
  int   rope_scaling_type = 0;   // 0 = unspecified
  float rope_freq_scale = 0.0f;
  KVCachePreset kv_cache_preset;
  // MTP speculative decoding (llama.cpp only; basalt ignores)
  bool mtp_enabled = true;
  int  mtp_n_max = 3;
  int  mtp_n_min = 0;
  float mtp_p_min = 0.0f;
  float mtp_pos_threshold = 0.30f;
  bool mtp_adaptive = true;
  // Suffix drafting (llama.cpp only; basalt ignores)
  bool suffix_enabled = false;
  int  suffix_ngram = 4;
  int  suffix_max_draft = 2;
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

struct ModelIter;

// ---------------------------------------------------------------------------
// Engine - abstract interface
// ---------------------------------------------------------------------------

struct Engine {
  virtual ~Engine() = default;

  // --- lifecycle -----------------------------------------------------------
  virtual bool load_model(const ModelLoad &load) = 0;
  virtual void reset() = 0;
  virtual const char *last_error() const = 0;

  // --- configuration -------------------------------------------------------
  virtual void add_stop(const char *stop) = 0;
  virtual void set_max_tokens(int n) = 0;
  virtual void set_temperature(float v) = 0;
  virtual void set_top_k(int v) = 0;
  virtual void set_top_p(float v) = 0;
  virtual void set_min_p(float v) = 0;
  virtual void set_penalty_repeat(float v) = 0;
  virtual void set_penalty_freq(float v) = 0;
  virtual void set_penalty_present(float v) = 0;
  virtual void set_penalty_last_n(int32_t v) = 0;
  virtual void set_seed(unsigned int v) = 0;
  virtual void set_log_level(int level) = 0;

  // --- generation ----------------------------------------------------------
  virtual bool add_message(ModelIter &iter, const string &role,
                           const string &content) = 0;
  virtual string next(ModelIter &iter) = 0;

  // --- memory --------------------------------------------------------------
  virtual ModelMemoryInfo memory_info() const = 0;
  virtual float memory_kv_percent() const = 0;
  virtual bool is_memory_flush() const = 0;

  // --- model info ----------------------------------------------------------
  virtual bool is_gemma_4() const { return false; }
  virtual bool is_can_shift() const { return false; }

  // --- capabilities (agent.cpp gates features on these) ---------------------
  virtual bool supports_rag() const { return false; }
  virtual bool supports_mtp_tuning() const { return false; }
  virtual bool supports_kv_control() const { return false; }
};

// ---------------------------------------------------------------------------
// ModelIter - per-generation iteration state (engine-agnostic)
// ---------------------------------------------------------------------------

struct ModelIter {
  explicit ModelIter();
  ~ModelIter() = default;

  ModelIter(ModelIter &&other) noexcept;
  ModelIter(const ModelIter &) = delete;
  ModelIter &operator=(const ModelIter &) = delete;

  Engine *_engine;
  string _last_word;
  string _tail;
  chrono::high_resolution_clock::time_point _t_start;
  int _repetition_count;
  int _tokens_generated;
  bool _has_next;
  bool _ignore_stop;
};

#endif // ENGINE_H