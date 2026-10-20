// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "llama.h"
#include "config.h"
#include "tui.h"
#include "audio.h"
#include "agent.h"
#include "logging.h"
#include "curl.h"
#include "string_utils.h"
#include "file.h"
#include "graph.h"
#include "webview.h"
#include "icon.h"
#include "agent_utils.h"
#include "build.h"

//
// SESSION.md - see config.cpp
//
constexpr std::string SESSION_MD = "SESSION.md";

static std::string tool_run(HaliConfig &cfg, Tui &tui, const std::string &arg1, const std::string &arg2) {
  const std::string args = arg1 + " " + arg2;
  bool permitted = ranges::any_of(cfg.run_allowed_, [&](const std::string &a) {return a == args;});
  if (!permitted) {
    if (!tui.confirm_dialog(std::format("Allow {} {} to run?", arg1, arg2))) {
      return "ERROR: '" + args + "' is not permitted to run.";
    }
    cfg.run_allowed_.emplace_back(args);
    cfg.user_run_allowed_.add(args);
  }
  const std::string command = args + " 2>&1";
  tui.show_tool("running: " + command);
  FILE *fp = popen(command.c_str(), "r");
  if (!fp) {
    return "ERROR: popen failed";
  }
  std::string out;
  char buf[256];
  while (fgets(buf, sizeof(buf), fp)) {
    out += buf;
  }
  pclose(fp);
  if (out.size() > 4096) {
    out = out.substr(0, 4096) + "\n…(truncated)";
  }
  return out;
}

static void backup_file(const HaliConfig &cfg, Tui &tui, const std::string &path) {
  if (!utils::is_blank(cfg.backup_path_)) {
    const auto confirm = tool_write_backup(cfg.backup_path_, path);
    tui.show_tool("backup: " + confirm);
  }
}

static void broadcast_reload(const HaliConfig &cfg, Tui &tui) {
  if (cfg.web_port_ != -1) {
    tui.show_tool("reload browser");
    webview::broadcast_reload();
  }
}

static void broadcast_message(const HaliConfig &cfg, Tui &tui, const std::string &message) {
  if (cfg.web_port_ != -1 && !utils::is_blank(message)) {
    tui.show_tool("send to browser");
    webview::broadcast_message(message);
  }
}

void Agent::apply_generation_params() const {
  llama_->add_stop("<|turn|>");
  llama_->add_stop("<|im_end|>");
  llama_->set_max_tokens(512000);
  llama_->set_temperature(cfg_.temperature_);
  llama_->set_top_k(cfg_.top_k_);
  llama_->set_top_p(cfg_.top_p_);
  llama_->set_min_p(cfg_.min_p_);
  llama_->set_penalty_repeat(cfg_.penalty_repeat_);
  llama_->set_penalty_freq(cfg_.penalty_freq_);
  llama_->set_penalty_present(cfg_.penalty_present_);
  llama_->set_penalty_last_n(cfg_.penalty_last_n_);
  llama_->set_log_level(cfg_.log_level_);
}

//
// Shows a modal loading popup while the model loads.
//
bool Agent::setup_model() {
  if (cfg_.model_path_.empty()) {
    tui_.append_line(ICON_SYS + "No model loaded.  Use /model <path> to load a GGUF.");
    tui_.redraw_all();
    return false;
  }
  set_sandbox_root(cfg_.sandbox_);

  // Show a modal popup so the user knows loading is in progress.
  std::string model_name = fs::path(cfg_.model_path_).filename().string();
  tui_.show_loading_modal_popup(model_name);
  // Destroy the iterator first — it holds references into the llama context.
  // Freeing llama while iter is still alive causes use-after-free / load failure.
  iter_.reset();
  model_loaded_ = false;
  llama_ = std::make_unique<Llama>();

  apply_generation_params();

  LlamaLoad load;
  load.model_path = cfg_.model_path_;
  load.n_ctx = cfg_.n_ctx_;
  load.n_batch = cfg_.n_batch_;
  load.n_gpu_layers = cfg_.n_gpu_layers_;
  load.n_threads = cfg_.n_threads_;
  load.n_threads_batch = cfg_.n_threads_batch_;
  load.log_level = cfg_.log_level_;
  load.offload_kqv = cfg_.offload_kqv_;
  load.kv_cache_preset = cfg_.kv_preset_;
  load.mtp_enabled = cfg_.mtp_enabled_;
  load.mtp_n_max = cfg_.mtp_n_max_;
  load.mtp_n_min = cfg_.mtp_n_min_;
  load.mtp_p_min = cfg_.mtp_p_min_;
  load.mtp_pos_threshold = cfg_.mtp_pos_threshold_;
  load.mtp_adaptive = cfg_.mtp_adaptive_;
  load.suffix_enabled = cfg_.suffix_enabled_;
  load.suffix_ngram = cfg_.suffix_ngram_;
  load.suffix_max_draft = cfg_.suffix_max_draft_;

  if (!llama_->load_model(load)) {
    tui_.dismiss_modal_popup();
    tui_.append_line(ICON_ERR + llama_->last_error());
    tui_.redraw_all();
    return false;
  }

  LlamaMemoryInfo mem = llama_->memory_info();
  tui_.dismiss_modal_popup();
  tui_.setup_model(model_name, mem, cfg_.thinking_);

  model_loaded_ = true;
  return true;
}

