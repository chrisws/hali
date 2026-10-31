// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "slash_commands.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <system_error>

#include "config.h"
#include "agent.h"
#include "tui.h"
#include "icon.h"
#include "webview.h"
#include "audio.h"
#include "ui_text.h"
#include "audit.h"
#include "build.h"
#include "skills_generated.h"
#include "security_audit.h"
#include "knowledge.h"

namespace fs = std::filesystem;

//
// ── Slash command support functions ──────────────────────────────────────
//

//
// ── Sampling profiles ────────────────────────────────────────────────────
//
struct SamplingProfile {
  const char *name;
  float temp;
  float top_p;
  int   top_k;
  float min_p;
  float pen_repeat;
  float pen_freq;
  float pen_present;
  int   pen_last_n;
};

static const SamplingProfile PROFILES[] = {
  { "General",     0.60f, 0.95f, 20, 0.00f, 1.00f, 0.00f, 0.00f, 256 },
  { "Programming", 0.20f, 0.90f, 10, 0.05f, 1.10f, 0.30f, 0.00f, 256 },
  { "Creative",    0.90f, 0.98f, 40, 0.00f, 0.90f, 0.00f, 0.00f, 128 },
  { "Concise",     0.30f, 0.85f, 10, 0.10f, 1.20f, 0.50f, 0.10f, 256 },
};

