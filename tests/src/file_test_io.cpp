#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>
#include <fstream>

using namespace std;
namespace fs = std::filesystem;

// Forward declarations (defined in file.cpp, included by file_test.cpp)
string tool_append(const string &path, const string &content);
string tool_write(const string &path, const string &content);
string tool_write_validate(const string &path, const string &content, int size_warn = 1000);
string tool_patch(const string &path, const string &patch);
string read_file(const string &path);
string read_file_range(const string &path, int start, int end);
string list_dir(const string &path);
bool make_dir(const string &path);
bool isBalanced(const string &s);
void set_sandbox_root(const string &path);
string tool_move(const string &src, const string &dst);

//
// Helper: check that a string contains a substring
//
static bool contains(const string &haystack, const string &needle) {
  return haystack.find(needle) != string::npos;
}

// Test tool_append with multiple appends
static void test_tool_append_multiple() {
  string test_file = "/tmp/test_append_multiple.txt";

  // First append
  tool_append(test_file, "line 1\n");
  // Second append
  tool_append(test_file, "line 2\n");
  // Third append
  tool_append(test_file, "line 3\n");

  // Verify all content is there
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == "line 1\nline 2\nline 3\n");

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_multiple passed" << endl;
}

// Test tool_append with nested directories
static void test_tool_append_nested_dirs() {
  string test_file = "/tmp/nested/dir1/dir2/test.txt";
  string content = "Hello World";

  string result = tool_append(test_file, content);
  assert(result == "OK: appended to " + test_file);

  // Verify file was created
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());
  fs::remove_all("/tmp/nested");

  cout << "test_tool_append_nested_dirs passed" << endl;
}

// Test tool_append with curly brace language and balanced content
static void test_tool_append_curly_balanced() {
  string test_file = "/tmp/test_curly_append.js";
  string content = "function test() { return 1; }";

  string result = tool_append(test_file, content);
  assert(result == "OK: appended to " + test_file);

  // Verify file was created
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_curly_balanced passed" << endl;
}

// Test tool_append with curly brace language and unbalanced content
static void test_tool_append_curly_unbalanced() {
  string test_file = "/tmp/test_curly_balanced.js";
  string content = "function test( { return 1; }";

  string result = tool_append(test_file, content);
  assert(result.find("ERROR") != string::npos);
  assert(result.find("unbalanced") != string::npos);

  // Verify file was NOT created
  assert(!fs::exists(test_file));

  cout << "test_tool_append_curly_unbalanced passed" << endl;
}

// Test tool_append with binary content
static void test_tool_append_binary() {
  string test_file = "/tmp/test_append_binary.bin";
  string content = "\x00\x01\x02\x03\x04\x05";

  string result = tool_append(test_file, content);
  assert(result == "OK: appended to " + test_file);

  // Verify binary content was appended correctly
  ifstream in(test_file, ios::binary);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_binary passed" << endl;
}

// Test tool_append with empty content
static void test_tool_append_empty() {
  string test_file = "/tmp/test_append_empty.cpp";
  string content = "";

  string result = tool_append(test_file, content);
  assert(result == "OK: appended to " + test_file);

  // Verify file was created (empty)
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == "");

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_empty passed" << endl;
}

// Test tool_append with special characters
static void test_tool_append_special_chars() {
  string test_file = "/tmp/test_append_special.cpp";
  string content = "int main() { return 1; } // comment with \"quotes\" and 'apostrophes'";

  string result = tool_append(test_file, content);
  assert(result == "OK: appended to " + test_file);

  // Verify file was created with correct content
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_special_chars passed" << endl;
}

// Test tool_append with multi-line content
static void test_tool_append_multiline() {
  string test_file = "/tmp/test_append_multiline.cpp";
  string content = "int main() {\n    int x = 1;\n    int y = 2;\n    return x + y;\n}";

  string result = tool_append(test_file, content);
  assert(result == "OK: appended to " + test_file);

  // Verify file was created with correct content
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_multiline passed" << endl;
}

