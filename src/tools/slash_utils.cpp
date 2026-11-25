// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "slash_utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/agent.h"
#include "core/agent_utils.h"
#include "ui/tui.h"
#include "ui/icon.h"
#include "knowledge.h"
#include "string_utils.h"
#include "audit.h"

namespace fs = std::filesystem;

//
// ── Sampling profiles ────────────────────────────────────────────────────
//

static const SamplingProfile PROFILES[] = {
  { "General",     0.60f, 0.95f, 20, 0.00f, 1.00f, 0.00f, 0.00f, 256 },
  { "Programming", 0.20f, 0.90f, 10, 0.05f, 1.10f, 0.30f, 0.00f, 256 },
  { "Creative",    0.90f, 0.98f, 40, 0.00f, 0.90f, 0.00f, 0.00f, 128 },
  { "Concise",     0.30f, 0.85f, 10, 0.10f, 1.20f, 0.50f, 0.10f, 256 },
};

void apply_profile(HaliConfig &cfg, const SamplingProfile &p) {
  cfg.temperature_     = p.temp;
  cfg.top_p_           = p.top_p;
  cfg.top_k_           = p.top_k;
  cfg.min_p_           = p.min_p;
  cfg.penalty_repeat_  = p.pen_repeat;
  cfg.penalty_freq_    = p.pen_freq;
  cfg.penalty_present_ = p.pen_present;
  cfg.penalty_last_n_  = p.pen_last_n;
}

//
// Smart defaults: inspect VRAM + layers after model load and auto-tune.
// Changes that require a reload are saved but noted as "next load".
//
void apply_smart_defaults(HaliConfig &cfg, const ModelMemoryInfo &mem, Tui &tui) {
  std::string changes;

  // If VRAM has headroom and layers are on CPU, push all layers to GPU.
  if (mem.n_layers_cpu > 0 && mem.vram_percent < 75.0f) {
    cfg.n_gpu_layers_ = mem.n_layers_total;
    changes += "n_gpu_layers → " + std::to_string(mem.n_layers_total) + " (VRAM headroom)";
  }

  // If VRAM is under pressure, tighten settings.
  if (mem.vram_percent > 90.0f) {
    if (cfg.kv_preset_ != KVCachePreset::Compact) {
      cfg.kv_preset_ = KVCachePreset::Compact;
      changes += std::string(changes.empty() ? "" : "; ") + "kv_preset → compact (VRAM pressure)";
    }
    if (cfg.n_ctx_ > 32768) {
      cfg.n_ctx_ = 32768;
      changes += std::string(changes.empty() ? "" : "; ") + "n_ctx → 32768 (VRAM pressure)";
    }
  }

  if (!changes.empty()) {
    tui.append_line(ICON_SYS + "Smart defaults: " + changes);
    tui.append_line(ICON_SYS + "  (n_gpu_layers / n_ctx / kv_preset take effect on next /model load)");
  }
}

//
// Prompt the user to pick a sampling profile after model load.
//
void prompt_profile(HaliConfig &cfg, Agent &agent, Tui &tui) {
  std::vector<std::string> names;
  for (const auto &p : PROFILES) {
    names.emplace_back(p.name);
  }
  int idx = tui.list_picker(names, "Sampling Profile");
  if (idx < 0) {
    return;  // user cancelled – keep current settings
  }
  apply_profile(cfg, PROFILES[idx]);
  if (agent.model_loaded()) {
    agent.apply_generation_params();
  }
  cfg.save_settings();
  tui.append_line(ICON_SYS + "Profile applied: " + PROFILES[idx].name);
}

