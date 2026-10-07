#include <iostream>
#include <cassert>
#include <string>
#include "file.cpp"

using namespace std;

//
// Helper: check that a string contains a substring
//
static bool contains(const string &haystack, const string &needle) {
  return haystack.find(needle) != string::npos;
}

//
// Test: empty input produces a valid SESSION.md skeleton
//
static void test_empty_input() {
  std::vector<std::string> chat;
  string result = format_session_md(chat);

  assert(contains(result, "# Session State Snapshot"));
  assert(contains(result, "**Timestamp:**"));
  assert(contains(result, "**Current Task:** (unknown)"));
  assert(contains(result, "**Pending Actions:**"));
  assert(contains(result, "- (none)"));
  assert(contains(result, "**Last Output:**"));
  assert(contains(result, "(none)"));

  cout << "test_empty_input passed" << endl;
}

//
// Test: simple You/Hali exchange with no tool actions
//
static void test_simple_exchange() {
  std::vector<std::string> chat;
  chat.push_back("You: hello");
  chat.push_back("Hali:");
  chat.push_back("nHi there! How can I help?");
  string result = format_session_md(chat);

  assert(contains(result, "**Current Task:** hello"));
  assert(contains(result, "- (none)"));
  assert(contains(result, "Hi there! How can I help?"));

  cout << "test_simple_exchange passed" << endl;
}

//
// Test: tool actions are collected as pending actions
//
static void test_tool_actions() {
  // 🔧 ▏→ listing: /some/path
  // 🔧 ▏→ reading: file.cpp
  std::vector<std::string> chat;
  chat.push_back("You: check the file\n");
  chat.push_back("Hali:");
  chat.push_back(" 🔧 ▏→ listing: /some/path\n");
  chat.push_back(" 🔧 ▏→ reading: file.cpp\n");
  chat.push_back("Here is the file content.\n");
  string result = format_session_md(chat);

  assert(contains(result, "- listing: /some/path"));
  assert(contains(result, "- reading: file.cpp"));
  assert(contains(result, "Here is the file content."));

  cout << "test_tool_actions passed" << endl;
}

//
// Test: thinking blocks are skipped from output
//
static void test_thinking_skipped() {
  // 🤔 ▏Let me think about this...
  std::vector<std::string> chat;
  chat.push_back("You: explain this\n");
  chat.push_back("Hali:\n");
  chat.push_back(" 🤔 ▏ Let me think about this...\n");
  chat.push_back("Here is the explanation.\n");

  string result = format_session_md(chat);

  assert(!contains(result, "Let me think about this..."));
  assert(contains(result, "Here is the explanation."));

  cout << "test_thinking_skipped passed" << endl;
}

//
// Test: error messages are collected
//
static void test_error_messages() {
  // ⚡ ▏add_message: Failed to remove old KV tokens
  std::vector<std::string> chat;
  chat.push_back("You: do something\n");
  chat.push_back("Hali:\n");
  chat.push_back("Working on it.\n");
  chat.push_back(" ⚡ ▏add_message: Failed to remove old KV tokens\n");

  string result = format_session_md(chat);

  assert(contains(result, "**Errors:**"));
  assert(contains(result, "add_message: Failed to remove old KV tokens"));

  cout << "test_error_messages passed" << endl;
}

//
// Test: meta-commands like /save are skipped from Current Task
//
static void test_meta_command_skipped() {
  std::vector<std::string> chat;
  chat.push_back("You: read file.cpp\n");
  chat.push_back("Hali:\n");
  chat.push_back("Here is the file.\n");
  chat.push_back("You: /save error.txt\n");

  string result = format_session_md(chat);

  // Current Task should be "read file.cpp", not "/save error.txt"
  assert(contains(result, "**Current Task:** read file.cpp"));
  assert(!contains(result, "/save error.txt"));

  cout << "test_meta_command_skipped passed" << endl;
}

