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
#include <format>

#include "line_edit.h"
#include "config.h"
#include "string_utils.h"

namespace fs = std::filesystem;

static std::vector<std::string> g_buffer;

//
// Static helpers
//

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

// Format a line with its number: "   4: content"
static std::string format_line(int num, const std::string &content) {
  return std::format("{:>4}: {}", num, content);
}

// Show context around a range of lines (2 before, 2 after).
// Returns formatted string with line numbers.
static std::string show_context(const std::vector<std::string> &lines,
                                int start, int end) {
  int ctx_start = std::max(1, start - 2);
  int ctx_end = std::min((int)lines.size(), end + 2);
  std::string out;
  for (int i = ctx_start; i <= ctx_end; ++i) {
    out += format_line(i, lines[i - 1]) + "\n";
  }
  return out;
}

// Parse "file start,end" from a2. Returns false on error.
static bool parse_file_range(const std::string &sandbox, const std::string &a2,
                             std::string &path, int &start, int &end,
                             std::string &err) {
  auto sp = a2.find(' ');
  if (sp == std::string::npos) {
    err = "ERROR: missing file argument";
    return false;
  }
  std::string file = a2.substr(0, sp);
  std::string range = a2.substr(sp + 1);

  path = utils::resolve_path(sandbox, file);
  if (!fs::exists(path)) {
    err = "ERROR: file not found: " + file;
    return false;
  }
  if (!utils::path_in_sandbox(sandbox, path)) {
    err = "ERROR: path outside sandbox";
    return false;
  }

  auto comma = range.find(',');
  if (comma == std::string::npos) {
    err = "ERROR: expected range start,end";
    return false;
  }
  start = std::stoi(range.substr(0, comma));
  end = std::stoi(range.substr(comma + 1));
  return true;
}

// Parse "file line content" from a2.
static bool parse_file_line_content(const std::string &sandbox, const std::string &a2,
                                    std::string &path, int &line, std::string &content,
                                    std::string &err) {
  auto sp1 = a2.find(' ');
  if (sp1 == std::string::npos) {
    err = "ERROR: missing file argument";
    return false;
  }
  std::string file = a2.substr(0, sp1);
  std::string rest = a2.substr(sp1 + 1);

  auto sp2 = rest.find(' ');
  if (sp2 == std::string::npos) {
    err = "ERROR: missing line number";
    return false;
  }
  line = std::stoi(rest.substr(0, sp2));
  content = rest.substr(sp2 + 1);

  path = utils::resolve_path(sandbox, file);
  if (!fs::exists(path)) {
    err = "ERROR: file not found: " + file;
    return false;
  }
  if (!utils::path_in_sandbox(sandbox, path)) {
    err = "ERROR: path outside sandbox";
    return false;
  }
  return true;
}

// Parse "file line" from a2 (for paste).
static bool parse_file_line(const std::string &sandbox, const std::string &a2,
                            std::string &path, int &line, std::string &err) {
  auto sp = a2.find(' ');
  if (sp == std::string::npos) {
    err = "ERROR: missing file argument";
    return false;
  }
  std::string file = a2.substr(0, sp);
  line = std::stoi(a2.substr(sp + 1));

  path = utils::resolve_path(sandbox, file);
  if (!fs::exists(path)) {
    err = "ERROR: file not found: " + file;
    return false;
  }
  if (!utils::path_in_sandbox(sandbox, path)) {
    err = "ERROR: path outside sandbox";
    return false;
  }
  return true;
}

//
// Command handlers
//

