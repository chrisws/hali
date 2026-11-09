// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
// Unit tests for the HALI_END_TOOL guard logic in Agent::invoke_tool

#include <cassert>
#include <cstdio>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
  do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
  } while (0)

// Mirror of the static helper in agent.cpp
static bool is_valid_tool_call(const std::string &buffer) {
  return buffer.find("HALI_END_TOOL") != std::string::npos;
}

// --- test cases -----------------------------------------------------------

static bool test_valid_tool_call_with_end_marker() {
  std::string buf = "TOOL:WRITE foo.txt hello\nHALI_END_TOOL";
  assert(is_valid_tool_call(buf));
  return true;
}

static bool test_valid_tool_call_end_marker_at_start() {
  std::string buf = "HALI_END_TOOL\nTOOL:READ bar.txt";
  assert(is_valid_tool_call(buf));
  return true;
}

static bool test_prose_mention_without_end_marker() {
  std::string buf = "You can use TOOL:WRITE to create files in the sandbox.";
  assert(!is_valid_tool_call(buf));
  return true;
}

static bool test_empty_buffer() {
  std::string buf = "";
  assert(!is_valid_tool_call(buf));
  return true;
}

static bool test_multiple_end_markers() {
  std::string buf = "HALI_END_TOOL extra HALI_END_TOOL";
  assert(is_valid_tool_call(buf));
  return true;
}

static bool test_end_marker_as_substring() {
  // "HALI_END_TOOL" appearing as part of a larger token should still match
  std::string buf = "xHALI_END_TOOLx";
  assert(is_valid_tool_call(buf));
  return true;
}

static bool test_tool_name_only_no_marker() {
  std::string buf = "TOOL:LIST";
  assert(!is_valid_tool_call(buf));
  return true;
}

static bool test_newline_before_end_marker() {
  std::string buf = "TOOL:READ file.txt\nHALI_END_TOOL";
  assert(is_valid_tool_call(buf));
  return true;
}

static bool test_whitespace_only() {
  std::string buf = "   \n\t  ";
  assert(!is_valid_tool_call(buf));
  return true;
}

// --- entry point -------------------------------------------------------------

void agent_tool_guard_test() {
  printf("\nAgent HALI_END_TOOL guard tests\n");
  printf("================================\n");

  TEST(valid_tool_call_with_end_marker);
  TEST(valid_tool_call_end_marker_at_start);
  TEST(prose_mention_without_end_marker);
  TEST(empty_buffer);
  TEST(multiple_end_markers);
  TEST(end_marker_as_substring);
  TEST(tool_name_only_no_marker);
  TEST(newline_before_end_marker);
  TEST(whitespace_only);

  printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  assert(tests_passed == tests_run);
}

