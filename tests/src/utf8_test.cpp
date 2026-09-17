#include <cassert>
#include <cstdio>
#include "utf8.h"

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

  std::puts("all tests passed");
}
