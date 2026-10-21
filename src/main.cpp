// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <random>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "llama.h"
#include "config.h"
#include "tui.h"
#include "agent.h"
#include "logging.h"
#include "curl.h"
#include "mcp_client.h"
#include "string_utils.h"
#include "run_allowed.h"
#include "ui_text.h"
#include "audit.h"
#include "webview.h"
#include "icon.h"
#include "audio.h"

//
// ── Slash command support functions ──────────────────────────────────────
//

//
// /model – pick or set the inference model
//
static void slash_model(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  std::string path = rest;
  if (path.empty()) {
    tui.append_line(ICON_SYS + "Opening model picker…");
    tui.redraw_all();
    path = tui.file_picker(cfg.sandbox_, "Model File");
    if (path.empty()) {
      tui.append_line(ICON_SYS + "/model cancelled.");
      tui.redraw_all();
      return;
    }
  }
  cfg.model_path_ = path;
  if (agent.setup_model()) {
    std::string sysp = cfg.build_system_prompt();
    agent.reset_conversation(sysp);
    cfg.save_settings();
  }
  tui.redraw_all();
}

//
// /embed – pick or set the embedding model
//
static void slash_embed(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  std::string path = rest;
  if (path.empty()) {
    tui.append_line(ICON_SYS + "Opening embedding model picker…");
    tui.redraw_all();
    path = tui.file_picker(cfg.sandbox_, "Embedding Model");
    if (path.empty()) {
      tui.append_line(ICON_SYS + "/embed cancelled.");
      tui.redraw_all();
      return;
    }
  }
  cfg.embed_path_ = path;
  if (agent.setup_embed(path)) {
    cfg.save_settings();
  }
  tui.redraw_all();
}

//
// /rag – index a folder or load a .bin index
//
static void slash_rag(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  std::string path = rest;
  if (path.empty()) {
    path = tui.rag_folder_picker(cfg.sandbox_);
    if (path.empty()) {
      tui.append_line(ICON_SYS + "RAG indexing cancelled.");
      tui.redraw_all();
      return;
    }
  }
  if (path.ends_with(".bin")) {
    tui.append_line(ICON_SYS + "Loading index: " + path);
    tui.redraw_all();
    agent.rag_load_index(path);
  } else {
    tui.append_line(ICON_SYS + "Indexing: " + path);
    tui.redraw_all();
    agent.rag_index(path);
  }
  tui.append_line(ICON_SYS + "done");
  tui.redraw_all();
}

//
// /audio – toggle, test, or set volume
//
static void slash_audio(const std::string &rest, Tui &tui) {
  if (rest.empty()) {
    bool new_state = !audio::is_enabled();
    audio::set_enabled(new_state);
    tui.append_line(ICON_SYS + std::string("Audio ") + (new_state ? "enabled." : "disabled."));
  } else if (rest == "test") {
    audio::play_test();
    tui.append_line(ICON_SYS + "Audio test played (polyphonic).");
  } else if (rest.rfind("volume", 0) == 0) {
    auto space = rest.find(' ', 6);
    if (space != std::string::npos) {
      float v = std::stof(rest.substr(space + 1));
      audio::set_volume(v);
      tui.append_line(ICON_SYS + "Audio volume set to " + std::to_string(v));
    } else {
      tui.append_line(ICON_ERR + "Usage: /audio volume <0.0-1.0>");
    }
  } else {
    tui.append_line(ICON_ERR + "Usage: /audio [test | volume <0.0-1.0>]");
  }
  tui.redraw_all();
}

//
// /set <key> <value> – update a config value at runtime
//
static void slash_set(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  auto sp = rest.find(' ');
  std::string key = (sp == std::string::npos) ? rest : rest.substr(0, sp);
  std::string val = (sp == std::string::npos) ? "" : rest.substr(sp + 1);
  val.erase(0, val.find_first_not_of(" \t"));

  if (key.empty() || val.empty()) {
    tui.append_line(ICON_ERR + "Usage: /set <key> <value>");
    tui.redraw_all();
    return;
  }

  bool needs_reparam = false;
  std::string msg;
  int rc = cfg.set_value(key, val, needs_reparam, msg);
  if (rc == 0) {
    if (needs_reparam && agent.model_loaded()) {
      agent.apply_generation_params();
    }
    cfg.save_settings();
    if (!msg.empty()) {
      tui.append_line(ICON_SYS + msg);
    }
    tui.append_line(ICON_SYS + "" + key + " = " + val);
  } else if (rc == 1) {
    tui.append_line(ICON_ERR + "Unknown key '" + key + "'.  Try /help for list.");
  } else {
    tui.append_line(ICON_ERR + "/set: " + msg);
  }
  tui.redraw_all();
}