// Test tool_append with log-like content
static void test_tool_append_log() {
  string test_file = "/tmp/test_append.log";

  // Simulate log entries
  tool_append(test_file, "2024-01-01 10:00:00 INFO Starting application\n");
  tool_append(test_file, "2024-01-01 10:00:01 DEBUG Loading config\n");
  tool_append(test_file, "2024-01-01 10:00:02 INFO Application ready\n");

  // Verify log entries are in order
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == "2024-01-01 10:00:00 INFO Starting application\n2024-01-01 10:00:01 DEBUG Loading config\n2024-01-01 10:00:02 INFO Application ready\n");

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_log passed" << endl;
}

//
// Test: appending a closing brace to a file with an open brace now succeeds
// (the resulting file is balanced, even though the chunk alone is not)
//
static void test_tool_append_closing_brace() {
  string test_file = "/tmp/test_append_close_brace.java";

  // Create a file with an open method body
  {
    ofstream out(test_file);
    out << "class Foo {\n  void bar() {\n";
  }

  // Append the closing braces — chunk alone is unbalanced (2 closes, 0 opens)
  // but the resulting file is balanced
  string result = tool_append(test_file, "  }\n}\n");
  assert(result == "OK: appended to " + test_file);

  // Verify the file content
  ifstream in(test_file);
  string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();
  assert(content == "class Foo {\n  void bar() {\n  }\n}\n");

  remove(test_file.c_str());
  cout << "test_tool_append_closing_brace passed" << endl;
}

//
// Test: appending content that makes the *result* unbalanced is still rejected
//
static void test_tool_append_result_unbalanced() {
  string test_file = "/tmp/test_append_result_unbalanced.java";

  // Create a file with balanced content
  {
    ofstream out(test_file);
    out << "class Foo {\n  void bar() {\n  }\n}\n";
  }

  // Append an extra opening brace — result would be unbalanced
  string result = tool_append(test_file, "  void baz() {\n");
  assert(result.find("ERROR") != string::npos);
  assert(result.find("unbalanced") != string::npos);

  // Verify file was NOT modified
  ifstream in(test_file);
  string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();
  assert(content == "class Foo {\n  void bar() {\n  }\n}\n");

  remove(test_file.c_str());
  cout << "test_tool_append_result_unbalanced passed" << endl;
}

//
// Test: C++ files now get brace validation in tool_write_validate
//
static void test_tool_write_validate_cpp_braces() {
  string test_file = "/tmp/test_cpp_braces.cpp";

  // Balanced C++ content — no warning
  string balanced = "int main() { return 0; }";
  string result = tool_write_validate(test_file, balanced);
  assert(result.find("unbalanced") == string::npos);

  // Unbalanced C++ content — should warn
  string unbalanced = "int main() { return 0;";
  result = tool_write_validate(test_file, unbalanced);
  assert(result.find("unbalanced") != string::npos);

  cout << "test_tool_write_validate_cpp_braces passed" << endl;
}

//
// read_file tests
//

static void test_read_file_basic() {
  const string path = "/tmp/hali_test_read_file.txt";
  {
    ofstream f(path);
    f << "hello world";
  }
  string result = read_file(path);
  assert(result == "hello world");
  remove(path.c_str());
  cout << "test_read_file_basic passed" << endl;
}

static void test_read_file_not_found() {
  string result = read_file("/tmp/hali_test_nonexistent_xyz.txt");
  assert(result.find("ERROR: cannot open") != string::npos);
  cout << "test_read_file_not_found passed" << endl;
}

static void test_read_file_too_large() {
  const string path = "/tmp/hali_test_read_large.txt";
  {
    ofstream f(path, ios::binary);
    // Write 33 KB (exceeds 32 KB limit)
    string big(33 * 1024, 'x');
    f.write(big.data(), big.size());
  }
  string result = read_file(path);
  assert(result.find("ERROR: file too large") != string::npos);
  remove(path.c_str());
  cout << "test_read_file_too_large passed" << endl;
}

