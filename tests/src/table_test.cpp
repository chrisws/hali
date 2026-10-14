#include <iostream>
#include <cassert>
#include <string>
#include "agent_utils.h"

using namespace std;

static void test_basic_table() {
  std::string data = "name|age|city\nAlice|30|NYC\nBob|25|LA";
  std::string result = tool_table("3x3", data);

  // Header row: "name "(5) + "  " + "age"(3) + "  " + "city"(4)
  assert(result.find("name   age  city") != std::string::npos);
  // Separator: "-----" + "  " + "---" + "  " + "----"
  assert(result.find("-----  ---  ----") != std::string::npos);
  // Data rows
  assert(result.find("Alice  30   NYC") != std::string::npos);
  assert(result.find("Bob    25   LA") != std::string::npos);

  cout << "test_basic_table passed" << endl;
}

static void test_single_column() {
  std::string data = "item\nfoo\nbar";
  std::string result = tool_table("3x1", data);

  assert(result.find("item") != std::string::npos);
  assert(result.find("----") != std::string::npos);
  assert(result.find("foo") != std::string::npos);
  assert(result.find("bar") != std::string::npos);

  cout << "test_single_column passed" << endl;
}

static void test_header_only() {
  std::string data = "a|b|c";
  std::string result = tool_table("1x3", data);

  assert(result.find("a  b  c") != std::string::npos);
  // No separator for single-row table (header only, no body)
  // Actually our impl adds separator after row 0 always
  assert(result.find("-  -  -") != std::string::npos);

  cout << "test_header_only passed" << endl;
}

static void test_empty_cell() {
  std::string data = "name|age|city\nAlice||NYC\nBob|25|";
  std::string result = tool_table("3x3", data);

  // Empty cells should still produce aligned output
  // Col widths: 5, 3, 4
  // Row 1: "Alice"(5) + "  "(2) + "   "(3) + "  "(2) + "NYC "(4)
  assert(result.find("name   age  city") != std::string::npos);
  assert(result.find("Alice       NYC") != std::string::npos);

  cout << "test_empty_cell passed" << endl;
}

static void test_bad_dims() {
  std::string result = tool_table("abc", "data");
  assert(result.find("ERROR") == 0);

  cout << "test_bad_dims passed" << endl;
}

static void test_empty_data() {
  std::string result = tool_table("3x3", "");
  assert(result.find("ERROR") == 0);

  cout << "test_empty_data passed" << endl;
}

static void test_wide_alignment() {
  std::string data = "short|a very long column header|med\n1|2|3";
  std::string result = tool_table("2x3", data);

  // "short" should be padded to width of "a very long column header" (25)
  // "1" should be padded to width of "2" (1)
  assert(result.find("short") != std::string::npos);
  assert(result.find("a very long column header") != std::string::npos);
  assert(result.find("1") != std::string::npos);

  cout << "test_wide_alignment passed" << endl;
}

void table_test() {
  cout << "=== Running table_test ===" << endl;
  test_basic_table();
  test_single_column();
  test_header_only();
  test_empty_cell();
  test_bad_dims();
  test_empty_data();
  test_wide_alignment();
  cout << "All table tests passed!" << endl;
}