//
// Test: only the last Hali block's output is kept
//
static void test_last_output_only() {
  std::vector<std::string> chat;
  chat.push_back("You: first question\n");
  chat.push_back("Hali:\n");
  chat.push_back("First answer here.\n");
  chat.push_back("You: second question\n");
  chat.push_back("Hali:\n");
  chat.push_back("Second answer here.\n");

  string result = format_session_md(chat);

  assert(contains(result, "**Current Task:** second question"));
  assert(contains(result, "Second answer here."));
  assert(!contains(result, "First answer here."));

  cout << "test_last_output_only passed" << endl;
}

//
// Test: multiple tool actions across Hali blocks are all collected
//
static void test_multiple_tool_actions() {
  std::vector<std::string> chat;
  chat.push_back("You: build the project\n");
  chat.push_back("Hali:\n");
  chat.push_back(" 🔧 ▏→ listing: /src\n");
  chat.push_back("Let me check the files.\n");
  chat.push_back(" 🔧 ▏→ reading: main.cpp\n");
  chat.push_back("Found the issue.\n");

  string result = format_session_md(chat);

  assert(contains(result, "- listing: /src"));
  assert(contains(result, "- reading: main.cpp"));
  assert(contains(result, "Found the issue."));

  cout << "test_multiple_tool_actions passed" << endl;
}

//
// Test: Hali content on the same line as the prefix
//
static void test_hali_inline_content() {
  std::vector<std::string> chat;
  chat.push_back("You: hi\n");
  chat.push_back("Hali: Hello! How can I help?\n");

  string result = format_session_md(chat);

  assert(contains(result, "Hello! How can I help?"));

  cout << "test_hali_inline_content passed" << endl;
}

//
// Test: realistic multi-turn conversation (mirrors error.txt structure)
//
static void test_realistic_conversation() {
  std::vector<std::string> chat;
  chat.push_back("You: understand scan.c\n");
  chat.push_back("Hali:\n");
  chat.push_back(" 🤔 ▏Okay, the user wants me to understand scan.c.\n");
  chat.push_back(" 🔧 ▏→  listing: /home/chrisws/src/SmallBASIC/src/common\n");
  chat.push_back(" 🤔 ▏Now I should read the contents of scan.c.\n");
  chat.push_back(" 🔧 ▏→  reading: scan.c\n");
  chat.push_back(" 🤔 ▏→ The file is part of the compiler.\n");
  chat.push_back("The `scan.c` file is a core component of the SmallBASIC compiler.\n");
  chat.push_back("### **1. Compiler Phases**\n");
  chat.push_back("- **Pass 1**: Parses source code.\n");
  chat.push_back("You: read blib_graph.c\n");
  chat.push_back("⚡ ▏add_message: Failed to remove old KV tokens: Failed to evaluate token\n");
  chat.push_back("You: /save error.txt\n");

  string result = format_session_md(chat);

  // Current task is the last non-meta user message
  assert(contains(result, "**Current Task:** read blib_graph.c"));

  // Both tool actions collected
  assert(contains(result, "- listing: /home/chrisws/src/SmallBASIC/src/common"));
  assert(contains(result, "- reading: scan.c"));

  // Last output is from the first Hali block (the only one with real output)
  assert(contains(result, "The `scan.c` file is a core component"));
  assert(contains(result, "### **1. Compiler Phases**"));

  // Thinking blocks excluded
  assert(!contains(result, "Okay, the user wants me to understand"));
  assert(!contains(result, "Now I should read the contents"));

  // Meta command excluded
  assert(!contains(result, "/save error.txt"));

  cout << "test_realistic_conversation passed" << endl;
}

//
// Test: no errors section when no errors present
//
static void test_no_errors_section() {
  std::vector<std::string> chat;
  chat.push_back("You: hello\n");
  chat.push_back("Hali:\n");
  chat.push_back("Hi!\n");

  string result = format_session_md(chat);

  assert(!contains(result, "**Errors:**"));

  cout << "test_no_errors_section passed" << endl;
}

