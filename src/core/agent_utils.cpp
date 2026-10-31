// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <future>
#include <cctype>
#include <system_error>
#include <ios>
#include <format>

#include "agent_utils.h"
#include "string_utils.h"
#include "shell_util.h"
#include "tui.h"
#include "config.h"
#include "file.h"
#include "webview.h"

namespace fs = std::filesystem;

//
// Run a callable in a background thread, ticking the TUI spinner while waiting.
//
std::string run_with_spinner(Tui &tui, const std::function<std::string()> &fn) {
  auto fut = std::async(std::launch::async, fn);
  while (fut.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout) {
    tui.tick_spinner();
  }
  return fut.get();
}

//
// handling for strip_code_fences
//
static const std::vector<std::string> CODE_EXTENSIONS = {
  ".py",".c",".cpp",".h",".bas",".java",".html",".js",".ts",
  ".json",".yaml",".toml",".sh",".go",".rs",".jsx",".tsx",".xml",".svg"
};

static const std::vector<std::string> TEXT_EXTENSIONS = {
  ".md",".txt",".json",".yaml",".toml",".xml",".svg",
  ".cfg",".ini",".conf",".lock",".env"
};

//
// File priority for search result ordering:
//   0 = source code
//   1 = text / config
//   2 = binary / unknown
//
static int file_category(const std::string &filename) {
  auto ext = fs::path(filename).extension().string();
  if (std::ranges::any_of(CODE_EXTENSIONS, [&](const std::string &e){ return ext == e; })) {
    return 0;
  }
  if (std::ranges::any_of(TEXT_EXTENSIONS, [&](const std::string &e){ return ext == e; })) {
    return 1;
  }
  return 2;
}

//
// whether TOOL:RUN arguments contain unsafe actions
//
bool hasDangerousPatterns(const std::string &command, const std::vector<std::string> &run_allowed) {
  if (command.find('|') != std::string::npos) {
    auto pipePos = command.rfind('|');
    std::string afterPipe = command.substr(pipePos + 1);
    afterPipe.erase(0, afterPipe.find_first_not_of(" \t"));
    auto spacePos = afterPipe.find_first_of(" \t");
    std::string prog = (spacePos != std::string::npos) ? afterPipe.substr(0, spacePos) : afterPipe;
    if (std::find(run_allowed.begin(), run_allowed.end(), prog) == run_allowed.end()) {
      return true;
    }
  }
  // Command separators and substitutions
  if (command.find("&&") != std::string::npos) return true;
  if (command.find(';') != std::string::npos) return true;
  if (command.find('`') != std::string::npos) return true;
  if (command.find("$(") != std::string::npos) return true;
  // File manipulation commands
  if (command.find("cp ") != std::string::npos) return true;
  if (command.find("mv ") != std::string::npos) return true;
  if (command.find("dd ") != std::string::npos) return true;
  if (command.find("chmod ") != std::string::npos) return true;
  if (command.find("chown ") != std::string::npos) return true;
  // Redirects and destructive commands
  return ((command.find('>') != std::string::npos) ||
          (command.find('<') != std::string::npos) ||
          (command.find("rm ") != std::string::npos));
}

//
// Join the two paths with a '/' separator
//
std::string join_path(const std::string &a, const std::string &b) {
  if (b.empty()) return a;
  std::string pa = a;
  if (!pa.empty() && pa.back() == '/') pa.pop_back();
  // Strip leading '/' from b to prevent absolute-path sandbox escape
  std::string pb = (b.front() == '/') ? b.substr(1) : b;
  if (pb.empty()) return pa;
  return pa + "/" + pb;
}

