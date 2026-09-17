// This file is part of Nitro
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
//
// A standalone agentic LLM shell with notcurses TUI.
// Uses llama-sb.h as the sole llama.cpp integration layer.
//
// Usage:
//   ./nitro [options] [project_dir]
//
// Options:
//   -m, --model  <path>       GGUF model to load on startup
//   -e, --embed  <path>       embedding model for RAG
//   -g, --gpu-layers <n>      layers to offload to GPU (default: 32)
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
#include "ui_text.h"
#include "webview.h"

// Returns the history file path: ~/.config/nitro/history.txt
static std::string history_path() {
  const char *home = getenv("HOME");
  std::string base = home ? std::string(home) : ".";
  return base + "/.config/nitro/history.txt";
}

//
// Slash command handler
//
static void handle_slash(const std::string &input, NitroConfig &cfg, Agent &agent, Tui &tui) {
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
  // ── /model ──────────────────────────────────────────────────────────────
  // If no path is given, open the file picker so the user can browse to a
  // GGUF.  The picker starts in the current sandbox directory.
  if (verb == "/model") {
    if (rest.empty()) {
      tui.append_line(ICON_SYS + "Opening model picker…");
      tui.redraw_all();
      rest = tui.file_picker(cfg.sandbox_, "Model File");
      if (rest.empty()) {
        tui.append_line(ICON_SYS + "/model cancelled.");
        tui.redraw_all();
        return;
      }
    }
    cfg.model_path_ = rest;
    if (agent.setup_model()) {
      std::string sysp = cfg.build_system_prompt();
      agent.reset_conversation(sysp);
      cfg.save_settings();
    }
    tui.redraw_all();
    return;
  }

  // ── /embed ──────────────────────────────────────────────────────────────
  // If no path is given, open the file picker so the user can browse to an
  // embedding GGUF.
  if (verb == "/embed") {
    if (rest.empty()) {
      tui.append_line(ICON_SYS + "Opening embedding model picker…");
      tui.redraw_all();
      rest = tui.file_picker(cfg.sandbox_, "Embedding Model");
      if (rest.empty()) {
        tui.append_line(ICON_SYS + "/embed cancelled.");
        tui.redraw_all();
        return;
      }
    }
    cfg.embed_path_ = rest;
    if (agent.setup_embed(rest)) {
      cfg.save_settings();
    }
    return;
  }

  // ── /rag ────────────────────────────────────────────────────────────────
  if (verb == "/rag") {
    std::string path = rest;
    if (path.empty()) {
      // Launch the interactive folder picker starting from the sandbox.
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

  if (verb == "/save") {
    tui.append_line(ICON_TOOL + tui.save_chat(rest));
    tui.redraw_all();
    return;
  }

  if (verb == "/settings") {
    ui::settings(tui, cfg);
    return;
  }

  if (verb == "/set") {
    // Usage: /set <key> <value>
    // Parses the key and value, updates cfg in place, re-applies generation
    // params if needed, and saves settings to disk.
    auto sp2 = rest.find(' ');
    std::string key = (sp2 == std::string::npos) ? rest : rest.substr(0, sp2);
    std::string val = (sp2 == std::string::npos) ? "" : rest.substr(sp2 + 1);
    val.erase(0, val.find_first_not_of(" \t"));

    if (key.empty() || val.empty()) {
      tui.append_line(ICON_ERR + "Usage: /set <key> <value>");
      tui.redraw_all(); return;
    }

    bool ok = true;
    bool needs_reparam = false;
    try {
      if (key == "temperature")    { cfg.temperature_    = std::stof(val); needs_reparam = true; }
      else if (key == "top_p")     { cfg.top_p_          = std::stof(val); needs_reparam = true; }
      else if (key == "min_p")     { cfg.min_p_          = std::stof(val); needs_reparam = true; }
      else if (key == "top_k")     { cfg.top_k_          = std::stoi(val); needs_reparam = true; }
      else if (key == "penalty_repeat") { cfg.penalty_repeat_ = std::stof(val); needs_reparam = true; }
      else if (key == "penalty_last_n") { cfg.penalty_last_n_ = std::stoi(val); needs_reparam = true; }
      else if (key == "rag_top_k")      { cfg.rag_top_k_      = std::stoi(val); }
      else if (key == "offload_kqv")    { cfg.offload_kqv_    = (val == "true"); }
      else if (key == "n_gpu_layers")   {
        cfg.n_gpu_layers_ = std::stoi(val);
        tui.append_line(ICON_SYS + "n_gpu_layers will take effect on next /model load.");
      } else if (key == "run_allowed") {
        // Accept a comma-separated list of basenames, or "none" to clear.
        cfg.run_allowed_.clear();
        if (val != "none") {
          std::istringstream iss(val);
          std::string tok;
          while (std::getline(iss, tok, ',')) {
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t") + 1);
            if (!tok.empty()) cfg.run_allowed_.push_back(tok);
          }
        }
        if (cfg.run_allowed_.empty()) {
          tui.append_line(ICON_SYS + "run_allowed cleared — all sandbox programs permitted.");
        } else {
          std::string list;
          for (const auto &e : cfg.run_allowed_) list += e + " ";
          tui.append_line(ICON_SYS + "run_allowed: " + list);
        }
      } else {
        tui.append_line(ICON_ERR + "Unknown key '" + key + "'.  Try /help for list.");
        ok = false;
      }
    } catch (const std::exception &ex) {
      tui.append_line(ICON_ERR + "/set: " + ex.what());
      ok = false;
    }

    if (ok) {
      if (needs_reparam && agent.model_loaded()) {
        agent.apply_generation_params();
      }
      cfg.save_settings();
      tui.append_line(ICON_SYS + "" + key + " = " + val);
    }
    tui.redraw_all();
    return;
  }

  tui.append_line(ICON_ERR + "Unknown command: " + verb + "  (try /help)");
  tui.redraw_all();
}

//
// main()
//
int main(int argc, char **argv) {
  NitroConfig cfg;

  // ── Parse arguments (command-line overrides saved settings) ──────
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

  bool do_mcp_test = false;
  mcp::Client mcp_client;

  for (int i = 1; i < argc; ++i) {
    auto take_next = [&](const char *flag) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "nitro: %s requires an argument\n", flag);
        std::exit(1);
      }
      return argv[++i];
    };
    std:string a = argv[i];
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
      log_open(take_next(a.c_str()));
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
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "nitro: unknown option '%s'  (try --help)\n", a.c_str());
      std::exit(1);
    } else {
      cfg.sandbox_ = resolve_path(a);
    }
  }

  // ── Load persisted settings first (provides defaults) ─────────────
  cfg.load_settings();

  // ── Init curl globally ────────────────────────────────────────────
  curl_init();

  // ── Init MCP ──────────────────────────────────────────────────────
  if (do_mcp_test) {
    log_open_console();
    log_write(LEVEL_INFO, mcp_client.get_system_context(cfg.mcp_filter_).c_str());
    curl_close();
    log_close();
    return 0;
  }

  if (mcp_client.enabled()) {
    cfg.mcp_context_ = mcp_client.get_system_context(cfg.mcp_filter_);
  }

  // ── Resolve sandbox ───────────────────────────────────────────────
  if (cfg.sandbox_.empty()) {
    std::error_code ec;
    cfg.sandbox_ = fs::current_path(ec).string();
  }
  {
    std::error_code ec;
    fs::create_directories(cfg.sandbox_, ec);
  }

  // ── Auto-discover knowledge files ─────────────────────────────────
  for (const char *kf : {"nitro.md", "persona.md", "AGENTS.md"}) {
    if (fs::exists(kf)) {
      cfg.knowledge_files_.emplace_back(kf);
    }
  }

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
  log_write(LEVEL_INFO, "nitro starting");
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
    }
  }

  if (cfg.web_port_ != -1) {
    webview::stop();
  }

  log_write(LEVEL_INFO, "nitro exiting");
  log_close();

  // Persist input history for the next session.
  tui.history_save(history_path());
  curl_close();
  return 0;
}
