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

#include "agent.h"
#include "config.h"
#include "tui.h"
#include "audio.h"
#include "icon.h"
#include "string_utils.h"
#include "file.h"
#include "graph.h"
#include "webview.h"
#include "shell_util.h"
#include "build.h"
#include "planner.h"
#include "sysinfo.h"
#include "git_tool.h"
#include "agent_utils.h"
#include "curl.h"

namespace fs = std::filesystem;

ToolMap Agent::build_tool_dispatch() {
  ToolMap tools;

  tools["TOOL:DATE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:DATE");
    char buf[32]; time_t t = time(nullptr);
    strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&t));
    return buf;
  };
  tools["TOOL:TIME"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:TIME");
    char buf[32]; time_t t = time(nullptr);
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
    return buf;
  };
  tools["TOOL:RND"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:RND");
    return std::to_string(static_cast<double>(rand()) / RAND_MAX);
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
    tui_.show_tool("writing: " + a1);
    turn_.files_written++;
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_in_sandbox(cfg_.sandbox_, path)) {
      return "ERROR: path outside sandbox";
    }
    const auto data = strip_code_fences(a1, a2);
    if (!utils::ends_with(path, SESSION_MD)) {
      if (cfg_.permission_prompt_ && !tui_.confirm_dialog(std::format("Allow model to write {}?", path))) {
        return "ERROR: action prevented by user";
      } else if (!cfg_.disable_write_validate_) {
        const auto validate = tool_write_validate(path, data, cfg_.val_write_size_warn_);
        if (!validate.empty() && !tui_.confirm_dialog(std::format("[{}] - Allow model to write {}?", validate, path))) {
          return "ERROR: action prevented by user - " + validate;
        }
      }
    }
    backup_file(cfg_, tui_, path);
    const auto result = tool_write(path, data);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + data);
      tui_.append_token(ICON_ERR + result);
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  };
  tools["TOOL:PATCH"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("patch: " + a1);
    turn_.files_written++;
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_in_sandbox(cfg_.sandbox_, path)) {
      return "ERROR: path outside sandbox";
    }
    if (!cfg_.disable_patch_validate_) {
      const auto validate = tool_patch_validate(path, a2);
      if (!validate.empty()) {
        tui_.append_token(ICON_ERR + a2);
        if (!tui_.confirm_dialog(std::format("[{}] - Allow model to patch {}?", validate, path))) {
          return "ERROR: action prevented by user - " + validate;
        }
      }
    }
    backup_file(cfg_, tui_, path);
    const auto result = tool_patch(path, a2);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + a2);
      tui_.append_token(ICON_ERR + result);
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
  };
  tools["TOOL:APPEND"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("append: " + a1);
    turn_.files_written++;
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_in_sandbox(cfg_.sandbox_, path)) {
      return "ERROR: path outside sandbox";
    }
    backup_file(cfg_, tui_, path);
    const auto result = tool_append(path, a2);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + result);
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
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
    return run_with_spinner(tui_, [this, &a1, &a2]() -> std::string {
      auto result = build_run(cfg_.sandbox_, a1, [&](const std::string &msg) {
        tui_.show_tool(msg);
      }, a2);
      return result.output_;
    });
  };
  tools["TOOL:STAT"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:STAT " + a1);
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, path)) {
      return "ERROR: path outside sandbox";
    }
    return stat_file(path);
  };
  tools["TOOL:SYSINFO"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:SYSINFO");
    return sysinfo_text();
  };
  tools["TOOL:GIT"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:GIT " + a1);
    return run_with_spinner(tui_, [this, &a1, &a2]() {
      return git_run(a1, a2, cfg_.sandbox_);
    });
  };
  tools["TOOL:HEX"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("TOOL:HEX " + a1);
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (!path_allowed(cfg_.sandbox_, cfg_.extra_dirs_, path)) {
      return "ERROR: path outside sandbox";
    }
    return hex_dump(path, a2);
  };
  tools["TOOL:REPLACE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    tui_.show_tool("replace: " + a1);
    const auto path = resolve_path(cfg_.sandbox_, a1);
    if (cfg_.permission_prompt_ && !tui_.confirm_dialog(std::format("Allow model to replace in {}?", path))) {
      return "ERROR: action prevented by user";
    }
    backup_file(cfg_, tui_, path);
    auto result = tool_replace(cfg_.sandbox_, a1, a2);
    if (!utils::starts_with(result, "OK")) {
      tui_.append_token(ICON_ERR + result);
    } else {
      broadcast_reload(cfg_, tui_);
    }
    return result;
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
  tools["TOOL:PLAN"] = [this](const std::string &a1, const std::string &a2) -> std::string {
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
  };
  tools["TOOL:CHOICE"] = [this](const std::string &a1, const std::string &a2) -> std::string {
    if (a1.empty() || a2.empty()) {
      return "ERROR: usage: TOOL:CHOICE \"question\" \"opt1|opt2|opt3\"";
    }
    tui_.show_tool("choice: " + a1);
    std::vector<std::string> options;
    std::istringstream iss(a2);
    std::string opt;
    while (std::getline(iss, opt, '|')) {
      auto trimmed = utils::trim(opt);
      if (!trimmed.empty()) {
        options.push_back(trimmed);
      }
    }
    if (options.size() < 2) {
      return "ERROR: need at least 2 options (got " + std::to_string(options.size()) + ")";
    }
    int idx = tui_.list_picker(options, a1);
    if (idx < 0) {
      return "CANCELLED";
    }
    tui_.append_line(ICON_TOOL + "← " + options[idx]);
    tui_.redraw_all();
    return options[idx];
  };
  return tools;
}