//
// /revoke <name> – remove an approved RUN command
//
static void slash_revoke(const std::string &rest, HaliConfig &cfg, Tui &tui) {
  if (rest.empty()) {
    tui.append_line(ICON_ERR + "Usage: /revoke <name>");
    tui.redraw_all();
    return;
  }
  if (cfg.user_run_allowed_.remove(rest)) {
    cfg.run_allowed_.erase(
      std::remove(cfg.run_allowed_.begin(), cfg.run_allowed_.end(), rest),
      cfg.run_allowed_.end());
    cfg.user_run_allowed_.save(RunAllowed::default_path());
    tui.append_line(ICON_SYS + "Revoked: " + rest);
  } else {
    tui.append_line(ICON_ERR + "Not found in approved list: " + rest);
  }
  tui.redraw_all();
}

//
// /allow <path> – add a directory to the allowed list
//
static void slash_allow(const std::string &rest, HaliConfig &cfg, Tui &tui, Agent &agent) {
  std::string path = rest;
  if (path.empty()) {
    tui.append_line(ICON_SYS + "Opening directory picker…");
    tui.redraw_all();
    path = tui.file_picker(cfg.sandbox_, "Allow Directory");
    if (path.empty()) {
      tui.append_line(ICON_SYS + "/allow cancelled.");
      tui.redraw_all();
      return;
    }
  }
  std::error_code ec;
  if (!fs::is_directory(path, ec) || ec) {
    tui.append_line(ICON_ERR + "Not a directory: " + path);
    tui.redraw_all();
    return;
  }
  if (std::find(cfg.extra_dirs_.begin(), cfg.extra_dirs_.end(), path) == cfg.extra_dirs_.end()) {
    cfg.extra_dirs_.emplace_back(path);
    cfg.save_settings();
    tui.append_line(ICON_SYS + "Allowed: " + path);
    agent.notify("You are now permitted to access " + path);
  } else {
    tui.append_line(ICON_SYS + "Already allowed: " + path);
  }
  tui.redraw_all();
}

//
// /allow-list – list allowed directories
//
static void slash_allow_list(HaliConfig &cfg, Tui &tui) {
  if (cfg.extra_dirs_.empty()) {
    tui.append_line(ICON_SYS + "No extra directories allowed.");
  } else {
    tui.append_line(ICON_SYS + "Allowed directories:");
    for (const auto &d : cfg.extra_dirs_) {
      tui.append_line(ICON_SYS + "  - " + d);
    }
  }
  tui.redraw_all();
}

//
// /allow-remove [path] – remove an allowed directory
//
static void slash_allow_remove(const std::string &rest, HaliConfig &cfg, Tui &tui, Agent &agent) {
  std::string target = rest;
  target.erase(0, target.find_first_not_of(" \t"));
  if (target.empty()) {
    if (cfg.extra_dirs_.empty()) {
      tui.append_line(ICON_SYS + "Nothing is allowed.");
      tui.redraw_all();
      return;
    }
    int idx = tui.list_picker(cfg.extra_dirs_, "Remove Allowed Directory");
    if (idx < 0) {
      tui.append_line(ICON_SYS + "/allow-remove cancelled.");
      tui.redraw_all();
      return;
    }
    target = cfg.extra_dirs_[idx];
  }
  auto it = std::find(cfg.extra_dirs_.begin(), cfg.extra_dirs_.end(), target);
  if (it != cfg.extra_dirs_.end()) {
    cfg.extra_dirs_.erase(it);
    cfg.save_settings();
    tui.append_line(ICON_SYS + "Removed: " + target);
    agent.notify("You are no longer permitted to access " + target);
  } else {
    tui.append_line(ICON_ERR + "Not found: " + target);
  }
  tui.redraw_all();
}

//
// /session [path] – save the current session to a file
//
static void slash_session(const std::string &rest, HaliConfig &cfg, Tui &tui) {
  std::string path = rest.empty() ? cfg.sandbox_ + "/SESSION.md" : rest;
  std::error_code ec;
  if (fs::exists(path, ec) && !ec) {
    if (!tui.confirm_dialog("Overwrite " + path + "?")) {
      tui.append_line(ICON_SYS + "Cancelled.");
      tui.redraw_all();
      return;
    }
  }
  tui.append_line(ICON_TOOL + tui.save_session(path));
  tui.redraw_all();
}

