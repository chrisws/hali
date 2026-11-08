// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

#include "clipboard.h"
#include "config.h"
#include "string_utils.h"

namespace fs = std::filesystem;

static std::vector<std::string> g_clip;

static std::vector<std::string> read_file_lines(const std::string &path) {
  std::vector<std::string> lines;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    lines.push_back(line);
  }
  return lines;
}

static bool write_file_lines(const std::string &path, const std::vector<std::string> &lines) {
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  for (const auto &l : lines) {
    f << l << "\n";
  }
  return f.good();
}

static std::string join_lines(const std::vector<std::string> &lines) {
  std::string out;
  for (const auto &l : lines) {
    out += l + "\n";
  }
  return out;
}

std::string clipboard_exec(const std::string &sandbox, const std::string &a1, const std::string &a2) {
  if (a1 == "show") {
    if (g_clip.empty()) return "Clipboard is empty.";
    return join_lines(g_clip);
  }
  if (a1 == "clear") {
    g_clip.clear();
    return "Clipboard cleared.";
  }
  if (a1 == "copy" || a1 == "cut" || a1 == "paste") {
    // Parse: file start,end  OR  file line
    auto sp = a2.find(' ');
    if (sp == std::string::npos) {
      return "ERROR: usage: " + a1 + " <file> <start>,<end>";
    }
    std::string file = a2.substr(0, sp);
    std::string range = a2.substr(sp + 1);

    auto path = utils::resolve_path(sandbox, file);
    if (!fs::exists(path)) {
      return "ERROR: file not found: " + file;
    }

    auto lines = read_file_lines(path);

    if (a1 == "paste") {
      int at = std::stoi(range);
      if (at < 1 || at > (int)lines.size() + 1) {
        return "ERROR: line " + range + " out of range (1-" + std::to_string(lines.size() + 1) + ")";
      }
      if (g_clip.empty()) return "ERROR: clipboard is empty.";
      int idx = at - 1;
      for (int i = (int)g_clip.size() - 1; i >= 0; --i) {
        lines.insert(lines.begin() + idx, g_clip[i]);
      }
      if (!write_file_lines(path, lines)) {
        return "ERROR: failed to write " + file;
      }
      return "Pasted " + std::to_string(g_clip.size()) + " line(s) at line " + range + " in " + file;
    }

    // copy or cut: parse start,end
    auto comma = range.find(',');
    if (comma == std::string::npos) {
      return "ERROR: usage: " + a1 + " <file> <start>,<end>";
    }
    int start = std::stoi(range.substr(0, comma));
    int end = std::stoi(range.substr(comma + 1));
    if (start < 1 || end > (int)lines.size() || start > end) {
      return "ERROR: range " + range + " invalid (file has " + std::to_string(lines.size()) + " lines)";
    }

    g_clip.clear();
    for (int i = start; i <= end; ++i) {
      g_clip.push_back(lines[i - 1]);
    }

    if (a1 == "cut") {
      lines.erase(lines.begin() + (start - 1), lines.begin() + end);
      if (!write_file_lines(path, lines)) {
        return "ERROR: failed to write " + file;
      }
      return "Cut " + std::to_string(g_clip.size()) + " line(s) from " + file + " (lines " + range + ")";
    }

    return "Copied " + std::to_string(g_clip.size()) + " line(s) from " + file + " (lines " + range + ")";
  }
  return "ERROR: unknown clipboard command: " + a1 + " (use copy|cut|paste|show|clear)";
}