//
// Parse "FP <file> <line> <severity> <message>" lines from model output.
//
std::vector<FalsePositive> parse_false_positives(const std::string &text) {
  std::vector<FalsePositive> fps;
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    auto pos = line.find_first_not_of(" \t");
    if (pos == std::string::npos) continue;
    line = line.substr(pos);
    if (line.rfind("FP ", 0) != 0) continue;

    std::istringstream ls(line.substr(3));
    FalsePositive fp;
    if (!(ls >> fp.file)) continue;
    int ln = 0;
    if (!(ls >> ln)) continue;
    fp.line = ln;
    if (!(ls >> fp.severity)) continue;
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
// ── Pick a source from a list (or use provided arg) ───────────────────────
//
std::string pick_source(Tui &tui, const std::string &arg,
                        const std::vector<std::string> &labels,
                        const std::vector<std::string> &ids,
                        const std::string &prompt,
                        const std::string &empty_msg) {
  if (!arg.empty()) {
    return arg;
  }
  if (labels.empty()) {
    tui.append_line(ICON_SYS + empty_msg);
    tui.redraw_all();
    return "";
  }
  int idx = tui.list_picker(labels, prompt);
  if (idx < 0) {
    tui.redraw_all();
    return "";
  }
  return ids[idx];
}

//
// Discover .zim files from sandbox, sandbox/kiwix/, and extra_dirs.
//
std::vector<std::string> discover_zim_files(const HaliConfig &cfg) {
  std::vector<std::string> results;
  std::set<std::string> seen;

  auto scan_dir = [&](const std::string &dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
      return;
    }
    for (auto &entry : fs::directory_iterator(dir, ec)) {
      if (entry.is_regular_file() && entry.path().extension() == ".zim") {
        std::string p = entry.path().string();
        if (seen.insert(p).second) {
          results.push_back(p);
        }
      }
    }
  };

  scan_dir(cfg.sandbox_);
  scan_dir(cfg.sandbox_ + "/kiwix");
  for (const auto &d : cfg.extra_dirs_) {
    scan_dir(d);
  }

  std::sort(results.begin(), results.end());
  return results;
}

//
// ── Index helpers ──────────────────────────────────────────────────────────
//

void index_zim_file(const std::string &path, Agent &agent, Tui &tui,
                    const std::function<void(const std::string &)> &progress,
                    KnowledgeManager &km) {
  if (!km.embed_model()) {
    tui.append_line(ICON_ERR + "No embedding model loaded. Use /embed first.");
    tui.redraw_all();
    return;
  }
  if (!KnowledgeManager::zimdump_available()) {
    tui.append_line(ICON_ERR + "zimdump not found.");
    tui.append_line(ICON_ERR + KnowledgeManager::zimdump_install_hint());
    tui.redraw_all();
    return;
  }
  tui.append_line(ICON_SYS + "Indexing " + path + "…");
  tui.redraw_all();
  std::string build_err;
  if (km.build_from_path(path, *km.embed_model(), tui, progress, &build_err)) {
    fs::path zp(path);
    std::string id = zp.stem().string();
    if (km.load(id)) {
      tui.append_line(ICON_SYS + "Indexed and loaded: " + id);
    } else {
      tui.append_line(ICON_ERR + "Build succeeded but load failed: " + id);
    }
  } else {
    tui.append_line(ICON_ERR + "Index build failed: " + build_err);
  }
  tui.redraw_all();
}

void index_help(Tui &tui) {
  tui.append_line(ICON_SYS + "/index [name|remove <name>|create|load|unload <name>|list|help]");
  tui.append_line(ICON_SYS + "  (no arg)       show this help");
  tui.append_line(ICON_SYS + "  <name>         load a source by name");
  tui.append_line(ICON_SYS + "  <path>         index a .zim file from disk");
  tui.append_line(ICON_SYS + "  create         create a new index from a .zim file");
  tui.append_line(ICON_SYS + "  load           load a previously built index");
  tui.append_line(ICON_SYS + "  list           show all indexed sources with metadata");
  tui.append_line(ICON_SYS + "  remove <name>  remove an indexed source");
  tui.append_line(ICON_SYS + "  unload <name>  unload a loaded source");
  tui.append_line(ICON_SYS + "  help           show this help");
  tui.redraw_all();
}

void index_remove(KnowledgeManager &km, std::string &arg, Tui &tui) {
  arg = pick_source(tui, arg, km.indexed_labels(), km.indexed_ids(),
                    "Select a source to remove", "No sources to remove.");
  if (arg.empty()) {
    return;
  }
  if (km.remove(arg)) {
    tui.append_line(ICON_SYS + "Removed: " + arg);
  } else {
    tui.append_line(ICON_ERR + "Not found: " + arg);
  }
  tui.redraw_all();
}

