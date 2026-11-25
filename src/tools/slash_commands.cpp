// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "slash_commands.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <system_error>

#include "core/config.h"
#include "core/agent.h"
#include "ui/tui.h"
#include "ui/icon.h"
#include "ui_text.h"

#include "skills_generated.h"
#include "knowledge.h"
#include "slash_utils.h"
#include "util/shell_util.h"

namespace fs = std::filesystem;

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
    if (key == "audio_theme" && audio::is_enabled()) {
      audio::play_tool_tick(cfg.audio_theme_);
    }
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
// /skill [name] [kickoff] – load a skill into the conversation
//
static void slash_skill(const std::string &rest_in, HaliConfig &,
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
// /index [name|remove <name>|create|load|unload <name>|list|help]
//
static void slash_index(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  KnowledgeManager &km = agent.knowledge();

  static bool initialized = false;
  if (!initialized) {
    std::string hali_dir = cfg.sandbox_ + "/.hali";
    std::error_code ec;
    fs::create_directories(hali_dir + "/rag", ec);
    km.set_rag_dir(hali_dir + "/rag");
    km.set_index_json_path(hali_dir + "/kiwix-index.json");
    km.load_index_json();
    initialized = true;
  }

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
    sub = "help";
  }

  if (sub == "help") {
    index_help(tui);
    return;
  }

  if (sub == "remove") {
    index_remove(km, arg, tui);
    return;
  }

  if (sub == "create") {
    index_create(cfg, agent, tui);
    return;
  }

  if (sub == "load") {
    index_load(km, cfg, tui);
    return;
  }

  if (sub == "unload") {
    index_unload(km, cfg, arg, tui);
    return;
  }

  if (sub == "list") {
    index_list(km, tui);
    return;
  }

  if (fs::exists(sub)) {
    index_zim_file(sub, agent, tui, progress, km);
    return;
  }

  if (km.rag_exists(sub)) {
    if (km.load(sub)) {
      tui.append_line(ICON_SYS + "Loaded: " + sub);
      if (std::find(cfg.rag_files_.begin(), cfg.rag_files_.end(), sub)
          == cfg.rag_files_.end()) {
        cfg.rag_files_.emplace_back(sub);
        cfg.save_settings();
      }
    } else {
      tui.append_line(ICON_ERR + "Failed to load: " + sub);
    }
    tui.redraw_all();
    return;
  }

  tui.append_line(ICON_ERR + "Unknown source or file: " + sub);
  tui.append_line(ICON_SYS + "Type /index load to see available sources.");
  tui.redraw_all();
}



