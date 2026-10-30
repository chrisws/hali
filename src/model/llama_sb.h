// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef LLAMA_SB_H
#define LLAMA_SB_H

#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "llama.h"
#include "speculative.h"
#include "sampling.h"
#include "llama_sb_suffix.h"

using namespace std;

struct Llama;
struct RagDB;
struct RagSession;
struct RagLibrary;

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

  // Model layers (offloadable tensor slots: n_layer_all + 1)
  int     n_layers_total; // total offloadable slots (transformer layers + output tensor)
  int     n_layers_gpu;   // slots offloaded to GPU
  int     n_layers_cpu;   // slots on CPU
  int     model_native_max_ctx;

  // Advice
  string  advice;
};

struct LlamaIter {
  explicit LlamaIter();
  ~LlamaIter() = default;

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
};

struct LlamaLoad {
  string model_path;
  int n_ctx;
  int n_batch;
  int n_ubatch;
  int n_gpu_layers;
  int log_level;
  bool offload_kqv;
  int n_threads;        // threads for single-token generation
  int n_threads_batch;  // threads for prompt / batch processing
  // Context extension (relevant when VRAM caps n_ctx)
  enum llama_rope_scaling_type rope_scaling_type = LLAMA_ROPE_SCALING_TYPE_UNSPECIFIED;
  float rope_freq_scale = 0.0f;  // 0 = use model default
  KVCachePreset kv_cache_preset;
  // MTP (Multi-Token Prediction) speculative decoding
  bool mtp_enabled = true;   // auto-detect if true, force off if false
  int  mtp_n_max = 3;        // max draft tokens per step
  int  mtp_n_min = 0;        // min accepted tokens to use MTP
  float mtp_p_min = 0.0f;    // min probability threshold for draft acceptance
  float mtp_pos_threshold = 0.30f; // initial per-position acceptance threshold
  bool mtp_adaptive = true;  // enable self-tuning of MTP parameters
  // Suffix drafting (plan 08)
  bool suffix_enabled = false;
  int  suffix_ngram = 4;
  int  suffix_max_draft = 2;
};

// ---------------------------------------------------------------------------
// MTP (Multi-Token Prediction) speculative decoding state
// ---------------------------------------------------------------------------
struct MtpState {
  // --- capability & config ceiling ---------------------------------------
  int   n_layers             = 0;
  bool  enabled              = false;
  bool  disabled_permanently = false;
  int   n_max                = 3;    // config ceiling (hard bound)
  int   n_min                = 0;    // config floor
  float p_min                = 0.0f; // config initial

  // --- speculative context ------------------------------------------------
  common_speculative_init_result_ptr spec_init;
  common_speculative_ptr             spec;
  common_params                      spec_params;
  common_sampler                    *smpl = nullptr;

  // --- pending-token ring buffer -------------------------------------------
  static constexpr int BUF_MAX = 16;
  llama_token buf[BUF_MAX] = {};
  int buf_head = 0;
  int buf_len  = 0;

  // ring-buffer helpers
  void buf_clear()   { buf_head = 0; buf_len = 0; }
  bool buf_empty() const { return buf_len == 0; }
  int  buf_size()  const { return buf_len; }
  llama_token buf_front() const { return buf[buf_head]; }
  void buf_pop_front() { buf_head = (buf_head + 1) % BUF_MAX; buf_len--; }
  void buf_push_back(llama_token tok) {
    buf[(buf_head + buf_len) % BUF_MAX] = tok; buf_len++;
  }
  void buf_assign(const llama_token *src, int n) {
    buf_head = 0; buf_len = n;
    for (int i = 0; i < n; ++i) { buf[i] = src[i]; }
  }

  // --- anchor / pending ----------------------------------------------------
  llama_token anchor      = LLAMA_TOKEN_NULL;
  bool        has_pending = false;

  // --- cumulative stats ------------------------------------------------------
  uint64_t rounds   = 0;
  uint64_t drafted  = 0;
  uint64_t accepted = 0;

  // --- suffix drafting stats (plan 08) ---------------------------------------
  uint64_t suffix_rounds   = 0;
  uint64_t suffix_drafted  = 0;
  uint64_t suffix_accepted = 0;
  int      suffix_hits     = 0;   // lookups that found a match
  int      suffix_misses   = 0;   // lookups that missed
  bool     suffix_active   = false; // hysteresis: currently in suffix mode
  int      suffix_low_hit_streak = 0; // consecutive evals with hit_rate < 0.05
  bool     suffix_disabled = false;   // circuit breaker: suffix turned off