//
// unwrap() - Remove a matching outer "wrapper" from a string.
//
// Trims leading/trailing whitespace first, then checks (in order):
//
//  1. Same-character pairs   "..."  '...'  |...|  `...`
//  2. Mirror pairs           (...)  [...]  {...}
//  3. HTML-like tags         <tag>...</tag>
//  4. Plain angle brackets   <...>          (fallback if tags don't match)
//
// If none of the above apply, returns the whitespace-trimmed input unchanged.
//
// Examples:
//   unwrap("\"hello\"")        -> "hello"
//   unwrap("  [foo]  ")        -> "foo"
//   unwrap("<b>bold</b>")      -> "bold"
//   unwrap("<file>x</file>")   -> "x"
//   unwrap("<hello>")          -> "hello"
//   unwrap("plain")            -> "plain"
//   unwrap("")                 -> ""
//
std::string unwrap(const std::string &input) {
  if (input.empty()) {
    return input;
  }

  size_t left = 0;
  size_t right = input.length() - 1;

  while (left <= right && std::isspace(static_cast<unsigned char>(input[left]))) {
    left++;
  }
  while (left <= right && std::isspace(static_cast<unsigned char>(input[right]))) {
    right--;
  }

  if (left > right) {
    return "";
  }

  // Same-character pairs: "", '', ||, ``
  // Note: [], {} are NOT same-char pairs — they belong in mirror pairs only
  if (input[left] == input[right]) {
    if (input[left] == '"'  || input[left] == '\'' ||
        input[left] == '|'  || input[left] == '`') {
      return input.substr(left + 1, right - left - 1);
    }
  }

  // Mirror pairs: (), [], {}, but NOT <> (handled below as possible HTML tags)
  if (input[left] != input[right]) {
    if ((input[left] == '(' && input[right] == ')') ||
        (input[left] == '[' && input[right] == ']') ||
        (input[left] == '{' && input[right] == '}')) {
      return input.substr(left + 1, right - left - 1);
    }
  }

  // HTML-like tags: <tag>content</tag>
  // Also handles plain <...> as a fallback at the end
  if (input[left] == '<' && input[right] == '>') {
    // Find end of opening tag
    size_t openTagEnd = left + 1;
    while (openTagEnd <= right && input[openTagEnd] != '>') openTagEnd++;

    if (openTagEnd < right) {
      std::string openTagName = input.substr(left + 1, openTagEnd - left - 1);

      // Find start of closing tag (search backwards for '<')
      size_t closeTagStart = right;
      while (closeTagStart > openTagEnd && input[closeTagStart] != '<') closeTagStart--;

      if (closeTagStart > openTagEnd && input[closeTagStart + 1] == '/') {
        std::string closeTagName = input.substr(closeTagStart + 2, right - closeTagStart - 2);

        if (!openTagName.empty() && openTagName == closeTagName) {
          // Return content between the tags
          return input.substr(openTagEnd + 1, closeTagStart - openTagEnd - 1);
        }
      }
    }

    // Fallback: plain <...> with no matching HTML tags — unwrap the angle brackets
    return input.substr(left + 1, right - left - 1);
  }

  return input.substr(left, right - left + 1);
}

//
// Removes backticks from the source text for TOOL:WRITE
//
std::string strip_code_fences(const std::string &filename, const std::string &src) {
  auto ext = fs::path(filename).extension().string();
  bool is_code = std::ranges::any_of(CODE_EXTENSIONS, [&](const std::string &e){ return ext == e; });
  if (!is_code) {
    return unwrap(src);
  }
  auto pos = src.find("```");
  if (pos == std::string::npos) {
    return src;
  }
  auto nl = src.find('\n', pos + 3);
  if (nl == std::string::npos) {
    return src;
  }
  std::string inner = src.substr(nl + 1);
  auto end = inner.rfind("```");
  if (end != std::string::npos) {
    inner = inner.substr(0, end);
  }
  return inner;
}

//
// Path helpers
//
std::string resolve_path(const std::string &sandbox, const std::string &p) {
  if (p.empty() || p == ".") {
    return sandbox;
  }
  if (p.substr(0, 2) == "./") {
    return join_path(sandbox, p.substr(2));
  }
  if (p[0] == '/') {
    return p;
  }
  return join_path(sandbox, unwrap(p));
}

bool path_in_sandbox(const std::string &sandbox, const std::string &path) {
  std::error_code ec;
  auto base   = fs::canonical(sandbox, ec);  if (ec) return false;
  auto target = fs::weakly_canonical(path, ec);
  std::string bstr = base.string() + "/";
  std::string tstr = target.string();
  return tstr == base.string() || tstr.compare(0, bstr.size(), bstr) == 0;
}

