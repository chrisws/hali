#include <iostream>
#include <cassert>
#include <string>
#include <fstream>
#include <filesystem>
#include "agent_utils.h"

namespace fs = std::filesystem;
using namespace std;

#define RUN_TEST(name) do {                         \
    std::cout << "Running " << #name << "... ";     \
    try { name(); std::cout << "PASSED\n"; }        \
    catch (const std::exception& e) {               \
      std::cout << "FAILED: " << e.what() << "\n";  \
    }                                               \
  } while(0)

static const string SANDBOX = "/tmp/hali_test_replace_sandbox";

static void setup_sandbox() {
  fs::remove_all(SANDBOX);
  fs::create_directories(SANDBOX + "/src");
}

static void teardown_sandbox() {
  fs::remove_all(SANDBOX);
}

static void write_test_file(const string &path, const string &content) {
  ofstream out(path);
  out << content;
}

static string read_test_file(const string &path) {
  ifstream in(path);
  ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

//
// parse_replace_args tests
//

static void test_parse_replace_args_basic() {
  auto r = parse_replace_args("old|new");
  assert(r.find_str == "old");
  assert(r.replace_str == "new");
  assert(r.all == false);
  assert(r.count == false);
}

static void test_parse_replace_args_flags() {
  auto r = parse_replace_args("old|new --all");
  assert(r.find_str == "old");
  assert(r.replace_str == "new");
  assert(r.all == true);
  assert(r.count == false);

  auto r2 = parse_replace_args("old|new --count");
  assert(r2.count == true);
  assert(r2.all == false);

  auto r3 = parse_replace_args("old|new --all --count");
  assert(r3.all == true);
  assert(r3.count == true);
}

static void test_parse_replace_args_no_pipe() {
  auto r = parse_replace_args("no-pipe-here");
  assert(r.find_str.empty());
  assert(r.replace_str.empty());
}

static void test_parse_replace_args_empty_find() {
  auto r = parse_replace_args("|new");
  assert(r.find_str.empty());
  assert(r.replace_str == "new");
}

static void test_parse_replace_args_spaces_in_replace() {
  auto r = parse_replace_args("foo|bar baz");
  assert(r.find_str == "foo");
  assert(r.replace_str == "bar baz");
}

//
// tool_replace tests
//

static void test_replace_single_occurrence() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "hello world\nhello again\n");

  string result = tool_replace(SANDBOX, "test.txt", "hello|goodbye");
  assert(result.find("OK") == 0);
  assert(result.find("1 occurrence") != string::npos);

  string content = read_test_file(SANDBOX + "/test.txt");
  assert(content == "goodbye world\nhello again\n");
  teardown_sandbox();
}

static void test_replace_all_occurrences() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "foo bar foo baz foo\n");

  string result = tool_replace(SANDBOX, "test.txt", "foo|qux --all");
  assert(result.find("OK") == 0);
  assert(result.find("3 occurrence") != string::npos);

  string content = read_test_file(SANDBOX + "/test.txt");
  assert(content == "qux bar qux baz qux\n");
  teardown_sandbox();
}

static void test_replace_count_only() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "abc def abc ghi abc\n");

  string result = tool_replace(SANDBOX, "test.txt", "abc|xyz --count");
  assert(result == "3 occurrence(s) found");

  // File should be unmodified
  string content = read_test_file(SANDBOX + "/test.txt");
  assert(content == "abc def abc ghi abc\n");
  teardown_sandbox();
}

static void test_replace_not_found() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "hello world\n");

  string result = tool_replace(SANDBOX, "test.txt", "goodbye|hello");
  assert(result.find("ERROR") == 0);
  assert(result.find("not found") != string::npos);
  teardown_sandbox();
}

static void test_replace_empty_find() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "hello\n");

  string result = tool_replace(SANDBOX, "test.txt", "|new");
  assert(result.find("ERROR") == 0);
  assert(result.find("empty find") != string::npos);
  teardown_sandbox();
}

static void test_replace_outside_sandbox() {
  setup_sandbox();
  string result = tool_replace(SANDBOX, "../../etc/passwd", "a|b");
  assert(result.find("ERROR") == 0);
  assert(result.find("outside sandbox") != string::npos);
  teardown_sandbox();
}

static void test_replace_missing_file() {
  setup_sandbox();
  string result = tool_replace(SANDBOX, "nonexistent.txt", "a|b");
  assert(result.find("ERROR") == 0);
  assert(result.find("cannot open") != string::npos);
  teardown_sandbox();
}

static void test_replace_multiline_find() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "int main() {\n  return 0;\n}\n");

  string result = tool_replace(SANDBOX, "test.txt", "int main()|void main()");
  assert(result.find("OK") == 0);

  string content = read_test_file(SANDBOX + "/test.txt");
  assert(content == "void main() {\n  return 0;\n}\n");
  teardown_sandbox();
}