bool Agent::setup_embed(const std::string &path) {
  tui_.show_loading_modal_popup("embedding model: " + fs::path(path).filename().string());
  tui_.redraw_all();
  embed_llama_ = std::make_unique<Llama>();
  if (!embed_llama_->load_embedding_model(path)) {
    tui_.dismiss_modal_popup();
    tui_.append_line(ICON_ERR + embed_llama_->last_error());
    tui_.redraw_all();
    embed_llama_.reset();
    return false;
  }
  tui_.dismiss_modal_popup();
  rag_db_      = std::make_unique<RagDB>();
  rag_session_ = std::make_unique<RagSession>();
  tui_.append_line(ICON_SYS + "Embedding model ready.");
  tui_.redraw_all();
  return true;
}

void Agent::reset_conversation(const std::string &sysprompt) {
  system_prompt_ = sysprompt;
  llama_->reset();
  cfg_.load_settings();
  apply_generation_params();
  iter_ = std::make_unique<LlamaIter>();
  if (!llama_->add_message(*iter_, "system", system_prompt_)) {
    tui_.append_line(ICON_ERR + "System prompt injection: " + llama_->last_error());
    tui_.redraw_all();
  } else {
    tui_.update_usage(tokens_per_sec(), llama_->memory_info());
  }
}

void Agent::notify(const std::string &msg) {
  if (!model_loaded_ || !iter_) {
    return;
  }
  if (!llama_->add_message(*iter_, "user", msg)) {
    tui_.append_line(ICON_ERR + "notify: " + llama_->last_error());
    tui_.redraw_all();
  }
}

double Agent::elapsed_seconds() const {
  if (!iter_) return 0.0;
  auto now = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double>(now - iter_->_t_start).count();
}

float Agent::tokens_per_sec() const {
  if (!iter_) return 0.0f;
  auto now = std::chrono::high_resolution_clock::now();
  double elapsed = std::chrono::duration<double>(now - iter_->_t_start).count();
  if (elapsed <= 0.0 || iter_->_tokens_generated <= 0) return 0.0f;
  return static_cast<float>(iter_->_tokens_generated / elapsed);
}

std::string Agent::memory_info_status() const {
  float kv_percent = llama_->memory_kv_percent();
  auto message = kv_percent > 75 ? "(Warning: Approaching limit)" : "";
  return std::format("\n[KV-INFO] KV Cache: {}%{} [/KV-INFO]", kv_percent, message);
}

std::string Agent::memory_info_text() const {
  if (!model_loaded_) return "No model loaded.";
  LlamaMemoryInfo m = llama_->memory_info();
  std::ostringstream oss;
  oss << "KV cache  : " << m.kv_used << " / " << m.kv_total
      << "  (" << m.kv_percent << "%)\n";
  if (m.vram_total > 0) {
    oss << "VRAM      : " << (m.vram_used >> 20) << " MB / "
        << (m.vram_total >> 20) << " MB  (" << m.vram_percent << "%)\n";
  }
  oss << "GPU layers: " << m.n_layers_gpu << " / " << m.n_layers_total << "\n";
  oss << "CPU layers: " << m.n_layers_cpu << "\n";
  oss << "Advice    : " << m.advice << "\n";
  return oss.str();
}