//
// Parse "FP <file> <line> <severity> <message>" lines from model output.
//
static std::vector<FalsePositive> parse_false_positives(const std::string &text) {
  std::vector<FalsePositive> fps;
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    // Trim leading whitespace
    auto pos = line.find_first_not_of(" \t");
    if (pos == std::string::npos) continue;
    line = line.substr(pos);
    if (line.rfind("FP ", 0) != 0) continue;

    // FP <file> <line> <severity> <message>
    std::istringstream ls(line.substr(3));
    FalsePositive fp;
    if (!(ls >> fp.file)) continue;
    int ln = 0;
    if (!(ls >> ln)) continue;
    fp.line = ln;
    if (!(ls >> fp.severity)) continue;
    // Rest of the line is the message
    std::string msg;
    std::getline(ls, msg);
    msg.erase(0, msg.find_first_not_of(" \t"));
    if (msg.empty()) continue;
    fp.message = msg;
    fps.push_back(std::move(fp));
  }
  return fps;
}

//
// /audit – scan the codebase for model-hostility issues
//
static void slash_audit(const std::string &rest, HaliConfig &cfg, Tui &tui, Agent &agent) {
  std::string root = rest.empty() ? cfg.sandbox_ : rest;
  std::string fp_path = cfg.sandbox_ + "/audit_feedback.json";

  // Load previously confirmed false positives
  auto fps = load_false_positives(fp_path);

  tui.append_line(ICON_SYS + "Scanning " + root + " …");
  tui.redraw_all();

  AuditResult r = audit_cpp(root);

  // Filter out known false positives
  int before = static_cast<int>(r.issues.size());
  filter_false_positives(r, fps);
  int filtered = before - static_cast<int>(r.issues.size());
  if (filtered > 0) {
    tui.append_line(ICON_SYS + std::to_string(filtered) + " known false positive(s) skipped");
  }

  tui.append_line(ICON_SYS + std::to_string(r.total_files) + " files, " +
                   std::to_string(r.total_lines) + " lines");
  tui.append_line(ICON_SYS + "Difficulty score: " + std::to_string(r.score) + "/100");

  if (r.issues.empty()) {
    tui.append_line(ICON_SYS + "No issues found.");
    tui.redraw_all();
    return;
  }

  tui.append_line(ICON_SYS + std::to_string(r.issues.size()) + " issue(s) found:");
  auto table = audit_format_table(r);
  for (const auto &line : table) {
    tui.append_line(line);
  }
  tui.redraw_all();

  // Offer to let the model resolve the issues
  if (tui.confirm_dialog("Allow Hali to resolve these issues?")) {
    // Write the report as a plan file so it survives KV flushes
    std::string plan_path = cfg.sandbox_ + "/plans/audit_report.md";
    audit_write_report(r, plan_path);
    tui.append_line(ICON_SYS + "Report saved to " + plan_path);

    std::string prompt = audit_format_prompt(r, plan_path);
    tui.append_line(ICON_SYS + "Handing audit report to model…");
    tui.redraw_all();
    agent.run_turn(prompt);

    // Parse the model's response for confirmed false positives
    auto new_fps = parse_false_positives(agent.last_response());
    if (!new_fps.empty()) {
      // Avoid duplicates
      for (const auto &nf : new_fps) {
        bool dup = false;
        for (const auto &existing : fps) {
          if (existing.file == nf.file && existing.line == nf.line &&
              existing.severity == nf.severity && existing.message == nf.message) {
            dup = true;
            break;
          }
        }
        if (!dup) {
          fps.push_back(nf);
        }
      }
      save_false_positives(fp_path, fps);
      tui.append_line(ICON_SYS + std::to_string(new_fps.size()) +
                       " false positive(s) recorded to " + fp_path);
      tui.redraw_all();
    }
  }
}