static std::string cmd_read(const std::string &sandbox, const std::string &a2) {
  std::string path;
  int start = 1, end = 0;

  if (a2.empty()) {
    return "ERROR: usage: read <file> [start,end]";
  }

  auto sp = a2.find(' ');
  std::string file, range;
  if (sp == std::string::npos) {
    file = a2;
  } else {
    file = a2.substr(0, sp);
    range = a2.substr(sp + 1);
  }

  path = utils::resolve_path(sandbox, file);
  if (!fs::exists(path)) {
    return "ERROR: file not found: " + file;
  }
  if (!utils::path_in_sandbox(sandbox, path)) {
    return "ERROR: path outside sandbox";
  }

  auto lines = read_file_lines(path);
  if (lines.empty()) {
    return "OK: " + file + " is empty (0 lines)";
  }

  if (!range.empty()) {
    auto comma = range.find(',');
    if (comma == std::string::npos) {
      return "ERROR: expected range start,end";
    }
    start = std::stoi(range.substr(0, comma));
    end = std::stoi(range.substr(comma + 1));
    if (start < 1 || end > (int)lines.size() || start > end) {
      return "ERROR: range " + range + " out of bounds (file has " +
             std::to_string(lines.size()) + " lines)";
    }
  } else {
    end = (int)lines.size();
  }

  std::string out = "OK: " + file + " (" + std::to_string(lines.size()) + " lines)\n";
  for (int i = start; i <= end; ++i) {
    out += format_line(i, lines[i - 1]) + "\n";
  }
  return out;
}

static std::string cmd_write(const std::string &sandbox, const std::string &a2) {
  std::string path, content;
  int line;
  std::string err;

  if (!parse_file_line_content(sandbox, a2, path, line, content, err)) {
    return err;
  }

  auto lines = read_file_lines(path);
  if (line < 1 || line > (int)lines.size()) {
    return "ERROR: line " + std::to_string(line) + " out of range (1-" +
           std::to_string(lines.size()) + ")";
  }

  lines[line - 1] = content;
  if (!write_file_lines(path, lines)) {
    return "ERROR: failed to write file";
  }

  std::string file = fs::path(path).filename().string();
  std::string out = "OK: replaced line " + std::to_string(line) + " in " + file + "\n";
  out += show_context(lines, line, line);
  return out;
}

static std::string cmd_insert(const std::string &sandbox, const std::string &a2) {
  std::string path, content;
  int line;
  std::string err;

  if (!parse_file_line_content(sandbox, a2, path, line, content, err)) {
    return err;
  }

  auto lines = read_file_lines(path);
  if (line < 1 || line > (int)lines.size() + 1) {
    return "ERROR: line " + std::to_string(line) + " out of range (1-" +
           std::to_string(lines.size() + 1) + ")";
  }

  lines.insert(lines.begin() + (line - 1), content);
  if (!write_file_lines(path, lines)) {
    return "ERROR: failed to write file";
  }

  std::string file = fs::path(path).filename().string();
  std::string out = "OK: inserted at line " + std::to_string(line) + " in " + file + "\n";
  out += show_context(lines, line, line);
  return out;
}

static std::string cmd_delete(const std::string &sandbox, const std::string &a2) {
  std::string path;
  int start, end;
  std::string err;

  if (!parse_file_range(sandbox, a2, path, start, end, err)) {
    return err;
  }

  auto lines = read_file_lines(path);
  if (start < 1 || end > (int)lines.size() || start > end) {
    return "ERROR: range " + std::to_string(start) + "," + std::to_string(end) +
           " out of bounds (file has " + std::to_string(lines.size()) + " lines)";
  }

  int count = end - start + 1;
  lines.erase(lines.begin() + (start - 1), lines.begin() + end);
  if (!write_file_lines(path, lines)) {
    return "ERROR: failed to write file";
  }

  std::string file = fs::path(path).filename().string();
  std::string out = "OK: deleted " + std::to_string(count) + " line(s) " +
                    std::to_string(start) + "-" + std::to_string(end) + " from " + file + "\n";
  if (!lines.empty()) {
    int ctx_start = std::max(1, start - 2);
    int ctx_end = std::min((int)lines.size(), start + 1);
    out += show_context(lines, ctx_start, ctx_end);
  }
  return out;
}

static std::string cmd_copy(const std::string &sandbox, const std::string &a2) {
  std::string path;
  int start, end;
  std::string err;

  if (!parse_file_range(sandbox, a2, path, start, end, err)) {
    return err;
  }

  auto lines = read_file_lines(path);
  if (start < 1 || end > (int)lines.size() || start > end) {
    return "ERROR: range " + std::to_string(start) + "," + std::to_string(end) +
           " out of bounds (file has " + std::to_string(lines.size()) + " lines)";
  }

  g_buffer.clear();
  for (int i = start; i <= end; ++i) {
    g_buffer.push_back(lines[i - 1]);
  }

  std::string file = fs::path(path).filename().string();
  return "OK: copied " + std::to_string(g_buffer.size()) + " line(s) from " +
         file + " (lines " + std::to_string(start) + "-" + std::to_string(end) + ")";
}

