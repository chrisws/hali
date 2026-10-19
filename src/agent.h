// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef AGENT_H
#define AGENT_H

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

#include "llama_sb_rag.h"
#include "config.h"
#include "tui.h"
#include "mcp_client.h"
#include "usage_log.h"

using ToolFn  = std::function<std::string(const std::string &, const std::string &)>;
using ToolMap = std::unordered_map<std::string, ToolFn>;

constexpr int MAX_FLUSH_RETRIES = 3;

//
// Agent
//
struct Agent {
  Agent(HaliConfig &cfg, Tui &tui, mcp::Client &mcp_client)
    : cfg_(cfg)
    , tui_(tui)
    , mcp_client_(mcp_client) {
    tools_ = build_tool_dispatch();
  }
  ~Agent() = default;

  bool run_turn(const std::string &user_message);
  std::string last_response() const { return last_response_; }
  bool setup_embed(const std::string &path);
  bool setup_model();
  void reset_conversation(const std::string &sysprompt);
  void notify(const std::string &msg);
  bool rag_index(const std::string &path) const;
  bool rag_load_index(const std::string &path) const;
  std::string memory_info_text() const;
  void apply_generation_params() const;
  bool model_loaded() const { return model_loaded_; }
  void save_mtp_best_settings();
  void dump_usage_log(const std::string &path) const;

  private:
  std::string memory_info_status() const;
  std::string process_tool(const std::string &cmd);
  ToolMap build_tool_dispatch();
  std::string rag_tool(const std::string &agent_query) const;
  std::string restart();
  double elapsed_seconds() const;
  float tokens_per_sec() const;
  void invoke_tool(const std::string &buffer, std::string_view template_str);

  HaliConfig &cfg_;
  Tui &tui_;
  mcp::Client &mcp_client_;

  std::unique_ptr<Llama> llama_;
  std::unique_ptr<LlamaIter> iter_;
  std::unique_ptr<Llama> embed_llama_;
  std::unique_ptr<RagDB> rag_db_;
  std::unique_ptr<RagSession> rag_session_;
  ToolMap tools_;

  bool model_loaded_ = false;
  std::string system_prompt_;
  int flush_count_ = 0;

  UsageLog usage_log_;
  std::string last_response_;
  int turn_number_ = 0;
  int turn_tool_calls_ = 0;
  int turn_tool_errors_ = 0;
  int turn_files_read_ = 0;
  int turn_files_written_ = 0;
};

#endif
