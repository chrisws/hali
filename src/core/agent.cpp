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
#include <ctime>
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
  if (cfg.web_enabled_ && !utils::is_blank(message)) {
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
  return knowledge_.setup_embed(path, tui_);
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
  if (!cfg_.web_enabled_ || utils::is_blank(msg)) {
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
  if (!cfg_.web_enabled_) {
    broadcast_.broadcast_buffer.clear();
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

std::string Agent::rag_tool(const std::string &agent_query) {
  return knowledge_.query(agent_query, cfg_.rag_top_k_, cfg_.n_ctx_);
}

std::string Agent::rag_inspect() const {
  return knowledge_.inspect(cfg_.rag_top_k_);
}

std::string Agent::handle_ragindex(const std::string &zim_path) {
  if (zim_path.empty()) {
    return "ERROR: usage: TOOL:RAGINDEX <zim_file_path> | --validate <rag_file> | --repair <rag_file>";
  }

  /* --repair mode: walk a .rag file, fix stale n_chunks header */
  if (utils::starts_with(zim_path, "--repair")) {
    std::string rag_path = utils::trim(zim_path.substr(8));
    if (rag_path.empty()) {
      return "ERROR: usage: TOOL:RAGINDEX --repair <rag_file_path>";
    }
    tui_.show_tool("ragrepair: " + rag_path);
    int count = RagDB::repair(rag_path);
    if (count < 0) {
      return "ERROR: failed to repair " + rag_path;
    }
    std::string msg = "OK: repaired " + rag_path + " (" +
                      std::to_string(count) + " chunks)";
    tui_.append_line(ICON_TOOL + msg);
    return msg;
  }

  /* --validate mode: load an existing .rag file and report issues */
  if (utils::starts_with(zim_path, "--validate")) {
    std::string rag_path = utils::trim(zim_path.substr(10));
    if (rag_path.empty()) {
      return "ERROR: usage: TOOL:RAGINDEX --validate <rag_file_path>";
    }
    tui_.show_tool("ragvalidate: " + rag_path);

    RagDB db;
    if (!db.load_mmap(rag_path)) {
      if (!db.load(rag_path)) {
        return "ERROR: failed to load " + rag_path;
      }
    }

    auto issues = db.validate();
    if (issues.empty()) {
      std::string msg = "OK: " + std::to_string(db.size()) +
                        " chunks, no issues found";
      tui_.append_line(ICON_TOOL + msg);
      return msg;
    }

    std::ostringstream out;
    out << "VALIDATION: " << db.size() << " chunks, "
        << issues.size() << " issue(s) found:\n";
    for (const auto &i : issues) {
      out << "  chunk " << i.chunk_index << ": " << i.reason << "\n";
      tui_.append_line("  [" + std::to_string(i.chunk_index) + "] " + i.reason);
    }
    return out.str();
  }

  /* normal indexing mode */
  if (!embed_model()) {
    return "ERROR: no embedding model loaded. Use /embed first.";
  }

  // Work on a local copy since zim_path is const ref
  std::string path = zim_path;

  // Parse --new / --append flags
  std::string target_name;
  bool force_new = false;
  if (utils::starts_with(path, "--new ")) {
    force_new = true;
    path = utils::trim(path.substr(6));
  } else if (utils::starts_with(path, "--append ")) {
    std::string rest = utils::trim(path.substr(9));
    if (rest.empty()) {
      return "ERROR: usage: TOOL:RAGINDEX --append <name> <zim_file>";
    }
    // The ZIM path follows the name
    auto sp = rest.find(' ');
    if (sp != std::string::npos) {
      path = utils::trim(rest.substr(sp + 1));
      target_name = rest.substr(0, sp);
    } else {
      return "ERROR: --append requires a ZIM file path after the name";
    }
  }

  tui_.show_tool("ragindex: " + path);

  // If no explicit flag, check for existing .rag files and offer picker
  if (!force_new && target_name.empty()) {
    std::vector<std::string> existing;
    const std::string &rd = knowledge_.rag_dir();
    if (fs::exists(rd)) {
      for (auto &e : fs::directory_iterator(rd)) {
        if (e.path().extension() == ".rag") {
          existing.emplace_back(e.path().stem().string());
        }
      }
    }
    if (!existing.empty()) {
      std::vector<std::string> options;
      fs::path zp(path);
      options.emplace_back("Create new: " + zp.stem().string() + ".rag");
      for (const auto &name : existing) {
        options.emplace_back("Append to: " + name + ".rag");
      }
      int sel = tui_.list_picker(options, "RAG index target");
      if (sel < 0) {
        tui_.append_line(ICON_ERR + "RAG index cancelled");
        return "CANCELLED";
      }
      if (sel == 0) {
        // Create new — use ZIM stem, but make unique if it collides
        fs::path zp(path);
        std::string stem = zp.stem().string();
        bool collides = std::find(existing.begin(), existing.end(), stem)
                        != existing.end();
        if (collides) {
          auto now = std::chrono::system_clock::now();
          auto t = std::chrono::system_clock::to_time_t(now);
          std::tm tm_buf {};
          localtime_r(&t, &tm_buf);
          char ts[16];
          std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_buf);
          target_name = stem + "_" + ts;
        }
      } else {
        target_name = existing[sel - 1];
      }
    }
  }

  const std::string filter_path = cfg_.sandbox_ + "/.hali/topics.txt";
  std::string err;
  auto progress = [this](const std::string &msg) {
    tui_.show_tool(msg);
  };
  bool ok = knowledge_.build_from_path(path, *embed_model(), tui_, progress, &err, filter_path, target_name);
  if (ok) {
    fs::path zp(path);
    std::string id = target_name.empty() ? zp.stem().string() : target_name;
    if (!knowledge_.load(id)) {
      tui_.append_line(ICON_ERR + "ERROR: indexed but failed to load " + id);
    } else {
      tui_.append_line(ICON_TOOL + "OK: indexed and loaded " + id);
      // Track active .rag files in config
      if (std::find(cfg_.rag_files_.begin(), cfg_.rag_files_.end(), id)
          == cfg_.rag_files_.end()) {
        cfg_.rag_files_.emplace_back(id);
        cfg_.save_settings();
      }
    }
  } else {
    tui_.append_line(ICON_ERR + "ERROR: " + err);
  }
  return "OK";
}

bool Agent::rag_load_index(const std::string &path) {
  return knowledge_.load_bin(path, tui_);
}

int Agent::rag_load_dir(const std::string &dir) {
  return knowledge_.load_dir(dir);
}

bool Agent::rag_index(const std::string &path) {
  return knowledge_.index_path(path, tui_, cfg_.sandbox_);
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

  std::string effective_message = build_effective_message(user_message);
  if (!iter_) {
    tui_.append_line(ICON_ERR + "Conversation not initialised (call /clear to reset)");
    tui_.redraw_all();
    return false;
  }
  broadcast_.reset();
  turn_.number++;
  turn_.reset();
  knowledge_.reset_cache();
  last_response_.clear();
  if (!llama_->add_message(*iter_, "user", effective_message)) {
    if (!handle_memory_flush()) {
      return false;
    }
  }
  tui_.append_line("Hali: ");

  // Track cumulative tokens and elapsed time across all generation segments
  // (add_message resets _tokens_generated and _t_start on each tool_result)
  auto turn_start = iter_->_t_start;
  int turn_tokens = 0;

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
        if (!utils::is_blank(er.thought)) {
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
      turn_tokens += iter_->_tokens_generated;
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

  turn_tokens += iter_->_tokens_generated;

  auto turn_end = std::chrono::high_resolution_clock::now();
  double turn_elapsed = std::chrono::duration<double>(turn_end - turn_start).count();

  record_turn_metrics(turn_elapsed, turn_tokens);

  tui_.redraw_all();
  return true;
}

std::string Agent::build_effective_message(const std::string &user_message) {
  std::string effective_message = user_message;
  if (knowledge_.active()) {
    std::string context = knowledge_.query(user_message, cfg_.rag_top_k_, cfg_.n_ctx_);
    if (!context.empty() && context != "RAG: not enabled") {
      log_write(LEVEL_DEBUG, "agent", "RAG: %s", context.c_str());
      effective_message = "Context:\n" + context + "\n\nUser: " + user_message;
    } else {
      log_write(LEVEL_DEBUG, "agent", "RAG: no context found");
    }
  }
  return effective_message;
}

bool Agent::handle_memory_flush() {
  if (llama_->is_memory_flush()) {
    if (++broadcast_.flush_count > MAX_FLUSH_RETRIES) {
      tui_.append_line(ICON_ERR + "Multiple memory flushes detected. Stopping auto-continue.");
      tui_.redraw_all();
      return false;
    }
    tui_.append_line(ICON_ERR + "Warning! - memory has been flushed!");
    reset_conversation(cfg_.build_system_prompt() + "\n" + format_session_md(tui_.get_chat()));
    bool flushed = llama_->add_message(*iter_, "user",
                                       "The session context was restored after a memory flush. "
                                       "Continue with the pending actions from the session state.");
    if (!flushed) {
      tui_.redraw_all();
      return false;
    }
    tui_.append_line(ICON_SYS + "Auto-continuing from session context...");
    tui_.redraw_all();
    return true;
  }
  tui_.append_line(ICON_ERR + "add_message: " + llama_->last_error());
  tui_.redraw_all();
  return false;
}

void Agent::record_turn_metrics(double turn_elapsed, int turn_tokens) {
  double turn_tps = (turn_elapsed > 0.0 && turn_tokens > 0) ? (turn_tokens / turn_elapsed) : 0.0;

  tui_.set_thinking(false);
  tui_.update_usage(static_cast<float>(turn_tps), get_mem_info());

  char stat[128];
  const auto pattern = ICON_SYS + "%.1f tok/s  (%d tokens)  %.1fs elapsed  KV %.1f%%";
  std::snprintf(stat, sizeof(stat), pattern.c_str(),
                turn_tps,
                turn_tokens,
                turn_elapsed,
                static_cast<double>(tui_.get_kv_percent()));
  tui_.append_line(stat);

  TurnMetrics metrics;
  metrics.turn_number       = turn_.number;
  metrics.llm_latency_ms    = turn_elapsed * 1000.0;
  metrics.completion_tokens = turn_tokens;
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