bool path_allowed(const std::string &sandbox, const std::vector<std::string> &extra_dirs, const std::string &path) {
  if (path_in_sandbox(sandbox, path)) {
    return true;
  }
  for (const auto &dir : extra_dirs) {
    if (path_in_sandbox(dir, path)) {
      return true;
    }
  }
  return false;
}

//
// Tool argument splitting
//
ToolArgs split_tool_args(const std::string &rest) {
  ToolArgs r;
  if (rest.empty()) return r;

  if (rest[0] == '"' || rest[0] == '\'') {
    char q = rest[0];
    auto close = rest.find(q, 1);
    if (close != std::string::npos) {
      r.arg1 = rest.substr(0, close + 1);
      r.arg2 = rest.substr(close + 1);
      r.arg2.erase(0, r.arg2.find_first_not_of(" \t"));
      return r;
    }
    // No closing quote — treat whole string as arg1
    r.arg1 = rest;
    return r;
  }

  // Existing behaviour: split on first space or newline
  auto NL = rest.find('\n');
  auto SP = rest.find(' ');
  int sep;
  if (NL != std::string::npos && (SP == std::string::npos || NL < SP)) {
    sep = NL;
  } else {
    sep = SP;
  }
  if (sep == std::string::npos) {
    r.arg1 = rest;
  } else {
    r.arg1 = rest.substr(0, sep);
    r.arg2 = rest.substr(sep + 1);
  }
  return r;
}

//
// TOOL:SEARCH
//
SearchArgs parse_search_args(const std::string &arg1, const std::string &arg2) {
  SearchArgs result;
  result.pattern = arg1;
  result.path_and_flags = arg2;

  if (!result.pattern.empty() &&
      (result.pattern.front() == '"' || result.pattern.front() == '\'')) {
    char quote = result.pattern.front();
    result.pattern = result.pattern.substr(1);

    // closing quote may still be in pattern (single-token: "struct")
    if (!result.pattern.empty() && result.pattern.back() == quote) {
      result.pattern = result.pattern.substr(0, result.pattern.size() - 1);
    } else {
      auto qpos = result.path_and_flags.find(quote);
      if (qpos != std::string::npos) {
        result.pattern += result.path_and_flags.substr(0, qpos);
        result.path_and_flags = result.path_and_flags.substr(qpos + 1);
        result.path_and_flags.erase(0, result.path_and_flags.find_first_not_of(" \t"));
      } else {
        result.pattern += result.path_and_flags;
        result.path_and_flags = "";
      }
    }
  }

  return result;
}

SearchFlags parse_search_flags(const std::string &flags_str) {
  SearchFlags f;
  std::istringstream iss(flags_str);
  std::string token;
  while (iss >> token) {
    if (token == "--recursive") {
      f.recursive = true;
    } else if (token == "--line-numbers") {
      f.line_numbers = true;
    } else if (token == "--count") {
      f.count = true;
    } else if (token == "--line-count") {
      f.line_count = true;
    } else if (token == "--files-only") {
      f.files_only = true;
    } else if (token == "--name") {
      f.name = true;
    } else if (token.rfind("--include=", 0) == 0) {
      f.include_glob = token.substr(10);
    } else if (token == "--include") {
      iss >> f.include_glob;
    } else if (token.rfind("--context=", 0) == 0) {
      f.context = std::stoi(token.substr(10));
    } else if (token == "--context") {
      int val;
      if (iss >> val) f.context = val;
    } else if (token.rfind("--lines=", 0) == 0) {
      std::string val = token.substr(8);
      auto comma = val.find(',');
      if (comma != std::string::npos) {
        f.lines_start = std::stoi(val.substr(0, comma));
        f.lines_end = std::stoi(val.substr(comma + 1));
      } else {
        f.lines_start = std::stoi(val);
        f.lines_end = f.lines_start;
      }
    } else if (token == "--lines") {
      std::string val;
      if (iss >> val) {
        auto comma = val.find(',');
        if (comma != std::string::npos) {
          f.lines_start = std::stoi(val.substr(0, comma));
          f.lines_end = std::stoi(val.substr(comma + 1));
        } else {
          f.lines_start = std::stoi(val);
          f.lines_end = f.lines_start;
        }
      }
    }
  }
  return f;
}