static void test_read_file_empty() {
  const string path = "/tmp/hali_test_read_empty.txt";
  {
    ofstream f(path);
  }
  string result = read_file(path);
  assert(result.empty());
  remove(path.c_str());
  cout << "test_read_file_empty passed" << endl;
}

//
// make_dir tests
//

static void test_make_dir_new() {
  const string path = "/tmp/hali_test_mkdir_new/sub";
  fs::remove_all(path);
  bool result = make_dir(path);
  assert(result == true);
  assert(fs::exists(path));
  fs::remove_all("/tmp/hali_test_mkdir_new");
  cout << "test_make_dir_new passed" << endl;
}

static void test_make_dir_existing() {
  const string path = "/tmp/hali_test_mkdir_existing";
  fs::create_directories(path);
  bool result = make_dir(path);
  assert(result == true);
  fs::remove_all(path);
  cout << "test_make_dir_existing passed" << endl;
}

static void test_make_dir_nested() {
  const string path = "/tmp/hali_test_mkdir_nested/a/b/c";
  fs::remove_all("/tmp/hali_test_mkdir_nested");
  bool result = make_dir(path);
  assert(result == true);
  assert(fs::exists(path));
  fs::remove_all("/tmp/hali_test_mkdir_nested");
  cout << "test_make_dir_nested passed" << endl;
}

//
// list_dir tests
//

static void test_list_dir_basic() {
  const string dir = "/tmp/hali_test_listdir";
  fs::remove_all(dir);
  fs::create_directories(dir + "/subdir");
  {
    ofstream f(dir + "/file1.txt");
    f << "a";
  }
  {
    ofstream f(dir + "/file2.txt");
    f << "b";
  }
  string result = list_dir(dir);
  assert(result.find("[subdir]") != string::npos);
  assert(result.find("file1.txt") != string::npos);
  assert(result.find("file2.txt") != string::npos);
  fs::remove_all(dir);
  cout << "test_list_dir_basic passed" << endl;
}

static void test_list_dir_empty() {
  const string dir = "/tmp/hali_test_listdir_empty";
  fs::remove_all(dir);
  fs::create_directories(dir);
  string result = list_dir(dir);
  assert(result == "[.]");
  fs::remove_all(dir);
  cout << "test_list_dir_empty passed" << endl;
}

static void test_list_dir_hides_dotfiles() {
  const string dir = "/tmp/hali_test_listdir_dot";
  fs::remove_all(dir);
  fs::create_directories(dir);
  {
    ofstream f(dir + "/.hidden");
    f << "x";
  }
  {
    ofstream f(dir + "/visible.txt");
    f << "y";
  }
  string result = list_dir(dir);
  assert(result.find(".hidden") == string::npos);
  assert(result.find("visible.txt") != string::npos);
  fs::remove_all(dir);
  cout << "test_list_dir_hides_dotfiles passed" << endl;
}

//
// Internal path validation tests (P2-1 defense in depth)
//

static void test_file_internal_validation() {
  const string sandbox = "/tmp/hali_test_file_sandbox";
  fs::create_directories(sandbox);

  // Set sandbox root to enable internal validation
  set_sandbox_root(sandbox);

  // Write inside sandbox should succeed
  string result = tool_write(sandbox + "/test.txt", "hello");
  assert(result.find("OK") != string::npos);

  // Write outside sandbox should fail
  result = tool_write("/tmp/hali_outside_test.txt", "evil");
  assert(result.find("ERROR: path outside sandbox") != string::npos);

  // Append inside sandbox should succeed
  result = tool_append(sandbox + "/test.txt", " world");
  assert(result.find("OK") != string::npos);

  // Append outside sandbox should fail
  result = tool_append("/tmp/hali_outside_append.txt", "evil");
  assert(result.find("ERROR: path outside sandbox") != string::npos);

  assert(result.find("ERROR: path outside sandbox") != string::npos);
  // Patch outside sandbox should fail (path check happens before patch parsing)
  result = tool_patch("/tmp/hali_outside_patch.txt", "some patch data");
  assert(result.find("ERROR: path outside sandbox") != string::npos);  assert(result.find("ERROR: path outside sandbox") != string::npos);

  // Traversal escape should fail
  result = tool_write(sandbox + "/../../tmp/hali_escape.txt", "evil");
  assert(result.find("ERROR: path outside sandbox") != string::npos);

  // Reset sandbox root (disables check for remaining tests)
  set_sandbox_root("");

  // Clean up
  fs::remove_all(sandbox);

  cout << "test_file_internal_validation passed" << endl;
}

