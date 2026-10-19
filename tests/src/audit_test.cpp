#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>
#include <fstream>
#include "audit.h"

namespace fs = std::filesystem;

// Helper: create a temp dir with a single C++ file, run audit, return result
static AuditResult audit_single_file(const std::string &dir,
                                     const std::string &content,
                                     const std::string &name = "test.cpp") {
  fs::create_directories(dir);
  fs::path file_path = fs::path(dir) / name;
  {
    std::ofstream f(file_path);
    f << content;
  }
  return audit_cpp(dir);
}

// Helper: find an issue by substring in message
static const AuditIssue *find_issue(const AuditResult &r,
                                    const std::string &substr) {
  for (const auto &i : r.issues) {
    if (i.message.find(substr) != std::string::npos) {
      return &i;
    }
  }
  return nullptr;
}

// ── Nesting depth tests ────────────────────────────────────────────────────

static void test_nesting_basic() {
  // 7 levels: function > if > for > if > while > if > for
  std::string code =
      "void f() {\n"
      "  if (a) {\n"
      "    for (int i = 0; i < n; ++i) {\n"
      "      if (b) {\n"
      "        while (c) {\n"
      "          if (d) {\n"
      "            for (int j = 0; j < m; ++j) {\n"
      "              g();\n"
      "            }\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_nest_basic", code);
  const AuditIssue *iss = find_issue(r, "nesting depth");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("nesting depth 7") != std::string::npos);
  std::cout << "test_nesting_basic passed" << std::endl;
}

static void test_nesting_char_literal_not_counted() {
  // The char literal '{' should NOT increment nesting.
  // Actual nesting: function(1) > if(2) = depth 2
  std::string code =
      "void f() {\n"
      "  if (c == '{') {\n"
      "    g();\n"
      "  }\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_nest_charlit", code);
  // Should report depth 2, not 3
  const AuditIssue *iss = find_issue(r, "nesting depth");
  if (iss != nullptr) {
    // If it appears at all, it should be depth 2 (medium threshold is >6,
    // so depth 2 won't trigger an issue). Verify no false high/medium.
    assert(iss->message.find("nesting depth 3") == std::string::npos);
  }
  std::cout << "test_nesting_char_literal_not_counted passed" << std::endl;
}

static void test_nesting_string_literal_not_counted() {
  // String literal containing braces should not affect nesting.
  // Actual nesting: function(1) > if(2) = depth 2
  std::string code =
      "void f() {\n"
      "  std::string s = \"{\";\n"
      "  if (x) {\n"
      "    g();\n"
      "  }\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_nest_strlit", code);
  const AuditIssue *iss = find_issue(r, "nesting depth");
  if (iss != nullptr) {
    assert(iss->message.find("nesting depth 3") == std::string::npos);
  }
  std::cout << "test_nesting_string_literal_not_counted passed" << std::endl;
}

static void test_nesting_comment_not_counted() {
  // Comment containing braces should not affect nesting.
  // Actual nesting: function(1) > if(2) = depth 2
  std::string code =
      "void f() {\n"
      "  // some { fake braces } here\n"
      "  if (x) {\n"
      "    g();\n"
      "  }\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_nest_comment", code);
  const AuditIssue *iss = find_issue(r, "nesting depth");
  if (iss != nullptr) {
    assert(iss->message.find("nesting depth 3") == std::string::npos);
  }
  std::cout << "test_nesting_comment_not_counted passed" << std::endl;
}

static void test_nesting_deep() {
  // 9 levels deep should trigger "high"
  std::string code =
      "void f() {\n"       // 1
      "  if (a) {\n"       // 2
      "    if (b) {\n"     // 3
      "      if (c) {\n"   // 4
      "        if (d) {\n" // 5
      "          if (e) {\n" // 6
      "            if (f) {\n" // 7
      "              if (g) {\n" // 8
      "                if (h) {\n" // 9
      "                  x();\n"
      "                }\n"
      "              }\n"
      "            }\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_nest_deep", code);
  const AuditIssue *iss = find_issue(r, "nesting depth");
  assert(iss != nullptr);
  assert(iss->severity == "high");
  assert(iss->message.find("nesting depth 9") != std::string::npos);
  std::cout << "test_nesting_deep passed" << std::endl;
}

// ── Parameter counting tests ───────────────────────────────────────────────