//
// Test: whitespace-only lines are ignored
//
static void test_whitespace_lines() {
  std::vector<std::string> chat;
  chat.push_back("You: hello\n");
  chat.push_back("Hali:\n");
  chat.push_back("\n");
  chat.push_back("   \n");
  chat.push_back("Hi there.\n");
  chat.push_back("\n");

  string result = format_session_md(chat);

  assert(contains(result, "Hi there."));

  cout << "test_whitespace_lines passed" << endl;
}

// Test isBalanced function
static void test_isBalanced() {
  // Valid balanced strings
  assert(isBalanced("{}") == true);
  assert(isBalanced("()") == true);
  assert(isBalanced("{{}}") == true);
  assert(isBalanced("()()") == true);
  assert(isBalanced("{()}") == true);
  assert(isBalanced("if (x) { return 1; }") == true);
  assert(isBalanced("int main() { return 0; }") == true);

  // Invalid unbalanced strings
  assert(isBalanced("{") == false);
  assert(isBalanced("}") == false);
  assert(isBalanced("(") == false);
  assert(isBalanced(")") == false);
  assert(isBalanced("{{") == false);
  assert(isBalanced("}}") == false);
  assert(isBalanced("if (x) { return 1") == false);

  assert(isBalanced("{()}") == true);

  // Strings with strings (should still be balanced)
  assert(isBalanced("\"hello\"") == true);
  assert(isBalanced("\"hello\";") == true);
  assert(isBalanced("string s = \"hello\";") == true);

  cout << "test_isBalanced passed" << endl;
}

// Test parsePatch function
static void test_parsePatch() {
  string patch1 = "<<<<<<< OLD\nint foo() { return 1; }\n=======\nint foo() { return 2; }\n>>>>>>> NEW";

  auto [old1, new1] = parsePatch(patch1);
  assert(old1 == "int foo() { return 1; }");
  assert(new1 == "int foo() { return 2; }");

  string patch2 = "<<<<<<< FUNCTION\nvoid bar() {\n  int x = 5;\n  return x;\n}\n=======\nvoid bar() {\n  int y = 10;\n  return y;\n}\n>>>>>>> FUNCTION";

  auto [old2, new2] = parsePatch(patch2);
  assert(old2 == "void bar() {\n  int x = 5;\n  return x;\n}");
  assert(new2 == "void bar() {\n  int y = 10;\n  return y;\n}");

  cout << "test_parsePatch passed" << endl;
}

// Test countOccurrences function
static void test_countOccurrences() {
  string text = "foo foo foo";
  assert(countOccurrences(text, "foo") == 3);

  string text2 = "bar baz bar";
  assert(countOccurrences(text2, "bar") == 2);

  string text3 = "unique";
  assert(countOccurrences(text3, "unique") == 1);

  string text4 = "no match";
  assert(countOccurrences(text4, "xyz") == 0);

  cout << "test_countOccurrences passed" << endl;
}

// Test replaceAll function
static void test_replaceAll() {
  string text = "foo bar foo baz foo";
  string result = replaceAll(text, "foo", "qux");
  assert(result == "qux bar qux baz qux");

  string text2 = "hello world";
  string result2 = replaceAll(text2, "world", "universe");
  assert(result2 == "hello universe");

  cout << "test_replaceAll passed" << endl;
}

// Test tool_patch function with valid patch
static void test_tool_patch_valid() {
  // Create a temporary file with test content
  string test_file = "/tmp/test_patch.cpp";
  string content = "int foo() { return 1; }\nint bar() { return 2; }";

  ofstream out(test_file);
  out << content;
  out.close();

  string patch = "<<<<<<< OLD\nint foo() { return 1; }\n=======\nint foo() { return 42; }\n>>>>>>> NEW";

  string result = tool_patch(test_file, patch);
  assert(result == "OK: Patch applied to " + test_file);

  // Verify the file was updated
  ifstream in(test_file);
  string updated_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(updated_content == "int foo() { return 42; }\nint bar() { return 2; }");

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_patch_valid passed" << endl;
}

