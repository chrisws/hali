// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <iostream>
#include <cassert>
#include <string>
#include <fstream>
#include <filesystem>
#include <sstream>

#include "line_edit.h"

namespace fs = std::filesystem;
using namespace std;

#define RUN_TEST(name) do {                         \
    std::cout << "Running " << #name << "... ";     \
    try { name(); std::cout << "PASSED\n"; }        \
    catch (const std::exception& e) {               \
      std::cout << "FAILED: " << e.what() << "\n";  \
    }                                               \
  } while(0)

static const string SANDBOX = "/tmp/hali_test_line_edit_sandbox";

static void setup_sandbox() {
  fs::remove_all(SANDBOX);
  fs::create_directories(SANDBOX);
}

static void teardown_sandbox() {
  fs::remove_all(SANDBOX);
}

static void write_test_pom(const string &path) {
  ofstream out(path);
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<project xmlns=\"http://maven.apache.org/POM/4.0.0\">\n"
      << "  <modelVersion>4.0.0</modelVersion>\n"
      << "  <groupId>com.example</groupId>\n"
      << "  <artifactId>test-app</artifactId>\n"
      << "  <version>1.0-SNAPSHOT</version>\n"
      << "  <packaging>jar</packaging>\n"
      << "  <properties>\n"
      << "    <maven.compiler.source>17</maven.compiler.source>\n"
      << "    <maven.compiler.target>17</maven.compiler.target>\n"
      << "  </properties>\n"
      << "</project>\n";
}

static string read_test_file(const string &path) {
  ifstream in(path);
  ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

//
// read tests
//

static void test_read_full_file() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "read", "pom.xml");
  assert(result.find("OK") == 0);
  assert(result.find("1:") != string::npos);
  assert(result.find("<?xml") != string::npos);
  assert(result.find("11:") != string::npos);
  assert(result.find("</project>") != string::npos);
  teardown_sandbox();
}

static void test_read_range() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "read", "pom.xml 3,5");
  assert(result.find("OK") == 0);
  assert(result.find("3:") != string::npos);
  assert(result.find("<modelVersion>4.0.0</modelVersion>") != string::npos);
  assert(result.find("<groupId>com.example</groupId>") != string::npos);
  assert(result.find("<artifactId>test-app</artifactId>") != string::npos);
  // Should NOT contain line 1 content
  assert(result.find("<?xml") == string::npos);
  // Should NOT contain line 6 content
  assert(result.find("<version>1.0-SNAPSHOT</version>") == string::npos);
  teardown_sandbox();
}

static void test_read_out_of_range() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "read", "pom.xml 1,99");
  assert(result.find("ERROR") == 0);
  teardown_sandbox();
}

static void test_read_missing_file() {
  setup_sandbox();

  string result = line_edit_exec(SANDBOX, "read", "nonexistent.xml");
  assert(result.find("ERROR") == 0);
  assert(result.find("not found") != string::npos);
  teardown_sandbox();
}

//
// write tests
//

static void test_write_replace_line() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "write",
    "pom.xml 4 <groupId>com.neworg</groupId>");
  assert(result.find("OK") == 0);
  assert(result.find("replaced") != string::npos);

  string content = read_test_file(SANDBOX + "/pom.xml");
  assert(content.find("<groupId>com.neworg</groupId>") != string::npos);
  assert(content.find("<groupId>com.example</groupId>") == string::npos);
  teardown_sandbox();
}

static void test_write_preserves_indentation() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  // Line 8 is "    <maven.compiler.source>17</maven.compiler.source>"
  string result = line_edit_exec(SANDBOX, "write",
    "pom.xml 8     <maven.compiler.source>21</maven.compiler.source>");
  assert(result.find("OK") == 0);

  string content = read_test_file(SANDBOX + "/pom.xml");
  assert(content.find("    <maven.compiler.source>21</maven.compiler.source>") != string::npos);
  teardown_sandbox();
}

static void test_write_out_of_range() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "write",
    "pom.xml 99 some content");
  assert(result.find("ERROR") == 0);
  assert(result.find("out of range") != string::npos);
  teardown_sandbox();
}

static void test_write_returns_context() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "write",
    "pom.xml 4 <groupId>com.neworg</groupId>");
  assert(result.find("OK") == 0);
  // Should show surrounding context with line numbers
  assert(result.find("3:") != string::npos);
  assert(result.find("4:") != string::npos);
  assert(result.find("5:") != string::npos);
  teardown_sandbox();
}