bool local_glob_match(const std::string &str, const std::string &pattern, size_t si = 0, size_t pi = 0) {
  while (pi < pattern.size()) {
    if (pattern[pi] == '*') {
      while (pi < pattern.size() && pattern[pi] == '*') pi++;
      if (pi == pattern.size()) return true;
      for (size_t s = si; s <= str.size(); s++) {
        if (local_glob_match(str, pattern, s, pi)) return true;
      }
      return false;
    } else if (pattern[pi] == '?') {
      if (si >= str.size()) return false;
      si++; pi++;
    } else if (pattern[pi] == '[') {
      if (si >= str.size()) return false;
      size_t close = pattern.find(']', pi + 1);
      if (close == std::string::npos) {
        if (str[si] != '[') return false;
        si++; pi++;
      } else {
        bool negated = (pi + 1 < close && pattern[pi + 1] == '!');
        size_t start = negated ? pi + 2 : pi + 1;
        bool matched = false;
        for (size_t c = start; c < close; c++) {
          if (c + 2 < close && pattern[c + 1] == '-') {
            if (str[si] >= pattern[c] && str[si] <= pattern[c + 2]) { matched = true; break; }
            c += 2;
          } else {
            if (str[si] == pattern[c]) { matched = true; break; }
          }
        }
        if (negated) matched = !matched;
        if (!matched) return false;
        si++; pi = close + 1;
      }
    } else {
      if (si >= str.size() || str[si] != pattern[pi]) return false;
      si++; pi++;
    }
  }
  return si == str.size();
}

bool matches_include(const std::string &filename, const std::string &glob) {
  if (glob.empty()) return true;
  return local_glob_match(filename, glob);
}

std::string search_single_file(const std::string &filepath, const std::string &pattern, const SearchFlags &flags) {
  std::ifstream f(filepath);
  if (!f) return "";

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(f, line)) {
    lines.push_back(line);
  }

  // Determine effective line range (1-based → 0-based)
  size_t range_start = 0;
  size_t range_end = lines.size();
  if (flags.lines_start > 0) {
    range_start = static_cast<size_t>(std::max(1, flags.lines_start)) - 1;
    int end_val = (flags.lines_end > 0) ? flags.lines_end : flags.lines_start;
    range_end = static_cast<size_t>(std::min(end_val, static_cast<int>(lines.size())));
    if (range_start >= range_end) return "";
  }

  if (pattern.empty()) {
    // No pattern: if --lines specified, return those lines (sed-like)
    if (flags.lines_start > 0) {
      std::ostringstream oss;
      for (size_t i = range_start; i < range_end; i++) {
        if (flags.line_numbers) {
          oss << (i + 1) << ":" << lines[i] << "\n";
        } else {
          oss << lines[i] << "\n";
        }
      }
      return oss.str();
    }
    // --line-count with empty pattern: return total line count
    if (flags.line_count) {
      return std::to_string(lines.size());
    }
    return "";
  }

  // Split pattern on '|' for alternation support
  std::vector<std::string> sub_patterns;
  {
    std::string remaining = pattern;
    size_t pos;
    while ((pos = remaining.find('|')) != std::string::npos) {
      sub_patterns.push_back(remaining.substr(0, pos));
      remaining = remaining.substr(pos + 1);
    }
    sub_patterns.push_back(remaining);
  }

  auto line_matches = [&](const std::string &line) -> bool {
    for (const auto &sp : sub_patterns) {
      if (!sp.empty() && line.find(sp) != std::string::npos) return true;
    }
    return false;
  };

  std::ostringstream oss;
  int match_count = 0;
  size_t last_end = 0;

  for (size_t i = range_start; i < range_end; i++) {
    if (!line_matches(lines[i])) continue;

    match_count++;

    if (flags.files_only) {
      return filepath + "\n";
    }

    if (flags.count || flags.line_count) {
      continue;
    }

    size_t start = (flags.context > 0) ? (i >= static_cast<size_t>(flags.context) ? i - flags.context : 0) : i;
    size_t end = (flags.context > 0) ? std::min(range_end - 1, i + static_cast<size_t>(flags.context)) : i;
    // Clamp context to the --lines range
    if (flags.lines_start > 0) {
      start = std::max(start, range_start);
    }

    if (match_count > 1 && start > last_end) {
      oss << "--\n";
    }

    for (size_t j = start; j <= end; j++) {
      if (flags.line_numbers) {
        oss << (j + 1) << ":" << lines[j] << "\n";
      } else {
        oss << lines[j] << "\n";
      }
    }
    last_end = end;
  }

  if (flags.count || flags.line_count) {
    return match_count > 0 ? std::to_string(match_count) : "";
  }

  return oss.str();
}

