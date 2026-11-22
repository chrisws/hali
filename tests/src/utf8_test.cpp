#include <cassert>
#include <cstdio>
#include "utf8.h"
#include "json_escape.h"

void utf8_test() {
  // Test ASCII
  {
    std::string input = "Hello";
    std::string out;
    auto it = input.begin();
    auto end = input.end();
    while (it != end) {
      char32_t cp = utf8::next(it, end);
      utf8::append(cp, out);
    }
    assert(out == "Hello");
  }

  // Test peek_next doesn't advance
  {
    std::string input = "A\342\200\224";  // A + em-dash (U+2014)
    auto it = input.begin();
    auto end = input.end();
    char32_t cp = utf8::peek_next(it, end);
    assert(cp == U'A');
    // it should not have moved
    assert(*it == 'A');
    cp = utf8::next(it, end);
    assert(cp == U'A');
    cp = utf8::peek_next(it, end);
    assert(cp == U'\u2014');
    cp = utf8::next(it, end);
    assert(cp == U'\u2014');
    assert(it == end);
  }

  // Test emoji (4-byte)
  {
    std::string emoji = "\360\237\220\201";  // U+1F401 (smiling cat)
    std::string out;
    utf8::append(U'\U0001F401', out);
    assert(out == emoji);
    auto it = out.begin();
    auto end = out.end();
    char32_t cp = utf8::next(it, end);
    assert(cp == U'\U0001F401');
    assert(it == end);
  }

  // Test box characters (3-byte)
  {
    std::string box = "\342\224\200";  // U+2500 BOX DRAWINGS LIGHT HORIZONTAL
    std::string s;
    utf8::append(U'\u2500', s);
    std::string out;
    utf8::append(U'\u2500', out);
    assert(out == box);
  }

  // Test em-dash
  {
    std::string out;
    utf8::append(U'\u2014', out);
    assert(out == "\342\200\224");
  }

  // Test invalid: stray continuation byte
  {
    std::string bad = "\341";
    auto it = bad.begin();
    auto end = bad.end();
    char32_t cp = utf8::next(it, end);
    assert(cp == U'\uFFFD');
  }

  // Test truncated sequence
  {
    std::string bad = "\342\200";
    auto it = bad.begin();
    auto end = bad.end();
    char32_t cp = utf8::next(it, end);
    assert(cp == U'\uFFFD');
  }


  // --- json_escape_utf8 tests ---
  {
    // ASCII passthrough
    std::string out;
    json_escape_utf8(out, "Hello");
    assert(out == "Hello");
  }
  {
    // JSON special characters
    std::string out;
    json_escape_utf8(out, "a\"b\\c\nd\re\tf");
    assert(out == "a\\\"b\\\\c\\nd\\re\\tf");
  }
  {
    // 2-byte UTF-8: "Ġ" (U+0120) — the original bug
    // Must produce \u0120, NOT \u00c4\u00a0
    std::string out;
    json_escape_utf8(out, "\304\240");  // UTF-8 for U+0120
    assert(out == "\\u0120");
  }
  {
    // 3-byte UTF-8: em-dash (U+2014)
    std::string out;
    json_escape_utf8(out, "\342\200\224");
    assert(out == "\\u2014");
  }
  {
    // 4-byte UTF-8: U+1F401 (smiling cat) → surrogate pair
    std::string out;
    json_escape_utf8(out, "\360\237\220\201");
    assert(out == "\\ud83d\\udc01");
  }
  {
    // Mixed: ASCII + 2-byte + special
    std::string out;
    json_escape_utf8(out, "a\304\240b\"c");
    assert(out == "a\\u0120b\\\"c");
  }
  {
    // Control character (U+0001)
    std::string out;
    json_escape_utf8(out, std::string(1, '\x01'));
    assert(out == "\\u0001");
  }
  {
    // Empty string
    std::string out;
    json_escape_utf8(out, "");
    assert(out == "");
  }
  {
    // Invalid UTF-8: stray continuation byte → U+FFFD
    std::string out;
    json_escape_utf8(out, std::string(1, '\x81'));
    assert(out == "\\ufffd");
  }
  {
    // Truncated 2-byte sequence → U+FFFD
    std::string out;
    json_escape_utf8(out, "\304");
    assert(out == "\\ufffd");
  }

  std::puts("all tests passed");
}