//
// Test: tool_write_validate with pure ASCII content in a .cpp file
// Should produce no non-ASCII warning
//
static void test_write_validate_ascii_clean() {
  string test_file = "/tmp/test_ascii_clean.cpp";
  string content = "int main() { return 0; }\n";
  string result = tool_write_validate(test_file, content);
  assert(result.find("non-ASCII") == string::npos);
  cout << "test_write_validate_ascii_clean passed" << endl;
}

//
// Test: tool_write_validate with non-ASCII bytes in a .cpp file
// Simulates LLM unicode corruption (em-dash, curly quotes)
//
static void test_write_validate_non_ascii_warning() {
  string test_file = "/tmp/test_non_ascii_warn.cpp";
  // Contains an em-dash (U+2014) and a curly quote (U+2019)
  string content = "int main() { // \xE2\x80\x94 note \xE2\x80\x99\n  return 0;\n}\n";
  string result = tool_write_validate(test_file, content);
  assert(result.find("non-ASCII") != string::npos);
  assert(result.find("2 non-ASCII byte") == string::npos); // 6 bytes total (3+3)
  assert(result.find("non-ASCII byte(s) detected") != string::npos);
  cout << "test_write_validate_non_ascii_warning passed" << endl;
}

//
// Test: tool_write_validate with non-ASCII in a non-brace file (.txt)
// Should NOT trigger the non-ASCII check
//
static void test_write_validate_non_ascii_non_brace() {
  string test_file = "/tmp/test_non_ascii_txt.txt";
  string content = "hello \xE2\x80\x94 world\n";
  string result = tool_write_validate(test_file, content);
  assert(result.find("non-ASCII") == string::npos);
  cout << "test_write_validate_non_ascii_non_brace passed" << endl;
}

//
// Test: tool_append with --no-balance-check flag allows unbalanced braces
//
static void test_append_no_balance_check() {
  string test_file = "/tmp/test_no_balance.cpp";
  // Remove if exists from prior runs
  fs::remove(test_file);

  // Append unbalanced opening brace WITH the flag — should succeed
  string content = "--no-balance-check\nint main() {\n";
  string result = tool_append(test_file, content);
  assert(result.find("OK: appended") != string::npos);

  // Verify the flag was stripped from the file content
  string file_content = read_file(test_file);
  assert(file_content.find("--no-balance-check") == string::npos);
  assert(file_content == "int main() {\n");

  fs::remove(test_file);
  cout << "test_append_no_balance_check passed" << endl;
}

//
// Test: tool_append with --no-balance-check on existing file
// Appending a closing brace that completes the structure
//
static void test_append_no_balance_check_existing() {
  string test_file = "/tmp/test_no_balance_existing.cpp";
  fs::remove(test_file);

  // First append: opening (with flag to skip balance check)
  tool_append(test_file, "--no-balance-check\nvoid foo() {\n");

  // Second append: closing (with flag, since file alone is unbalanced)
  string result = tool_append(test_file, "--no-balance-check\n}\n");
  assert(result.find("OK: appended") != string::npos);

  // Final file should be balanced
  string file_content = read_file(test_file);
  assert(file_content == "void foo() {\n}\n");
  assert(isBalanced(file_content));

  fs::remove(test_file);
  cout << "test_append_no_balance_check_existing passed" << endl;
}

