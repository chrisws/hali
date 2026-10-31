// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <format>
#include <algorithm>
#include <cstddef>

#include "agent.h"
#include "agent_utils.h"
#include "config.h"
#include "tui.h"
#include "audio.h"
#include "icon.h"
#include "string_utils.h"
#include "file.h"
#include "graph.h"
#include "build.h"
#include "planner.h"
#include "sysinfo.h"
#include "git_tool.h"
#include "curl.h"
#include "music_project.h"
#include "webview.h"

namespace fs = std::filesystem;

//
// Common handler for file-write tools (WRITE, PATCH, APPEND, REPLACE).
// Encapsulates: show_tool, path resolution, sandbox check, optional
// permission prompt, optional validation, backup, execute, result handling.
//
std::string Agent::handle_file_write(
    const std::string &tool_name,
    const std::string &path_arg,
    const std::string &data,
    bool do_permission,
    const ValidateFn &validate,
    const ExecuteFn &execute) {
  tui_.show_tool(tool_name + ": " + path_arg);
  turn_.files_written++;
  const auto path = resolve_path(cfg_.sandbox_, path_arg);
  if (!path_in_sandbox(cfg_.sandbox_, path)) {
    return "ERROR: path outside sandbox";
  }
  if (do_permission && cfg_.permission_prompt_ &&
      !tui_.confirm_dialog(std::format("Allow model to write {}?", path))) {
    return "ERROR: action prevented by user";
  }
  if (validate) {
    const auto warning = validate(path, data);
    if (!warning.empty()) {
      tui_.append_token(ICON_ERR + data);
      if (!tui_.confirm_dialog(std::format("[{}] - Allow model to write {}?", warning, path))) {
        return "ERROR: action prevented by user - " + warning;
      }
    }
  }
  backup_file(cfg_, tui_, path);
  const auto result = execute(path);
  if (!utils::starts_with(result, "OK")) {
    tui_.append_token(ICON_ERR + result);
  } else {
    broadcast_reload(cfg_, tui_);
  }
  return result;
}

std::string Agent::handle_build(const std::string &a1, const std::string &a2) {
  return run_with_spinner(tui_, [this, &a1, &a2]() -> std::string {
    auto result = build_run(cfg_.sandbox_, a1, [&](const std::string &msg) {
      tui_.show_tool(msg);
    }, a2);
    std::string out = result.output_;

    // Format lint warnings as a table
    if (!result.lint_details_.empty()) {
      int show = std::min(static_cast<int>(result.lint_details_.size()), 30);
      std::string dims = std::to_string(show) + "x4";
      std::string data = "#|File|Check|Description\n";
      for (int i = 0; i < show; i++) {
        const auto &w = result.lint_details_[i];
        data += std::to_string(i + 1) + "|" +
                w.file + ":" + std::to_string(w.line) + "|" +
                w.check + "|" + w.description + "\n";
      }
      auto table = tool_table(dims, data);
      if (table.error.empty()) {
        out += "\n";
        for (const auto &row : table.rows) {
          out += row + "\n";
        }
        if (static_cast<int>(result.lint_details_.size()) > show) {
          out += "... and " + std::to_string(result.lint_details_.size() - show) + " more\n";
        }
      }
      tui_.append_lines(table.rows);
    }

    return out;
  });
}

