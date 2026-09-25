// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "ui_text.h"

namespace ui {

void help(Tui &tui) {
  tui.append_line(ICON_SYS + "Commands:");
  tui.append_line(ICON_SYS + "  /model  [path]           load a GGUF model (picker if no path)");
  tui.append_line(ICON_SYS + "  /embed  [path]           load an embedding model (picker if no path)");
  tui.append_line(ICON_SYS + "  /rag    [path]           index file or directory (picker if no path)");
  tui.append_line(ICON_SYS + "  /memory                  KV / VRAM / layer stats");
  tui.append_line(ICON_SYS + "  /clear                   reset conversation");
  tui.append_line(ICON_SYS + "  /settings                show current settings");
  tui.append_line(ICON_SYS + "  /theme                   toggle the theme (F12)");
  tui.append_line(ICON_SYS + "  /set    <key> <value>    change a setting live");
  tui.append_line(ICON_SYS + "  /help                    this message");
  tui.append_line(ICON_SYS + "  exit / quit              exit Hali");
  tui.append_line(ICON_SYS + "Settable keys (via /set):");
  tui.append_line(ICON_SYS + "  temperature  top_p  top_k  min_p  penalty_repeat");
  tui.append_line(ICON_SYS + "  penalty_last_n  rag_top_k  n_gpu_layers");
  tui.append_line(ICON_SYS + "  run_allowed  (comma-separated list, e.g. python3,make)");
  tui.redraw_all();
}

void settings(Tui &tui, HaliConfig &cfg) {
  tui.append_line(ICON_SYS + "Current settings:");
  tui.append_line(ICON_SYS + "  model_path      : " + cfg.model_path_);
  tui.append_line(ICON_SYS + "  embed_path      : " + cfg.embed_path_);
  tui.append_line(ICON_SYS + "  sandbox         : " + cfg.sandbox_);
  tui.append_line(ICON_SYS + "  n_ctx           : " + std::to_string(cfg.n_ctx_));
  tui.append_line(ICON_SYS + "  n_gpu_layers    : " + std::to_string(cfg.n_gpu_layers_));
  tui.append_line(ICON_SYS + "  temperature     : " + std::to_string(cfg.temperature_));
  tui.append_line(ICON_SYS + "  top_p           : " + std::to_string(cfg.top_p_));
  tui.append_line(ICON_SYS + "  top_k           : " + std::to_string(cfg.top_k_));
  tui.append_line(ICON_SYS + "  penalty_repeat  : " + std::to_string(cfg.penalty_repeat_));
  tui.append_line(ICON_SYS + "  penalty_freq    : " + std::to_string(cfg.penalty_freq_));
  tui.append_line(ICON_SYS + "  penalty_present : " + std::to_string(cfg.penalty_present_));
  tui.append_line(ICON_SYS + "  penalty_last_n  : " + std::to_string(cfg.penalty_last_n_));
  tui.append_line(ICON_SYS + "  offload_kqv     : " + std::format("{}", cfg.offload_kqv_));
  tui.append_line(ICON_SYS + "  kv_cache_preset : " + cfg.kv_preset_to_string());
  tui.append_line(ICON_SYS + "  rag_top_k       : " + std::to_string(cfg.rag_top_k_));
  tui.append_line(ICON_SYS + "  saved to        : " + cfg.settings_path());
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
  std::puts("Usage: hali [options] [project_dir]\n"
            "\n"
            "Options:\n"
            "  -m, --model  <path>      GGUF model to load on startup\n"
            "  -e, --embed  <path>      embedding model for RAG\n"
            "  -g, --gpu-layers <n>     GPU layers to offload (default: 32)\n"
            "  -l, --log <n>            enabled logging at verbosity level [1-4]\n"
            "  -h, --help               show this help\n"
            "\n"
            "project_dir defaults to the current working directory.\n"
            "Settings are persisted to ~/.config/hali/settings.json.\n"
            "\n"
            "Slash commands inside hali:\n"
            "  /model  [path]           load / hot-reload a GGUF (picker if no path)\n"
            "  /embed  [path]           load an embedding model  (picker if no path)\n"
            "  /rag    [path]           index file or directory  (picker if no path)\n"
            "  /memory                  KV / VRAM / layer stats\n"
            "  /settings                show current settings\n"
            "  /clear                   reset conversation\n"
            "  /help                    list commands\n"
            );
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