  // --- sliding window --------------------------------------------------------
  static constexpr int WIN = 32;
  struct WinEntry {
    int drafted;
    int accepted;
    int pos_acc[4] = {};   // per-position acceptance (plan 3)
    int source = 0;        // 0=MTP, 1=suffix (plan 08)
  };
  WinEntry win[WIN] = {};
  int win_idx      = 0;
  int win_filled   = 0;
  int win_drafted  = 0;
  int win_accepted = 0;
  int last_limit   = 0;

  // --- adaptive state (plan 3) -----------------------------------------------
  struct Adaptive {
    int   eval_counter  = 0;
    int   cooldown      = 0;
    int   eff_n_max     = 3;
    float pos_threshold = 0.30f;
    float p_min         = 0.0f;
    int   n_min         = 1;
    // best-settings memory: the combo that produced the highest t/s
    struct Best {
      int   eff_n_max     = 3;
      float p_min         = 0.0f;
      int   n_min         = 1;
      float pos_threshold = 0.30f;
      double tps          = 0.0;
      bool   valid        = false;
    };
    Best best;
    // windowed t/s (computed each eval)
    double current_tps = 0.0;
    // timing snapshot for windowed t/s delta
    uint64_t time_gpu_at_eval = 0;
    uint64_t time_cpu_at_eval = 0;
  };
  Adaptive adapt;
  bool     adaptive_enabled = true;

  // --- pre-allocated round buffers -------------------------------------------
  vector<llama_token>   acc_tokens;
  vector<llama_token>   verify_toks;
  vector<llama_pos>     verify_pos;
  vector<int32_t>       verify_n_seq;
  vector<llama_seq_id>  verify_seq_ids;
  vector<llama_seq_id*> verify_seq_ptrs;
  vector<int8_t>        verify_logits;
  vector<llama_token>   draft_toks;
  common_batch          spec_batch;

  // --- cached context params ---------------------------------------------------
  int n_ctx   = 0;
  int n_batch = 0;

  // --- phase timers (cumulative µs) ---------------------------------------------
  uint64_t time_gpu = 0;
  uint64_t time_cpu = 0;

  // default constructor
  MtpState() = default;

  // move constructor (handles raw pointer + fixed array)
  MtpState(MtpState &&other) noexcept
    : n_layers(other.n_layers)
    , enabled(other.enabled)
    , disabled_permanently(other.disabled_permanently)
    , n_max(other.n_max)
    , n_min(other.n_min)
    , p_min(other.p_min)
    , spec_init(std::move(other.spec_init))
    , spec(std::move(other.spec))
    , spec_params(std::move(other.spec_params))
    , smpl(std::exchange(other.smpl, nullptr))
    , buf_head(other.buf_head)
    , buf_len(other.buf_len)
    , anchor(other.anchor)
    , has_pending(other.has_pending)
    , rounds(other.rounds)
    , drafted(other.drafted)
    , accepted(other.accepted)
    , suffix_rounds(other.suffix_rounds)
    , suffix_drafted(other.suffix_drafted)
    , suffix_accepted(other.suffix_accepted)
    , suffix_hits(other.suffix_hits)
    , suffix_misses(other.suffix_misses)
    , win_idx(other.win_idx)
    , win_filled(other.win_filled)
    , win_drafted(other.win_drafted)
    , win_accepted(other.win_accepted)
    , last_limit(other.last_limit)
    , adapt(other.adapt)
    , adaptive_enabled(other.adaptive_enabled)
    , acc_tokens(std::move(other.acc_tokens))
    , verify_toks(std::move(other.verify_toks))
    , verify_pos(std::move(other.verify_pos))
    , verify_n_seq(std::move(other.verify_n_seq))
    , verify_seq_ids(std::move(other.verify_seq_ids))
    , verify_seq_ptrs(std::move(other.verify_seq_ptrs))
    , verify_logits(std::move(other.verify_logits))
    , draft_toks(std::move(other.draft_toks))
    , spec_batch(std::move(other.spec_batch))
    , n_ctx(other.n_ctx)
    , n_batch(other.n_batch)
    , time_gpu(other.time_gpu)
    , time_cpu(other.time_cpu) {
    std::memcpy(buf, other.buf, sizeof(buf));
  }
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
  void set_penalty_last_n(int32_t penalty_last_n) { _penalty_last_n = penalty_last_n; dirty(); }
  void set_penalty_repeat(float penalty_repeat) { _penalty_repeat = penalty_repeat; dirty(); }
  void set_penalty_freq(float penalty_freq) { _penalty_freq = penalty_freq; dirty(); }
  void set_penalty_present(float penalty_present) { _penalty_present = penalty_present; dirty(); }
  void set_max_tokens(int max_tokens) { _max_tokens = max_tokens; dirty(); }
  void set_min_p(float min_p) { _min_p = min_p; dirty(); }
  void set_temperature(float temperature) { _temperature = temperature; dirty(); }
  void set_top_k(int top_k) { _top_k = top_k; dirty(); }
  void set_top_p(float top_p) { _top_p = top_p; dirty(); }
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