std::string Agent::handle_tune(const std::string &a1, const std::string &a2) {
  tui_.show_tool("tune");
  if (a1.empty()) return "ERROR: usage: TOOL:TUNE key=value [key=value ...]";
  std::istringstream iss(a1);
  std::string tok;
  std::string summary;
  int changed = 0;
  while (std::getline(iss, tok, ' ')) {
    auto eq = tok.find('=');
    if (eq == std::string::npos || eq == 0) {
      return "ERROR: invalid token '" + tok + "' — expected key=value";
    }
    std::string key = tok.substr(0, eq);
    std::string val = tok.substr(eq + 1);
    // Clamp values.
    if (key == "temperature" || key == "top_p" || key == "min_p" ||
        key == "penalty_repeat" || key == "penalty_freq" || key == "penalty_present") {
      float f = 0;
      try { f = std::stof(val); } catch (...) { return "ERROR: '" + val + "' is not a number"; }
      float lo = 0.0f, hi = 2.0f;
      if (key == "top_p" || key == "min_p") hi = 1.0f;
      if (f < lo) f = lo;
      if (f > hi) f = hi;
      val = std::to_string(f);
    } else if (key == "top_k" || key == "penalty_last_n") {
      int i = 0;
      try { i = std::stoi(val); } catch (...) { return "ERROR: '" + val + "' is not an integer"; }
      if (key == "top_k") { if (i < 1) i = 1; if (i > 200) i = 200; }
      if (key == "penalty_last_n") { if (i < 0) i = 0; if (i > 2048) i = 2048; }
      val = std::to_string(i);
    } else {
      return "ERROR: unknown key '" + key +
             "'. Valid: temperature, top_p, min_p, top_k, "
             "penalty_repeat, penalty_freq, penalty_present, penalty_last_n";
    }
    bool needs_reparam = false;
    std::string msg;
    int rc = cfg_.set_value(key, val, needs_reparam, msg);
    if (rc != 0) {
      return "ERROR: failed to set " + key + (rc == 1 ? " (unknown key)" : " (parse error)");
    }
    if (needs_reparam && model_loaded_) {
      apply_generation_params();
    }
    summary += key + "=" + val + "  ";
    changed++;
  }
  if (changed == 0) return "No changes made.";
  return "Tuned " + std::to_string(changed) + " setting(s): " + summary;
}

std::string Agent::handle_plan(const std::string &a1, const std::string &a2) {
  const std::string cmd = utils::trim(a1 + " " + a2);
  if (cmd.empty()) {
    tui_.show_tool("plan: display");
    if (planner_.steps().empty()) {
      return "No active plan. Use TOOL:PLAN set <goal> to create one.";
    }
    return planner_.to_markdown();
  }
  auto space = cmd.find(' ');
  const std::string verb = (space == std::string::npos) ? cmd : cmd.substr(0, space);
  const std::string arg  = (space == std::string::npos) ? "" : utils::trim(cmd.substr(space + 1));
  if (verb == "set") {
    tui_.show_tool("plan: set goal");
    planner_.set_goal(arg);
    return "OK: goal set to \"" + arg + "\"";
  }
  if (verb == "add") {
    tui_.show_tool("plan: add step");
    planner_.add_step(arg);
    return "OK: added step \"" + arg + "\" (" + std::to_string(planner_.steps().size()) + " total)";
  }
  if (verb == "advance") {
    tui_.show_tool("plan: advance");
    planner_.advance();
    if (planner_.complete()) {
      return "OK: plan complete.";
    }
    return "OK: advanced. " + planner_.to_markdown();
  }
  if (verb == "skip") {
    tui_.show_tool("plan: skip");
    size_t idx = 0;
    try { idx = std::stoul(arg); } catch (...) { return "ERROR: invalid index: " + arg; }
    planner_.skip(idx);
    return "OK: skipped step " + arg + ". " + planner_.to_markdown();
  }
  return "ERROR: unknown plan command: " + verb + " (use: set, add, advance, skip)";
}

