// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <filesystem>
#include "llama.h"
#include "llama_sb.h"
#include "run_allowed.h"

namespace fs = std::filesystem;

//
// HaliConfig
//
struct HaliConfig {
  explicit HaliConfig();
  ~HaliConfig() = default;

  std::string build_system_prompt() const;
  std::string introspect() const;
  std::string settings_path() const;
  std::string kv_preset_to_string() const;
  bool save_settings() const;
  void load_settings();
  void set_config(const std::string &config);

  // Returns 0=ok, 1=unknown key, 2=parse error
  int set_value(const std::string &key, const std::string &val,
                bool &needs_reparam, std::string &msg);

  std::string model_path_;
  std::string embed_path_;
  std::string backup_path_;
  std::string sandbox_;
  std::string config_ = "hali.config.json";

  float min_p_          = 0.0f;
  float penalty_freq_   = 0.0f;
  float penalty_present_= 0.0f;
  float penalty_repeat_ = 1.0f;
  float rope_freq_scale_= 0.0f;
  float temperature_    = 0.6f;
  float top_p_          = 0.95f;
  bool  offload_kqv_    = true;
  bool  permission_prompt_ = false;
  bool  thinking_       = true;
  int   log_level_      = GGML_LOG_LEVEL_CONT;
  int   n_batch_        = 512;
  int   n_ctx_          = 65536;
  int   n_gpu_layers_   = 32;
  int   n_threads_      = 0;
  int   n_threads_batch_= 0;
  int   penalty_last_n_ = 256;
  int   rag_top_k_      = 5;
  int   top_k_          = 20;
  int   web_port_       = -1;
  int   mtp_n_max_      = 3;
  int   mtp_n_min_      = 0;
  float mtp_p_min_      = 0.0f;
  float mtp_pos_threshold_ = 0.30f;
  bool  mtp_enabled_    = true;
  bool  mtp_adaptive_   = true;

  // MCP (plan 12)
  std::string mcp_host_;
  int   mcp_port_       = 0;

  // Logging (plan 12)
  std::string log_level_str_ = "1";
  std::string log_file_;
  std::string log_filter_;

  // Suffix (n-gram) drafting (plan 08)
  bool  suffix_enabled_ = true;
  int   suffix_ngram_   = 4;
  int   suffix_max_draft_ = 2;

  // Audio (plan 11)
  bool  audio_enabled_    = false;
  float audio_volume_     = 0.5f;
  bool  audio_tool_tick_  = false;
  bool  audio_user_prompt_ = false;

  KVCachePreset kv_preset_ = KVCachePreset::Compact;

  // Context extension (relevant when VRAM caps n_ctx)
  // NONE / LINEAR / YARN / LONGROPE
  enum llama_rope_scaling_type rope_scaling_type_ = LLAMA_ROPE_SCALING_TYPE_UNSPECIFIED;

  // TOOL:RUN allowlist - if non-empty, only these program base names may run.
  // Empty means "allow anything inside the sandbox" (original behaviour).
  std::vector<std::string> run_allowed_;
  std::vector<std::string> knowledge_files_;
  RunAllowed user_run_allowed_;

  // MCP support
  std::string mcp_context_;
  std::vector<std::string> mcp_filter_;
};


#endif