static std::string cmd_cut(const std::string &sandbox, const std::string &a2) {
  std::string path;
  int start, end;
  std::string err;

  if (!parse_file_range(sandbox, a2, path, start, end, err)) {
    return err;
  }

  auto lines = read_file_lines(path);
  if (start < 1 || end > (int)lines.size() || start > end) {
    return "ERROR: range " + std::to_string(start) + "," + std::to_string(end) +
           " out of bounds (file has " + std::to_string(lines.size()) + " lines)";
  }

  g_buffer.clear();
  for (int i = start; i <= end; ++i) {
    g_buffer.push_back(lines[i - 1]);
  }

  lines.erase(lines.begin() + (start - 1), lines.begin() + end);
  if (!write_file_lines(path, lines)) {
    return "ERROR: failed to write file";
  }

  std::string file = fs::path(path).filename().string();
  std::string out = "OK: cut " + std::to_string(g_buffer.size()) + " line(s) from " +
                    file + " (lines " + std::to_string(start) + "-" + std::to_string(end) + ")\n";
  if (!lines.empty()) {
    int ctx_start = std::max(1, start - 2);
    int ctx_end = std::min((int)lines.size(), start + 1);
    out += show_context(lines, ctx_start, ctx_end);
  }
  return out;
}

static std::string cmd_paste(const std::string &sandbox, const std::string &a2) {
  std::string path;
  int line;
  std::string err;

  if (!parse_file_line(sandbox, a2, path, line, err)) {
    return err;
  }

  if (g_buffer.empty()) {
    return "ERROR: buffer is empty (use copy or cut first)";
  }

  auto lines = read_file_lines(path);
  if (line < 1 || line > (int)lines.size() + 1) {
    return "ERROR: line " + std::to_string(line) + " out of range (1-" +
           std::to_string(lines.size() + 1) + ")";
  }

  int idx = line - 1;
  for (int i = (int)g_buffer.size() - 1; i >= 0; --i) {
    lines.insert(lines.begin() + idx, g_buffer[i]);
  }
  if (!write_file_lines(path, lines)) {
    return "ERROR: failed to write file";
  }

  std::string file = fs::path(path).filename().string();
  int paste_end = line + (int)g_buffer.size() - 1;
  std::string out = "OK: pasted " + std::to_string(g_buffer.size()) + " line(s) at line " +
                    std::to_string(line) + " in " + file + "\n";
  out += show_context(lines, line, paste_end);
  return out;
}

static std::string cmd_show() {
  if (g_buffer.empty()) {
    return "Buffer is empty.";
  }
  std::string out = "Buffer (" + std::to_string(g_buffer.size()) + " lines):\n";
  for (int i = 0; i < (int)g_buffer.size(); ++i) {
    out += format_line(i + 1, g_buffer[i]) + "\n";
  }
  return out;
}

static std::string cmd_clear() {
  g_buffer.clear();
  return "Buffer cleared.";
}

//
// Main dispatch
//

std::string line_edit_exec(const std::string &sandbox, const std::string &a1, const std::string &a2) {
  if (a1 == "read") return cmd_read(sandbox, a2);
  if (a1 == "write") return cmd_write(sandbox, a2);
  if (a1 == "insert") return cmd_insert(sandbox, a2);
  if (a1 == "delete") return cmd_delete(sandbox, a2);
  if (a1 == "copy") return cmd_copy(sandbox, a2);
  if (a1 == "cut") return cmd_cut(sandbox, a2);
  if (a1 == "paste") return cmd_paste(sandbox, a2);
  if (a1 == "show") return cmd_show();
  if (a1 == "clear") return cmd_clear();
  return "ERROR: unknown line_edit command: " + a1 +
         " (use read|write|insert|delete|copy|cut|paste|show|clear)";
}