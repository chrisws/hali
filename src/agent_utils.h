// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>
#include <vector>

bool hasDangerousPatterns(const std::string &command, const std::vector<std::string> &run_allowed);
std::string join_path(const std::string &a, const std::string &b);
std::string unwrap(const std::string &input);
std::string strip_code_fences(const std::string &filename, const std::string &src);

//
// Path helpers
//
std::string resolve_path(const std::string &sandbox, const std::string &p);
bool path_in_sandbox(const std::string &sandbox, const std::string &path);

//
// TOOL:SEARCH
//
struct SearchFlags {
  bool recursive = false;
  bool line_numbers = false;
  bool count = false;
  bool line_count = false;
  bool files_only = false;
  std::string include_glob;
  int context = 0;
  int lines_start = 0;
  int lines_end = 0;
};

SearchFlags parse_search_flags(const std::string &flags_str);
bool matches_include(const std::string &filename, const std::string &glob);
bool has_shell_metachars(const std::string &pattern);
std::string search_single_file(const std::string &filepath, const std::string &pattern, const SearchFlags &flags);
std::string tool_search(const std::string &sandbox, const std::string &pattern, const std::string &path_arg, const std::string &flags_str);
