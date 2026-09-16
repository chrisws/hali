// This file is part of Nitro
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
#include "config.h"

namespace fs = std::filesystem;

//
// NitroConfig
//
struct NitroConfig {
  explicit NitroConfig();
  ~NitroConfig() = default;

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
  std::string config_ = "nitro.config.json";

  int   n_ctx_          = 65536;
  int   n_batch_        = 512;
  int   n_gpu_layers_   = 32;
  bool  offload_kqv_    = true;
  int   log_level_      = GGML_LOG_LEVEL_CONT;
  float temperature_    = 0.6f;
  float top_p_          = 0.95f;
  float min_p_          = 0.0f;
  int   top_k_          = 20;
  int   penalty_last_n_ = 256;
  float penalty_repeat_ = 1.0f;
  float penalty_freq_   = 0.0f;
  float penalty_present_= 0.0f;
  int   rag_top_k_      = 5;
  bool  thinking_       = true;
  bool  permission_prompt_ = false;
  int   web_port_       = -1;
  KVCachePreset kv_preset_ = KVCachePreset::Compact;

  // TOOL:RUN allowlist - if non-empty, only these program base names may run.
  // Empty means "allow anything inside the sandbox" (original behaviour).
  std::vector<std::string> run_allowed_;
  std::vector<std::string> knowledge_files_;

  // MCP support
  std::string mcp_context_;
  std::vector<std::string> mcp_filter_;
};