std::string tool_search(const std::string &sandbox, const std::string &pattern, const std::string &path_arg, const std::string &flags_str, const std::vector<std::string> &extra_dirs) {
  SearchFlags flags = parse_search_flags(flags_str);
  std::string path = resolve_path(sandbox, path_arg);
  if (!path_allowed(sandbox, extra_dirs, path)) {
    return "ERROR: path outside sandbox";
  }

  std::error_code ec;
  if (fs::is_regular_file(path, ec)) {
    std::string result = search_single_file(path, pattern, flags);
    if (result.size() > 4096) {
      result = result.substr(0, 4096) + "\n…(truncated)";
    }
    return result.empty() ? "no matches" : result;
  }

  if (!fs::is_directory(path, ec)) {
    return "ERROR: path not found: " + path;
  }

  std::ostringstream oss;
  std::vector<std::string> files;

  if (flags.recursive) {
    for (const auto &entry : fs::recursive_directory_iterator(path, ec)) {
      if (entry.is_regular_file() && matches_include(entry.path().filename().string(), flags.include_glob)) {
        files.push_back(entry.path().string());
      }
    }
  } else {
    for (const auto &entry : fs::directory_iterator(path, ec)) {
      if (entry.is_regular_file() && matches_include(entry.path().filename().string(), flags.include_glob)) {
        files.push_back(entry.path().string());
      }
    }
  }

  // Sort: source files first, then text, then binary
  std::stable_sort(files.begin(), files.end(), [](const std::string &a, const std::string &b) {
    return file_category(a) < file_category(b);
  });

  int total_count = 0;
  for (const auto &file : files) {
    if (flags.name) {
      std::string filename = fs::path(file).filename().string();
      if (local_glob_match(filename, pattern)) {
        oss << file << "\n";
      }
    } else {
      std::string result = search_single_file(file, pattern, flags);
      if (!result.empty()) {
        if (flags.count) {
          total_count += std::stoi(result);
        } else if (flags.files_only) {
          oss << result;
        } else {
          oss << file << ":\n" << result;
        }
      }
    }
  }

  std::string output;
  if (flags.count) {
    output = std::to_string(total_count);
  } else {
    output = oss.str();
  }
  if (output.size() > 4096) {
    output = output.substr(0, 4096) + "\n…(truncated)";
  }
  return output.empty() ? "no matches" : output;
}

//
// TOOL:REPLACE
//
ReplaceArgs parse_replace_args(const std::string &arg2) {
  ReplaceArgs r;
  size_t pipe_pos;

  if (!arg2.empty() && (arg2[0] == '"' || arg2[0] == '\'')) {
    // Quoted find string: | inside quotes is literal
    char quote = arg2[0];
    auto close = arg2.find(quote, 1);
    if (close != std::string::npos && close + 1 < arg2.size() && arg2[close + 1] == '|') {
      r.find_str = arg2.substr(1, close - 1);
      pipe_pos = close + 1;
    } else {
      // No valid | after closing quote — fall back to first |
      pipe_pos = arg2.find('|');
      if (pipe_pos == std::string::npos) {
        return r;
      }
      r.find_str = arg2.substr(0, pipe_pos);
    }
  } else {
    pipe_pos = arg2.find('|');
    if (pipe_pos == std::string::npos) {
      return r;
    }
    r.find_str = arg2.substr(0, pipe_pos);
  }

  std::string rest = arg2.substr(pipe_pos + 1);
  auto flag_pos = rest.find(" --");
  if (flag_pos != std::string::npos) {
    r.replace_str = rest.substr(0, flag_pos);
    std::string flags = rest.substr(flag_pos);
    if (flags.find("--all") != std::string::npos) {
      r.all = true;
    }
    if (flags.find("--count") != std::string::npos) {
      r.count = true;
    }
  } else {
    r.replace_str = rest;
  }
  return r;
}

