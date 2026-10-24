// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cstddef>
#include <cstdio>
#include <string>

#include "ui_text.h"
#include "icon.h"
#include "slash_commands.h"

namespace ui {

void help(Tui &tui) {
  tui.append_line(ICON_SYS + "Commands:");
  for (const auto &cmd : slash_commands()) {
    std::string line = cmd.name;
    if (cmd.usage[0] != '\0') {
      line += ' ';
      line += cmd.usage;
    }
    while (line.size() < 26) { line += ' '; }
    line += cmd.description;
    tui.append_line(ICON_SYS + "  " + line);
  }
  tui.append_line(ICON_SYS + "  exit / quit              exit Hali");
  tui.append_line(ICON_SYS + "Settable keys (via /set):");
  tui.append_line(ICON_SYS + "  temperature  top_p  top_k  min_p  penalty_repeat");
  tui.append_line(ICON_SYS + "  penalty_last_n  rag_top_k  n_gpu_layers");
  tui.append_line(ICON_SYS + "  run_allowed  (comma-separated list, e.g. python3,make)");
  tui.redraw_all();
}

void settings(Tui &tui, HaliConfig &cfg) {
  auto bool_str = [](bool v) -> std::string { return v ? "true" : "false"; };

  // Helper to format rope scaling type
  auto rope_str = [](int t) -> std::string {
    switch (t) {
      case 0: return "none";
      case 1: return "linear";
      case 2: return "yarn";
      case 3: return "longrope";
      default: return "unspecified";
    }
  };

  tui.append_line(ICON_SYS + "── Model ──────────────────────────────────────");
  tui.append_line(ICON_SYS + "  model_path      : " + cfg.model_path_);
  tui.append_line(ICON_SYS + "  embed_path      : " + cfg.embed_path_);
  tui.append_line(ICON_SYS + "  kv_cache_preset : " + cfg.kv_preset_to_string());
  tui.append_line(ICON_SYS + "  n_ctx           : " + std::to_string(cfg.n_ctx_));
  tui.append_line(ICON_SYS + "  n_batch         : " + std::to_string(cfg.n_batch_));
  tui.append_line(ICON_SYS + "  n_gpu_layers    : " + std::to_string(cfg.n_gpu_layers_));
  tui.append_line(ICON_SYS + "  n_threads       : " + std::to_string(cfg.n_threads_));
  tui.append_line(ICON_SYS + "  n_threads_batch : " + std::to_string(cfg.n_threads_batch_));
  tui.append_line(ICON_SYS + "  rope_scaling    : " + rope_str(static_cast<int>(cfg.rope_scaling_type_)));
  tui.append_line(ICON_SYS + "  rope_freq_scale : " + std::to_string(cfg.rope_freq_scale_));

  tui.append_line(ICON_SYS + "── Generation ─────────────────────────────────");
  tui.append_line(ICON_SYS + "  temperature     : " + std::to_string(cfg.temperature_));
  tui.append_line(ICON_SYS + "  top_p           : " + std::to_string(cfg.top_p_));
  tui.append_line(ICON_SYS + "  top_k           : " + std::to_string(cfg.top_k_));
  tui.append_line(ICON_SYS + "  min_p           : " + std::to_string(cfg.min_p_));
  tui.append_line(ICON_SYS + "  penalty_repeat  : " + std::to_string(cfg.penalty_repeat_));
  tui.append_line(ICON_SYS + "  penalty_freq    : " + std::to_string(cfg.penalty_freq_));
  tui.append_line(ICON_SYS + "  penalty_present : " + std::to_string(cfg.penalty_present_));
  tui.append_line(ICON_SYS + "  penalty_last_n  : " + std::to_string(cfg.penalty_last_n_));
  tui.append_line(ICON_SYS + "  offload_kqv     : " + bool_str(cfg.offload_kqv_));

  tui.append_line(ICON_SYS + "── MTP (speculative) ──────────────────────────");
  tui.append_line(ICON_SYS + "  mtp_enabled     : " + bool_str(cfg.mtp_enabled_));
  tui.append_line(ICON_SYS + "  mtp_adaptive    : " + bool_str(cfg.mtp_adaptive_));
  tui.append_line(ICON_SYS + "  mtp_n_max       : " + std::to_string(cfg.mtp_n_max_));
  tui.append_line(ICON_SYS + "  mtp_n_min       : " + std::to_string(cfg.mtp_n_min_));
  tui.append_line(ICON_SYS + "  mtp_p_min       : " + std::to_string(cfg.mtp_p_min_));
  tui.append_line(ICON_SYS + "  mtp_pos_thresh  : " + std::to_string(cfg.mtp_pos_threshold_));

  tui.append_line(ICON_SYS + "── Suffix (n-gram drafting) ───────────────────");
  tui.append_line(ICON_SYS + "  suffix_enabled  : " + bool_str(cfg.suffix_enabled_));
  tui.append_line(ICON_SYS + "  suffix_ngram    : " + std::to_string(cfg.suffix_ngram_));
  tui.append_line(ICON_SYS + "  suffix_max_draft: " + std::to_string(cfg.suffix_max_draft_));

  tui.append_line(ICON_SYS + "── RAG ────────────────────────────────────────");
  tui.append_line(ICON_SYS + "  rag_top_k       : " + std::to_string(cfg.rag_top_k_));

  tui.append_line(ICON_SYS + "── Audio ──────────────────────────────────────");
  tui.append_line(ICON_SYS + "  audio_enabled   : " + bool_str(cfg.audio_enabled_));
  tui.append_line(ICON_SYS + "  audio_volume    : " + std::to_string(cfg.audio_volume_));
  tui.append_line(ICON_SYS + "  audio_tool_tick : " + bool_str(cfg.audio_tool_tick_));
  tui.append_line(ICON_SYS + "  audio_user_prompt: " + bool_str(cfg.audio_user_prompt_));

  tui.append_line(ICON_SYS + "── Logging ────────────────────────────────────");
  tui.append_line(ICON_SYS + "  log_level       : " + cfg.log_level_str_);
  tui.append_line(ICON_SYS + "  log_file        : " + (cfg.log_file_.empty() ? "(none)" : cfg.log_file_));
  tui.append_line(ICON_SYS + "  log_filter      : " + (cfg.log_filter_.empty() ? "(none)" : cfg.log_filter_));
  tui.append_line(ICON_SYS + "  log_erase       : " + bool_str(cfg.log_erase_));

  tui.append_line(ICON_SYS + "── Validation ─────────────────────────────────");
  tui.append_line(ICON_SYS + "  disable_write_validate : " + bool_str(cfg.disable_write_validate_));
  tui.append_line(ICON_SYS + "  disable_patch_validate : " + bool_str(cfg.disable_patch_validate_));
  tui.append_line(ICON_SYS + "  write_size_warn          : " + std::to_string(cfg.val_write_size_warn_));

  tui.append_line(ICON_SYS + "── MCP ────────────────────────────────────────");
  tui.append_line(ICON_SYS + "  mcp_host        : " + (cfg.mcp_host_.empty() ? "(none)" : cfg.mcp_host_));
  tui.append_line(ICON_SYS + "  mcp_port        : " + std::to_string(cfg.mcp_port_));
  if (!cfg.mcp_filter_.empty()) {
    tui.append_line(ICON_SYS + "  mcp_filter      :");
    for (const auto &f : cfg.mcp_filter_) {
      tui.append_line(ICON_SYS + "    - " + f);
    }
  }

  tui.append_line(ICON_SYS + "── Paths ──────────────────────────────────────");
  tui.append_line(ICON_SYS + "  sandbox         : " + cfg.sandbox_);
  tui.append_line(ICON_SYS + "  backups         : " + (cfg.enabled_backups_ ? "enabled" : "disabled"));
  tui.append_line(ICON_SYS + "  config          : " + cfg.config_);
  tui.append_line(ICON_SYS + "  saved to        : " + cfg.settings_path());

  tui.append_line(ICON_SYS + "── Access ─────────────────────────────────────");
  if (!cfg.extra_dirs_.empty()) {
    tui.append_line(ICON_SYS + "  extra_dirs      :");
    for (const auto &d : cfg.extra_dirs_) {
      tui.append_line(ICON_SYS + "    - " + d);
    }
  } else {
    tui.append_line(ICON_SYS + "  extra_dirs      : (none)");
  }
  if (!cfg.run_allowed_.empty()) {
    std::string list;
    for (size_t i = 0; i < cfg.run_allowed_.size(); i++) {
      if (i > 0) { list += ", "; }
      list += cfg.run_allowed_[i];
    }
    tui.append_line(ICON_SYS + "  run_allowed     : " + list);
  } else {
    tui.append_line(ICON_SYS + "  run_allowed     : (none)");
  }
  if (!cfg.knowledge_files_.empty()) {
    tui.append_line(ICON_SYS + "  knowledge_files :");
    for (const auto &k : cfg.knowledge_files_) {
      tui.append_line(ICON_SYS + "    - " + k);
    }
  } else {
    tui.append_line(ICON_SYS + "  knowledge_files : (none)");
  }
  if (!cfg.user_run_allowed_.empty()) {
    tui.append_line(ICON_SYS + "  user_approved   :");
    for (const auto &u : cfg.run_allowed_) {
      if (cfg.user_run_allowed_.contains(u)) {
        tui.append_line(ICON_SYS + "    - " + u);
      }
    }
  }

  tui.append_line(ICON_SYS + "── Behavior ───────────────────────────────────");
  tui.append_line(ICON_SYS + "  thinking        : " + bool_str(cfg.thinking_));
  tui.append_line(ICON_SYS + "  permission_prompt: " + bool_str(cfg.permission_prompt_));
  tui.append_line(ICON_SYS + "  web_port        : " + std::to_string(cfg.web_port_));
  tui.redraw_all();
}

void no_model(Tui &tui) {
  tui.append_line(ICON_SYS + "No model specified.  Use /model to open the file picker,");
  tui.append_line(ICON_SYS + "or /model <path> to load directly.");
  tui.append_line(ICON_SYS + "Example: /model ~/models/qwen2.5-7b-q4_k_m.gguf");
  tui.redraw_all();
}
  
//
// print command line usage
//
void usage() {
  std::puts("Usage: hali [options]\n"
            "\n"
            "Options:\n"
            "-m, --model <path>        path to a GGUF model\n"
            "-e, --embed <path>        path to an embedding model (for RAG)\n"
            "-g, --gpu-layers <n>      number of layers to offload to GPU\n"
            "-s, --skill <name>        load a skill file into the system prompt (repeatable)\n"
            "--mcp                     enable the MCP client\n"
            "--mcp-filter <name>       only expose this MCP tool (repeatable)\n"
            "--mcp-test                print the resolved MCP system context and exit\n"
            "-c, --config <path>       load settings from a specific JSON file\n"
            "-l, --log <path>          write logs to a file\n"
            "-t, --think               disable model \"thinking\" output\n"
            "-n, --prompt-permission   require explicit confirmation before destructive tool calls\n"
            "-p, --web-port <port>     enable web development mode with live-reload on the given port\n"
            "-b, --enabled-backups     enable file backups to .hali/backups prior to writes\n"
            "-h, --help                show this help\n"
            "\n"
            "Settings are persisted to .hali/settings.json under the sandbox.\n"
            "\n"
            "Slash commands inside hali:\n");
  for (const auto &cmd : slash_commands()) {
    std::string line = "  ";
    line += cmd.name;
    if (cmd.usage[0] != '\0') {
      line += ' ';
      line += cmd.usage;
    }
    while (line.size() < 26) { line += ' '; }
    line += cmd.description;
    line += '\n';
    std::puts(line.c_str());
  }
}
  
//
// Welcome banner  — colourful multi-line ASCII logo
//
void welcome(Tui &tui, const std::string &sandbox) {
  tui.append_line("[logo_5]  ───── W E L C O M E  T O  H · A · L · I ────────");  
  tui.append_line("[logo_0]     ▄▅▆░██▄▅   ▄▅▆░██▄▅   ▄▅▆█░█░█▆▅▄   ▅▆░██▄▅  ");
  tui.append_line("[logo_1]        ▄▅█ ◉ █▄ ▄▅█ ◉ █▄  ▄▅█ ◕ ◕ █▄   ▄▅█ ◉ █▄  ");
  tui.append_line("[logo_2]      ▄▅░██░██▄▅ ▄▅░██░██▄▅ ▄▅░██░██▄▅ ▄▅░██░██▄▅ ");
  tui.append_line("[logo_3]      ▀▄█░█▓▄   ▀▄█░█▓▄      ▀▄█ ░█▓▄    ▓▓▓      ");
  tui.append_line("[logo_5]  ─────────── agentic LLM shell v1.0 ──────────────");
  tui.append_line(ICON_SYS + " Sandbox : " + sandbox);
  tui.append_line(ICON_SYS + " /help for commands, exit to quit");
  tui.append_line("");
  tui.redraw_all();
}

}