static void apply_profile(HaliConfig &cfg, const SamplingProfile &p) {
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
static void apply_smart_defaults(HaliConfig &cfg, const LlamaMemoryInfo &mem, Tui &tui) {
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
static void prompt_profile(HaliConfig &cfg, Agent &agent, Tui &tui) {
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
    // Apply smart defaults based on VRAM / layer info.
    apply_smart_defaults(cfg, agent.last_mem_info(), tui);
    // Prompt for a sampling profile.
    prompt_profile(cfg, agent, tui);
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
static void slash_audio(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  if (rest.empty()) {
    bool new_state = !audio::is_enabled();
    audio::set_enabled(new_state);
    tui.append_line(ICON_SYS + std::string("Audio ") + (new_state ? "enabled." : "disabled."));
  } else if (rest == "test") {
    audio::play_test();
    tui.append_line(ICON_SYS + "Audio test played (polyphonic).");

    // Save the test waveform to hali.wav for inspection
    static const audio::NoteEvent test_events[] = {
      { {261.63, 0.4}, {329.63, 0.4}, {392.00, 0.4} },
      { {349.23, 0.4}, {440.00, 0.4}, {523.25, 0.4} },
      { {392.00, 0.4}, {493.88, 0.4}, {587.33, 0.4} },
      { {523.25, 0.6}, {659.25, 0.6}, {783.99, 0.6}, {1046.50, 0.6} },
    };
    std::string hali_dir = cfg.sandbox_ + "/.hali";
    std::error_code ec;
    fs::create_directories(hali_dir, ec);
    std::string wav_path = hali_dir + "/hali.wav";
    if (audio::save_wav(std::vector<audio::NoteEvent>(std::begin(test_events), std::end(test_events)), wav_path)) {
      tui.append_line(ICON_SYS + "Waveform saved to " + wav_path);
      agent.notify("Inspect " + wav_path + " using TOOL:HEX to verify the waveform is a valid WAV file with the expected C-F-G-C chord progression.");
    } else {
      tui.append_line(ICON_ERR + "Failed to save " + wav_path);
    }
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
    cfg.user_run_allowed_.save(RunAllowed::default_path(cfg.sandbox_));
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
  std::string path = rest.empty() ? cfg.sandbox_ + "/.hali/SESSION.md" : rest;
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
// /audit – scan the codebase for model-hostility issues
//
static void slash_audit(const std::string &rest, HaliConfig &cfg, Tui &tui, Agent &agent) {
  std::string root = rest.empty() ? cfg.sandbox_ : rest;
  std::string fp_path = cfg.sandbox_ + "/.hali/audit_feedback.json";

  auto fps = load_false_positives(fp_path);

  tui.append_line(ICON_SYS + "Scanning " + root + " …");
  tui.redraw_all();

  AuditResult r = audit_cpp(root);

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

  if (tui.confirm_dialog("Allow Hali to resolve these issues?")) {
    std::string plan_path = cfg.sandbox_ + "/.hali/plans/audit_report.md";
    audit_write_report(r, plan_path);
    tui.append_line(ICON_SYS + "Report saved to " + plan_path);

    std::string prompt = audit_format_prompt(r, plan_path);
    tui.append_line(ICON_SYS + "Handing audit report to model…");
    tui.redraw_all();
    agent.run_turn(prompt);

    auto new_fps = parse_false_positives(agent.last_response());
    if (!new_fps.empty()) {
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
// /security-audit – mark the sandbox as security-audited
//
static void slash_security_audit(HaliConfig &cfg, Tui &tui) {
  if (sec_audit::is_audited(cfg.sandbox_)) {
    tui.append_line(ICON_SYS + "Sandbox is already marked as security-audited.");
    tui.append_line(ICON_SYS + sec_audit::status_message(cfg.sandbox_));
    tui.redraw_all();
    return;
  }

  if (!tui.confirm_dialog("Mark this sandbox as security-audited?\n\n"
                          "This should only be done after running the\n"
                          "security-audit skill and verifying no\n"
                          "prompt-injection or malicious content exists.")) {
    tui.append_line(ICON_SYS + "Cancelled.");
    tui.redraw_all();
    return;
  }

  if (sec_audit::mark_audited(cfg.sandbox_)) {
    tui.append_line(ICON_SYS + "Sandbox marked as security-audited.");
    tui.append_line(ICON_SYS + sec_audit::status_message(cfg.sandbox_));
  } else {
    tui.append_line(ICON_ERR + "Failed to write audit marker.");
  }
  tui.redraw_all();
}

//
// /skill [name] [kickoff] – load a skill into the conversation
//
static void slash_skill(const std::string &rest_in, HaliConfig &cfg,
                        Agent &agent, Tui &tui) {
  std::string rest = rest_in;
  if (rest.empty()) {
    auto items = skill_display_names();
    int idx = tui.list_picker(items, "Load Skill");
    if (idx < 0) {
      tui.append_line(ICON_SYS + "/skill cancelled.");
      tui.redraw_all();
      return;
    }
    rest = SKILLS[idx].name;
  }

  auto sp = rest.find(' ');
  std::string name = (sp == std::string::npos) ? rest : rest.substr(0, sp);
  std::string kickoff = (sp == std::string::npos) ? "" : rest.substr(sp + 1);

  const SkillEntry *entry = skill_lookup(name);
  if (!entry) {
    tui.append_line(ICON_ERR + "Unknown skill: " + name);
    tui.append_line(ICON_SYS + "Type /skill (no arg) to see available skills.");
    tui.redraw_all();
    return;
  }

  agent.run_turn_with_skill(entry->text, kickoff);
  tui.append_line(ICON_SYS + "Loaded skill: " + name);
  tui.redraw_all();
}

//
// /lint – list source files and run clang-tidy lint
//
static void slash_lint(const std::string &rest, HaliConfig &cfg, Tui &tui) {
  const std::string sandbox = cfg.sandbox_;
  const std::string src_dir = sandbox + "/src";

  // Collect source files
  std::vector<std::string> files;
  if (fs::exists(src_dir) && fs::is_directory(src_dir)) {
    for (const auto &entry : fs::recursive_directory_iterator(src_dir)) {
      if (entry.is_regular_file()) {
        auto ext = entry.path().extension();
        if (ext == ".cpp" || ext == ".cc" || ext == ".c") {
          files.push_back(entry.path().string());
        }
      }
    }
  }

  if (files.empty()) {
    tui.append_line(ICON_SYS + "No C/C++ source files found in src/.");
    tui.redraw_all();
    return;
  }

  // Show file list in table format
  tui.append_line(ICON_SYS + "Source files to lint (" + std::to_string(files.size()) + "):");
  std::string table_rows;
  int i = 0;
  for (const auto &f : files) {
    auto slash = f.rfind('/');
    std::string name = (slash != std::string::npos) ? f.substr(slash + 1) : f;
    table_rows += std::to_string(++i) + "|" + name + "\n";
  }
  tui.append_line("TOOL:TABLE " + std::to_string(files.size() + 1) + "x2 #|File\n" + table_rows);
  tui.redraw_all();

  // Run lint
  auto status = [&tui](const std::string &msg) {
    tui.show_tool(msg);
  };
  auto result = build_run(sandbox, "lint", status);
  tui.append_line(ICON_TOOL + result.output_);
  tui.redraw_all();
}

//
// /plan [text] – open a multiline text editor for a multi-line plan.
// On submit: saves to .hali/plans/plan_<timestamp>.md, then hands to the agent.
//
static void slash_plan(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  std::string text = tui.text_editor("Plan", rest);
  if (text.empty()) {
    tui.append_line(ICON_SYS + "/plan cancelled.");
    tui.redraw_all();
    return;
  }

  // Build a timestamped filename: plan_YYYYMMDD_HHMMSS.md
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&tt, &tm);
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);
  std::string filename = std::string("plan_") + ts + ".md";

  // Ensure plans/ directory exists.
  std::string plans_dir = cfg.sandbox_ + "/.hali/plans";
  std::error_code ec;
  fs::create_directories(plans_dir, ec);

  // Write the plan to the plans directory.
  std::string path = plans_dir + "/" + filename;
  {
    std::ofstream ofs(path);
    if (!ofs) {
      tui.append_line(ICON_ERR + "Failed to write " + path);
      tui.redraw_all();
      return;
    }
    ofs << text;
  }

  tui.append_line(ICON_SYS + "Saved plan to " + path);
  tui.redraw_all();

  // Submit to the agent with a prompt referencing the saved file.
  std::string prompt = "I have written a plan saved to " + filename +
                       " in the .hali/plans/ directory. Please review it and let me know "
                       "if you have any questions, suggestions, or if you are ready "
                       "to proceed. Here is the content:\n\n" + text;
  tui.append_line(ICON_SYS + "Submitting to agent…");
  tui.redraw_all();
  agent.run_turn(prompt);
  tui.redraw_all();
}

//
// /tool [TOOL:NAME [arg1 [arg2]]] – invoke a tool or list available tools
//
static void slash_tool(const std::string &rest, Agent &agent, Tui &tui) {
  if (rest.empty()) {
    auto names = agent.tool_names();
    tui.append_line(ICON_SYS + "Available tools:");
    for (const auto &n : names) {
      tui.append_line(ICON_SYS + "  " + n);
    }
    tui.redraw_all();
    return;
  }

  // Parse: TOOL:NAME arg1 arg2
  auto sp = rest.find(' ');
  std::string name = (sp == std::string::npos) ? rest : rest.substr(0, sp);
  std::string a1, a2;
  if (sp != std::string::npos) {
    std::string remainder = rest.substr(sp + 1);
    remainder.erase(0, remainder.find_first_not_of(" \t"));
    auto sp2 = remainder.find(' ');
    if (sp2 != std::string::npos) {
      a1 = remainder.substr(0, sp2);
      a2 = remainder.substr(sp2 + 1);
      a2.erase(0, a2.find_first_not_of(" \t"));
    } else {
      a1 = remainder;
    }
  }

  std::string result = agent.invoke_tool_by_name(name, a1, a2);
  tui.append_line(ICON_TOOL + result);
  tui.redraw_all();
}

//
// /index [name|status|remove <name>|load <name>|unload <name>]
//
static void slash_index(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  static KnowledgeManager km;

  // Set up directories on first use
  static bool initialized = false;
  if (!initialized) {
    std::string hali_dir = cfg.sandbox_ + "/.hali";
    std::error_code ec;
    fs::create_directories(hali_dir + "/rag", ec);
    fs::create_directories(hali_dir + "/zim", ec);
    km.set_rag_dir(hali_dir + "/rag");
    km.set_zim_dir(hali_dir + "/zim");
    km.load_meta("share/kiwix-meta.json");
    initialized = true;
  }

  // Parse subcommand
  auto sp = rest.find(' ');
  std::string sub = (sp == std::string::npos) ? rest : rest.substr(0, sp);
  std::string arg;
  if (sp != std::string::npos) {
    arg = rest.substr(sp + 1);
    arg.erase(0, arg.find_first_not_of(" \t"));
  }

  auto progress = [&tui](const std::string &msg) {
    tui.append_line(ICON_SYS + msg);
    tui.redraw_all();
  };

  if (sub.empty()) {
    // Show picker with all sources + "status" option
    auto labels = km.source_labels();
    auto ids = km.source_ids();
    labels.push_back("status");
    int idx = tui.list_picker(labels, "Select a knowledge source to index");
    if (idx < 0) {
      tui.redraw_all();
      return;
    }
    if (idx == (int)ids.size()) {
      // "status" was selected
      tui.append_line(ICON_SYS + km.status());
      tui.append_line(ICON_SYS + "");
      tui.append_line(ICON_SYS + "Loaded:");
      tui.append_line(km.list_loaded());
      tui.redraw_all();
      return;
    }
    sub = ids[idx];
    // Fall through to download + build + load below
  }

  if (sub == "status") {
    tui.append_line(ICON_SYS + km.status());
    tui.append_line(ICON_SYS + "");
    tui.append_line(ICON_SYS + "Loaded:");
    tui.append_line(km.list_loaded());
    tui.redraw_all();
    return;
  }

  if (sub == "remove") {
    if (arg.empty()) {
      // Show picker with sources that have files
      auto ids = km.source_ids();
      std::vector<std::string> labels;
      std::vector<std::string> valid_ids;
      for (const auto &id : ids) {
        if (km.rag_exists(id) || km.zim_exists(id)) {
          const KnowledgeSource *s = km.find(id);
          labels.push_back(id + (s ? " \u2014 " + s->title : ""));
          valid_ids.push_back(id);
        }
      }
      if (labels.empty()) {
        tui.append_line(ICON_SYS + "No sources with files to remove.");
        tui.redraw_all();
        return;
      }
      int idx = tui.list_picker(labels, "Select a source to remove");
      if (idx < 0) {
        tui.redraw_all();
        return;
      }
      arg = valid_ids[idx];
    }
    if (km.remove(arg)) {
      tui.append_line(ICON_SYS + "Removed: " + arg);
    } else {
      tui.append_line(ICON_ERR + "Not found: " + arg);
    }
    tui.redraw_all();
    return;
  }

  if (sub == "load") {
    if (arg.empty()) {
      // Show picker with sources that have .rag files
      auto ids = km.source_ids();
      std::vector<std::string> labels;
      std::vector<std::string> valid_ids;
      for (const auto &id : ids) {
        if (km.rag_exists(id)) {
          const KnowledgeSource *s = km.find(id);
          labels.push_back(id + (s ? " \u2014 " + s->title : ""));
          valid_ids.push_back(id);
        }
      }
      if (labels.empty()) {
        tui.append_line(ICON_SYS + "No indexed sources available to load.");
        tui.redraw_all();
        return;
      }
      int idx = tui.list_picker(labels, "Select a source to load");
      if (idx < 0) {
        tui.redraw_all();
        return;
      }
      arg = valid_ids[idx];
    }
    if (km.load(arg)) {
      tui.append_line(ICON_SYS + "Loaded: " + arg);
    } else {
      tui.append_line(ICON_ERR + "Failed to load: " + arg);
    }
    tui.redraw_all();
    return;
  }

  if (sub == "unload") {
    if (arg.empty()) {
      // Show picker with currently loaded sources
      auto labels = km.loaded_labels();
      auto ids = km.loaded_ids();
      if (labels.empty()) {
        tui.append_line(ICON_SYS + "No sources currently loaded.");
        tui.redraw_all();
        return;
      }
      int idx = tui.list_picker(labels, "Select a source to unload");
      if (idx < 0) {
        tui.redraw_all();
        return;
      }
      arg = ids[idx];
    }
    if (km.unload(arg)) {
      tui.append_line(ICON_SYS + "Unloaded: " + arg);
    } else {
      tui.append_line(ICON_ERR + "Not loaded: " + arg);
    }
    tui.redraw_all();
    return;
  }

  // Default: download + build + load
  const KnowledgeSource *src = km.find(sub);
  if (!src) {
    tui.append_line(ICON_ERR + "Unknown source: " + sub);
    tui.append_line(ICON_SYS + "Type /index (no arg) to see available sources.");
    tui.redraw_all();
    return;
  }

  // Check if already built
  if (km.rag_exists(sub)) {
    if (km.load(sub)) {
      tui.append_line(ICON_SYS + "Loaded existing index: " + sub);
      tui.redraw_all();
      return;
    }
    tui.append_line(ICON_ERR + "Failed to load existing index: " + sub);
    tui.redraw_all();
    return;
  }

  // Check for embed model
  if (!agent.embed_model()) {
    tui.append_line(ICON_ERR + "No embedding model loaded. Use /embed first.");
    tui.redraw_all();
    return;
  }

  // Check zimdump availability
  if (!KnowledgeManager::zimdump_available()) {
    tui.append_line(ICON_ERR + "zimdump not found. Install kiwix-tools: sudo apt install kiwix-tools");
    tui.redraw_all();
    return;
  }

  // Download
  if (!km.zim_exists(sub)) {
    tui.append_line(ICON_SYS + "Downloading " + src->title + " (" +
                     std::to_string(src->size_mb) + " MB)…");
    tui.redraw_all();
    if (!km.download(sub, progress)) {
      tui.append_line(ICON_ERR + "Download failed: " + sub);
      tui.redraw_all();
      return;
    }
  }

  // Build RAG index
  tui.append_line(ICON_SYS + "Extracting and indexing " + src->title + "…");
  tui.redraw_all();
  if (!km.build(sub, *agent.embed_model(), progress)) {
    tui.append_line(ICON_ERR + "Index build failed: " + sub);
    tui.redraw_all();
    return;
  }

  // Load into session
  if (km.load(sub)) {
    tui.append_line(ICON_SYS + "Indexed and loaded: " + src->title);
  } else {
    tui.append_line(ICON_ERR + "Build succeeded but load failed: " + sub);
  }
  tui.redraw_all();
}

//
// /task — direct agent behavior toward a specific goal
//
static void slash_task(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  auto apply_mode = [&](const std::string &name_in) {
    std::string name = name_in;
    std::ranges::transform(name.begin(), name.end(), name.begin(), ::tolower);
    if (name == "default") {
      cfg.modes_.web = false;
      cfg.modes_.music = false;
      cfg.modes_.audio = false;
      cfg.modes_.knowledge = false;
      cfg.web_enabled_ = false;
      cfg.audio_multi_track_ = false;
      cfg.audio_tool_tick_ = false;
      cfg.audio_user_prompt_ = false;
      if (webview::is_running()) {
        webview::stop();
      }
      tui.append_line(ICON_SYS + "Task: default (all tasks off)");
    } else if (name == "web") {
      bool new_state = !cfg.modes_.web;
      cfg.modes_.web = new_state;
      cfg.web_enabled_ = new_state;
      if (new_state) {
        if (!webview::is_running()) {
          if (webview::start(cfg.sandbox_, cfg.web_port_)) {
            tui.append_line(ICON_SYS + "  web server started on port " + std::to_string(cfg.web_port_));
          } else {
            tui.append_line(ICON_ERR + "  failed to start web server on port " + std::to_string(cfg.web_port_));
          }
        }
      } else {
        if (webview::is_running()) {
          webview::stop();
        }
      }
      tui.append_line(ICON_SYS + "Task: web");
      tui.append_line(ICON_SYS + "  web.enabled  → " + (new_state ? "true" : "false"));
      if (new_state) {
        tui.append_line(ICON_SYS + "  web.port     → " + std::to_string(cfg.web_port_));
        tui.append_line(ICON_SYS + "  Browse:      http://localhost:" + std::to_string(cfg.web_port_) + "/ai");
      }
    } else if (name == "music") {
      bool new_state = !cfg.modes_.music;
      cfg.modes_.music = new_state;
      cfg.audio_multi_track_ = new_state;
      tui.append_line(ICON_SYS + "Task: music");
      tui.append_line(ICON_SYS + "  audio.enable_multi_track → " + (new_state ? "true" : "false"));
    } else if (name == "knowledge") {
      bool new_state = !cfg.modes_.knowledge;
      if (new_state && agent.embed_model() == nullptr) {
        tui.append_line(ICON_ERR + "Knowledge mode requires an embedding model.");
        tui.append_line(ICON_ERR + "Load one first: /embed <path>");
      } else {
        cfg.modes_.knowledge = new_state;
        tui.append_line(ICON_SYS + "Task: knowledge");
        tui.append_line(ICON_SYS + "  RAG active, top_k → " + std::to_string(cfg.rag_top_k_));
      }
    } else {
      tui.append_line(ICON_ERR + "Unknown mode: " + name);
      tui.redraw_all();
      return;
    }
    agent.reset_conversation(cfg.build_system_prompt());
    tui.set_mode_str(cfg.mode_string());
    tui.redraw_all();
  };

  if (rest.empty()) {
    // Show current state + picker
    tui.append_line(ICON_SYS + "Active tasks: " + (cfg.mode_string().empty() ? "(none)" : cfg.mode_string()));
    tui.redraw_all();
    std::vector<std::string> options = { "Default", "Knowledge", "Music", "Web" };
    int sel = tui.list_picker(options, "Select task");
    if (sel >= 0) {
      apply_mode(options[sel]);
    }
  } else {
    apply_mode(rest);
  }
}

//
// Slash command dispatcher
//
void handle_slash(const std::string &input, HaliConfig &cfg, Agent &agent, Tui &tui) {
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
    slash_audio(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/task") {
    slash_task(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/save") {
    if (rest.empty()) {
      tui.append_line(ICON_SYS + "Usage: /save <file>");
      tui.redraw_all();
      return;
    }
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
  if (verb == "/security-audit") {
    slash_security_audit(cfg, tui);
    return;
  }
  if (verb == "/skill") {
    slash_skill(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/lint") {
    slash_lint(rest, cfg, tui);
    return;
  }
  if (verb == "/plan") {
    slash_plan(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/tool") {
    slash_tool(rest, agent, tui);
    return;
  }
  if (verb == "/index") {
    slash_index(rest, cfg, agent, tui);
    return;
  }

  tui.append_line(ICON_ERR + "Unknown command: " + verb + "  (try /help)");
  tui.redraw_all();
}

//
// ── Command list ─────────────────────────────────────────────────────────
//

const std::vector<SlashCommand> &slash_commands() {
  static const std::vector<SlashCommand> commands = {
    {"/model",    "[path]",            "load a GGUF model (picker if no path)"},
    {"/embed",    "[path]",            "load an embedding model (picker if no path)"},
    {"/rag",      "[path]",            "index file or directory (picker if no path)"},
    {"/memory",   "",                  "KV / VRAM / layer stats"},
    {"/clear",    "",                  "reset conversation"},
    {"/settings", "",                  "show current settings"},
    {"/theme",    "",                  "toggle the theme (F12)"},
    {"/audio",    "[test|volume <v>]", "enable/disable audio, test tone, set volume"},
    {"/task",     "[name]",            "direct agent behavior (web, music, audio, knowledge)"},
    {"/set",      "<key> <value>",     "change a setting live"},
    {"/save",     "[file]",            "save chat transcript (plain text)"},
    {"/session",  "[file]",            "save structured session snapshot"},
    {"/allow",        "[path]",        "allow read access to a directory"},
    {"/allow-list",   "",              "list allowed directories"},
    {"/allow-remove", "[path]",        "remove an allowed directory"},
    {"/revoke",       "<name>",        "remove an approved RUN command"},
    {"/audit",    "[path]",            "scan codebase for model-hostility issues"},
    {"/security-audit", "",            "mark sandbox as security-audited"},
    {"/skill",    "[name] [kickoff]",  "load a skill into the conversation"},
    {"/lint",     "",                  "list source files and run clang-tidy"},
    {"/tool",     "[TOOL:NAME [args]]","invoke a tool (no args lists all tools)"},
    {"/plan",     "[text]",            "open a multiline text editor for a plan"},
    {"/index",    "[name|status|remove|load|unload]", "manage knowledge sources (ZIM/Wikipedia)"},
    {"/help",     "",                  "this message"},
  };
  return commands;
}