//
// /logcat [package] [lines] – capture Android logcat output and feed to agent
//
static void slash_logcat(const std::string &rest, HaliConfig &cfg, Agent &agent, Tui &tui) {
  // Parse optional args: [package] [lines]
  std::string pkg;
  int lines = 100;
  {
    std::istringstream iss(rest);
    std::string tok;
    int arg_idx = 0;
    while (iss >> tok) {
      if (arg_idx == 0) {
        pkg = tok;
      } else if (arg_idx == 1) {
        try { lines = std::stoi(tok); } catch (...) { lines = 100; }
      }
      arg_idx++;
    }
  }

  // Determine adb path
  const std::string adb = cfg.adb_path_.empty() ? "adb" : cfg.adb_path_;

  // Check adb is available
  {
    auto check = run_shell_command(adb + " version 2>&1");
    if (check.find("Android Debug Bridge") == std::string::npos &&
        check.find("adb") == std::string::npos) {
      tui.append_line(ICON_ERR + "adb not found. Set tools.adb in config or add to PATH.");
      tui.redraw_all();
      return;
    }
  }

  // If no package given, try to extract from AndroidManifest.xml
  if (pkg.empty()) {
    const std::string app_dir = cfg.sandbox_ + "/app";
    if (fs::exists(app_dir) && fs::is_directory(app_dir)) {
      for (const auto &entry : fs::recursive_directory_iterator(app_dir)) {
        if (entry.is_regular_file() &&
            entry.path().filename() == "AndroidManifest.xml") {
          std::ifstream f(entry.path());
          if (f) {
            std::string content((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
            f.close();
            const std::string key = "package=\"";
            size_t pos = content.find(key);
            if (pos != std::string::npos) {
              pos += key.size();
              size_t end = content.find('"', pos);
              if (end != std::string::npos) {
                pkg = content.substr(pos, end - pos);
              }
            }
            break;
          }
        }
      }
    }
  }

  // Require a package – unfiltered logcat is too noisy to be useful
  if (pkg.empty()) {
    tui.append_line(ICON_ERR + "Could not determine package name. "
      "Specify it explicitly: /logcat <package> [lines]");
    tui.redraw_all();
    return;
  }

  // Build the logcat command (filtered to app package only)
  std::string cmd = adb + " logcat -d -t " + std::to_string(lines) + " -s " + pkg;

  tui.append_line(ICON_SYS + "Capturing logcat for " + pkg +
                  " (last " + std::to_string(lines) + " lines)…");
  tui.redraw_all();

  std::string output = run_shell_command(cmd);

  if (output.empty()) {
    tui.append_line(ICON_SYS + "No logcat output captured (app may not have logged anything).");
    tui.redraw_all();
    return;
  }
  if (output.find("error") != std::string::npos && output.find("I/") == std::string::npos) {
    tui.append_line(ICON_ERR + "Failed to capture logcat: " + output);
    tui.redraw_all();
    return;
  }

  // Write full output to .hali/logcat.log for later inspection
  {
    std::string hali_dir = cfg.sandbox_ + "/.hali";
    std::error_code ec;
    fs::create_directories(hali_dir, ec);
    std::string log_path = hali_dir + "/logcat.log";
    std::ofstream ofs(log_path);
    if (ofs) {
      ofs << output;
      tui.append_line(ICON_SYS + "Logcat saved to " + log_path);
    }
  }

  // Display a short snippet in TUI (full log is in the file)
  {
    std::istringstream dss(output);
    std::string line;
    int shown = 0;
    const int max_lines = 8;
    while (std::getline(dss, line) && shown < max_lines) {
      tui.append_line(ICON_SYS + line);
      shown++;
    }
    // Check if there are more lines
    std::string extra;
    if (std::getline(dss, extra)) {
      tui.append_line(ICON_SYS + "  … (see .hali/logcat.log for full output)");
    }
  }
  tui.redraw_all();

  // Build a contextual preamble for the agent
  std::string prompt =
    "You are debugging an Android application.\n"
    "Package: " + pkg + "\n"
    "Project root: " + cfg.sandbox_ + "\n"
    "Source code is in: " + cfg.sandbox_ + "/app/src/main/java/\n\n"
    "Below is the recent logcat output (filtered to this app only, last " +
    std::to_string(lines) + " lines). Analyze it for:\n"
    "  - Crashes (FATAL EXCEPTION, AndroidRuntime)\n"
    "  - Errors (E/ tags, stack traces)\n"
    "  - Warnings that indicate bugs (W/ tags)\n"
    "  - Unusual behavior or performance issues\n\n"
    "If you find issues, identify the relevant Kotlin/Java source file(s) "
    "and suggest specific fixes. If the logs look clean, say so.\n\n"
    "=== Logcat Output ===\n" + output;

  tui.append_line(ICON_SYS + "Submitting logcat to agent for analysis…");
  tui.redraw_all();
  agent.run_turn(prompt);
  tui.redraw_all();
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
    slash_security_audit(rest, cfg, tui, agent);
    return;
  }
  if (verb == "/skill") {
    slash_skill(rest, cfg, agent, tui);
    return;
  }
  if (verb == "/lint") {
    slash_lint(rest, cfg, agent, tui);
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
  if (verb == "/logcat") {
    slash_logcat(rest, cfg, agent, tui);
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
    {"/model",    "[path]",            "load a GGUF model (picker if no path)", "Model"},
    {"/embed",    "[path]",            "load an embedding model (picker if no path)", "Model"},
    {"/rag",      "[path]",            "index file or directory (picker if no path)", "Model"},
    {"/memory",   "",                  "KV / VRAM / layer stats", "Model"},
    {"/clear",    "",                  "reset conversation", "Session"},
    {"/save",     "[file]",            "save chat transcript (plain text)", "Session"},
    {"/session",  "[file]",            "save structured session snapshot", "Session"},
    {"/set",      "<key> <value>",     "change a setting live", "Settings"},
    {"/settings", "",                  "show current settings", "Settings"},
    {"/theme",    "",                  "toggle the theme (F12)", "Settings"},
    {"/allow",        "[path]",        "allow read access to a directory", "Access"},
    {"/allow-list",   "",              "list allowed directories", "Access"},
    {"/allow-remove", "[path]",        "remove an allowed directory", "Access"},
    {"/revoke",       "<name>",        "remove an approved RUN command", "Access"},
    {"/audit",    "[path]",            "scan codebase for model-hostility issues", "Audit"},
    {"/lint",     "",                  "list source files and run clang-tidy", "Audit"},
    {"/security-audit", "",            "mark sandbox as security-audited", "Audit"},
    {"/skill",    "[name] [kickoff]",  "load a skill into the conversation", "Tools"},
    {"/task",     "[name]",            "direct agent behavior (web, music, audio, knowledge)", "Tools"},
    {"/tool",     "[TOOL:NAME [args]]","invoke a tool (no args lists all tools)", "Tools"},
    {"/plan",     "[text]",            "open a multiline text editor for a plan", "Tools"},
    {"/index",    "[name|create|load|remove|unload|list|help]", "manage knowledge sources (ZIM/Wikipedia)", "Knowledge"},
    {"/logcat",   "[package] [lines]", "capture Android logcat and feed to agent", "Android"},
    {"/audio",    "[test|volume <v>|theme <n>]", "enable/disable, test, volume, theme", "Audio"},
    {"/help",     "",                  "this message", "Help"},
  };
  return commands;
}

//
// Return a formatted summary of all slash commands grouped by category.
//
std::string slash_commands_summary() {
  std::string out = "=== Slash Commands ===\n";
  const auto &cmds = slash_commands();
  std::string current_group;
  for (const auto &c : cmds) {
    if (c.group != current_group) {
      current_group = c.group;
      out += "\n[" + current_group + "]\n";
    }
    out += "  " + std::string(c.name);
    if (c.usage[0]) {
      out += " " + std::string(c.usage);
    }
    out += "  — " + std::string(c.description) + "\n";
  }
  return out;
}