//
// Slash command dispatcher
//
static void handle_slash(const std::string &input, HaliConfig &cfg, Agent &agent, Tui &tui) {
  auto sp = input.find(' ');
  std::string verb = (sp == std::string::npos) ? input : input.substr(0, sp);
  std::string rest;
  if (sp != std::string::npos) {
    rest = input.substr(sp + 1);
    rest.erase(0, rest.find_first_not_of(" \t"));
  }

  if (verb == "/help") {
    ui::help(tui);
    return;
  }
  if (verb == "/model") {
    slash_model(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/embed") {
    slash_embed(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/rag") {
    slash_rag(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/memory") {
    std::istringstream iss(agent.memory_info_text());
    std::string line;
    while (std::getline(iss, line)) {
      tui.append_line(ICON_SYS + line);
    }
    tui.redraw_all();
    return;
  }
  if (verb == "/clear") {
    std::string sysp = cfg.build_system_prompt();
    agent.reset_conversation(sysp);
    tui.append_line(ICON_SYS + "Conversation cleared.");
    tui.redraw_all();
    return;
  }
  if (verb == "/theme") {
    tui.toggle_theme();
    return;
  }
  if (verb == "/audio") {
    slash_audio(rest, tui);
    return;
  }
  if (verb == "/save") {
    tui.append_line(ICON_TOOL + tui.save_chat(rest));
    tui.redraw_all();
    return;
  }
  if (verb == "/session") {
    slash_session(rest, cfg, tui);
    return;
  }
  if (verb == "/settings") {
    ui::settings(tui, cfg);
    return;
  }
  if (verb == "/set") {
    slash_set(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/revoke") {
    slash_revoke(rest, cfg, tui);
    return;
  }
  if (verb == "/allow") {
    slash_allow(rest, cfg, tui, agent);
    return;
  }
  if (verb == "/allow-list") {
    slash_allow_list(cfg, tui);
    return;
  }
  if (verb == "/allow-remove") {
    slash_allow_remove(rest, cfg, tui, agent);
    return;
  }
  if (verb == "/audit") {
    slash_audit(rest, cfg, tui, agent);
    return;
  }

  tui.append_line(ICON_ERR + "Unknown command: " + verb + "  (try /help)");
  tui.redraw_all();
}

//
// Parse command-line arguments into config and local state.
//
static void parse_args(int argc, char **argv, HaliConfig &cfg,
                       mcp::Client &mcp_client, bool &do_mcp_test,
                       std::string &cli_log_level) {
  auto resolve_path = [](const std::string &arg) -> std::string {
    if (arg.substr(0, 2) == "~/") {
      const char *home = getenv("HOME");
      return std::string(home ? home : ".") + "/" + arg.substr(2);
    }
    if (arg.substr(0, 2) == "./") {
      std::error_code ec;
      return (fs::current_path(ec) / arg.substr(2)).string();
    }
    return arg;
  };

  do_mcp_test = false;
  cli_log_level.clear();

  for (int i = 1; i < argc; ++i) {
    auto take_next = [&](const char *flag) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "hali: %s requires an argument\n", flag);
        std::exit(1);
      }
      return argv[++i];
    };
    std::string a = argv[i];
    if (a == "-m" || a == "--model") {
      cfg.model_path_ = resolve_path(take_next(a.c_str()));
    } else if (a == "-e" || a == "--embed") {
      cfg.embed_path_ = resolve_path(take_next(a.c_str()));
    } else if (a == "-g" || a == "--gpu-layers") {
      cfg.n_gpu_layers_ = std::stoi(take_next(a.c_str()));
    } else if (a == "--mcp-filter") {
      cfg.mcp_filter_.emplace_back(take_next(a.c_str()));
    } else if (a == "--mcp") {
      mcp_client.enable();
    } else if (a == "--mcp-test") {
      do_mcp_test = true;
    } else if (a == "-s" || a == "--skill") {
      cfg.knowledge_files_.emplace_back(take_next(a.c_str()));
    } else if (a == "-c" || a == "--config") {
      cfg.set_config(take_next(a.c_str()));
    } else if (a == "-l" || a == "--log") {
      cli_log_level = take_next(a.c_str());
    } else if (a == "-t" || a == "--think") {
      cfg.thinking_ = false;
    } else if (a == "-n" || a == "--prompt-permission") {
      cfg.permission_prompt_ = true;
    } else if (a == "-p" || a == "--web-port") {
      cfg.web_port_ = std::stoi(take_next(a.c_str()));
    } else if (a == "-b" || a == "--backup-path") {
      cfg.backup_path_ = take_next(a.c_str());
    } else if (a == "-h" || a == "--help") {
      ui::usage();
      std::exit(0);
    } else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "hali: unknown option '%s'  (try --help)\n", a.c_str());
      std::exit(1);
    }
  }
}

