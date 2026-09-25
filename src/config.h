// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "llama.h"
#include "llama_sb.h"

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
  void set_config(std::string config);

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
  
  KVCachePreset kv_preset_ = KVCachePreset::Compact;

  // Context extension (relevant when VRAM caps n_ctx)
  // NONE / LINEAR / YARN / LONGROPE
  enum llama_rope_scaling_type rope_scaling_type_ = LLAMA_ROPE_SCALING_TYPE_UNSPECIFIED;

  // TOOL:RUN allowlist - if non-empty, only these program base names may run.
  // Empty means "allow anything inside the sandbox" (original behaviour).
  std::vector<std::string> run_allowed_;
  std::vector<std::string> knowledge_files_;

  // MCP support
  std::string mcp_context_;
  std::vector<std::string> mcp_filter_;
};