//
// Test: tool_append WITHOUT --no-balance-check still rejects unbalanced
// (regression check — existing behavior preserved)
//
static void test_append_balance_check_still_enforced() {
  string test_file = "/tmp/test_balance_enforced.cpp";
  fs::remove(test_file);

  // Append unbalanced content WITHOUT the flag — should fail
  string result = tool_append(test_file, "int main() {\n");
  assert(result.find("ERROR: resulting file would have unbalanced braces") != string::npos);

  fs::remove(test_file);
  cout << "test_append_balance_check_still_enforced passed" << endl;
}

//
// tool_move tests
//

static void test_tool_move_basic() {
  const string sandbox = "/tmp/hali_test_move_sandbox";
  fs::remove_all(sandbox);
  fs::create_directories(sandbox);

  // Create source file
  string src = sandbox + "/original.txt";
  {
    ofstream f(src);
    f << "hello world";
  }

  // Move to new location
  string dst = sandbox + "/moved.txt";
  string result = tool_move(src, dst);
  assert(result == "OK: moved " + src + " -> " + dst);

  // Verify source is gone
  assert(!fs::exists(src));

  // Verify destination exists with correct content
  assert(fs::exists(dst));
  ifstream in(dst);
  string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();
  assert(content == "hello world");

  fs::remove_all(sandbox);
  cout << "test_tool_move_basic passed" << endl;
}

static void test_tool_move_source_not_found() {
  const string sandbox = "/tmp/hali_test_move_nf";
  fs::remove_all(sandbox);
  fs::create_directories(sandbox);

  string result = tool_move(sandbox + "/nonexistent.txt", sandbox + "/dest.txt");
  assert(result.find("ERROR: source file not found") != string::npos);

  fs::remove_all(sandbox);
  cout << "test_tool_move_source_not_found passed" << endl;
}

static void test_tool_move_destination_exists() {
  const string sandbox = "/tmp/hali_test_move_dst";
  fs::remove_all(sandbox);
  fs::create_directories(sandbox);

  // Create both source and destination
  string src = sandbox + "/src.txt";
  string dst = sandbox + "/dst.txt";
  {
    ofstream f(src); f << "a";
  }
  {
    ofstream f(dst); f << "b";
  }

  string result = tool_move(src, dst);
  assert(result.find("ERROR: destination already exists") != string::npos);

  // Both files should still exist
  assert(fs::exists(src));
  assert(fs::exists(dst));

  fs::remove_all(sandbox);
  cout << "test_tool_move_destination_exists passed" << endl;
}

static void test_tool_move_creates_parent_dirs() {
  const string sandbox = "/tmp/hali_test_move_dirs";
  fs::remove_all(sandbox);
  fs::create_directories(sandbox);

  string src = sandbox + "/file.txt";
  {
    ofstream f(src);
    f << "data";
  }

  // Move to a nested path that doesn't exist yet
  string dst = sandbox + "/a/b/c/file.txt";
  string result = tool_move(src, dst);
  assert(result.find("OK: moved") != string::npos);

  assert(fs::exists(dst));
  assert(!fs::exists(src));

  fs::remove_all(sandbox);
  cout << "test_tool_move_creates_parent_dirs passed" << endl;
}

static void test_tool_move_sandbox_escape() {
  const string sandbox = "/tmp/hali_test_move_escape";
  fs::remove_all(sandbox);
  fs::create_directories(sandbox);
  set_sandbox_root(sandbox);

  // Create a valid source
  string src = sandbox + "/valid.txt";
  {
    ofstream f(src); f << "x";
  }

  // Try to move outside sandbox via traversal
  string result = tool_move(src, sandbox + "/../../tmp/hali_escape_move.txt");
  assert(result.find("ERROR: destination path outside sandbox") != string::npos);

  // Try to move from outside sandbox
  result = tool_move("/tmp/hali_outside_src.txt", sandbox + "/dest.txt");
  assert(result.find("ERROR: source path outside sandbox") != string::npos);

  set_sandbox_root("");
  fs::remove_all(sandbox);
  cout << "test_tool_move_sandbox_escape passed" << endl;
}