std::string Agent::handle_choice(const std::string &a1, const std::string &a2) {
  // Format: question|opt1|opt2|opt3 (pipe-delimited, no quotes)
  // a1 and a2 may be split by space, so join them first
  std::string full = a1;
  if (!a2.empty()) {
    full += " " + a2;
  }
  full = utils::trim(full);
  if (full.empty()) {
    return "ERROR: usage: TOOL:CHOICE question|opt1|opt2|opt3";
  }

  // Split on pipes
  std::vector<std::string> parts;
  std::istringstream iss(full);
  std::string part;
  while (std::getline(iss, part, '|')) {
    auto trimmed = utils::trim(part);
    if (!trimmed.empty()) {
      parts.push_back(trimmed);
    }
  }

  if (parts.size() < 3) {
    return "ERROR: need a question and at least 2 options (got " +
           std::to_string(parts.size()) + " field(s))";
  }

  const std::string question = parts[0];
  std::vector<std::string> options;
  for (size_t i = 1; i < parts.size(); i++) {
    options.push_back(parts[i]);
  }

  tui_.show_tool("choice: " + question);
  int idx = tui_.list_picker(options, question);
  if (idx < 0) {
    return "CANCELLED";
  }
  tui_.append_line(ICON_TOOL + "\u2190 " + options[idx]);
  tui_.redraw_all();
  return options[idx];
}

