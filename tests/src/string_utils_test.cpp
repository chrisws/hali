#include <iostream>
#include <cassert>
#include <string>
#include <string_view>
#include "string_utils.h"

using namespace std;
using namespace utils;

// Test starts_with function
static void test_starts_with() {
  // Basic prefix matches
  assert(starts_with("hello world", "hello") == true);
  assert(starts_with("hello", "hello") == true);
  assert(starts_with("123abc", "123") == true);

  // No prefix match
  assert(starts_with("hello world", "world") == false);
  assert(starts_with("hello", "hello world") == false);
  assert(starts_with("abc", "abcd") == false);

  // Empty prefix
  assert(starts_with("hello", "") == true);
  assert(starts_with("", "") == true);

  // Empty string
  assert(starts_with("", "hello") == false);

  // Case sensitive
  assert(starts_with("Hello", "hello") == false);
  assert(starts_with("HELLO", "hello") == false);

  cout << "test_starts_with passed" << endl;
}

// Test is_blank function
static void test_is_blank() {
  // Whitespace only
  assert(is_blank("   ") == true);
  assert(is_blank("\t\t") == true);
  assert(is_blank("\n\n") == true);
  assert(is_blank(" \t\n\r\f\v") == true);

  // Empty string
  assert(is_blank("") == true);

  // Non-whitespace
  assert(is_blank("hello") == false);
  assert(is_blank("a") == false);
  assert(is_blank("123") == false);

  // Mixed (should be false)
  assert(is_blank("  hello  ") == false);
  assert(is_blank("hello world") == false);

  cout << "test_is_blank passed" << endl;
}

// Test is_word_char function
static void test_is_word_char() {
  // Alphanumeric characters
  assert(is_word_char('a') == true);
  assert(is_word_char('z') == true);
  assert(is_word_char('A') == true);
  assert(is_word_char('Z') == true);
  assert(is_word_char('0') == true);
  assert(is_word_char('9') == true);

  // Underscore
  assert(is_word_char('_') == true);

  // Non-word characters
  assert(is_word_char(' ') == false);
  assert(is_word_char('-') == false);
  assert(is_word_char('.') == false);
  assert(is_word_char('@') == false);
  assert(is_word_char('$') == false);

  cout << "test_is_word_char passed" << endl;
}

// Test trim function
static void test_trim() {
  // Leading and trailing whitespace
  assert(trim("  hello  ") == "hello");
  assert(trim("\thello\t") == "hello");
  assert(trim("\nhello\n") == "hello");

  // Only leading whitespace
  assert(trim("   hello") == "hello");

  // Only trailing whitespace
  assert(trim("hello   ") == "hello");

  // No whitespace
  assert(trim("hello") == "hello");

  // Empty string
  assert(trim("") == "");

  // All whitespace
  assert(trim("   \t\n  ") == "");

  // Mixed whitespace
  assert(trim(" \t  hello  \n\r\t ") == "hello");

  cout << "test_trim passed" << endl;
}

// Test trim with string_view input
static void test_trim_string_view() {
  std::string_view sv = "  test  ";
  assert(trim(sv) == "test");

  std::string_view sv2 = "\t\t";
  assert(trim(sv2) == "");

  std::string_view sv3 = "no spaces";
  assert(trim(sv3) == "no spaces");

  cout << "test_trim_string_view passed" << endl;
}

// Test helper macro
#define TEST(name) void name()
#define RUN_TEST(name) do {                         \
    std::cout << "Running " << #name << "... ";     \
    try { name(); std::cout << "PASSED\n"; }        \
    catch (const std::exception& e) {               \
      std::cout << "FAILED: " << e.what() << "\n";  \
    }                                               \
  } while(0)

TEST(test_split_utf8_string_basic) {
  std::string input = "Hello world this is a test";
  auto segments = split_utf8_string(input, 5);

  assert(segments.size() == 5);
  assert(segments[0] == "Hello");
  assert(segments[1] == "world");
  assert(segments[2] == "this ");
  assert(segments[3] == "is a ");
  assert(segments[4] == "test");
}

TEST(test_split_utf8_string_with_newline) {
  std::string input = "Hello\nworld\ntest";
  auto segments = split_utf8_string(input, 10);

  assert(segments.size() == 3);
  assert(segments[0] == "Hello");
  assert(segments[1] == "world");
  assert(segments[2] == "test");
}

TEST(test_split_utf8_string_empty) {
  std::string input = "";
  auto segments = split_utf8_string(input, 5);

  assert(segments.size() == 0);
}

TEST(test_split_utf8_string_multibyte) {
  // Test with UTF-8 multi-byte characters (Cyrillic)
  std::string input = "Привет мир";
  auto segments = split_utf8_string(input, 3);

  // Each Cyrillic character is 2 bytes in UTF-8
  assert(segments.size() == 3);
  assert(segments[0] == "При");
  assert(segments[1] == "вет");
  assert(segments[2] == "мир");
}