std::string tool_replace(const std::string &sandbox, const std::string &file, const std::string &arg2) {
  const auto path = resolve_path(sandbox, file);
  if (!path_in_sandbox(sandbox, path)) {
    return "ERROR: path outside sandbox";
  }
  auto args = parse_replace_args(arg2);
  if (args.find_str.empty()) {
    return "ERROR: empty find string";
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return "ERROR: cannot open [" + path + "]";
  }
  std::ostringstream oss;
  oss << f.rdbuf();
  std::string content = oss.str();
  if (content.size() > 32 * 1024) {
    return "ERROR: file too large";
  }
  size_t count = 0;
  size_t pos = 0;
  while ((pos = content.find(args.find_str, pos)) != std::string::npos) {
    count++;
    pos += args.find_str.size();
  }
  if (count == 0) {
    return "ERROR: pattern not found in " + file;
  }
  if (args.count) {
    return std::to_string(count) + " occurrence(s) found";
  }
  std::string modified = content;
  if (args.all) {
    pos = 0;
    while ((pos = modified.find(args.find_str, pos)) != std::string::npos) {
      modified.replace(pos, args.find_str.size(), args.replace_str);
      pos += args.replace_str.size();
    }
  } else {
    pos = modified.find(args.find_str);
    modified.replace(pos, args.find_str.size(), args.replace_str);
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return "ERROR: cannot write [" + path + "]";
  }
  out << modified;
  size_t replaced = args.all ? count : 1;
  return std::format("OK: replaced {} occurrence(s) in {}", replaced, file);
}

//
// Count display width (UTF-8 code points) of a string
//
static int display_width(const std::string &s) {
  int w = 0;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      i += 1;
    } else if (c < 0xE0) {
      i += 2;
    } else if (c < 0xF0) {
      i += 3;
    } else {
      i += 4;
    }
    w++;
  }
  return w;
}

//
// TOOL:TABLE
//
TableResult tool_table(const std::string &dims, const std::string &data) {
  TableResult result;

  // Parse RxC
  auto xpos = dims.find('x');
  if (xpos == std::string::npos) {
    result.error = "expected <R>x<C> format, got [" + dims + "]";
    return result;
  }
  int rows = std::stoi(dims.substr(0, xpos));
  int cols = std::stoi(dims.substr(xpos + 1));

  // Split data into rows
  std::vector<std::vector<std::string>> grid;
  std::istringstream ss(data);
  std::string line;
  while (std::getline(ss, line) && (int)grid.size() < rows) {
    std::vector<std::string> cells;
    std::istringstream ls(line);
    std::string cell;
    while (std::getline(ls, cell, '|')) {
      cells.push_back(utils::trim(cell));
    }
    grid.push_back(cells);
  }

  if (grid.empty()) {
    result.error = "no table data provided";
    return result;
  }

  // Compute max width per column
  std::vector<int> widths(cols, 0);
  for (const auto &row : grid) {
    for (int c = 0; c < cols && c < (int)row.size(); c++) {
      widths[c] = std::max(widths[c], display_width(row[c]));
    }
  }

  // Helper: build a horizontal border line
  auto border = [&](const std::string &left, const std::string &mid, const std::string &right) {
    std::ostringstream oss;
    oss << left;
    for (int c = 0; c < cols; c++) {
      for (int i = 0; i < widths[c] + 2; i++) oss << "\u2500";
      oss << ((c < cols - 1) ? mid : right);
    }
    return oss.str();
  };

  // Top border
  result.rows.push_back(border("\u250C", "\u252C", "\u2510"));

  // Rows
  for (int r = 0; r < (int)grid.size(); r++) {
    std::ostringstream oss;
    for (int c = 0; c < cols; c++) {
      oss << "\u2502 ";
      const std::string &val = (c < (int)grid[r].size()) ? grid[r][c] : "";
      oss << val;
      int pad = widths[c] - display_width(val);
      for (int i = 0; i < pad; i++) oss << ' ';
      oss << " ";
    }
    oss << "\u2502";
    result.rows.push_back(oss.str());
    // Separator after header
    if (r == 0) {
      result.rows.push_back(border("\u251C", "\u253C", "\u2524"));
    }
  }

  // Bottom border
  result.rows.push_back(border("\u2514", "\u2534", "\u2518"));
  return result;
}