  // retrieves from all DBs in a RagLibrary, sharing a token budget
  std::string rag_retrieve_multi(RagLibrary &lib, const std::string &query,
                                 int top_k, int ctx_size);

  // indexes the details from the given file
  bool rag_index(RagDB &db, const std::string &filepath);

  // indexes prose text (from ZIM extraction or plain text)
  bool rag_index_prose(RagDB &db, const std::string &text,
                       const std::string &source);

  //  returns the embedding dimension for the loaded model
  int get_embed_dim() const { return _model != nullptr ? llama_model_n_embd(_model) : 0; }

  // MTP (Multi-Token Prediction) speculative decoding
  bool is_mtp_enabled() const { return _mtp.enabled; }
  int  mtp_n_layers()  const { return _mtp.n_layers; }
  void set_mtp_n_max(int n) { _mtp.n_max = n; }
  void set_mtp_n_min(int n) { _mtp.n_min = n; }
  void set_mtp_p_min(float p) { _mtp.p_min = p; }
  // MTP best-settings (for persistence on exit)
  bool  mtp_has_best() const { return _mtp.adapt.best.valid; }
  int   mtp_best_n_max() const { return _mtp.adapt.best.eff_n_max; }
  int   mtp_best_n_min() const { return _mtp.adapt.best.n_min; }
  float mtp_best_p_min() const { return _mtp.adapt.best.p_min; }
  float mtp_best_pos_threshold() const { return _mtp.adapt.best.pos_threshold; }
  double mtp_best_tps() const { return _mtp.adapt.best.tps; }

private:
  bool batch_decode_tokens(vector<llama_token> &tokens);
  bool configure_sampler();
  void sync_and_capture_mtp(const llama_token *tokens, int n_tokens, llama_pos pos_start);
  void mtp_init(const LlamaLoad &load);
  bool mtp_round(llama_token anchor, vector<llama_token> &accepted, llama_token &next);
  void mtp_suffix_fallback(int &k, bool &from_suffix);
  void mtp_update_stats(int k, int n_acc, bool from_suffix, int limit,
                        std::chrono::high_resolution_clock::time_point t0,
                        std::chrono::high_resolution_clock::time_point t1,
                        std::chrono::high_resolution_clock::time_point t_end);
  void mtp_reset_state();
  void mtp_trim_draft(llama_pos from);
  bool decode_anchor(llama_token tok);
  string emit_token(LlamaIter &iter, llama_token tok);
  void mtp_rollback_unemitted(int n_unemitted);
  void dirty() {_sampler_dirty = true; }
  bool full_flush_except_system();
  bool make_space_for_tokens(int n_tokens);
  vector<llama_token> tokenize(const string &prompt);
  string token_to_string(LlamaIter &iter, llama_token tok) const;
  void set_last_error(const string &message);
  void set_decode_error(int32_t error, int index, int num_tokens);
  llama_token sample_next();

  // MTP (Multi-Token Prediction) speculative decoding - in llama_sb_mtp.cpp
  bool configure_mtp_sampler();
  int  compute_effective_n_max();
  void mtp_adapt_eval();
  void adapt_p_min();
  void adapt_n_min();
  void adapt_pos_threshold();

  llama_model *_model;
  llama_context *_ctx;
  llama_sampler *_sampler;
  const llama_vocab *_vocab;
  vector<string> _stop_sequences;
  string _last_error;
  string _template;
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
  int _n_system_tokens;
  int _tokens_physically_used;
  bool _is_gemma4;
  bool _sampler_dirty;
  bool _can_shift;
  bool _memory_flush;
  unsigned int _seed;

  // MTP (Multi-Token Prediction) speculative decoding state
  MtpState _mtp;

  // Suffix (n-gram) drafting (plan 08)
  SuffixDrafter _suffix;
  bool _suffix_enabled = false;
  int  _suffix_ngram   = 4;
  int  _suffix_max_draft = 2;

  // Recent-token ring buffer (feeds the suffix drafter)
  static constexpr int RECENT_BUF_SIZE = 16;
  llama_token _recent_buf[RECENT_BUF_SIZE] = {};
  int _recent_len = 0;
  void push_recent(llama_token tok) {
    _recent_buf[_recent_len % RECENT_BUF_SIZE] = tok;
    _recent_len++;
  }
  bool get_recent(llama_token *out, int n) const {
    if (n > _recent_len || n <= 0) { return false; }
    int start = (_recent_len - n) % RECENT_BUF_SIZE;
    for (int i = 0; i < n; ++i) {
      out[i] = _recent_buf[(start + i) % RECENT_BUF_SIZE];
    }
    return true;
  }
  void clear_recent() { _recent_len = 0; }
};
#endif
