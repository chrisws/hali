// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

// basalt.h - Basalt in-process engine backend
//
// Wraps basalt's ModelAdapter / ModelSession API behind the Engine
// interface so agent.cpp can drive it identically to Llama.

#ifndef BASALT_H
#define BASALT_H

#include "engine.h"

#include <memory>
#include <string>
#include <vector>

// Forward-declare basalt types to avoid pulling the full header
// into every translation unit that includes basalt.h.
namespace basalt::model {
  struct Started;
  class ModelAdapter;
  class ModelSession;
  struct TokenNames;
}
namespace basalt::program {
  class Tokenizer;
}

struct Basalt : public Engine {
  Basalt();
  ~Basalt() override;

  // --- lifecycle -----------------------------------------------------------
  bool load_model(const ModelLoad &load) override;
  void reset() override;
  const char *last_error() const override;

  // --- configuration -------------------------------------------------------
  void add_stop(const char *stop) override;
  void set_max_tokens(int n) override;
  void set_temperature(float v) override;
  void set_top_k(int v) override;
  void set_top_p(float v) override;
  void set_min_p(float v) override;
  void set_penalty_repeat(float v) override;
  void set_penalty_freq(float v) override;
  void set_penalty_present(float v) override;
  void set_penalty_last_n(int32_t v) override;
  void set_seed(unsigned int v) override;
  void set_log_level(int level) override;

  // --- generation ----------------------------------------------------------
  bool add_message(ModelIter &iter, const string &role,
                   const string &content) override;
  string next(ModelIter &iter) override;

  // --- memory --------------------------------------------------------------
  ModelMemoryInfo memory_info() const override;
  float memory_kv_percent() const override;
  bool is_memory_flush() const override;

  // --- capabilities --------------------------------------------------------
  bool supports_rag() const override { return false; }
  bool supports_mtp_tuning() const override { return false; }
  bool supports_kv_control() const override { return false; }

private:
  // basalt state
  std::unique_ptr<basalt::model::Started> _started;
  const basalt::model::ModelAdapter *_adapter = nullptr;
  basalt::model::ModelSession *_session = nullptr;

  // tokenizer (loaded from the .basalt pack's tokenizer sections)
  std::unique_ptr<basalt::program::Tokenizer> _tokenizer;
  bool _tokenizer_loaded = false;
  std::string _model_path;

  // token buffer (hali-side, like Llama)
  std::vector<int32_t> _tokens;
  int64_t _pos = 0;

  // EOS token IDs (resolved from adapter at load time)
  std::vector<int32_t> _eos_ids;

  // sampling params (passed to DecodeRequest::sampling)
  struct Sampling {
    int   top_k = 20;
    float top_p = 0.95f;
    float min_p = 0.0f;
    float temperature = 1.0f;
    int   penalty_last_n = 0;
    float penalty_repeat = 1.0f;
    float penalty_freq = 0.0f;
    float penalty_present = 0.0f;
    uint64_t seed = 0;
  };
  Sampling _sampling;

  // generation state
  int _max_tokens = 512;
  int _tokens_generated = 0;
  bool _prompt_done = false;
  bool _finished = false;

  // error tracking
  char _last_error[256] = "";

  // stop sequences (hali-side, shared with Llama via ModelIter)
  std::vector<std::string> _stops;

  // helpers
  void set_error(const char *fmt, ...);
  bool resolve_adapter(const ModelLoad &load, std::vector<std::string> &model_paths);
  bool start_model(const ModelLoad &load, const std::vector<std::string> &model_paths);
  bool load_tokenizer();
  void resolve_eos_tokens();
  void report_vram();
  void check_cuda_errors();
  bool do_prefill();
  int32_t do_decode();
};

#endif // BASALT_H