// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef AGENT_H
#define AGENT_H

#include <functional>
#include <set>
#include <string>
#include <unordered_map>

#include "llama_sb_rag.h"
#include "config.h"
#include "tui.h"
#include "mcp_client.h"
#include "usage_log.h"
#include "planner.h"

using ToolFn  = std::function<std::string(const std::string &, const std::string &)>;
using ToolMap = std::unordered_map<std::string, ToolFn>;

constexpr int MAX_FLUSH_RETRIES = 3;

//
// Per-turn state, reset at the start of each run_turn
//
struct TurnState {
  int number        = 0;
  int tool_calls    = 0;
  int tool_errors   = 0;
  int files_read    = 0;
  int files_written = 0;

  void reset() {
    tool_calls    = 0;
    tool_errors   = 0;
    files_read    = 0;
    files_written = 0;
  }
};

//
// Throttled broadcast / line-batching state
//
struct BroadcastState {
  int flush_count        = 0;
  std::string line_batch;
  int line_batch_count   = 0;
  std::string broadcast_buffer;
  std::chrono::steady_clock::time_point last_broadcast{};

  void reset() {
    flush_count      = 0;
    line_batch.clear();
    line_batch_count = 0;
    broadcast_buffer.clear();
  }
};

//
// RAG (retrieval-augmented generation) subsystem state
//
struct RagState {
  std::unique_ptr<Llama>      embed_llama;
  std::unique_ptr<RagDB>      rag_db;
  std::unique_ptr<RagSession> rag_session;
  mutable std::string last_query;
  mutable std::string last_result;

  bool active() const { return embed_llama && rag_db && rag_session; }
  void reset_cache() const { last_query.clear(); last_result.clear(); }
};

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
  bool run_turn_with_skill(const std::string &skill_text, const std::string &user_msg);
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
  void generate_badge(const std::string &sandbox);
  std::vector<std::string> tool_names() const;
  std::string invoke_tool_by_name(const std::string &name, const std::string &a1, const std::string &a2);

  private:
  std::string memory_info_status() const;
  std::string process_tool(const std::string &cmd);
  ToolMap build_tool_dispatch();
  std::string rag_tool(const std::string &agent_query) const;
  std::string restart();
  double elapsed_seconds() const;
  float tokens_per_sec() const;
  void invoke_tool(const std::string &buffer, std::string_view template_str);
  void broadcast_throttled(const std::string &msg);
  void flush_broadcast();

  HaliConfig &cfg_;
  Tui &tui_;
  mcp::Client &mcp_client_;

  std::unique_ptr<Llama> llama_;
  std::unique_ptr<LlamaIter> iter_;
  ToolMap tools_;
  bool model_loaded_ = false;
  std::string system_prompt_;
  std::set<std::string> loaded_skills_;
  UsageLog usage_log_;
  Planner planner_;
  std::string last_response_;
  TurnState turn_;
  BroadcastState broadcast_;
  std::chrono::steady_clock::time_point last_mem_update_{};
  LlamaMemoryInfo cached_mem_info_{};
  LlamaMemoryInfo get_mem_info();
  RagState rag_;
};

#endif