static void test_tool_move_same_file() {
  const string sandbox = "/tmp/hali_test_move_same";
  fs::remove_all(sandbox);
  fs::create_directories(sandbox);

  string src = sandbox + "/file.txt";
  {
    ofstream f(src); f << "data";
  }

  // Moving to same path = destination exists
  string result = tool_move(src, src);
  assert(result.find("ERROR: destination already exists") != string::npos);

  // File should still exist
  assert(fs::exists(src));

  fs::remove_all(sandbox);
  cout << "test_tool_move_same_file passed" << endl;
}

// Test read_file_range
static void test_read_file_range() {
  string test_file = "/tmp/test_read_range.txt";

  // Create a file with 10 known lines
  {
    ofstream out(test_file);
    for (int i = 1; i <= 10; i++) {
      out << "line" << i << "\n";
    }
  }

  // Basic range: lines 3-5
  string result = read_file_range(test_file, 3, 5);
  assert(result == "line3\nline4\nline5\n");

  // Single line
  result = read_file_range(test_file, 7, 7);
  assert(result == "line7\n");

  // Full range
  result = read_file_range(test_file, 1, 10);
  assert(result == "line1\nline2\nline3\nline4\nline5\nline6\nline7\nline8\nline9\nline10\n");

  // Range extending past EOF (should clamp)
  result = read_file_range(test_file, 9, 15);
  assert(result == "line9\nline10\n");

  // Invalid: start > end
  result = read_file_range(test_file, 5, 3);
  assert(result.find("ERROR: invalid line range") != string::npos);

  // Invalid: start < 1
  result = read_file_range(test_file, 0, 5);
  assert(result.find("ERROR: invalid line range") != string::npos);

  // Non-existent file
  result = read_file_range("/tmp/nonexistent_range_file.txt", 1, 5);
  assert(result.find("ERROR: cannot open") != string::npos);

  // Clean up
  fs::remove(test_file);

  cout << "test_read_file_range passed" << endl;
}

void file_test_io() {
  test_tool_append_multiple();
  test_tool_append_nested_dirs();
  test_tool_append_curly_balanced();
  test_tool_append_curly_unbalanced();
  test_tool_append_closing_brace();
  test_tool_append_result_unbalanced();
  test_tool_append_binary();
  test_tool_append_empty();
  test_tool_append_special_chars();
  test_tool_append_multiline();
  test_tool_append_log();
  test_tool_write_validate_cpp_braces();
  test_write_validate_ascii_clean();
  test_write_validate_non_ascii_warning();
  test_write_validate_non_ascii_non_brace();
  test_append_no_balance_check();
  test_append_no_balance_check_existing();
  test_append_balance_check_still_enforced();
  cout << "\nAll tool_append tests passed!\n" << endl;

  test_read_file_basic();
  test_read_file_not_found();
  test_read_file_too_large();
  test_read_file_empty();
  test_read_file_range();
  cout << "\nAll read_file tests passed!\n" << endl;

  test_make_dir_new();
  test_make_dir_existing();
  test_make_dir_nested();
  cout << "\nAll make_dir tests passed!\n" << endl;

  test_list_dir_basic();
  test_list_dir_empty();
  test_list_dir_hides_dotfiles();
  cout << "\nAll list_dir tests passed!\n" << endl;

  test_file_internal_validation();
  cout << "\nAll file internal validation tests passed!\n" << endl;

  test_tool_move_basic();
  test_tool_move_source_not_found();
  test_tool_move_destination_exists();
  test_tool_move_creates_parent_dirs();
  test_tool_move_sandbox_escape();
  test_tool_move_same_file();
  cout << "\nAll tool_move tests passed!\n" << endl;
}