// Test tool_patch function with missing OLD block
static void test_tool_patch_missing_old() {
  string test_file = "/tmp/test_patch2.cpp";
  string content = "int foo() { return 1; }\nint bar() { return 2; }";

  ofstream out(test_file);
  out << content;
  out.close();

  string patch = "<<<<<<< OLD\nint baz() { return 99; }\n=======\nint baz() { return 100; }\n>>>>>>> NEW";

  string result = tool_patch(test_file, patch);
  assert(result.find("ERROR") != string::npos);
  assert(result.find("not found verbatim") != string::npos);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_patch_missing_old passed" << endl;
}

// Test tool_patch function with multiple OLD blocks
static void test_tool_patch_multiple_old() {
  string test_file = "/tmp/test_patch3.cpp";
  string content = "int foo() { return 1; }\nint foo() { return 1; }\nint bar() { return 3; }";

  ofstream out(test_file);
  out << content;
  out.close();

  string patch = "<<<<<<< OLD\nint foo() { return 1; }\n=======\nint foo() { return 42; }\n>>>>>>> NEW";

  string result = tool_patch(test_file, patch);
  assert(result.find("ERROR") != string::npos);
  assert(result.find("found") != string::npos && result.find("times") != string::npos);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_patch_multiple_old passed" << endl;
}

// Test tool_patch function with unbalanced NEW block
static void test_tool_patch_unbalanced() {
  string test_file = "/tmp/test_patch4.cpp";
  string content = "int foo() { return 1; }\n";

  ofstream out(test_file);
  out << content;
  out.close();

  string patch = "<<<<<<< OLD\nint foo() { return 1; }\n=======\nint foo() { return 42; // missing brace\n>>>>>>> NEW";

  string result = tool_patch_validate(test_file, patch);
  assert(result.find("unbalanced") != string::npos);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_patch_unbalanced passed" << endl;
}

// Test tool_patch function with unbalanced NEW block
static void test_tool_patch_equal_unbalanced() {
  string test_file = "/tmp/test_patch4.cpp";
  string content = "int foo() { return 1; }\n";

  ofstream out(test_file);
  out << content;
  out.close();

  string patch = "<<<<<<< OLD\nint foo() { return 1; \n=======\nint foo() { return 42; // missing brace\n>>>>>>> NEW";
  string result = tool_patch_validate(test_file, patch);
  assert(result.find("unbalanced") == string::npos);

  // test when there are no braces
  patch = "<<<<<<< OLD\nint foo=1;\n=======\nint foo=2; // now set to 2\n>>>>>>> NEW";
  result = tool_patch_validate(test_file, patch);
  assert(result.find("unbalanced") == string::npos);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_patch_equal_unbalanced passed" << endl;
}

// Test tool_patch function with conflict markers in file
static void test_tool_patch_conflict_markers() {
  string test_file = "/tmp/test_patch5.cpp";
  string content = "<<<<<<< CONFLICT\nint foo() { return 1; }\n=======\nint foo() { return 2; }\n>>>>>>> CONFLICT\n";

  ofstream out(test_file);
  out << content;
  out.close();

  string patch = "<<<<<<< OLD\nint foo() { return 1; }\n=======\nint foo() { return 42; }\n>>>>>>> NEW";

  string result = tool_patch_validate(test_file, patch);
  assert(result.find("conflict markers") != string::npos);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_patch_conflict_markers passed" << endl;
}

// Test tool_patch function with empty OLD block
static void test_tool_patch_empty_old() {
  string test_file = "/tmp/test_patch6.cpp";
  string content = "int foo() { return 1; }\n";

  ofstream out(test_file);
  out << content;
  out.close();

  string patch = "<<<<<<< \n=======\nint foo() { return 42; }\n>>>>>>> NEW";

  string result = tool_patch(test_file, patch);
  assert(result.find("ERROR") != string::npos);
  assert(result.find("empty") != string::npos);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_patch_empty_old passed" << endl;
}