void index_create(const HaliConfig &cfg, Agent &agent, Tui &tui) {
  auto zim_files = discover_zim_files(cfg);

  if (zim_files.empty()) {
    tui.append_line(ICON_SYS + "No .zim files found to index.");
    tui.redraw_all();
    return;
  }

  int idx = tui.list_picker(zim_files, "Create index from .zim file");
  if (idx < 0) {
    tui.redraw_all();
    return;
  }

  std::string zim_path = zim_files[idx];

  // Ask for optional topic filter
  std::string topics = tui.text_editor("Topics (optional)", "");

  // Hand off to agent
  std::string prompt;
  if (!topics.empty()) {
    prompt = "Please index the ZIM file at " + zim_path + ". "
      "The user's topic request is: \"" + topics + "\". "
      "Based on this, build a list of related topic "
      "keywords (e.g. if the user says \"cats\", include cat, cats, "
      "tigers, lions, felines, domestic cat, wild cat, etc.). "
      "Write the resulting keywords to .hali/topics.txt "
      "(one keyword per line, at most 20 keywords). "
      "If TOOL:RAGINFO shows this index is already loaded, then "
      "use TOOL:RAG to confirm the words are not already indexed. "
      "Then call TOOL:RAGINDEX " + zim_path + ".";
  } else {
    prompt = "Please index the ZIM file at " + zim_path +
      " by calling TOOL:RAGINDEX " + zim_path;
  }

  tui.append_line(ICON_SYS + "Submitting to agent…");
  tui.redraw_all();
  agent.run_turn(prompt);
  tui.redraw_all();
}

void index_load(KnowledgeManager &km, HaliConfig &cfg, Tui &tui) {
  auto labels = km.indexed_labels();
  auto ids = km.indexed_ids();

  if (ids.empty()) {
    tui.append_line(ICON_ERR + "No indexes found. Use /index create to build one.");
    tui.redraw_all();
    return;
  }

  int idx = tui.list_picker(labels, "Load index");
  if (idx < 0) {
    tui.redraw_all();
    return;
  }

  std::string id = ids[idx];
  if (km.load(id)) {
    tui.append_line(ICON_SYS + "Loaded: " + id);
    // Track active .rag files in config
    if (std::find(cfg.rag_files_.begin(), cfg.rag_files_.end(), id)
        == cfg.rag_files_.end()) {
      cfg.rag_files_.emplace_back(id);
      cfg.save_settings();
    }
  } else {
    tui.append_line(ICON_ERR + "Failed to load: " + id);
  }
  tui.redraw_all();
}

void index_unload(KnowledgeManager &km, HaliConfig &cfg, std::string &arg, Tui &tui) {
  arg = pick_source(tui, arg, km.loaded_labels(), km.loaded_ids(),
                    "Select a source to unload", "No sources currently loaded.");
  if (arg.empty()) {
    return;
  }
  if (km.unload(arg)) {
    tui.append_line(ICON_SYS + "Unloaded: " + arg);
    // Remove from active .rag files in config
    cfg.rag_files_.erase(
        std::remove(cfg.rag_files_.begin(), cfg.rag_files_.end(), arg),
        cfg.rag_files_.end());
    cfg.save_settings();
  } else {
    tui.append_line(ICON_ERR + "Not loaded: " + arg);
  }
  tui.redraw_all();
}

//
// /index list — show all indexed sources with metadata
//
void index_list(KnowledgeManager &km, Tui &tui) {
  auto labels = km.indexed_labels();
  auto ids = km.indexed_ids();

  if (ids.empty()) {
    tui.append_line(ICON_SYS + "No indexes available. Use /index create to build one.");
    tui.redraw_all();
    return;
  }

  tui.append_line(ICON_SYS + "Indexed sources:");
  tui.append_line(ICON_SYS + "─────────────────────────────────────────────────────────");

  for (size_t i = 0; i < ids.size(); i++) {
    const std::string &id = ids[i];
    const IndexedSource *src = km.find(id);
    bool loaded = km.library().get(id) != nullptr;

    std::string status = loaded ? "[loaded]" : "[not loaded]";
    tui.append_line(ICON_SYS + "  " + id + "  " + status);
    if (src) {
      if (!src->description.empty()) {
        tui.append_line(ICON_SYS + "    " + src->description);
      }
      if (!src->topics.empty()) {
        tui.append_line(ICON_SYS + "    topics: " + src->topics);
      }
      tui.append_line(ICON_SYS + std::format("    chunks: {}  indexed: {}",
                                             src->chunks, src->indexed_at));
    }
  }
  tui.append_line(ICON_SYS + "─────────────────────────────────────────────────────────");
  tui.append_line(ICON_SYS + std::format("Total: {} indexed, {} loaded",
                                         ids.size(),
                                         km.library().size()));
  tui.redraw_all();
}