//
// ThinkParser — think-tag state machine
//
ThinkParser::ThinkParser(bool thinking_enabled, bool is_gemma_4)
  : mode_(thinking_enabled ? Init : Thunk), is_gemma_4_(is_gemma_4) {}

std::string ThinkParser::assemble_tag(const std::vector<std::string> &tokens, size_t max_len) {
  if (tokens.empty() || tokens[0] != "<") {
    return "";
  }
  std::string tag = tokens[0];
  for (size_t i = 1; i < tokens.size(); i++) {
    if (tag.find('>') != std::string::npos) {
      break;
    }
    if (tag.size() >= max_len) {
      break;
    }
    tag += tokens[i];
  }
  return tag;
}

size_t ThinkParser::check_start(const std::string &buffer) {
  if (mode_ == Think) {
    return 0;
  }
  static const std::vector<std::string> tags = {
    "<think>",
    "<think|>",
    "<|channel>thought",
    "<|think|>"
  };
  for (const auto &tag : tags) {
    if (buffer.rfind(tag, 0) == 0) {
      mode_ = Think;
      return tag.size();
    }
  }
  return 0;
}

ThinkParser::EndResult ThinkParser::check_end(const std::string &buffer) {
  EndResult r;
  if (mode_ != Think) {
    return r;
  }

  static const std::vector<std::string> tags = {
    "</think>",
    "</think|>",
    "</think|\n\n",
    "</thought>",
    "</|think|>",
    "</|think|\n\n"
  };

  for (const auto &tag : tags) {
    auto pos = buffer.find(tag);
    if (pos != std::string::npos) {
      r.found = true;
      r.pos = pos;
      r.tag_len = tag.size();
      r.thought = buffer.substr(0, pos);
      if (is_gemma_4_) {
        r.clear_buffer = true;
      }
      mode_ = Thunk;
      return r;
    }
  }
  return r;
}

ThinkParser::Mode ThinkParser::mode() const {
  return mode_;
}

void ThinkParser::set_mode(Mode m) {
  mode_ = m;
}

void ThinkParser::reset() {
  mode_ = is_gemma_4_ ? Thunk : Init;
}

//
// TOOL:RUN
//
std::string tool_run(HaliConfig &cfg, Tui &tui, const std::string &arg1, const std::string &arg2) {
  const std::string args = arg1 + " " + arg2;
  bool permitted = std::ranges::any_of(cfg.run_allowed_, [&](const std::string &a) {return a == args;});
  if (!permitted) {
    if (!tui.confirm_dialog(std::format("Allow {} {} to run?", arg1, arg2))) {
      return "ERROR: '" + args + "' is not permitted to run.";
    }
    cfg.run_allowed_.emplace_back(args);
    cfg.user_run_allowed_.add(args);
  }
  tui.show_tool("running: " + args);

  return run_with_spinner(tui, [args]() -> std::string {
    std::string out = run_shell_command(args);
    if (out.size() > 4096) {
      out = out.substr(0, 4096) + "\n…(truncated)";
    }
    return out;
  });
}

//
// Backup helper
//
void backup_file(const HaliConfig &cfg, Tui &tui, const std::string &path) {
  if (cfg.enabled_backups_) {
    const std::string backup_dir = cfg.sandbox_ + "/.hali/backups";
    const auto confirm = tool_write_backup(backup_dir, path);
    tui.show_tool("backup: " + confirm);
  }
}

//
// Broadcast reload to webview
//
void broadcast_reload(const HaliConfig &cfg, Tui &tui) {
  if (cfg.web_enabled_) {
    tui.show_tool("reload browser");
    webview::broadcast_reload();
  }
}
