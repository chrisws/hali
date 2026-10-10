// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "llama.h"
#include "sampling.h"

using namespace std;

struct Llama;
struct RagDB;
struct RagSession;
struct server_context;
struct server_response_reader;

enum class KVCachePreset {
  F16,       // ggml_type F16/F16, flash attn on - max quality, most VRAM
  Balanced,  // type Q8_0/Q8_0, flash attn on - good middle ground
  Compact,   // type Q4_0/Q4_0, flash attn on - max memory savings
};

struct LlamaMemoryInfo {
  // KV cache
  int     kv_used;        // slots currently used
  int     kv_total;       // total slots (== n_ctx)
  float   kv_percent;     // kv_used / kv_total

  // GPU VRAM (via ggml backend)
  size_t  vram_used;      // bytes
  size_t  vram_total;     // bytes
  float   vram_percent;

  // Model layers
  int     n_layers_total; // total model layers
  int     n_layers_gpu;   // layers offloaded to GPU
  int     n_layers_cpu;   // layers on CPU
  int     model_native_max_ctx;

  // Advice
  string  advice;
};

struct LlamaIter {
  explicit LlamaIter();
  ~LlamaIter();

  // move constructor
  LlamaIter(LlamaIter &&other) noexcept;

  // delete the copy
  LlamaIter(const LlamaIter &) = delete;
  LlamaIter &operator=(const LlamaIter &) = delete;

  Llama *_llama;
  string _last_word;
  string _tail;
  chrono::high_resolution_clock::time_point _t_start;
  int _repetition_count;
  int _tokens_generated;
  bool _has_next;
  std::unique_ptr<server_response_reader> _reader;
};

struct LlamaLoad {
  string model_path;
  int n_ctx;
  int n_batch;
  int n_gpu_layers;
  int log_level;
  bool offload_kqv;
  int n_threads;        // threads for single-token generation
  int n_threads_batch;  // threads for prompt / batch processing
  // Context extension (relevant when VRAM caps n_ctx)
  enum llama_rope_scaling_type rope_scaling_type;  // NONE / LINEAR / YARN / LONGROPE
  float rope_freq_scale;    // 0 = use model default
  KVCachePreset kv_cache_preset;
  // MTP (Multi-Token Prediction) speculative decoding
  bool mtp_enabled = true;   // auto-detect if true, force off if false
  int  mtp_n_max = 3;        // max draft tokens per step
  int  mtp_n_min = 0;        // min accepted tokens to use MTP
  float mtp_p_min = 0.0f;    // min probability threshold for draft acceptance
};

struct Llama {
  explicit Llama();

  // move constructor
  Llama(Llama &&other) noexcept;

  // delete the copy
  Llama(const Llama &) = delete;
  Llama &operator=(const Llama &) = delete;

  ~Llama();

  // init
  bool load_model(const LlamaLoad &load);
  bool load_embedding_model(const string &model_path);

  // generation
  bool add_message(LlamaIter &iter, const string &role, const string &content);
  string next(LlamaIter &iter);
  string all(LlamaIter &iter);

  // generation parameters
  void add_stop(const char *stop) { _stop_sequences.push_back(stop); }
  void clear_stops() { _stop_sequences.clear(); }
  void set_penalty_last_n(int32_t penalty_last_n) { _penalty_last_n = penalty_last_n; dirty(); }
  void set_penalty_repeat(float penalty_repeat) { _penalty_repeat = penalty_repeat; dirty(); }
  void set_penalty_freq(float penalty_freq) { _penalty_freq = penalty_freq; dirty(); }
  void set_penalty_present(float penalty_present) { _penalty_present = penalty_present; dirty(); }
  void set_max_tokens(int max_tokens) { _max_tokens = max_tokens; dirty(); }
  void set_min_p(float min_p) { _min_p = min_p; dirty(); }
  void set_temperature(float temperature) { _temperature = temperature; dirty(); }
  void set_top_k(int top_k) { _top_k = top_k; dirty(); }
  void set_top_p(float top_p) { _top_p = top_p; dirty(); }
  void set_grammar(const string &src, const string &root);
  void set_seed(unsigned int seed) { _seed = seed; dirty(); }

  // error handling
  const char *last_error() const { return _last_error.c_str(); }
  void set_log_level(int level) { _log_level = level; }
  void reset();
  bool is_memory_flush();
  bool is_can_shift() const { return _can_shift; }
  bool is_gemma_4() const { return _is_gemma4; }

  // memory info
  LlamaMemoryInfo memory_info() const;
  float memory_kv_percent() const;

  // creates an embedding vector of the given dimension for the given text
  bool embed_text(const std::string &text, std::vector<float> &out, int embed_dim);

  // retrieves rag query context information from the rag database
  std::string rag_retrieve(const RagDB &db, const std::string &query, int top_k, RagSession &session);

  // indexes the details from the given file
  bool rag_index(RagDB &db, const std::string &filepath);

  //  returns the embedding dimension for the loaded model
  int get_embed_dim() const { return _emb_model != nullptr ? llama_model_n_embd(_emb_model) : 0; }

  // MTP (Multi-Token Prediction) speculative decoding
  bool is_mtp_enabled() const { return _mtp_enabled; }
  int  mtp_n_layers() const { return _n_mtp_layers; }
  void set_mtp_n_max(int n) { _mtp_n_max = n; }
  void set_mtp_n_min(int n) { _mtp_n_min = n; }
  void set_mtp_p_min(float p) { _mtp_p_min = p; }

private:
  void dirty() {_sampler_dirty = true; }
  vector<llama_token> tokenize(const string &prompt);
  string token_to_string(LlamaIter &iter, llama_token tok) const;
  void set_last_error(const string &message);
  void set_decode_error(int32_t error, int index, int num_tokens);
  string process_text_chunk(LlamaIter &iter, const string &text);
  void stop_server();

  // generation via upstream server_context
  std::unique_ptr<server_context> _server_ctx;
  std::thread _server_thread;
  bool _server_running = false;

  // embedding model (independent of generation context)
  llama_model *_emb_model;
  llama_context *_emb_ctx;

  // shared state
  const llama_vocab *_vocab;
  string _template;
  vector<string> _stop_sequences;
  string _grammar_src;
  string _grammar_root;
  string _last_error;
  int32_t _penalty_last_n;
  float _penalty_repeat;
  float _penalty_freq;
  float _penalty_present;
  float _temperature;
  float _top_p;
  float _min_p;
  int _top_k;
  int _max_tokens;
  int _log_level;
  int _n_gpu_layers;
  bool _is_gemma4;
  bool _sampler_dirty;
  bool _can_shift;
  bool _memory_flush;
  unsigned int _seed;

  // MTP configuration (read from server context after load)
  int _n_mtp_layers;
  bool _mtp_enabled;
  int _mtp_n_max;
  int _mtp_n_min;
  float _mtp_p_min;
};