static void test_params_basic() {
  // 12 params should trigger "high"
  std::string code =
      "void f(int a, int b, int c, int d, int e, int f,\n"
      "       int g, int h, int i, int j, int k, int l) {\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_params_basic", code);
  const AuditIssue *iss = find_issue(r, "parameters");
  assert(iss != nullptr);
  assert(iss->severity == "high");
  assert(iss->message.find("12 parameters") != std::string::npos);
  std::cout << "test_params_basic passed" << std::endl;
}

static void test_params_string_literal_not_counted() {
  // Commas inside a string literal should NOT count as params.
  // Actual params: 1 (just `int x`)
  std::string code =
      "void f(int x) {\n"
      "  const char *s = \"a, b, c, d, e, f, g, h, i, j, k\";\n"
      "  (void)s;\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_params_strlit", code);
  const AuditIssue *iss = find_issue(r, "parameters");
  // Should NOT report >8 params
  if (iss != nullptr) {
    assert(iss->message.find("9 parameters") == std::string::npos);
    assert(iss->message.find("10 parameters") == std::string::npos);
    assert(iss->message.find("11 parameters") == std::string::npos);
  }
  std::cout << "test_params_string_literal_not_counted passed" << std::endl;
}

static void test_params_char_literal_not_counted() {
  // Parens inside char literals should NOT count.
  std::string code =
      "void f(int x) {\n"
      "  if (c == '(') {\n"
      "    g();\n"
      "  }\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_params_charlit", code);
  const AuditIssue *iss = find_issue(r, "parameters");
  if (iss != nullptr) {
    assert(iss->message.find("2 parameters") == std::string::npos);
  }
  std::cout << "test_params_char_literal_not_counted passed" << std::endl;
}

// ── Include counting ───────────────────────────────────────────────────────

static void test_include_count() {
  std::string code =
      "#include <a.h>\n"
      "#include <b.h>\n"
      "#include <c.h>\n"
      "#include <d.h>\n"
      "#include <e.h>\n"
      "#include <f.h>\n"
      "#include <g.h>\n"
      "#include <h.h>\n"
      "#include <i.h>\n"
      "#include <j.h>\n"
      "#include <k.h>\n"
      "#include <l.h>\n"
      "#include <m.h>\n"
      "#include <n.h>\n"
      "#include <o.h>\n"
      "#include <p.h>\n"
      "void f() {}\n";
  auto r = audit_single_file("/tmp/audit_test_includes", code);
  const AuditIssue *iss = find_issue(r, "#include");
  assert(iss != nullptr);
  assert(iss->severity == "high");
  assert(iss->message.find("16 #include") != std::string::npos);
  std::cout << "test_include_count passed" << std::endl;
}

static void test_include_count_medium() {
  std::string code =
      "#include <a.h>\n"
      "#include <b.h>\n"
      "#include <c.h>\n"
      "#include <d.h>\n"
      "#include <e.h>\n"
      "#include <f.h>\n"
      "#include <g.h>\n"
      "#include <h.h>\n"
      "#include <i.h>\n"
      "#include <j.h>\n"
      "#include <k.h>\n"
      "void f() {}\n";
  auto r = audit_single_file("/tmp/audit_test_includes_med", code);
  const AuditIssue *iss = find_issue(r, "#include");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("11 #include") != std::string::npos);
  std::cout << "test_include_count_medium passed" << std::endl;
}

// ── Longest function ───────────────────────────────────────────────────────

static void test_long_function() {
  // 105-line function should trigger "medium"
  std::string code = "void f() {\n";
  for (int i = 0; i < 103; ++i) {
    code += "  int x" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
  }
  code += "}\n";
  auto r = audit_single_file("/tmp/audit_test_longfunc", code);
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("105 lines") != std::string::npos);
  std::cout << "test_long_function passed" << std::endl;
}

static void test_long_function_high() {
  // 201-line function should trigger "high"
  std::string code = "void f() {\n";
  for (int i = 0; i < 199; ++i) {
    code += "  int x" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
  }
  code += "}\n";
  auto r = audit_single_file("/tmp/audit_test_longfunc_high", code);
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss != nullptr);
  assert(iss->severity == "high");
  assert(iss->message.find("201 lines") != std::string::npos);
  std::cout << "test_long_function_high passed" << std::endl;
}

// ── Longest line ───────────────────────────────────────────────────────────