std::string Agent::handle_music(const std::string &a1, const std::string &a2) {
  std::string full = a1;
  if (!a2.empty()) {
    full += " " + a2;
  }
  full = utils::trim(full);
  if (full.empty()) {
    return "ERROR: usage: TOOL:MUSIC <command> [args]";
  }

  const std::string proj_dir = cfg_.sandbox_ + "/.hali/projects";
  auto space = full.find(' ');
  const std::string cmd = (space == std::string::npos) ? full : full.substr(0, space);
  const std::string arg  = (space == std::string::npos) ? "" : utils::trim(full.substr(space + 1));

  if (cmd == "list") {
    tui_.show_tool("music: list");
    auto names = MusicProject::list(proj_dir);
    if (names.empty()) {
      return "No projects found in " + proj_dir;
    }
    std::string out;
    for (const auto &n : names) {
      out += n + "\n";
    }
    return out;
  }

  if (cmd == "create") {
    if (arg.empty()) {
      return "ERROR: usage: TOOL:MUSIC create <name>";
    }
    tui_.show_tool("music: create " + arg);
    MusicProject proj;
    proj.name = arg;
    if (!proj.save(proj_dir)) {
      return "ERROR: failed to save project \"" + arg + "\"";
    }
    return "OK: created project \"" + arg + "\"";
  }

  if (cmd == "load") {
    if (arg.empty()) {
      return "ERROR: usage: TOOL:MUSIC load <name>";
    }
    tui_.show_tool("music: load " + arg);
    std::string path = proj_dir + "/" + arg + ".json";
    if (!fs::exists(path)) {
      return "ERROR: project \"" + arg + "\" not found";
    }
    MusicProject proj = MusicProject::load(path);
    if (proj.name.empty()) {
      return "ERROR: failed to load project \"" + arg + "\"";
    }
    // Store loaded project in a temp file for subsequent commands
    // For now, just confirm load
    return "OK: loaded project \"" + proj.name + "\" (" +
           std::to_string(proj.tracks.size()) + " tracks, tempo " +
           std::to_string(proj.tempo) + " BPM)";
  }

  if (cmd == "add") {
    // Format: add <track_name> <script>
    // arg = "<track_name> <script>"
    auto sp = arg.find(' ');
    if (sp == std::string::npos) {
      return "ERROR: usage: TOOL:MUSIC add <track_name> <script>";
    }
    std::string track_name = arg.substr(0, sp);
    std::string script = utils::trim(arg.substr(sp + 1));
    if (script.empty()) {
      return "ERROR: empty script";
    }
    tui_.show_tool("music: add " + track_name);
    // Validate script
    auto parsed = audio::play_script(script);
    if (!parsed.error.empty()) {
      return "ERROR: invalid script: " + parsed.error;
    }
    // For now, we need a current project. Use a "current" project file.
    std::string cur_path = proj_dir + "/_current.json";
    MusicProject proj;
    if (fs::exists(cur_path)) {
      proj = MusicProject::load(cur_path);
    }
    MusicTrack track;
    track.name = track_name;
    track.script = script;
    proj.tracks.push_back(track);
    if (!proj.save(proj_dir)) {
      return "ERROR: failed to save project";
    }
    // Also save as _current for subsequent commands
    MusicProject cur = proj;
    cur.name = "_current";
    cur.save(proj_dir);
    return "OK: added track \"" + track_name + "\" (" +
           std::to_string(proj.tracks.size()) + " tracks total)";
  }

  if (cmd == "set") {
    // Format: set <n> <volume|pan|script> <value>
    std::istringstream iss(arg);
    std::string idx_str, field, value;
    if (!(iss >> idx_str >> field >> value)) {
      return "ERROR: usage: TOOL:MUSIC set <n> <volume|pan|script> <value>";
    }
    int idx = 0;
    try { idx = std::stoi(idx_str); } catch (...) { return "ERROR: invalid index: " + idx_str; }
    if (idx < 1) { return "ERROR: track index must be >= 1"; }
    tui_.show_tool("music: set " + idx_str + " " + field);
    std::string cur_path = proj_dir + "/_current.json";
    if (!fs::exists(cur_path)) {
      return "ERROR: no active project. Use TOOL:MUSIC create <name> first.";
    }
    MusicProject proj = MusicProject::load(cur_path);
    if (idx > static_cast<int>(proj.tracks.size())) {
      return "ERROR: track " + idx_str + " out of range (1-" +
             std::to_string(proj.tracks.size()) + ")";
    }
    auto &track = proj.tracks[idx - 1];
    if (field == "volume") {
      int v = 0;
      try { v = std::stoi(value); } catch (...) { return "ERROR: invalid volume: " + value; }
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      track.volume = v;
    } else if (field == "pan") {
      int p = 0;
      try { p = std::stoi(value); } catch (...) { return "ERROR: invalid pan: " + value; }
      if (p < -100) p = -100;
      if (p > 100) p = 100;
      track.pan = p;
    } else if (field == "script") {
      auto parsed = audio::play_script(value);
      if (!parsed.error.empty()) {
        return "ERROR: invalid script: " + parsed.error;
      }
      track.script = value;
    } else {
      return "ERROR: unknown field \"" + field + "\" (use: volume, pan, script)";
    }
    proj.save(proj_dir);
    MusicProject cur = proj;
    cur.name = "_current";
    cur.save(proj_dir);
    return "OK: set track " + idx_str + " " + field + " = " + value;
  }

  if (cmd == "remove") {
    int idx = 0;
    try { idx = std::stoi(arg); } catch (...) { return "ERROR: invalid index: " + arg; }
    if (idx < 1) { return "ERROR: track index must be >= 1"; }
    tui_.show_tool("music: remove " + arg);
    std::string cur_path = proj_dir + "/_current.json";
    if (!fs::exists(cur_path)) {
      return "ERROR: no active project";
    }
    MusicProject proj = MusicProject::load(cur_path);
    if (idx > static_cast<int>(proj.tracks.size())) {
      return "ERROR: track " + arg + " out of range";
    }
    proj.tracks.erase(proj.tracks.begin() + (idx - 1));
    proj.save(proj_dir);
    MusicProject cur = proj;
    cur.name = "_current";
    cur.save(proj_dir);
    return "OK: removed track " + arg + " (" +
           std::to_string(proj.tracks.size()) + " remaining)";
  }

  if (cmd == "tempo") {
    int bpm = 0;
    try { bpm = std::stoi(arg); } catch (...) { return "ERROR: invalid BPM: " + arg; }
    if (bpm < 20 || bpm > 300) { return "ERROR: BPM must be 20-300"; }
    tui_.show_tool("music: tempo " + arg);
    std::string cur_path = proj_dir + "/_current.json";
    if (!fs::exists(cur_path)) {
      return "ERROR: no active project";
    }
    MusicProject proj = MusicProject::load(cur_path);
    proj.tempo = bpm;
    proj.save(proj_dir);
    MusicProject cur = proj;
    cur.name = "_current";
    cur.save(proj_dir);
    return "OK: tempo set to " + arg + " BPM";
  }

  if (cmd == "play") {
    tui_.show_tool("music: play");
    std::string cur_path = proj_dir + "/_current.json";
    if (!fs::exists(cur_path)) {
      return "ERROR: no active project to play";
    }
    MusicProject proj = MusicProject::load(cur_path);
    if (proj.tracks.empty()) {
      return "ERROR: no tracks in project";
    }
    std::vector<audio::TrackMix> mixes;
    for (const auto &track : proj.tracks) {
      auto parsed = audio::play_script(track.script);
      if (!parsed.error.empty()) {
        return "ERROR: track \"" + track.name + "\": " + parsed.error;
      }
      audio::TrackMix mix;
      mix.events = parsed.events;
      mix.volume = static_cast<float>(track.volume) / 100.0f;
      mix.pan = static_cast<float>(track.pan) / 100.0f;
      mixes.push_back(mix);
    }
    audio::play_mix(mixes);
    return "OK: played " + std::to_string(mixes.size()) + " tracks";
  }

  if (cmd == "show") {
    tui_.show_tool("music: show");
    std::string cur_path = proj_dir + "/_current.json";
    if (!fs::exists(cur_path)) {
      return "No active project. Use TOOL:MUSIC create <name> first.";
    }
    MusicProject proj = MusicProject::load(cur_path);
    std::string out = "Project: " + proj.name + "\n";
    out += "Tempo: " + std::to_string(proj.tempo) + " BPM\n";
    out += "Tracks: " + std::to_string(proj.tracks.size()) + "\n\n";
    for (size_t i = 0; i < proj.tracks.size(); i++) {
      const auto &t = proj.tracks[i];
      out += "  " + std::to_string(i + 1) + ". " + t.name +
             "  [vol=" + std::to_string(t.volume) +
             " pan=" + std::to_string(t.pan) + "]\n";
      out += "     " + t.script + "\n";
    }
    return out;
  }

  if (cmd == "save") {
    tui_.show_tool("music: save");
    std::string cur_path = proj_dir + "/_current.json";
    if (!fs::exists(cur_path)) {
      return "ERROR: no active project to save";
    }
    MusicProject proj = MusicProject::load(cur_path);
    if (proj.name == "_current" || proj.name.empty()) {
      return "ERROR: project has no name. Use TOOL:MUSIC create <name> first.";
    }
    if (!proj.save(proj_dir)) {
      return "ERROR: failed to save project";
    }
    return "OK: saved project \"" + proj.name + "\" to " + proj_dir;
  }

  if (cmd == "clear") {
    tui_.show_tool("music: clear");
    std::string cur_path = proj_dir + "/_current.json";
    if (!fs::exists(cur_path)) {
      return "No active project.";
    }
    MusicProject proj = MusicProject::load(cur_path);
    proj.tracks.clear();
    proj.save(proj_dir);
    MusicProject cur = proj;
    cur.name = "_current";
    cur.save(proj_dir);
    return "OK: cleared all tracks";
  }

  return "ERROR: unknown music command: " + cmd +
         " (use: create, load, list, add, set, remove, tempo, play, show, save, clear)";
}