//
// insert tests
//

static void test_insert_before_line() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  // Insert before line 11 (</project>)
  string result = line_edit_exec(SANDBOX, "insert",
    "pom.xml 11   <description>A test project</description>");
  assert(result.find("OK") == 0);
  assert(result.find("inserted") != string::npos);

  string content = read_test_file(SANDBOX + "/pom.xml");
  assert(content.find("<description>A test project</description>") != string::npos);
  // File should now have 13 lines (12 original + 1 inserted)
  istringstream ss(content);
  string line;
  int count = 0;
  while (getline(ss, line)) count++;
  assert(count == 13);
  teardown_sandbox();
}

static void test_insert_at_end() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  // Insert at line 12 (after last line 11)
  string result = line_edit_exec(SANDBOX, "insert",
    "pom.xml 12 <new-line/>");
  assert(result.find("OK") == 0);

  string content = read_test_file(SANDBOX + "/pom.xml");
  assert(content.find("<new-line/>") != string::npos);
  teardown_sandbox();
}

static void test_insert_out_of_range() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "insert",
    "pom.xml 99 some content");
  assert(result.find("ERROR") == 0);
  assert(result.find("out of range") != string::npos);
  teardown_sandbox();
}

//
// delete tests
//

static void test_delete_single_line() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "delete", "pom.xml 7,7");
  assert(result.find("OK") == 0);
  assert(result.find("deleted") != string::npos);

  string content = read_test_file(SANDBOX + "/pom.xml");
  assert(content.find("<packaging>jar</packaging>") == string::npos);
  teardown_sandbox();
}

static void test_delete_range() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  // Delete lines 7-10 (packaging + properties block)
  string result = line_edit_exec(SANDBOX, "delete", "pom.xml 7,10");
  assert(result.find("OK") == 0);
  assert(result.find("deleted") != string::npos);

  string content = read_test_file(SANDBOX + "/pom.xml");
  assert(content.find("<packaging>") == string::npos);
  assert(content.find("<properties>") == string::npos);
  assert(content.find("</project>") != string::npos);
  teardown_sandbox();
}

static void test_delete_out_of_range() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "delete", "pom.xml 1,99");
  assert(result.find("ERROR") == 0);
  teardown_sandbox();
}

//
// copy/paste tests
//

static void test_copy_and_paste() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  // Copy lines 3-5
  string result = line_edit_exec(SANDBOX, "copy", "pom.xml 3,5");
  assert(result.find("OK") == 0);
  assert(result.find("copied") != string::npos);

  // Paste at line 11 (before </project>)
  result = line_edit_exec(SANDBOX, "paste", "pom.xml 11");
  assert(result.find("OK") == 0);
  assert(result.find("pasted") != string::npos);

  string content = read_test_file(SANDBOX + "/pom.xml");
  // Should have duplicate groupId/artifactId/version
  assert(content.find("<groupId>com.example</groupId>") != string::npos);
  // Count occurrences
  int count = 0;
  size_t pos = 0;
  while ((pos = content.find("<groupId>", pos)) != string::npos) {
    count++;
    pos++;
  }
  assert(count == 2);
  teardown_sandbox();
}

static void test_cut_and_paste() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  // Cut lines 7-10
  string result = line_edit_exec(SANDBOX, "cut", "pom.xml 7,10");
  assert(result.find("OK") == 0);
  assert(result.find("cut") != string::npos);

  // File should no longer have those lines
  string content = read_test_file(SANDBOX + "/pom.xml");
  assert(content.find("<packaging>") == string::npos);
  assert(content.find("<properties>") == string::npos);

  // Paste at line 6 (after version)
  result = line_edit_exec(SANDBOX, "paste", "pom.xml 6");
  assert(result.find("OK") == 0);
  assert(result.find("pasted") != string::npos);

  content = read_test_file(SANDBOX + "/pom.xml");
  assert(content.find("<packaging>jar</packaging>") != string::npos);
  assert(content.find("<properties>") != string::npos);
  teardown_sandbox();
}

static void test_paste_empty_buffer() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  // Clear first
  line_edit_exec(SANDBOX, "clear", "");

  string result = line_edit_exec(SANDBOX, "paste", "pom.xml 5");
  assert(result.find("ERROR") == 0);
  assert(result.find("empty") != string::npos);
  teardown_sandbox();
}

