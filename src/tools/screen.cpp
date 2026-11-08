// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <string>
#include <vector>
#include <map>
#include <sstream>

#include "screen.h"

static std::map<std::string, std::vector<std::string>> g_snapshots;

std::string screen_exec(const std::string &a1, const std::string &a2,
                        const std::vector<std::string> &current_lines,
                        std::vector<std::string> &restore_out) {
  if (a1 == "save") {
    if (a2.empty()) return "ERROR: usage: save <name>";
    g_snapshots[a2] = current_lines;
    return "Saved screen as '" + a2 + "' (" + std::to_string(current_lines.size()) + " lines).";
  }
  if (a1 == "show") {
    if (a2.empty()) return "ERROR: usage: show <name>";
    auto it = g_snapshots.find(a2);
    if (it == g_snapshots.end()) return "ERROR: no snapshot named '" + a2 + "'.";
    std::string out;
    for (const auto &l : it->second) {
      out += l + "\n";
    }
    return out;
  }
  if (a1 == "restore") {
    if (a2.empty()) return "ERROR: usage: restore <name>";
    auto it = g_snapshots.find(a2);
    if (it == g_snapshots.end()) return "ERROR: no snapshot named '" + a2 + "'.";
    restore_out = it->second;
    return "Restored screen from '" + a2 + "' (" + std::to_string(restore_out.size()) + " lines).";
  }
  if (a1 == "list") {
    if (g_snapshots.empty()) return "No saved snapshots.";
    std::string out;
    for (const auto &[name, lines] : g_snapshots) {
      out += name + " (" + std::to_string(lines.size()) + " lines)\n";
    }
    return out;
  }
  if (a1 == "clear") {
    if (a2.empty()) {
      int n = g_snapshots.size();
      g_snapshots.clear();
      return "Cleared all " + std::to_string(n) + " snapshot(s).";
    }
    auto it = g_snapshots.find(a2);
    if (it == g_snapshots.end()) return "ERROR: no snapshot named '" + a2 + "'.";
    g_snapshots.erase(it);
    return "Cleared snapshot '" + a2 + "'.";
  }
  return "ERROR: unknown screen command: " + a1 + " (use save|show|restore|list|clear)";
}