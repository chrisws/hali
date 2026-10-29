// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "agent.h"
#include "agent_utils.h"
#include "audio.h"
#include "config.h"
#include "file.h"
#include "icon.h"
#include "llama_sb.h"
#include "llama_sb_rag.h"
#include "logging.h"
#include "planner.h"
#include "string_utils.h"
#include "tui.h"
#include "usage_log.h"
#include "webview.h"

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
  load.n_ubatch = cfg_.n_ubatch_;
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
  cached_mem_info_ = mem;
  last_mem_update_ = std::chrono::steady_clock::now();
  tui_.dismiss_modal_popup();
  tui_.setup_model(model_name, mem, cfg_.thinking_);

  model_loaded_ = true;
  return true;
}

bool Agent::setup_embed(const std::string &path) {
  tui_.show_loading_modal_popup("embedding model: " + fs::path(path).filename().string());
  tui_.redraw_all();
  rag_.embed_llama = std::make_unique<Llama>();
  if (!rag_.embed_llama->load_embedding_model(path)) {
    tui_.dismiss_modal_popup();
    tui_.append_line(ICON_ERR + rag_.embed_llama->last_error());
    tui_.redraw_all();
    rag_.embed_llama.reset();
    return false;
  }
  tui_.dismiss_modal_popup();
  rag_.rag_db      = std::make_unique<RagDB>();
  rag_.rag_session = std::make_unique<RagSession>();
  tui_.append_line(ICON_SYS + "Embedding model ready.");
  tui_.redraw_all();
  return true;
}