//
// show/clear tests
//

static void test_show_empty() {
  setup_sandbox();
  line_edit_exec(SANDBOX, "clear", "");

  string result = line_edit_exec(SANDBOX, "show", "");
  assert(result.find("empty") != string::npos);
  teardown_sandbox();
}

static void test_show_with_content() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  line_edit_exec(SANDBOX, "copy", "pom.xml 4,5");
  string result = line_edit_exec(SANDBOX, "show", "");
  assert(result.find("<groupId>") != string::npos);
  assert(result.find("<artifactId>") != string::npos);
  teardown_sandbox();
}

static void test_clear() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  line_edit_exec(SANDBOX, "copy", "pom.xml 3,4");
  string result = line_edit_exec(SANDBOX, "clear", "");
  assert(result.find("cleared") != string::npos);

  result = line_edit_exec(SANDBOX, "show", "");
  assert(result.find("empty") != string::npos);
  teardown_sandbox();
}

//
// edge cases
//

static void test_single_line_file() {
  setup_sandbox();
  {
    ofstream out(SANDBOX + "/single.txt");
    out << "hello\n";
  }

  string result = line_edit_exec(SANDBOX, "read", "single.txt");
  assert(result.find("OK") == 0);
  assert(result.find("1: hello") != string::npos);

  result = line_edit_exec(SANDBOX, "write", "single.txt 1 world");
  assert(result.find("OK") == 0);

  string content = read_test_file(SANDBOX + "/single.txt");
  assert(content == "world\n");
  teardown_sandbox();
}

static void test_unknown_command() {
  setup_sandbox();

  string result = line_edit_exec(SANDBOX, "bogus", "");
  assert(result.find("ERROR") == 0);
  assert(result.find("unknown") != string::npos);
  teardown_sandbox();
}

static void test_outside_sandbox() {
  setup_sandbox();

  string result = line_edit_exec(SANDBOX, "read", "../../etc/passwd");
  assert(result.find("ERROR") == 0);
  assert(result.find("outside sandbox") != string::npos);
  teardown_sandbox();
}

//
// XML indentation preservation
//

static void test_xml_2space_indent_preserved() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  // Write a new line with correct 2-space indentation
  string result = line_edit_exec(SANDBOX, "write",
    "pom.xml 6   <name>test-app</name>");
  assert(result.find("OK") == 0);

  string content = read_test_file(SANDBOX + "/pom.xml");
  // Verify the line has exactly 2 spaces of indentation
  istringstream ss(content);
  string line;
  int n = 0;
  while (getline(ss, line)) {
    n++;
    if (n == 6) {
      assert(line == "  <name>test-app</name>");
    }
  }
  teardown_sandbox();
}

static void test_no_tabs_in_output() {
  setup_sandbox();
  write_test_pom(SANDBOX + "/pom.xml");

  string result = line_edit_exec(SANDBOX, "read", "pom.xml");
  assert(result.find("\t") == string::npos);
  teardown_sandbox();
}

void line_edit_test() {
  cout << "=== Running TOOL:LINE_EDIT Unit Tests ===\n\n";

  RUN_TEST(test_read_full_file);
  RUN_TEST(test_read_range);
  RUN_TEST(test_read_out_of_range);
  RUN_TEST(test_read_missing_file);
  RUN_TEST(test_write_replace_line);
  RUN_TEST(test_write_preserves_indentation);
  RUN_TEST(test_write_out_of_range);
  RUN_TEST(test_write_returns_context);
  RUN_TEST(test_insert_before_line);
  RUN_TEST(test_insert_at_end);
  RUN_TEST(test_insert_out_of_range);
  RUN_TEST(test_delete_single_line);
  RUN_TEST(test_delete_range);
  RUN_TEST(test_delete_out_of_range);
  RUN_TEST(test_copy_and_paste);
  RUN_TEST(test_cut_and_paste);
  RUN_TEST(test_paste_empty_buffer);
  RUN_TEST(test_show_empty);
  RUN_TEST(test_show_with_content);
  RUN_TEST(test_clear);
  RUN_TEST(test_single_line_file);
  RUN_TEST(test_unknown_command);
  RUN_TEST(test_outside_sandbox);
  RUN_TEST(test_xml_2space_indent_preserved);
  RUN_TEST(test_no_tabs_in_output);

  cout << "\n=== All TOOL:LINE_EDIT tests completed ===\n";
}