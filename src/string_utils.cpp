// This file is part of Nitro
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cctype>
#include <cstdint>

#include "utf8.h"
#include "string_utils.h"

static bool is_space(const char32_t cp) {
  return std::isspace(static_cast<unsigned char>(cp));
}

namespace utils {

std::string trim(const std::string_view str) {
  constexpr std::string_view whitespace = " \t\n\r\f\v";

  // Find the first non-whitespace character
  const auto start = str.find_first_not_of(whitespace);
  if (start == std::string_view::npos) {
    return ""; // The string is entirely whitespace
  }

  // Find the last non-whitespace character
  const auto end = str.find_last_not_of(whitespace);

  // Return the substring between start and end
  return std::string(str.substr(start, end - start + 1));
}

std::vector<std::string> split_utf8_string(const std::string &input, size_t max_chars_per_segment) {
  std::vector<std::string> result;

  //
  // cats and \ndogsrabbits  | line break
  // catssandssdogsrabbits   | no space to break
  // cats and dogsrabbits    | break on space
  // cats and dogs rabbits   | break on space
  // [----^---^----^--]
  //               ^  ^-- pending
  //               |----- current
  std::string current;
  std::string pending;

  unsigned current_count = 0;
  unsigned pending_count = 0;
  unsigned back_count = 0;
  auto it = input.begin();
  const auto end = input.end();

  auto clear_pending = [&]() -> void {
    pending.clear();
    pending_count = 0;
  };

  auto clear_current = [&]() -> void {
    current.clear();
    current_count = 0;
  };

  auto append_result = [&](const std::string &str, int count) -> void {
    result.push_back(str);
    back_count = count;
  };

  auto push_text = [&]() -> void {
    current.append(pending);
    if (!is_blank(current)) {
      append_result(current, current_count + pending_count);
    }
    clear_pending();
    clear_current();
  };

  while (it != end) {
    char32_t code_point;
    try {
      code_point = utf8::next(it, end);
    } catch(const utf8::exception& utfcpp_ex) {
      break;
    }
    if (code_point == '\n' || code_point == '\r') {
      push_text();
      back_count = max_chars_per_segment;
      continue;
    }
    utf8::append(code_point, pending);
    ++pending_count;
    if (pending_count + current_count >= max_chars_per_segment) {
      if (is_space(code_point)) {
        push_text();
      } else if (is_blank(current)) {
        append_result(pending, pending_count);
        clear_pending();
      } else if (it != end && is_space(utf8::peek_next(it, end))) {
        // both current and pending fit inside the row
        push_text();
      } else {
        // wrap pending onto the next line
        append_result(current, current_count);
        clear_current();
      }
      if (it != end && is_space(utf8::peek_next(it, end))) {
        // discard leading whitespace on next line
        utf8::next(it, end);
      }
    } else if (is_space(code_point)) {
      current.append(pending);
      current_count += pending_count;
      clear_pending();
    }
  }
  if (result.empty()) {
    push_text();
  } else {
    current.append(pending);
    if (!is_blank(current)) {
      if (back_count + pending_count + current_count >= max_chars_per_segment) {
        append_result(current, pending_count + current_count);
      } else {
        result.back() += " " + current;
      }
    }
  }
  return result;
}

}