std::string Agent::rag_tool(const std::string &agent_query) const {
  std::string result;
  if (embed_llama_ && rag_db_ && rag_session_) {
    result = embed_llama_->rag_retrieve(*rag_db_, agent_query, cfg_.rag_top_k_, *rag_session_);
    if (result.empty()) {
      result = std::string("RAG: no context found: ") + embed_llama_->last_error();
    }
  } else {
    result = "RAG: not enabled";
  }
  return result;
}

bool Agent::rag_load_index(const std::string &path) const {
  if (!embed_llama_ || !rag_db_) {
    tui_.append_line(ICON_ERR + "Load an embedding model first: /embed <path>");
    tui_.redraw_all();
    return false;
  }

  if (!rag_db_->load(path)) {
    tui_.append_line(ICON_SYS + "failed to load");
    tui_.redraw_all();
  }

  return true;
}

bool Agent::rag_index(const std::string &path) const {
  if (!embed_llama_ || !rag_db_) {
    tui_.append_line(ICON_ERR + "Load an embedding model first: /embed <path>");
    tui_.redraw_all();
    return false;
  }

  auto index_one = [&](const std::string &filepath) {
    tui_.append_line(ICON_SYS + "  indexing: " + filepath);
    tui_.redraw_all();
    if (!embed_llama_->rag_index(*rag_db_, filepath)) {
      tui_.append_line(ICON_ERR + "rag_load: " + embed_llama_->last_error());
      tui_.redraw_all();
    }
  };

  // must be set before indexing
  rag_db_->embed_dim = embed_llama_->get_embed_dim();

  fs::path rp(path);
  std::error_code ec;
  if (fs::is_directory(rp, ec)) {
    for (const auto &entry : fs::recursive_directory_iterator(rp, ec)) {
      if (entry.is_regular_file()) {
        index_one(entry.path().string());
      }
    }
  } else {
    index_one(path);
  }

  std::string save_path = join_path(cfg_.sandbox_, "rag-index.bin");
  tui_.append_line(ICON_SYS + "saving index: " + save_path);
  tui_.redraw_all();
  return rag_db_->save(save_path);
}

std::string Agent::restart() {
  if (fs::exists(SESSION_MD)) {
    std::vector<std::string> knowledge_files;
    reset_conversation(cfg_.build_system_prompt());
    tui_.append_line(ICON_ERR + "Session restarted");
    tui_.redraw_all();
    return "continue the pending actions found SESSION.md";
  }
  return "save work context to SESSION.md then try again";
}