TEST(test_split_utf8_string_unicode) {
  // Test with emoji (4-byte UTF-8)
  std::string input = "Hello 😀 world 🌍";
  auto segments = split_utf8_string(input, 5);

  assert(segments.size() == 4);
  assert(segments[0] == "Hello");
  assert(segments[1] == "😀 ");
  assert(segments[2] == "world");
  assert(segments[3] == "🌍");
}

TEST(test_split_utf8_string_unicode_prefix) {
  std::string PREFIX_4 = " 🤔 ▏";
  std::string CHARS_42 = "this is a line of text blah blah blah blah";
  std::string input = PREFIX_4 + CHARS_42 + " WRAP!";
  auto segments = split_utf8_string(input, 46);
  assert(segments.size() == 2);
  assert(segments[1] == "WRAP!");
}

TEST(test_split_utf8_string_newline_unicode) {
  // Test with newline in UTF-8 string
  std::string input = "Hello\nПривет\nworld";
  auto segments = split_utf8_string(input, 10);

  assert(segments.size() == 3);
  assert(segments[0] == "Hello");
  assert(segments[1] == "Привет");
  assert(segments[2] == "world");
}

TEST(test_split_utf8_string_word_boundary) {
  std::string input = "Hello world! This is a test.";
  //                   123456789012
  auto segments = split_utf8_string(input, 12);

  // Should break at word boundaries
  assert(segments.size() >= 2);
  assert(segments[0] == "Hello world!");
}

TEST(test_split_utf8_string_punctuation_boundary) {
  std::string input = "Hello, world!";
  auto segments = split_utf8_string(input, 5);

  assert(segments.size() == 4);
  assert(segments[0] == "Hello");
  assert(segments[1] == ", ");
  assert(segments[2] == "world");
  assert(segments[3] == "!");
}

TEST(test_split_utf8_string_preserves_content) {
  std::string input = "Hello world! This is a test string.";
  auto segments = split_utf8_string(input, 10);

  std::string reconstructed;
  for (const auto& seg : segments) {
    reconstructed += seg + " ";
  }

  // Should contain all original characters
  assert(reconstructed.find("Hello") != std::string::npos);
  assert(reconstructed.find("world") != std::string::npos);
  assert(reconstructed.find("test") != std::string::npos);
}

TEST(test_split_utf8_string_edge_cases) {
  // Only newlines
  std::string input = "\n\n\n";
  auto segments = split_utf8_string(input, 5);
  assert(segments.size() == 0);

  // Single character
  std::string input2 = "a";
  auto segments2 = split_utf8_string(input2, 5);
  assert(segments2.size() == 1);
  assert(segments2[0] == "a");
}

TEST(test_split_utf8_string_crlf) {
  std::string input = "Line1\r\nLine2\r\nLine3";
  auto segments = split_utf8_string(input, 10);

  assert(segments.size() == 3);
  assert(segments[0] == "Line1");
  assert(segments[1] == "Line2");
  assert(segments[2] == "Line3");
}

TEST(test_split_utf8_string_wrap) {
  std::string input = " 🤔 ▏this is a long line of text that should only wrap to at most 3 segments given the input length";
  for (int i = 100; i > 10; i-= 1) {
    auto segments = split_utf8_string(input, i);
    fprintf(stderr, "at %d have %lu segments\n", i, segments.size());
    for (int j = 0; j < segments.size(); j++) {
      fprintf(stderr, "   %d %s\n", j, segments[j].c_str());
    }
  }
}

void run_split_tests() {
  std::cout << "=== Running string_utils_2 Unit Tests ===\n\n";

  RUN_TEST(test_split_utf8_string_word_boundary);
  RUN_TEST(test_split_utf8_string_punctuation_boundary);
  RUN_TEST(test_split_utf8_string_basic);
  RUN_TEST(test_split_utf8_string_unicode);
  RUN_TEST(test_split_utf8_string_unicode_prefix);  
  RUN_TEST(test_split_utf8_string_with_newline);
  RUN_TEST(test_split_utf8_string_empty);
  RUN_TEST(test_split_utf8_string_multibyte);
  RUN_TEST(test_split_utf8_string_newline_unicode);
  RUN_TEST(test_split_utf8_string_preserves_content);
  RUN_TEST(test_split_utf8_string_edge_cases);
  RUN_TEST(test_split_utf8_string_crlf);
  RUN_TEST(test_split_utf8_string_wrap);

  std::cout << "\n=== All tests completed ===\n";
}

void string_utils_test() {
  test_starts_with();
  test_is_blank();
  test_is_word_char();
  test_trim();
  test_trim_string_view();
  run_split_tests();
  cout << "\nAll string_utils tests passed!\n" << endl;
}