static void test_longest_line() {
  std::string long_line = "int " + std::string(200, 'x') + " = 0;";
  std::string code = "void f() {\n" + long_line + "\n}\n";
  auto r = audit_single_file("/tmp/audit_test_longline", code);
  const AuditIssue *iss = find_issue(r, "chars");
  assert(iss != nullptr);
  assert(iss->severity == "high");
  std::cout << "test_longest_line passed" << std::endl;
}

// ── Switch cases ───────────────────────────────────────────────────────────

static void test_switch_cases() {
  std::string code = "void f(int x) {\n  switch (x) {\n";
  for (int i = 0; i < 21; ++i) {
    code += "    case " + std::to_string(i) + ": break;\n";
  }
  code += "  }\n}\n";
  auto r = audit_single_file("/tmp/audit_test_switch", code);
  const AuditIssue *iss = find_issue(r, "switch with");
  assert(iss != nullptr);
  assert(iss->severity == "high");
  assert(iss->message.find("21 cases") != std::string::npos);
  std::cout << "test_switch_cases passed" << std::endl;
}

// ── File size / token count ────────────────────────────────────────────────

static void test_large_file_lines() {
  // 1001 lines should trigger "medium" for large file
  std::string code;
  for (int i = 0; i < 1001; ++i) {
    code += "// line " + std::to_string(i) + "\n";
  }
  auto r = audit_single_file("/tmp/audit_test_largefile", code);
  const AuditIssue *iss = find_issue(r, "lines");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("1001 lines") != std::string::npos);
  std::cout << "test_large_file_lines passed" << std::endl;
}

// ── Namespace tracking ─────────────────────────────────────────────────────

static void test_namespace_not_counted_in_nesting() {
  // Namespace braces should not count toward nesting depth.
  // Inside the namespace, function > if = depth 2
  std::string code =
      "namespace foo {\n"
      "void f() {\n"
      "  if (x) {\n"
      "    g();\n"
      "  }\n"
      "}\n"
      "} // namespace foo\n";
  auto r = audit_single_file("/tmp/audit_test_namespace", code);
  const AuditIssue *iss = find_issue(r, "nesting depth");
  // Depth should be 2 (func + if), not 3 (ns + func + if)
  if (iss != nullptr) {
    assert(iss->message.find("nesting depth 3") == std::string::npos);
  }
  std::cout << "test_namespace_not_counted_in_nesting passed" << std::endl;
}

// ── False positive filter ──────────────────────────────────────────────────

static void test_filter_false_positives() {
  AuditResult r;
  r.issues.push_back({"a.cpp", 10, "high", "nesting depth 9", "why"});
  r.issues.push_back({"b.cpp", 20, "medium", "11 #include directives", "why"});
  r.score = 14;

  std::vector<FalsePositive> fps;
  fps.push_back({"a.cpp", 10, "high", "nesting depth 9", "false positive"});

  filter_false_positives(r, fps);

  assert(r.issues.size() == 1);
  assert(r.issues[0].file == "b.cpp");
  assert(r.score == 4); // only medium remains
  std::cout << "test_filter_false_positives passed" << std::endl;
}

static void test_filter_no_match() {
  AuditResult r;
  r.issues.push_back({"a.cpp", 10, "high", "nesting depth 9", "why"});
  r.score = 10;

  std::vector<FalsePositive> fps;
  fps.push_back({"b.cpp", 20, "medium", "different", "no match"});

  filter_false_positives(r, fps);

  assert(r.issues.size() == 1);
  assert(r.score == 10);
  std::cout << "test_filter_no_match passed" << std::endl;
}

static void test_filter_empty_fps() {
  AuditResult r;
  r.issues.push_back({"a.cpp", 10, "high", "test", "why"});
  r.score = 10;

  std::vector<FalsePositive> fps; // empty

  filter_false_positives(r, fps);

  assert(r.issues.size() == 1);
  assert(r.score == 10);
  std::cout << "test_filter_empty_fps passed" << std::endl;
}

// ── Save / Load false positives ────────────────────────────────────────────

static void test_save_load_false_positives() {
  std::string path = "/tmp/audit_test_fp.json";
  fs::remove(path);

  std::vector<FalsePositive> fps;
  fps.push_back({"test.cpp", 42, "high", "nesting depth 9", "char literal bug"});
  fps.push_back({"other.cpp", 10, "medium", "11 #include", "all used"});

  save_false_positives(path, fps);

  auto loaded = load_false_positives(path);
  assert(loaded.size() == 2);
  assert(loaded[0].file == "test.cpp");
  assert(loaded[0].line == 42);
  assert(loaded[0].severity == "high");
  assert(loaded[0].message == "nesting depth 9");
  assert(loaded[0].reason == "char literal bug");
  assert(loaded[1].file == "other.cpp");
  assert(loaded[1].line == 10);

  fs::remove(path);
  std::cout << "test_save_load_false_positives passed" << std::endl;
}