//
// main()
//
int main(int argc, char **argv) {
  HaliConfig cfg;

  // ── Parse arguments (command-line overrides saved settings) ──────
  bool do_mcp_test = false;
  std::string cli_log_level;
  mcp::Client mcp_client;
  parse_args(argc, argv, cfg, mcp_client, do_mcp_test, cli_log_level);

  // ── Load persisted settings first (provides defaults) ─────────────
  cfg.load_settings();

  // ── Init logging: CLI -l overrides config defaults ───────────────
  if (!cli_log_level.empty()) {
    log_open(cli_log_level);
  } else {
    log_open(cfg.log_level_str_, cfg.log_file_, cfg.log_filter_, cfg.log_erase_);
  }

  // ── Init curl globally ────────────────────────────────────────────
  curl_init();

  // ── Init MCP ──────────────────────────────────────────────────────
  if (do_mcp_test) {
    log_open_console();
    log_write(LEVEL_INFO, "main", mcp_client.get_system_context(cfg.mcp_filter_).c_str());
    curl_close();
    log_close();
    return 0;
  }

  if (mcp_client.enabled()) {
    if (!cfg.mcp_host_.empty() && cfg.mcp_port_ > 0) {
      mcp_client.set_settings(cfg.mcp_host_, cfg.mcp_port_);
    }
    cfg.mcp_context_ = mcp_client.get_system_context(cfg.mcp_filter_);
  }

  // ── Auto-discover knowledge files ─────────────────────────────────
  for (const char *kf : {"hali.md", "persona.md", "AGENTS.md"}) {
    if (fs::exists(kf)) {
      cfg.knowledge_files_.emplace_back(kf);
    }
  }

  // ── Init Audio (plan 11) ──────────────────────────────────────────
  audio::init();
  audio::set_enabled(cfg.audio_enabled_);
  audio::set_volume(cfg.audio_volume_);

  // ── Init TUI ──────────────────────────────────────────────────────
  Tui tui;
  tui.init();
  // Load persisted input history so up-arrow works across sessions.
  tui.history_load(history_path());
  ui::welcome(tui, cfg.sandbox_);

  // ── Init WebDev ───────────────────────────────────────────────────
  if (cfg.web_port_ != -1) {
    if (webview::start(cfg.sandbox_, cfg.web_port_)) {
      tui.append_line(ICON_SYS + std::format("Web mode enabled on port: {}", cfg.web_port_));
    } else {
      tui.append_line(ICON_SYS + "Failed to enable web mode");
    }
  }

  // ── Init agent ────────────────────────────────────────────────────
  Agent agent(cfg, tui, mcp_client);
  if (!cfg.model_path_.empty()) {
    if (agent.setup_model()) {
      if (mcp_client.enabled()) {
        if (cfg.mcp_context_.empty()) {
          tui.append_line(ICON_SYS + "Failed to load MCP context");
        } else {
          tui.append_line(ICON_SYS + "MCP Enabled");
        }
      } else {
        tui.append_line(ICON_SYS + "MCP not enabled");
      }
      tui.append_line(ICON_SYS + "Loading context...");
      for (const auto &kf : cfg.knowledge_files_) {
        std::ifstream f(kf);
        if (f) {
          tui.append_line(ICON_SYS + kf);
        }
      }
      tui.redraw_all();
      std::string sysp = cfg.build_system_prompt();
      agent.reset_conversation(sysp);
      tui.append_line(ICON_SYS + "Ready");
      tui.redraw_all();
    }
    if (!cfg.embed_path_.empty()) {
      agent.setup_embed(cfg.embed_path_);
    }
  } else {
    ui::no_model(tui);
  }

  // ── Main loop ─────────────────────────────────────────────────────
  log_write(LEVEL_INFO, "main", "hali starting");
  for (;;) {
    if (cfg.web_port_ != -1) {
      if (webview::has_message()) {
        std::string input = webview::get_message();
        tui.append_line("Web: " + input);
        tui.redraw_all();
        agent.run_turn(input);
        continue;
      }
      if (!tui.has_input()) {
        continue;
      }
    }
    tui.resize();
    std::string input = tui.readline();
    input.erase(0, input.find_first_not_of(" \t"));
    if (!input.empty()) {
      input.erase(input.find_last_not_of(" \t\r\n") + 1);
    }
    if (input.empty()) {
      continue;
    }
    if (input == "exit" || input == "quit") {
      break;
    }
    tui.append_line("You: " + input);
    tui.redraw_all();
    if (input[0] == '/') {
      handle_slash(input, cfg, agent, tui);
    } else {
      agent.run_turn(input);
      if (cfg.audio_user_prompt_) {
        audio::play_user_prompt();
      }
    }
  }

  if (cfg.web_port_ != -1) {
    webview::stop();
  }

  // Show a "shutting down" popup while cleanup runs.
  tui.show_modal_popup("⏳ Shutting down…", "Saving state and cleaning up");

  log_write(LEVEL_INFO, "main", "hali exiting");

  // Dump usage log for post-hoc analysis.
  agent.dump_usage_log(cfg.sandbox_ + "/usage_log.json");

  log_close();

  // Persist MTP best settings if the model found improvements.
  agent.save_mtp_best_settings();

  // Persist input history for the next session.
  cfg.user_run_allowed_.save(RunAllowed::default_path());
  tui.history_save(history_path());
  audio::shutdown();
  curl_close();

  return 0;
}
