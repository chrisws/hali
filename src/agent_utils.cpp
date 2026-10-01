// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <filesystem>
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
  return ((command.find('>') != std::string::npos) ||
          (command.find('<') != std::string::npos) ||
          (command.find("rm ") != std::string::npos));
}

//
// Join the two paths with a '/' separator
//
std::string join_path(const std::string &a, const std::string &b) {
  if (b.empty()) return a;
  if (b[0] == '/') return b;
  std::string pa = a;
  if (!pa.empty() && pa.back() == '/') pa.pop_back();
  std::string pb = (b.front() == '/') ? b.substr(1) : b;
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