static void test_replace_empty_replace_str() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "hello world\n");

  string result = tool_replace(SANDBOX, "test.txt", "world|");
  assert(result.find("OK") == 0);

  string content = read_test_file(SANDBOX + "/test.txt");
  assert(content == "hello \n");
  teardown_sandbox();
}

//
// Quoted find string tests (| inside quotes is literal)
//

static void test_parse_replace_args_quoted_double() {
  auto r = parse_replace_args("\"<|think|>\"|<new_tag>");
  assert(r.find_str == "<|think|>");
  assert(r.replace_str == "<new_tag>");
  assert(r.all == false);
  assert(r.count == false);
}

static void test_parse_replace_args_quoted_single() {
  auto r = parse_replace_args("'<|think|>'|<new_tag>");
  assert(r.find_str == "<|think|>");
  assert(r.replace_str == "<new_tag>");
}

static void test_parse_replace_args_quoted_with_flags() {
  auto r = parse_replace_args("\"a|b|c\"|x --all");
  assert(r.find_str == "a|b|c");
  assert(r.replace_str == "x");
  assert(r.all == true);
  assert(r.count == false);
}

static void test_parse_replace_args_quoted_pipe_in_replace() {
  auto r = parse_replace_args("\"old\"|new|more");
  assert(r.find_str == "old");
  assert(r.replace_str == "new|more");
}

static void test_parse_replace_args_quoted_empty_find() {
  auto r = parse_replace_args("\"\"|new");
  assert(r.find_str.empty());
  assert(r.replace_str == "new");
}

static void test_parse_replace_args_quoted_no_pipe_after() {
  // Quoted string with no | after closing quote — fall back to first |
  auto r = parse_replace_args("\"hello world\"");
  assert(r.find_str.empty());
  assert(r.replace_str.empty());
}

static void test_replace_quoted_pipe_in_find() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "start <|think|> end\n");

  string result = tool_replace(SANDBOX, "test.txt", "\"<|think|>\"|<|channel>thought|>");
  assert(result.find("OK") == 0);
  assert(result.find("1 occurrence") != string::npos);

  string content = read_test_file(SANDBOX + "/test.txt");
  assert(content == "start <|channel>thought|> end\n");
  teardown_sandbox();
}

static void test_replace_quoted_pipe_all() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "a|b and a|b and a|b\n");

  string result = tool_replace(SANDBOX, "test.txt", "\"a|b\"|x --all");
  assert(result.find("OK") == 0);
  assert(result.find("3 occurrence") != string::npos);

  string content = read_test_file(SANDBOX + "/test.txt");
  assert(content == "x and x and x\n");
  teardown_sandbox();
}

static void test_replace_quoted_pipe_count() {
  setup_sandbox();
  write_test_file(SANDBOX + "/test.txt", "foo|bar foo|bar baz\n");

  string result = tool_replace(SANDBOX, "test.txt", "\"foo|bar\"|x --count");
  assert(result == "2 occurrence(s) found");

  // File unmodified
  string content = read_test_file(SANDBOX + "/test.txt");
  assert(content == "foo|bar foo|bar baz\n");
  teardown_sandbox();
}

void replace_test() {
  cout << "=== Running TOOL:REPLACE Unit Tests ===\n\n";

  RUN_TEST(test_parse_replace_args_basic);
  RUN_TEST(test_parse_replace_args_flags);
  RUN_TEST(test_parse_replace_args_no_pipe);
  RUN_TEST(test_parse_replace_args_empty_find);
  RUN_TEST(test_parse_replace_args_spaces_in_replace);
  RUN_TEST(test_parse_replace_args_quoted_double);
  RUN_TEST(test_parse_replace_args_quoted_single);
  RUN_TEST(test_parse_replace_args_quoted_with_flags);
  RUN_TEST(test_parse_replace_args_quoted_pipe_in_replace);
  RUN_TEST(test_parse_replace_args_quoted_empty_find);
  RUN_TEST(test_parse_replace_args_quoted_no_pipe_after);
  RUN_TEST(test_replace_single_occurrence);
  RUN_TEST(test_replace_all_occurrences);
  RUN_TEST(test_replace_count_only);
  RUN_TEST(test_replace_not_found);
  RUN_TEST(test_replace_empty_find);
  RUN_TEST(test_replace_outside_sandbox);
  RUN_TEST(test_replace_missing_file);
  RUN_TEST(test_replace_multiline_find);
  RUN_TEST(test_replace_empty_replace_str);
  RUN_TEST(test_replace_quoted_pipe_in_find);
  RUN_TEST(test_replace_quoted_pipe_all);
  RUN_TEST(test_replace_quoted_pipe_count);

  cout << "\n=== All TOOL:REPLACE tests completed ===\n";
}