// Test tool_patch function with empty NEW block
static void test_tool_patch_empty_new() {
  string test_file = "/tmp/test_patch7.cpp";
  string content = "int foo() { return 1; }\n";

  ofstream out(test_file);
  out << content;
  out.close();

  string patch = "<<<<<<< OLD\nint foo() { return 1; }\n=======\n>>>>>>> NEW";

  string result = tool_patch_validate(test_file, patch);
  assert(result.find("empty") != string::npos);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_patch_empty_new passed" << endl;
}

// Test tool_write with new file
static void test_tool_write_new_file() {
  string test_file = "/tmp/test_write_new.cpp";
  string content = "int main() { return 0; }";

  string result = tool_write(test_file, content);
  assert(result == "OK: written to " + test_file);

  // Verify file was created with correct content
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_new_file passed" << endl;
}

// Test tool_write with existing file (overwrite)
static void test_tool_write_overwrite() {
  string test_file = "/tmp/test_write_overwrite.cpp";
  string original_content = "int foo() { return 1; }";
  string new_content = "int foo() { return 2; }";

  // Create original file
  ofstream out(test_file);
  out << original_content;
  out.close();

  // Overwrite with new content
  string result = tool_write(test_file, new_content);
  assert(result == "OK: written to " + test_file);

  // Verify file was updated
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == new_content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_overwrite passed" << endl;
}

// Test tool_write with nested directories
static void test_tool_write_nested_dirs() {
  string test_file = "/tmp/nested/dir1/dir2/test.cpp";
  string content = "int bar() { return 42; }";

  string result = tool_write(test_file, content);
  assert(result == "OK: written to " + test_file);

  // Verify file was created
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());
  fs::remove_all("/tmp/nested");

  cout << "test_tool_write_nested_dirs passed" << endl;
}

// Test tool_write with curly brace language and balanced content
static void test_tool_write_curly_balanced() {
  string test_file = "/tmp/test_curly_balanced.js";
  string content = "function test() { return 1; }";

  string result = tool_write(test_file, content);
  assert(result == "OK: written to " + test_file);

  // Verify file was created
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_curly_balanced passed" << endl;
}

// Test tool_write with curly brace language and unbalanced content
static void test_tool_write_curly_unbalanced() {
  string test_file = "/tmp/test_curly_balanced.js";
  string content = "function test() { return 1; }}";

  string result = tool_write_validate(test_file, content);
  assert(result.find("Warning") != string::npos);
  assert(result.find("unbalanced") != string::npos);

  // Verify file was NOT created
  assert(!fs::exists(test_file));

  cout << "test_tool_write_curly_unbalanced passed" << endl;
}

// Test tool_write with non-curly brace language
static void test_tool_write_non_curly() {
  string test_file = "/tmp/test_non_curly.txt";
  string content = "Hello World";

  string result = tool_write(test_file, content);
  assert(result == "OK: written to " + test_file);

  // Verify file was created
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_non_curly passed" << endl;
}

// Test tool_write with large file (> 200 bytes)
static void test_tool_write_large_file() {
  string test_file = "/tmp/test_large.cpp";
  string content = "int main() { return 0; }";

  // Create a large file
  for (int i = 0; i < 100; i++) {
    content += "int foo" + to_string(i) + "() { return " + to_string(i) + "; }\n";
  }

  ofstream out(test_file);
  out << content;
  out.close();

  // Try to write smaller content (should fail due to size check)
  string smaller_content = "int bar() { return 1; }";
  string result = tool_write_validate(test_file, smaller_content);
  assert(result.find("Warning") != string::npos);
  assert(result.find("shrinks") != string::npos);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_large_file passed" << endl;
}

// Test tool_write with binary content
static void test_tool_write_binary() {
  string test_file = "/tmp/test_binary.bin";
  string content = "\x00\x01\x02\x03\x04\x05";

  string result = tool_write(test_file, content);
  assert(result == "OK: written to " + test_file);

  // Verify binary content was written correctly
  ifstream in(test_file, ios::binary);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_binary passed" << endl;
}