//
// Tool dispatch
//
ToolMap Agent::build_tool_dispatch() {
  ToolMap tools;

  tools["TOOL:DATE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:DATE");
    char buf[32]; time_t t = time(nullptr);
    strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&t));
    return buf;
  };
  tools["TOOL:TIME"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:TIME");
    char buf[32]; time_t t = time(nullptr);
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
    return buf;
  };
  tools["TOOL:RND"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:RND");
    return std::to_string(static_cast<double>(rand()) / RAND_MAX);
  };
  tools["TOOL:RAG"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:RAG");
    return rag_tool(a1);
  };
  tools["TOOL:LIST"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    std::string dir = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, dir)) {
      return "ERROR: path outside sandbox";
    }
    tui_.show_tool("listing: " + dir);
    return list_dir(dir);
  };
  tools["TOOL:EXISTS"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    std::string p = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, p)) {
      return "ERROR: path outside sandbox";
    }
    tui_.show_tool("checking: " + p);
    return fs::exists(p) ? "YES" : "NO";
  };
  tools["TOOL:READ"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("reading: " + a1);
    turn_files_read_++;
    std::string p = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, p)) {
      return "ERROR: path outside sandbox";
    }
    return read_file(p);
  };
  tools["TOOL:SEARCH"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    SearchArgs sa = parse_search_args(a1, a2);
    tui_.show_tool("search: " + sa.pattern + " " + sa.path_and_flags);
    turn_files_read_++;
    std::string path_part, flags_part;
    auto space_pos = sa.path_and_flags.find(' ');
    if (space_pos != std::string::npos) {
      path_part = sa.path_and_flags.substr(0, space_pos);
      flags_part = sa.path_and_flags.substr(space_pos + 1);
    } else {
      path_part = sa.path_and_flags;
    }
    return tool_search(cfg_.sandbox_, sa.pattern, path_part, flags_part, cfg_.extra_dirs_);
  };
  tools["TOOL:WRITE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("writing: " + a1);
    turn_files_written_++;
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_in_sandbox(cfg_.sandbox_, path)) {
      return "ERROR: path outside sandbox";
    }
    const auto data = strip_code_fences(a1, a2);
    if (!utils::ends_with(path, SESSION_MD)) {
      if (cfg_.permission_prompt_ && !tui_.confirm_dialog(std::format("Allow model to write {}?", path))) {
        return "ERROR: action prevented by user";
      } else {
        const auto validate = tool_write_validate(path, data);
        if (!validate.empty() && !tui_.confirm_dialog(std::format("[{}] - Allow model to write {}?", validate, path))) {
          return "ERROR: action prevented by user - " + validate;
        }
      }
    }
    backup_file(cfg_, tui_, path);
    const auto result = tool_write(path, data);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + data);
      tui_.append_token(ICON_ERR + result);
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  };
  tools["TOOL:PATCH"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("patch: " + a1);
    turn_files_written_++;
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_in_sandbox(cfg_.sandbox_, path)) {
      return "ERROR: path outside sandbox";
    }
    const auto validate = tool_patch_validate(path, a2);
    if (!validate.empty()) {
      tui_.append_token(ICON_ERR + a2);
      if (!tui_.confirm_dialog(std::format("[{}] - Allow model to patch {}?", validate, path))) {
        return "ERROR: action prevented by user - " + validate;
      }
    }
    backup_file(cfg_, tui_, path);
    const auto result = tool_patch(path, a2);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + a2);
      tui_.append_token(ICON_ERR + result);
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  };
  tools["TOOL:APPEND"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("append: " + a1);
    turn_files_written_++;
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_in_sandbox(cfg_.sandbox_, path)) {
      return "ERROR: path outside sandbox";
    }
    backup_file(cfg_, tui_, path);
    const auto result = tool_append(path, a2);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + result);
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  };
  tools["TOOL:MKDIR"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    std::string p = resolve_path(cfg_.sandbox_, a1);
    tui_.show_tool("mkdir: " + a1);
    if (!path_in_sandbox(cfg_.sandbox_, p)) {
      return "ERROR: path outside sandbox";
    }
    return make_dir(p) ? "OK: created " + a1 : "ERROR: mkdir failed for " + a1;
  };
  tools["TOOL:CURL"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("curl: " + a1);
    return tool_curl(a1);
  };
  tools["TOOL:INTROSPECT"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("introspecting: " + a1);
    return cfg_.introspect();
  };
  tools["TOOL:ASK"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.set_thinking(false);
    tui_.show_tool("asking: " + a1 + " " + a2);
    if (cfg_.audio_user_prompt_) { audio::play_user_prompt(); }
    return tui_.readline();
  };
  tools["TOOL:RESTART"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("restart ...");
    return restart();
  };
  tools["TOOL:PERMISSION"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.set_thinking(false);
    tui_.show_tool("asking permission: " + a1 + " " + a2);
    if (cfg_.audio_user_prompt_) { audio::play_user_prompt(); }
    return tui_.confirm_dialog(a1 + " " + a2) ? "YES" : "NO";
  };
  tools["TOOL:RUN"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return tool_run(cfg_, tui_, a1, a2);
  };
  tools["TOOL:BUILD"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    auto result = build_run(cfg_.sandbox_, a1, [&](const std::string &msg) {
      tui_.show_tool(msg);
    }, a2);
    return result.output_;
  };
  tools["TOOL:REPLACE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("replace: " + a1);
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (cfg_.permission_prompt_ && !tui_.confirm_dialog(std::format("Allow model to replace in {}?", path))) {
      return "ERROR: action prevented by user";
    }
    backup_file(cfg_, tui_, path);
    auto result = tool_replace(cfg_.sandbox_, a1, a2);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + result);
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  };
  tools["TOOL:MCP"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("mcp: " + a1);
    return mcp_client_.call_tool(a1, a2, cfg_.sandbox_);
  };
  tools["TOOL:GRAPH"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    const int cols = tui_.get_term_cols() * 0.75;
    auto graphResult = tool_graph(cols, a1 + a2);
    if (graphResult.success_) { tui_.append_lines(graphResult.data_); }
    return graphResult.success_ ? "OK" : graphResult.message_;
  };
  tools["TOOL:TABLE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    const auto result = tool_table(a1, a2);
    if (!result.error.empty()) {
      tui_.show_tool("table error: " + result.error);
      tui_.append_line(a2);
      return result.error;
    }
    tui_.append_lines(result.rows);
    return "OK";
  };
  tools["TOOL:PLAY"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    const std::string notes = a1 + " " + a2;
    tui_.show_tool("play: " + notes);
    auto result = audio::play_script(notes);
    if (!result.error.empty()) { return result.error; }
    audio::play_events(result.events);
    size_t total = 0;
    for (const auto &ev : result.events) { total += ev.size(); }
    return "OK: played " + std::to_string(total) + " notes in "
           + std::to_string(result.events.size()) + " events";
  };
  return tools;
}

