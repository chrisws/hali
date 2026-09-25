// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace utils {
  // Check if string starts with a prefix
  inline bool starts_with(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
  }

  // Check if string contains only whitespace
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

} // namespace utils

