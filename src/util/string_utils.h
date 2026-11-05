// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>
#include <string_view>
#include <vector>

namespace utils {
  //
  // Check if string starts with a prefix
  //
  inline bool starts_with(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
  }

  //
  // Check if string ends with a suffix
  //
  inline bool ends_with(const std::string &s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  //
  // Check if string contains only whitespace
  //
  inline bool is_blank(const std::string &s) {
    for (char c : s) if (!isspace(static_cast<unsigned char>(c))) return false;
    return true;
  }

  //
  // Helper: is the character a word character?
  //
  static bool is_word_char(const unsigned char c) {
    return isalnum(c) || c == '_';
  }

  //
  // Trims whitespace from both ends of a string
  //
  std::string trim(std::string_view str);

  //
  // Split a UTF-8 string into segments based on word boundaries or newlines.
  //
  // @param input The UTF-8 encoded string to split
  // @param max_chars Maximum number of characters per segment (before checking word boundary)
  // @return Vector of string segments
  //
  std::vector<std::string> split_utf8_string(const std::string &input, size_t max_rows_segment);

  //
  // Strip HTML markup, decode basic entities, collapse whitespace.
  // Removes <style>, <script>, <table> blocks entirely,
  // then all remaining tags, then decodes entities.
  // Operates in-place on the provided string.
  //
  void strip_html(std::string &html);

  //
  // Join two path strings with a '/' separator.
  // Strips trailing '/' from a, leading '/' from b (prevents absolute escape).
  //
  std::string join_path(const std::string &a, const std::string &b);

  //
  // Remove a matching outer "wrapper" from a string.
  // Handles: "..." '...' |...| `...`  (...) [...] {...}  <tag>...</tag>  <...>
  //
  std::string unwrap(const std::string &input);

  //
  // Resolve a path relative to a sandbox root.
  //
  std::string resolve_path(const std::string &sandbox, const std::string &p);

  //
  // Check if a path is within a given directory (canonical comparison).
  //
  bool path_in_sandbox(const std::string &sandbox, const std::string &path);

  //
  // Check if a path is within the sandbox or any of the extra allowed dirs.
  //
  bool path_allowed(const std::string &sandbox, const std::vector<std::string> &extra_dirs, const std::string &path);

} // namespace utils


#endif