std::string Agent::process_tool(const std::string &cmd) {
  std::string op, arg1, arg2;
  auto WS = cmd.find_first_of(" \n");
  if (WS == std::string::npos) {
    op = utils::trim(cmd);
  } else {
    op = utils::trim(cmd.substr(0, WS));
    std::string rest = cmd.substr(WS + 1);
    // clear leading WS from rest
    rest.erase(0, rest.find_first_not_of(" \t"));

    // Quote-aware arg splitting (handles "multi word" patterns)
    ToolArgs ta = split_tool_args(rest);
    arg1 = ta.arg1;
    arg2 = ta.arg2;
  }

  auto it = tools_.find(op);
  if (it == tools_.end()) {
    return "ERROR: unknown tool: [" + op + "]";
  }
  return it->second(arg1, arg2);
}

void Agent::invoke_tool(const std::string &buffer, const std::string_view template_str) {
  static constexpr std::string KV_START = "[KV-INFO]";
  static constexpr std::string KV_END = "[/KV-INFO]";
  static const std::string_view END_TOOL = "\nHALI_END_TOOL";
  static const std::string TOOL_RESULT = "HALI_TOOL_RESULT: ";

  std::string tool;
  if (const auto pos = buffer.rfind(END_TOOL); pos != std::string::npos) {
    tool = buffer.substr(0, pos);
    const auto endTool = buffer.substr(pos);
    if (endTool.length() > END_TOOL.length()) {
      log_write(LEVEL_DEBUG, "agent", "ERROR: trailing delimiter: [%s]", endTool.c_str());
    }
  } else {
    tool = buffer;
  }

  // strip any final [KV-INFO] ... [/KV-INFO] mistakenly added by the agent
  if (const auto kvEnd = tool.rfind(KV_END); kvEnd == (tool.length() - KV_END.length())) {
    if (const auto kvStart = tool.rfind(KV_START); kvStart != std::string::npos) {
      tool = buffer.substr(0, kvStart);
      log_write(LEVEL_DEBUG, "agent", "stripped KV_INFO details output by agent");
    }
  }

  log_write(LEVEL_DEBUG, "agent", "tool request: [%s]", tool.c_str());
  turn_tool_calls_++;
  std::string result = process_tool(tool);
  if (result.empty()) {
    result = "TOOL produced no results";
  }
  if (result.rfind("ERROR", 0) == 0) {
    turn_tool_errors_++;
  }
  const std::string content = TOOL_RESULT + std::vformat(template_str, std::make_format_args(result)) + memory_info_status();
  // log_write(LEVEL_DEBUG, "tool: [%s] result: [%s]", tool.c_str(), result.c_str());
  tui_.update_usage(tokens_per_sec(), llama_->memory_info());
  if (!llama_->add_message(*iter_, "tool_result", content)) {
    tui_.append_line(ICON_ERR + "tool result inject: " + llama_->last_error());
  }
  if (!iter_->_has_next) {
    tui_.append_line(ICON_ERR + "failed to evoke tool response: " + llama_->last_error());
  }
  if (llama_->is_memory_flush()) {
    if (++flush_count_ > MAX_FLUSH_RETRIES) {
      tui_.append_line(ICON_ERR + "Multiple memory flushes detected. Stopping auto-continue.");
      tui_.redraw_all();
      return;
    }
    tui_.append_line(ICON_ERR + "Warning! - memory has been flushed!");
    reset_conversation(cfg_.build_system_prompt() + "\n" + format_session_md(tui_.get_chat()));
    const std::string retry_content = TOOL_RESULT + result + memory_info_status();
    if (!llama_->add_message(*iter_, "tool_result", retry_content)) {
      tui_.append_line(ICON_ERR + "failed to re-inject tool result after flush");
    }
    tui_.append_line(ICON_SYS + "Auto-continuing after memory flush...");
  }
  tui_.redraw_all();
  if (cfg_.audio_tool_tick_) {
    audio::play_tool_tick();
  }
};

