// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "utf8.h"
#include "string_utils.h"
#include <notcurses/notcurses.h>

static bool is_space(const char32_t cp) {
  return std::isspace(static_cast<unsigned char>(cp));
}

static char *codepoint_to_utf8_string(char32_t cp) {
  static char buf[5] = {};
  int len = 0;

  if (cp <= 0x7F) {
    buf[len++] = static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    buf[len++] = static_cast<char>(0xC0 | (cp >> 6));
    buf[len++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    buf[len++] = static_cast<char>(0xE0 | (cp >> 12));
    buf[len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[len++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0x10FFFF) {
    buf[len++] = static_cast<char>(0xF0 | (cp >> 18));
    buf[len++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    buf[len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[len++] = static_cast<char>(0x80 | (cp & 0x3F));
  }

  return buf;
}

static int column_width(char32_t cp) {
  int result;
  if (cp < 0x7F) {
    result = 1;
  } else {
    int width = ncstrwidth(codepoint_to_utf8_string(cp), nullptr, nullptr);
    result = width < 1 ? 1: width;
  }
  return result;
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
    char32_t code_point = utf8::next(it, end);
    if (code_point == '\n' || code_point == '\r') {
      push_text();
      back_count = max_chars_per_segment;
      continue;
    }
    utf8::append(code_point, pending);
    pending_count += column_width(code_point);
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

//
// Remove <prefix...>...</close> blocks from text (in-place)
//
static void remove_blocks(std::string &text,
                          const std::string &open_prefix,
                          const std::string &close_tag) {
  size_t pos = 0;
  while ((pos = text.find(open_prefix, pos)) != std::string::npos) {
    size_t tag_end = text.find('>', pos);
    if (tag_end == std::string::npos) {
      break;
    }
    size_t block_end = text.find(close_tag, tag_end + 1);
    if (block_end == std::string::npos) {
      text.erase(pos);
      break;
    }
    text.erase(pos, (block_end + close_tag.size()) - pos);
  }
}

//
// Strip HTML markup, decode basic entities, collapse whitespace
//
std::string strip_html(const std::string &html) {
  std::string text = html;

  remove_blocks(text, "<style", "</style>");
  remove_blocks(text, "<script", "</script>");
  remove_blocks(text, "<table", "</table>");

  std::string result;
  result.reserve(text.size() / 2);
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '<') {
      size_t end = text.find('>', i);
      if (end == std::string::npos) {
        break;
      }
      i = end;
      continue;
    }
    result += text[i];
  }

  std::string decoded;
  decoded.reserve(result.size());
  for (size_t i = 0; i < result.size(); i++) {
    if (result[i] == '&') {
      if (result.compare(i, 5, "&amp;") == 0)   { decoded += '&';    i += 4; continue; }
      if (result.compare(i, 4, "&lt;") == 0)    { decoded += '<';    i += 3; continue; }
      if (result.compare(i, 4, "&gt;") == 0)    { decoded += '>';    i += 3; continue; }
      if (result.compare(i, 6, "&quot;") == 0)  { decoded += '"';    i += 5; continue; }
      if (result.compare(i, 5, "&#39;") == 0)   { decoded += '\'';   i += 4; continue; }
      if (result.compare(i, 6, "&nbsp;") == 0)  { decoded += ' ';    i += 5; continue; }
    }
    decoded += result[i];
  }

  std::string out;
  out.reserve(decoded.size());
  bool in_space = false;
  for (char c : decoded) {
    if (isspace((unsigned char)c)) {
      if (!in_space && !out.empty()) {
        out += ' ';
      }
      in_space = true;
    } else {
      out += c;
      in_space = false;
    }
  }

  while (!out.empty() && isspace((unsigned char)out.front())) {
    out.erase(0, 1);
  }
  while (!out.empty() && isspace((unsigned char)out.back())) {
    out.pop_back();
  }

  return out;
}

}
