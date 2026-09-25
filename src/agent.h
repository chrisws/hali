// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <filesystem>
#include <string>

#include "llama_sb_rag.h"
#include "config.h"
#include "tui.h"
#include "mcp_client.h"

//
// Agent
//
struct Agent {
  Agent(HaliConfig &cfg, Tui &tui, mcp::Client &mcp_client)
    : cfg_(cfg)
    , tui_(tui)
    , mcp_client_(mcp_client) {
  }
  ~Agent() = default;

  bool run_turn(const std::string &user_message);
  bool setup_embed(const std::string &path);
  bool setup_model();
  void reset_conversation(const std::string &sysprompt);
  bool rag_index(const std::string &path) const;
  bool rag_load_index(const std::string &path) const;
  std::string memory_info_text() const;
  void apply_generation_params() const;
  bool model_loaded() const { return model_loaded_;}

  private:
  HaliConfig &cfg_;
  Tui &tui_;
  mcp::Client &mcp_client_;

  std::unique_ptr<Llama> llama_;
  std::unique_ptr<LlamaIter> iter_;
  std::unique_ptr<Llama> embed_llama_;
  std::unique_ptr<RagDB> rag_db_;
  std::unique_ptr<RagSession> rag_session_;
  bool model_loaded_ = false;
  std::string system_prompt_;

  std::string memory_info_status() const;
  std::string process_tool(const std::string &cmd);
  std::string rag_tool(const std::string &agent_query) const;
  std::string restart();
  double elapsed_seconds() const;
  float tokens_per_sec() const;
  void invoke_tool(const std::string &buffer, std::string_view template_str);
};