//
// Agent turn
//
bool Agent::run_turn(const std::string &user_message) {
  if (!model_loaded_) {
    tui_.append_line(ICON_ERR + "No model loaded. Use /model <path>");
    tui_.redraw_all();
    return false;
  }

  std::string effective_message = user_message;
  if (embed_llama_ && rag_db_ && rag_session_) {
    std::string context = embed_llama_->rag_retrieve(*rag_db_, user_message, cfg_.rag_top_k_, *rag_session_);
    if (!context.empty()) {
      log_write(LEVEL_DEBUG, "agent", "RAG: %s", context.c_str());
      effective_message = "Context:\n" + context + "\n\nUser: " + user_message;
    } else {
      log_write(LEVEL_DEBUG, "agent", "RAG: no context found [%s]", embed_llama_->last_error());
    }
  }
  if (!iter_) {
    tui_.append_line(ICON_ERR + "Conversation not initialised (call /clear to reset)");
    tui_.redraw_all();
    return false;
  }
  flush_count_ = 0;
  turn_number_++;
  turn_tool_calls_ = 0;
  turn_tool_errors_ = 0;
  turn_files_read_ = 0;
  turn_files_written_ = 0;
  last_response_.clear();
  bool flushed = false;
  if (!llama_->add_message(*iter_, "user", effective_message)) {
    if (llama_->is_memory_flush()) {
      if (++flush_count_ > MAX_FLUSH_RETRIES) {
        tui_.append_line(ICON_ERR + "Multiple memory flushes detected. Stopping auto-continue.");
        tui_.redraw_all();
        return false;
      }
      tui_.append_line(ICON_ERR + "Warning! - memory has been flushed!");
      reset_conversation(cfg_.build_system_prompt() + "\n" + format_session_md(tui_.get_chat()));
      flushed = llama_->add_message(*iter_, "user",
          "The session context was restored after a memory flush. "
          "Continue with the pending actions from the session state.");
    } else {
      tui_.append_line(ICON_ERR + "add_message: " + llama_->last_error());
    }
    if (!flushed) {
      tui_.redraw_all();
      return false;
    }
    tui_.append_line(ICON_SYS + "Auto-continuing from session context...");
    tui_.redraw_all();
  }
  tui_.append_line("Hali: ");

  ThinkParser thinker(cfg_.thinking_, llama_->is_gemma_4());

  tui_.set_thinking(true);
  std::string buffer;

  auto fetch_tool = [&]() -> void {
    while (iter_->_has_next && !tui_.is_escape()) {
      const std::string tok = llama_->next(*iter_);
      buffer += tok;
      tui_.tick_spinner();
      if (auto pos = buffer.find("</think>"); pos != std::string::npos) {
        break;
      }
    }
    // log_write(DEBUG_LEVEL, "fetch_tool: \n%s\n\n", buffer.c_str());
  };

  while (iter_->_has_next && !tui_.is_escape()) {
    if (std::string tok = llama_->next(*iter_); tok == "<") {
      // fetch the complete tag (capped to avoid runaway if '>' is missing)
      std::vector<std::string> tokens;
      tokens.push_back(tok);
      size_t total_len = 1;
      while (iter_->_has_next && total_len < 32) {
        std::string next = llama_->next(*iter_);
        tokens.push_back(next);
        total_len += next.size();
        if (next == ">") { break; }
      }
      std::string tag = ThinkParser::assemble_tag(tokens);
      if (tag.empty() || tag.size() >= 32) {
        // not a real tag — treat as plain text
        for (const auto &t : tokens) { buffer += t; }
      } else {
        buffer += tag;
      }
    } else {
      buffer += tok;
    }

    // log_write(DEBUG_LEVEL, "%s", buffer.c_str());

    if (auto skip = thinker.check_start(buffer); skip > 0) {
      buffer = buffer.substr(skip);
    }

    auto er = thinker.check_end(buffer);
    if (er.found) {
      if (er.clear_buffer) {
        buffer = "";
      } else {
        if (!er.thought.empty()) {
          tui_.append_token(ICON_THINK + er.thought);
        }
        buffer = '\n' + buffer.substr(er.pos + er.tag_len);
      }
    }

    if (thinker.mode() == ThinkParser::Think) {
      tui_.set_tokens_per_sec(tokens_per_sec());
      tui_.tick_spinner();
    }
    auto tool_start = buffer.find("TOOL:");
    if (tool_start == 0) {
      fetch_tool();
      invoke_tool(utils::trim(buffer), "TOOL_RESULT: {}");
      buffer.clear();
      thinker.set_mode(ThinkParser::Init);
      continue;
    }
    auto pos = buffer.find('\n');
    if (pos != std::string::npos) {
      if (thinker.mode() == ThinkParser::Think) {
        if (auto thought = buffer.substr(0, pos + 1); !utils::is_blank(thought)) {
          tui_.append_token(ICON_THINK + thought);
        }
      } else {
        auto token = buffer.substr(0, pos + 1);
        tui_.append_token(token);
        broadcast_message(cfg_, tui_, token);
        last_response_ += token;
      }
      buffer = buffer.substr(pos + 1);
    }
  }

  if (!buffer.empty()) {
    tui_.append_token(buffer);
    broadcast_message(cfg_, tui_, buffer);
    last_response_ += buffer;
  }

  tui_.set_thinking(false);
  tui_.update_usage(tokens_per_sec(), llama_->memory_info());

  char stat[128];
  const auto pattern = ICON_SYS + "%.1f tok/s  (%d tokens)  %.1fs elapsed  KV %.1f%%";
  std::snprintf(stat, sizeof(stat), pattern.c_str(),
                static_cast<double>(tui_.get_tokens_per_sec()),
                iter_->_tokens_generated,
                elapsed_seconds(),
                static_cast<double>(tui_.get_kv_percent()));
  tui_.append_line(stat);

  // Record usage log for this turn
  TurnMetrics tm;
  tm.turn_number       = turn_number_;
  tm.llm_latency_ms    = elapsed_seconds() * 1000.0;
  tm.completion_tokens = iter_->_tokens_generated;
  tm.tool_call_count   = turn_tool_calls_;
  tm.tool_error_count  = turn_tool_errors_;
  tm.kv_usage_pct      = llama_->memory_kv_percent();
  tm.files_read        = turn_files_read_;
  tm.files_written     = turn_files_written_;
  usage_log_.record_turn(tm);
  log_write(LEVEL_DEBUG, "usage_log", "%s", usage_log_.summary().c_str());
  if (usage_log_.kv_trending_high()) {
    tui_.append_line(ICON_SYS + "KV usage trending high — consider restarting soon.");
  }

  tui_.redraw_all();
  return true;
}

void Agent::dump_usage_log(const std::string &path) const {
  usage_log_.dump(path);
}

void Agent::save_mtp_best_settings() {
  if (!model_loaded_ || !llama_->mtp_has_best()) {
    return;
  }
  cfg_.mtp_n_max_ = llama_->mtp_best_n_max();
  cfg_.mtp_n_min_ = llama_->mtp_best_n_min();
  cfg_.mtp_p_min_ = llama_->mtp_best_p_min();
  cfg_.mtp_pos_threshold_ = llama_->mtp_best_pos_threshold();
  cfg_.save_settings();
  log_write(LEVEL_INFO, "agent", "MTP best settings saved: n_max=%d n_min=%d p_min=%.3f pos_threshold=%.3f",
            cfg_.mtp_n_max_, cfg_.mtp_n_min_, cfg_.mtp_p_min_, cfg_.mtp_pos_threshold_);
}
