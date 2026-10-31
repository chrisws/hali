// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <format>
#include <system_error>

#include "ggml.h"

#include "config.h"
#include "tui.h"
#include "agent.h"
#include "logging.h"
#include "curl.h"
#include "mcp_client.h"
#include "run_allowed.h"
#include "ui_text.h"
#include "webview.h"
#include "icon.h"
#include "audio.h"
#include "slash_commands.h"

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
      cfg.web_enabled_ = true;
      cfg.web_port_ = std::stoi(take_next(a.c_str()));
    } else if (a == "-b" || a == "--enabled-backups") {
      cfg.enabled_backups_ = true;
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
// ggml abort handler
//
extern "C" void abort_callback(const char *error_message) {
  Tui::cleanup();
  std::fprintf(stderr, "\n\033[1;31mHali fatal error:\033[0m\n%s\n\n", error_message);
  std::fprintf(stderr, "Reduce gpu_layers or close other applications using the GPU.\n\n");
  exit(1);
}

//
// main()
//
int main(int argc, char **argv) {
  HaliConfig cfg;

  ggml_set_abort_callback(abort_callback);

  // ── Parse arguments (command-line overrides saved settings) ──────
  bool do_mcp_test = false;
  std::string cli_log_level;
  mcp::Client mcp_client;
  parse_args(argc, argv, cfg, mcp_client, do_mcp_test, cli_log_level);

  // ── Load persisted settings first (provides defaults) ─────────────
  cfg.load_settings();

  // ── Load user-approved RUN commands ──────────────────────────────
  cfg.user_run_allowed_.load(RunAllowed::default_path(cfg.sandbox_));

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
  for (const auto &kf : HaliConfig::AUTO_KNOWLEDGE_FILES) {
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
  tui.set_mode_str(cfg.mode_string());
  // Load persisted input history so up-arrow works across sessions.
  tui.history_load(history_path(cfg.sandbox_));
  ui::welcome(tui, cfg.sandbox_);

  // ── Init WebDev ───────────────────────────────────────────────────
  if (cfg.web_enabled_) {
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
      // Auto-load .hali/rag/*.rag indexes
      std::string rag_dir = (fs::path(cfg.sandbox_) / ".hali" / "rag").string();
      int n_loaded = agent.rag_load_dir(rag_dir);
      if (n_loaded > 0) {
        tui.append_line(ICON_SYS + std::format("Loaded {} RAG index(es) from .hali/rag/", n_loaded));
        tui.redraw_all();
      }
    }
  } else {
    ui::no_model(tui);
  }

  // ── Main loop ─────────────────────────────────────────────────────
  log_write(LEVEL_INFO, "main", "hali starting");
  for (;;) {
    if (cfg.web_enabled_) {
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

  if (cfg.web_enabled_) {
    webview::stop();
  }

  // Show a "shutting down" popup while cleanup runs.
  tui.show_modal_popup("⏳ Shutting down…", "Saving state and cleaning up");

  log_write(LEVEL_INFO, "main", "hali exiting");

  // Persist settings on exit (unless disabled).
  if (!cfg.disable_save_on_exit_) {
    cfg.save_settings();
  }

  // Usage log is always written (even when save is disabled).
  agent.dump_usage_log(usage_log_path(cfg.sandbox_));

  // Generate SVG badge (tied to disable_save_on_exit).
  if (!cfg.disable_save_on_exit_) {
    agent.generate_badge(cfg.sandbox_);
  }

  log_close();

  // Persist MTP best settings if the model found improvements.
  agent.save_mtp_best_settings();

  // Persist input history for the next session.
  cfg.user_run_allowed_.save(RunAllowed::default_path(cfg.sandbox_));
  tui.history_save(history_path(cfg.sandbox_));
  audio::shutdown();
  curl_close();

  return 0;
}