// Test tool_write with empty content
static void test_tool_write_empty() {
  string test_file = "/tmp/test_empty.cpp";
  string content = "";

  string result = tool_write(test_file, content);
  assert(result == "OK: written to " + test_file);

  // Verify file was created (empty)
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == "");

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_empty passed" << endl;
}

// Test tool_write with special characters
static void test_tool_write_special_chars() {
  string test_file = "/tmp/test_special.cpp";
  string content = "int main() { return 1; } // comment with \"quotes\" and 'apostrophes'";

  string result = tool_write(test_file, content);
  assert(result == "OK: written to " + test_file);

  // Verify file was created with correct content
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_special_chars passed" << endl;
}

// Test tool_write with multi-line content
static void test_tool_write_multiline() {
  string test_file = "/tmp/test_multiline.cpp";
  string content = "int main() {\n    int x = 1;\n    int y = 2;\n    return x + y;\n}";

  string result = tool_write(test_file, content);
  assert(result == "OK: written to " + test_file);

  // Verify file was created with correct content
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_write_multiline passed" << endl;
}

// Test tool_append with new file
static void test_tool_append_new_file() {
  string test_file = "/tmp/test_append_new.cpp";
  string content = "int main() { return 0; }";

  string result = tool_append(test_file, content);
  assert(result == "OK: appended to " + test_file);

  // Verify file was created with correct content
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_new_file passed" << endl;
}

// Test tool_append with existing file
static void test_tool_append_existing_file() {
  string test_file = "/tmp/test_append_existing.cpp";
  string original_content = "int foo() { return 1; }";
  string append_content = "int bar() { return 2; }";

  // Create original file
  ofstream out(test_file);
  out << original_content;
  out.close();

  // Append new content
  string result = tool_append(test_file, append_content);
  assert(result == "OK: appended to " + test_file);

  // Verify file has both contents
  ifstream in(test_file);
  string file_content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
  in.close();

  assert(file_content == original_content + append_content);

  // Clean up
  remove(test_file.c_str());

  cout << "test_tool_append_existing_file passed" << endl;
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

void file_test() {
  test_isBalanced();
  test_parsePatch();
  test_countOccurrences();
  test_replaceAll();
  test_tool_patch_valid();
  test_tool_patch_missing_old();
  test_tool_patch_multiple_old();
  test_tool_patch_unbalanced();
  test_tool_patch_equal_unbalanced();
  test_tool_patch_conflict_markers();
  test_tool_patch_empty_old();
  test_tool_patch_empty_new();

  cout << "\nAll tool_patch tests passed!\n" << endl;

  test_tool_write_new_file();
  test_tool_write_overwrite();
  test_tool_write_nested_dirs();
  test_tool_write_curly_balanced();
  test_tool_write_curly_unbalanced();
  test_tool_write_non_curly();
  test_tool_write_large_file();
  test_tool_write_binary();
  test_tool_write_empty();
  test_tool_write_special_chars();
  test_tool_write_multiline();

  cout << "\nAll tool_write tests passed!\n" << endl;

  test_tool_append_new_file();
  test_tool_append_existing_file();
  test_tool_append_multiple();
  test_tool_append_nested_dirs();
  test_tool_append_curly_balanced();
  test_tool_append_curly_unbalanced();
  test_tool_append_binary();
  test_tool_append_empty();
  test_tool_append_special_chars();
  test_tool_append_multiline();
  test_tool_append_log();
  cout << "\nAll tool_append tests passed!\n" << endl;

  test_empty_input();
  test_simple_exchange();
  test_tool_actions();
  test_thinking_skipped();
  test_error_messages();
  test_meta_command_skipped();
  test_last_output_only();
  test_multiple_tool_actions();
  test_hali_inline_content();
  test_realistic_conversation();
  test_no_errors_section();
  test_whitespace_lines();
  cout << "\nAll format_session tests passed!\n" << endl;
}