void Agent::reset_conversation(const std::string &sysprompt) {
  system_prompt_ = sysprompt;
  llama_->reset();
  cfg_.load_settings();
  apply_generation_params();
  loaded_skills_.clear();
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

LlamaMemoryInfo Agent::get_mem_info() {
  auto now = std::chrono::steady_clock::now();
  if (now - last_mem_update_ > std::chrono::seconds(1)) {
    cached_mem_info_ = llama_->memory_info();
    last_mem_update_ = now;
  }
  return cached_mem_info_;
}

void Agent::broadcast_throttled(const std::string &msg) {
  if (cfg_.web_port_ == -1 || utils::is_blank(msg)) {
    return;
  }
  broadcast_.broadcast_buffer += msg;
  auto now = std::chrono::steady_clock::now();
  if (now - broadcast_.last_broadcast > std::chrono::milliseconds(100)) {
    flush_broadcast();
  }
}

void Agent::flush_broadcast() {
  if (broadcast_.broadcast_buffer.empty()) {
    return;
  }
  tui_.show_tool("send to browser");
  webview::broadcast_message(broadcast_.broadcast_buffer);
  broadcast_.broadcast_buffer.clear();
  broadcast_.last_broadcast = std::chrono::steady_clock::now();
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
  // Return cached result if same query this turn
  if (agent_query == rag_.last_query && !rag_.last_result.empty()) {
    return rag_.last_result;
  }
  std::string result;
  if (rag_.active()) {
    result = rag_.embed_llama->rag_retrieve(*rag_.rag_db, agent_query, cfg_.rag_top_k_, *rag_.rag_session);
    if (result.empty()) {
      result = std::string("RAG: no context found: ") + rag_.embed_llama->last_error();
    }
  } else {
    result = "RAG: not enabled";
  }
  rag_.last_query = agent_query;
  rag_.last_result = result;
  return result;
}

bool Agent::rag_load_index(const std::string &path) const {
  if (!rag_.active()) {
    tui_.append_line(ICON_ERR + "Load an embedding model first: /embed <path>");
    tui_.redraw_all();
    return false;
  }

  if (!rag_.rag_db->load(path)) {
    tui_.append_line(ICON_SYS + "failed to load");
    tui_.redraw_all();
  }

  return true;
}

bool Agent::rag_index(const std::string &path) const {
  if (!rag_.active()) {
    tui_.append_line(ICON_ERR + "Load an embedding model first: /embed <path>");
    tui_.redraw_all();
    return false;
  }

  auto index_one = [&](const std::string &filepath) {
    tui_.append_line(ICON_SYS + "  indexing: " + filepath);
    tui_.redraw_all();
    if (!rag_.embed_llama->rag_index(*rag_.rag_db, filepath)) {
      tui_.append_line(ICON_ERR + "rag_load: " + rag_.embed_llama->last_error());
      tui_.redraw_all();
    }
  };

  // must be set before indexing
  rag_.rag_db->embed_dim = rag_.embed_llama->get_embed_dim();

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

  std::string save_path = join_path(cfg_.sandbox_, ".hali/rag-index.bin");
  tui_.append_line(ICON_SYS + "saving index: " + save_path);
  tui_.redraw_all();
  return rag_.rag_db->save(save_path);
}

std::string Agent::restart() {
  if (fs::exists(SESSION_MD)) {
    // Append active plan state to SESSION.md before reset
    if (!planner_.steps().empty()) {
      const std::string plan_md = "\n## Active Plan\n" + planner_.to_markdown() + "\n";
      std::ofstream ofs(SESSION_MD, std::ios::app);
      if (ofs) {
        ofs << plan_md;
      }
    }
    reset_conversation(cfg_.build_system_prompt());
    tui_.append_line(ICON_ERR + "Session restarted");
    tui_.redraw_all();
    return "continue the pending actions found .hali/SESSION.md";
  }
  return "save work context to .hali/SESSION.md then try again";
}

//
// Tool dispatch
//
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
  turn_.tool_calls++;
  std::string result = process_tool(tool);
  if (result.empty()) {
    result = "TOOL produced no results";
  }
  if (result.rfind("ERROR", 0) == 0) {
    turn_.tool_errors++;
  }
  const std::string content = TOOL_RESULT + std::vformat(template_str, std::make_format_args(result)) + memory_info_status();
  tui_.update_usage(tokens_per_sec(), get_mem_info());
  if (!llama_->add_message(*iter_, "tool_result", content)) {
    tui_.append_line(ICON_ERR + "tool result inject: " + llama_->last_error());
  }
  if (!iter_->_has_next) {
    tui_.append_line(ICON_ERR + "failed to evoke tool response: " + llama_->last_error());
  }
  if (llama_->is_memory_flush()) {
    if (++broadcast_.flush_count > MAX_FLUSH_RETRIES) {
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
  if (rag_.active()) {
    std::string context = rag_.embed_llama->rag_retrieve(*rag_.rag_db, user_message, cfg_.rag_top_k_, *rag_.rag_session);
    rag_.last_query = user_message;
    rag_.last_result = context;
    if (!context.empty()) {
      log_write(LEVEL_DEBUG, "agent", "RAG: %s", context.c_str());
      effective_message = "Context:\n" + context + "\n\nUser: " + user_message;
    } else {
      log_write(LEVEL_DEBUG, "agent", "RAG: no context found [%s]", rag_.embed_llama->last_error());
    }
  }
  if (!iter_) {
    tui_.append_line(ICON_ERR + "Conversation not initialised (call /clear to reset)");
    tui_.redraw_all();
    return false;
  }
  broadcast_.reset();
  turn_.number++;
  turn_.reset();
  rag_.reset_cache();
  last_response_.clear();
  bool flushed = false;
  if (!llama_->add_message(*iter_, "user", effective_message)) {
    if (llama_->is_memory_flush()) {
      if (++broadcast_.flush_count > MAX_FLUSH_RETRIES) {
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
  int tick_count = 0;

  auto fetch_tool = [&]() -> void {
    while (iter_->_has_next && !tui_.is_escape()) {
      const std::string tok = llama_->next(*iter_);
      buffer += tok;
      tui_.tick_spinner();
      if (++tick_count % 5 == 0) {
        tui_.tick_spinner();
      }
    }
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
        if (next == ">") {
          break;
        }
      }
      std::string tag = ThinkParser::assemble_tag(tokens);
      if (tag.empty() || tag.size() >= 32) {
        // not a real tag — treat as plain text
        for (const auto &t : tokens) {
          buffer += t;
        }
      } else {
        buffer += tag;
      }
    } else {
      buffer += tok;
    }

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
        broadcast_.line_batch += token;
        last_response_ += token;
        if (++broadcast_.line_batch_count >= 3) {
          tui_.append_token(broadcast_.line_batch);
          broadcast_throttled(broadcast_.line_batch);
          broadcast_.line_batch.clear();
          broadcast_.line_batch_count = 0;
        }
      }
      buffer = buffer.substr(pos + 1);
    }
  }

  if (!broadcast_.line_batch.empty()) {
    tui_.append_token(broadcast_.line_batch);
    broadcast_throttled(broadcast_.line_batch);
    broadcast_.line_batch.clear();
    broadcast_.line_batch_count = 0;
  }
  if (!buffer.empty()) {
    tui_.append_token(buffer);
    broadcast_throttled(buffer);
    last_response_ += buffer;
  }
  flush_broadcast();

  tui_.set_thinking(false);
  tui_.update_usage(tokens_per_sec(), get_mem_info());

  char stat[128];
  const auto pattern = ICON_SYS + "%.1f tok/s  (%d tokens)  %.1fs elapsed  KV %.1f%%";
  std::snprintf(stat, sizeof(stat), pattern.c_str(),
                static_cast<double>(tui_.get_tokens_per_sec()),
                iter_->_tokens_generated,
                elapsed_seconds(),
                static_cast<double>(tui_.get_kv_percent()));
  tui_.append_line(stat);

  // Record usage log for this turn
  TurnMetrics metrics;
  metrics.turn_number       = turn_.number;
  metrics.llm_latency_ms    = elapsed_seconds() * 1000.0;
  metrics.completion_tokens = iter_->_tokens_generated;
  metrics.tool_call_count   = turn_.tool_calls;
  metrics.tool_error_count  = turn_.tool_errors;
  metrics.kv_usage_pct      = llama_->memory_kv_percent();
  metrics.files_read        = turn_.files_read;
  metrics.files_written     = turn_.files_written;
  usage_log_.record_turn(metrics);
  log_write(LEVEL_DEBUG, "usage_log", "%s", usage_log_.summary().c_str());
  if (usage_log_.kv_trending_high()) {
    tui_.append_line(ICON_SYS + "KV usage trending high — consider restarting soon.");
  }

  tui_.redraw_all();
  return true;
}

bool Agent::run_turn_with_skill(const std::string &skill_text, const std::string &user_msg) {
  if (!model_loaded_ || !iter_) {
    tui_.append_line(ICON_ERR + "No model loaded. Use /model <path>");
    tui_.redraw_all();
    return false;
  }
  // Inject skill text as a tool_result message
  std::string content = "Skill loaded:\n" + skill_text;
  if (!llama_->add_message(*iter_, "tool_result", content)) {
    tui_.append_line(ICON_ERR + "skill inject: " + llama_->last_error());
    tui_.redraw_all();
    return false;
  }
  // Default kickoff if none provided
  std::string msg = user_msg;
  if (msg.empty()) {
    msg = "Skill loaded. What would you like to do?";
  }
  return run_turn(msg);
}

void Agent::dump_usage_log(const std::string &path) const {
  usage_log_.dump(path);
}

void Agent::generate_badge(const std::string &sandbox) {
  const std::string log_path = sandbox + "/.hali/usage_log.json";
  const std::string badge_path = sandbox + "/.hali/badge.svg";

  // Add this session's totals to the cumulative values (loaded at startup).
  usage_log_.cumulative_tokens      += usage_log_.session_tokens();
  usage_log_.cumulative_tool_calls  += usage_log_.total_tool_calls;
  usage_log_.cumulative_tool_errors += usage_log_.total_tool_errors;
  usage_log_.cumulative_turns       += static_cast<int>(usage_log_.turns.size());
  usage_log_.cumulative_llm_ms      += usage_log_.total_llm_ms;

  // Write the updated usage log with cumulative totals.
  usage_log_.dump(log_path);

  // Generate and write the SVG badge.
  std::string svg = usage_log_.badge_svg();
  std::ofstream f(badge_path);
  if (f) {
    f << svg;
    log_write(LEVEL_INFO, "agent", "badge written to %s (%d cumulative tokens)",
              badge_path.c_str(), usage_log_.cumulative_tokens);
  } else {
    log_write(LEVEL_WARNING, "agent", "cannot write badge to %s", badge_path.c_str());
  }
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

std::vector<std::string> Agent::tool_names() const {
  std::vector<std::string> names;
  names.reserve(tools_.size());
  for (const auto &pair : tools_) {
    names.push_back(pair.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::string Agent::invoke_tool_by_name(const std::string &name, const std::string &a1, const std::string &a2) {
  auto it = tools_.find(name);
  if (it == tools_.end()) {
    return "ERROR: unknown tool: " + name;
  }
  return it->second(a1, a2);
}
