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
#include <memory>

#include "agent_utils.h"

namespace fs = std::filesystem;

//
// handling for strip_code_fences
//
static const std::vector<std::string> CODE_EXTENSIONS = {
  ".py",".c",".cpp",".h",".bas",".java",".html",".js",".ts",
  ".json",".yaml",".toml",".sh",".go",".rs",".jsx",".tsx",".xml"
};

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

//
// TOOL:SEARCH
//
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
    } else if (token.rfind("--include=", 0) == 0) {
      f.include_glob = token.substr(10);
    } else if (token.rfind("--context=", 0) == 0) {
      f.context = std::stoi(token.substr(10));
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
    }
  }
  return f;
}

bool matches_include(const std::string &filename, const std::string &glob) {
  if (glob.empty()) return true;
  if (glob.size() >= 2 && glob[0] == '*' && glob[1] == '.') {
    std::string ext = glob.substr(1);
    return filename.size() >= ext.size() &&
           filename.compare(filename.size() - ext.size(), ext.size(), ext) == 0;
  }
  return filename == glob;
}

bool has_shell_metachars(const std::string &pattern) {
  return pattern.find(';') != std::string::npos ||
         pattern.find('|') != std::string::npos ||
         pattern.find('&') != std::string::npos ||
         pattern.find('`') != std::string::npos ||
         pattern.find("$(") != std::string::npos ||
         pattern.find('>') != std::string::npos ||
         pattern.find('<') != std::string::npos;
}

std::string search_single_file(const std::string &filepath, const std::string &pattern, const SearchFlags &flags) {
  std::ifstream f(filepath);
  if (!f) return "";

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(f, line)) {
    lines.push_back(line);
  }

  if (flags.line_count) {
    return std::to_string(lines.size());
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
    // No pattern: if --lines specified, return those lines (sed-like); else ""
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
    return "";
  }

  std::ostringstream oss;
  int match_count = 0;
  size_t last_end = 0;

  for (size_t i = range_start; i < range_end; i++) {
    if (lines[i].find(pattern) == std::string::npos) continue;

    match_count++;

    if (flags.files_only) {
      return filepath + "\n";
    }

    if (flags.count) {
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

  if (flags.count) {
    return std::to_string(match_count);
  }

  return oss.str();
}

std::string tool_search(const std::string &sandbox, const std::string &pattern, const std::string &path_arg, const std::string &flags_str) {
  if (has_shell_metachars(pattern)) {
    return "ERROR: invalid pattern";
  }

  SearchFlags flags = parse_search_flags(flags_str);
  std::string path = resolve_path(sandbox, path_arg);
  if (!path_in_sandbox(sandbox, path)) {
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

  for (const auto &file : files) {
    std::string result = search_single_file(file, pattern, flags);
    if (!result.empty()) {
      if (flags.files_only || flags.count) {
        oss << result;
      } else {
        oss << file << ":\n" << result;
      }
    }
  }

  std::string output = oss.str();
  if (output.size() > 4096) {
    output = output.substr(0, 4096) + "\n…(truncated)";
  }
  return output.empty() ? "no matches" : output;
}