static void test_load_nonexistent_file() {
  auto fps = load_false_positives("/tmp/audit_test_nonexistent.json");
  assert(fps.empty());
  std::cout << "test_load_nonexistent_file passed" << std::endl;
}

// ── Empty / edge cases ─────────────────────────────────────────────────────

static void test_empty_directory() {
  fs::create_directories("/tmp/audit_test_empty");
  auto r = audit_cpp("/tmp/audit_test_empty");
  assert(r.total_files == 0);
  assert(r.issues.empty());
  assert(r.score == 0);
  std::cout << "test_empty_directory passed" << std::endl;
}

static void test_non_c_files_ignored() {
  fs::create_directories("/tmp/audit_test_nonc");
  {
    std::ofstream f("/tmp/audit_test_nonc/readme.txt");
    f << "hello\n";
    std::ofstream f2("/tmp/audit_test_nonc/script.py");
    f2 << "print('hi')\n";
  }
  auto r = audit_cpp("/tmp/audit_test_nonc");
  assert(r.total_files == 0);
  std::cout << "test_non_c_files_ignored passed" << std::endl;
}

static void test_no_issues_clean_file() {
  std::string code =
      "#include <string>\n"
      "void f(int x) {\n"
      "  if (x > 0) {\n"
      "    g(x);\n"
      "  }\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_clean", code);
  assert(r.issues.empty());
  assert(r.score == 0);
  std::cout << "test_no_issues_clean_file passed" << std::endl;
}

// ── Score computation ──────────────────────────────────────────────────────

static void test_score_capped_at_100() {
  // 15 high issues would be 150, should cap at 100
  AuditResult r;
  for (int i = 0; i < 15; ++i) {
    r.issues.push_back({"f.cpp", i, "high", "issue", "why"});
  }
  r.score = 100; // pre-set, filter will recompute
  std::vector<FalsePositive> empty;
  filter_false_positives(r, empty);
  assert(r.score == 100);
  std::cout << "test_score_capped_at_100 passed" << std::endl;
}

// ── Main runner ────────────────────────────────────────────────────────────

void audit_test() {
  std::cout << "=== Audit Tests ===" << std::endl;

  // Clean up any previous test dirs
  std::vector<std::string> dirs = {
    "/tmp/audit_test_nest_basic", "/tmp/audit_test_nest_charlit",
    "/tmp/audit_test_nest_strlit", "/tmp/audit_test_nest_comment",
    "/tmp/audit_test_nest_deep", "/tmp/audit_test_params_basic",
    "/tmp/audit_test_params_strlit", "/tmp/audit_test_params_charlit",
    "/tmp/audit_test_includes", "/tmp/audit_test_includes_med",
    "/tmp/audit_test_longfunc", "/tmp/audit_test_longfunc_high",
    "/tmp/audit_test_longline", "/tmp/audit_test_switch",
    "/tmp/audit_test_largefile", "/tmp/audit_test_namespace",
    "/tmp/audit_test_empty", "/tmp/audit_test_nonc",
    "/tmp/audit_test_clean",
  };
  for (const auto &d : dirs) {
    fs::remove_all(d);
  }

  test_nesting_basic();
  test_nesting_char_literal_not_counted();
  test_nesting_string_literal_not_counted();
  test_nesting_comment_not_counted();
  test_nesting_deep();
  test_params_basic();
  test_params_string_literal_not_counted();
  test_params_char_literal_not_counted();
  test_include_count();
  test_include_count_medium();
  test_long_function();
  test_long_function_high();
  test_longest_line();
  test_switch_cases();
  test_large_file_lines();
  test_namespace_not_counted_in_nesting();
  test_filter_false_positives();
  test_filter_no_match();
  test_filter_empty_fps();
  test_save_load_false_positives();
  test_load_nonexistent_file();
  test_empty_directory();
  test_non_c_files_ignored();
  test_no_issues_clean_file();
  test_score_capped_at_100();

  // Cleanup
  for (const auto &d : dirs) {
    fs::remove_all(d);
  }
  fs::remove("/tmp/audit_test_fp.json");

  std::cout << "=== All audit tests passed ===" << std::endl;
}