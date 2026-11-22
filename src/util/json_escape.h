// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef JSON_ESCAPE_H
#define JSON_ESCAPE_H

#include <cstdio>
#include <string>

#include "util/utf8.h"

// Escape a UTF-8 string for JSON output.
// Decodes multi-byte UTF-8 sequences to code points before emitting
// \uXXXX escapes, so e.g. "Ġ" (U+0120) becomes "\u0120" not "\u00c4\u00a0".
inline void json_escape_utf8(std::string &out, const std::string &s) {
  auto it = s.begin();
  const auto end = s.end();
  while (it != end) {
    char32_t cp = utf8::next(it, end);
    switch (cp) {
    case U'"':  out += "\\\""; break;
    case U'\\': out += "\\\\"; break;
    case U'\n': out += "\\n";  break;
    case U'\r': out += "\\r";  break;
    case U'\t': out += "\\t";  break;
    default:
      if (cp < 0x20 || cp > 0x7E) {
        if (cp <= 0xFFFF) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned) cp);
          out += buf;
        } else {
          // UTF-16 surrogate pair for U+10000..U+10FFFF
          cp -= 0x10000;
          char buf[16];
          snprintf(buf, sizeof(buf), "\\u%04x\\u%04x",
                   0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
          out += buf;
        }
      } else {
        out += static_cast<char>(cp);
      }
      break;
    }
  }
}

#endif