std::string Agent::handle_gui(const std::string &a1, const std::string &a2) {
  std::string full = a1;
  if (!a2.empty()) { full += " " + a2; }
  full = utils::trim(full);
  if (full.empty()) {
    return "ERROR: usage: TOOL:GUI <html> | input | clear | help";
  }
  if (full == "help") {
    return HaliConfig::gui_help();
  }
  if (!webview::is_running()) {
    return "ERROR: web display is not active. Enable it with /task web";
  }
  if (full == "input") {
    tui_.show_tool("gui: input");
    if (webview::has_message()) {
      return webview::get_message();
    }
    return "no input";
  }
  if (full == "clear") {
    tui_.show_tool("gui: clear");
    webview::broadcast_message("clear");
    return "OK: cleared";
  }
  tui_.show_tool("gui: display");

  // auto-save HTML to .hali/web/ for later inspection
  std::string web_dir = cfg_.sandbox_ + "/.hali/web";
  make_dir(web_dir);
  char ts[32];
  time_t now = time(nullptr);
  strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", localtime(&now));
  std::string save_path = web_dir + "/gui-" + ts + ".html";
  tool_write(save_path, a2);

  webview::broadcast_message(a2);
  return "OK: displayed (saved to " + save_path + ")";
}

ToolMap Agent::build_tool_dispatch() {
  ToolMap tools;

  tools["TOOL:DATE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("date");
    char buf[32]; time_t t = time(nullptr);
    strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&t));
    return buf;
  };
  tools["TOOL:TIME"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("time");
    char buf[32]; time_t t = time(nullptr);
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
    return buf;
  };
  tools["TOOL:RND"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("rnd");
    return std::to_string(static_cast<double>(rand()) / RAND_MAX);
  };
  tools["TOOL:TUNE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return handle_tune(a1, a2);
  };
  tools["TOOL:RAG"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:RAG");
    return rag_tool(a1);
  };
  tools["TOOL:LIST"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    std::string dir = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, dir)) {
      return "ERROR: path outside sandbox";
    }
    tui_.show_tool("listing: " + dir);
    return list_dir(dir);
  };
  tools["TOOL:EXISTS"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    std::string p = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, p)) {
      return "ERROR: path outside sandbox";
    }
    tui_.show_tool("checking: " + p);
    return fs::exists(p) ? "YES" : "NO";
  };
  tools["TOOL:READ"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("reading: " + a1);
    turn_.files_read++;
    std::string p = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, p)) {
      return "ERROR: path outside sandbox";
    }
    auto lines_pos = a2.find("--lines=");
    if (lines_pos != std::string::npos) {
      auto val = a2.substr(lines_pos + 8);
      auto comma = val.find(',');
      if (comma != std::string::npos) {
        int start = std::stoi(val.substr(0, comma));
        int end = std::stoi(val.substr(comma + 1));
        return read_file_range(p, start, end);
      }
    }
    return read_file(p);
  };
  tools["TOOL:SEARCH"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    SearchArgs sa = parse_search_args(a1, a2);
    tui_.show_tool("search: " + sa.pattern + " " + sa.path_and_flags);
    turn_.files_read++;
    std::string path_part, flags_part;
    auto space_pos = sa.path_and_flags.find(' ');
    if (space_pos != std::string::npos) {
      path_part = sa.path_and_flags.substr(0, space_pos);
      flags_part = sa.path_and_flags.substr(space_pos + 1);
    } else {
      path_part = sa.path_and_flags;
    }
    return tool_search(cfg_.sandbox_, sa.pattern, path_part, flags_part, cfg_.extra_dirs_);
  };
  tools["TOOL:WRITE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    const auto data = strip_code_fences(a1, a2);
    const bool skip_validate = utils::ends_with(resolve_path(cfg_.sandbox_, a1), SESSION_MD);
    return handle_file_write("writing", a1, data, true,
      [this, &data, skip_validate](const std::string &path, const std::string &) -> std::string {
        if (skip_validate || cfg_.disable_write_validate_) { return ""; }
        return tool_write_validate(path, data, cfg_.val_write_size_warn_);
      },
      [this, &data](const std::string &path) -> std::string {
        return tool_write(path, data);
      });
  };
  tools["TOOL:PATCH"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return handle_file_write("patch", a1, a2, false,
      [this](const std::string &path, const std::string &data) -> std::string {
        if (cfg_.disable_patch_validate_) { return ""; }
        return tool_patch_validate(path, data);
      },
      [this, &a2](const std::string &path) -> std::string {
        return tool_patch(path, a2);
      });
  };
  tools["TOOL:APPEND"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return handle_file_write("append", a1, a2, false,
      nullptr,
      [this, &a2](const std::string &path) -> std::string {
        return tool_append(path, a2);
      });
  };
  tools["TOOL:MKDIR"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    std::string p = resolve_path(cfg_.sandbox_, a1);
    tui_.show_tool("mkdir: " + a1);
    if (!path_in_sandbox(cfg_.sandbox_, p)) {
      return "ERROR: path outside sandbox";
    }
    return make_dir(p) ? "OK: created " + a1 : "ERROR: mkdir failed for " + a1;
  };
  tools["TOOL:CURL"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("curl: " + a1);
    return tool_curl(a1);
  };
  tools["TOOL:INTROSPECT"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("introspecting: " + a1);
    return cfg_.introspect();
  };
  tools["TOOL:ASK"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.set_thinking(false);
    tui_.show_tool("asking: " + a1 + " " + a2);
    if (cfg_.audio_user_prompt_) { audio::play_user_prompt(); }
    return tui_.readline();
  };
  tools["TOOL:RESTART"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("restart ...");
    return restart();
  };
  tools["TOOL:PERMISSION"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.set_thinking(false);
    tui_.show_tool("asking permission: " + a1 + " " + a2);
    if (cfg_.audio_user_prompt_) { audio::play_user_prompt(); }
    return tui_.confirm_dialog(a1 + " " + a2) ? "YES" : "NO";
  };
  tools["TOOL:RUN"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    if (cfg_.disable_run_) {
      return "ERROR: TOOL:RUN is disabled by configuration";
    }
    return tool_run(cfg_, tui_, a1, a2);
  };
  tools["TOOL:BUILD"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return handle_build(a1, a2);
  };
  tools["TOOL:STAT"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("stat " + a1);
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, path)) {
      return "ERROR: path outside sandbox";
    }
    return stat_file(path);
  };
  tools["TOOL:SYSINFO"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("sysinfo");
    return sysinfo_text();
  };
  tools["TOOL:GIT"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("git " + a1);
    return run_with_spinner(tui_, [this, &a1, &a2]() {
      return git_run(a1, a2, cfg_.sandbox_);
    });
  };
  tools["TOOL:HEX"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("hex " + a1);
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, path)) {
      return "ERROR: path outside sandbox";
    }
    return hex_dump(path, a2);
  };
  tools["TOOL:REPLACE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return handle_file_write("replace", a1, a2, true,
      nullptr,
      [this, &a1, &a2](const std::string &path) -> std::string {
        return tool_replace(cfg_.sandbox_, a1, a2);
      });
  };
  tools["TOOL:MCP"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("mcp: " + a1);
    return mcp_client_.call_tool(a1, a2, cfg_.sandbox_);
  };
  tools["TOOL:GRAPH"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    const int cols = tui_.get_term_cols() * 0.75;
    auto graphResult = tool_graph(cols, a1 + a2);
    if (graphResult.success_) { tui_.append_lines(graphResult.data_); }
    return graphResult.success_ ? "OK" : graphResult.message_;
  };
  tools["TOOL:TABLE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    const auto result = tool_table(a1, a2);
    if (!result.error.empty()) {
      tui_.show_tool("table error: " + result.error);
      tui_.append_line(a2);
      return result.error;
    }
    tui_.append_lines(result.rows);
    return "OK";
  };
  tools["TOOL:PLAY"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    const std::string notes = a1 + " " + a2;
    tui_.show_tool("play: " + notes);
    auto result = audio::play_script(notes);
    if (!result.error.empty()) { return result.error; }
    audio::play_events(result.events);
    size_t total = 0;
    for (const auto &ev : result.events) { total += ev.size(); }
    return "OK: played " + std::to_string(total) + " notes in "
           + std::to_string(result.events.size()) + " events";
  };
  if (cfg_.audio_multi_track_) {
    tools["TOOL:MUSIC"] = [this](const std::string &a1, const std::string &a2) -> std::string {
      return handle_music(a1, a2);
    };
  }
  tools["TOOL:PLAN"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return handle_plan(a1, a2);
  };
  tools["TOOL:CHOICE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return handle_choice(a1, a2);
  };
  tools["TOOL:GUI"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    return handle_gui(a1, a2);
  };
  return tools;
}
