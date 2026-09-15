#pragma once

#include <cstdint>
#include <iterator>
#include <string>

namespace utf8 {

// Returns U+FFFD (replacement character) for invalid input.

// Decodes the next UTF-8 code point starting at [it, end),
// advancing it past the consumed bytes.
template <typename It>
char32_t next(It &it, const It &end) {
  if (it == end) {
    return U'\uFFFD';
  }
  unsigned char c0 = static_cast<unsigned char>(*it);

  // 0xxxxxxx – ASCII
  if (c0 < 0x80) {
    ++it;
    return static_cast<char32_t>(c0);
  }
  // 10xxxxxx – stray continuation byte
  if (c0 < 0xC0) {
    ++it;
    return U'\uFFFD';
  }
  // 110xxxxx 10xxxxxx – 2-byte (U+0080..U+07FF)
  if (c0 < 0xE0) {
    if (it + 1 == end) { ++it; return U'\uFFFD'; }
    unsigned char c1 = static_cast<unsigned char>(*(it + 1));
    if ((c1 & 0xC0) != 0x80) { ++it; return U'\uFFFD'; }
    char32_t cp = (static_cast<char32_t>(c0 & 0x1F) << 6)
                 | static_cast<char32_t>(c1 & 0x3F);
    if (cp < 0x80) { // overlong encoding
      it += 2;
      return U'\uFFFD';
    }
    it += 2;
    return cp;
  }
  // 1110xxxx 10xxxxxx 10xxxxxx – 3-byte (U+0800..U+FFFF)
  if (c0 < 0xF0) {
    if (it + 2 == end) { ++it; return U'\uFFFD'; }
    unsigned char c1 = static_cast<unsigned char>(*(it + 1));
    unsigned char c2 = static_cast<unsigned char>(*(it + 2));
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
      ++it;
      return U'\uFFFD';
    }
    char32_t cp = (static_cast<char32_t>(c0 & 0x0F) << 12)
                 | (static_cast<char32_t>(c1 & 0x3F) << 6)
                 | static_cast<char32_t>(c2 & 0x3F);
    if (cp < 0x800) { // overlong encoding
      it += 3;
      return U'\uFFFD';
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) { // UTF-16 surrogate
      it += 3;
      return U'\uFFFD';
    }
    it += 3;
    return cp;
  }
  // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx – 4-byte (U+10000..U+10FFFF)
  if (c0 < 0xF8) {
    if (it + 3 == end) { ++it; return U'\uFFFD'; }
    unsigned char c1 = static_cast<unsigned char>(*(it + 1));
    unsigned char c2 = static_cast<unsigned char>(*(it + 2));
    unsigned char c3 = static_cast<unsigned char>(*(it + 3));
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
      ++it;
      return U'\uFFFD';
    }
    char32_t cp = (static_cast<char32_t>(c0 & 0x07) << 18)
                 | (static_cast<char32_t>(c1 & 0x3F) << 12)
                 | (static_cast<char32_t>(c2 & 0x3F) << 6)
                 | static_cast<char32_t>(c3 & 0x3F);
    if (cp < 0x10000 || cp > 0x10FFFF) { // overlong or out of range
      it += 4;
      return U'\uFFFD';
    }
    it += 4;
    return cp;
  }
  // 11111xxx – invalid start byte
  ++it;
  return U'\uFFFD';
}

// Decodes the next UTF-8 code point from [it, end) WITHOUT advancing it.
template <typename It>
char32_t peek_next(It &it, const It &end) {
  It tmp = it;
  return next(tmp, end);
}

// Appends the UTF-8 byte sequence for code_point to text.
// code_point must be a valid scalar value (0..0x10FFFF, excluding surrogates).
inline void append(char32_t cp, std::string &text) {
  if (cp <= 0x7F) {
    text.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    text.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    text.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    text.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

} // namespace utf8