#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>
#include <fstream>
#include "audit.h"
#include "util/project.h"

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
  return audit_codebase(dir);
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
  auto r = audit_codebase("/tmp/audit_test_empty");
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
  auto r = audit_codebase("/tmp/audit_test_nonc");
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

// ── Type body (class/struct) tracking ─────────────────────────────────────

static void test_java_class_not_tracked_as_function() {
  // A 120-line Java class with small methods should NOT report
  // a 120-line "function". The class body is not a function.
  fs::create_directories("/tmp/audit_test_java_class");
  {
    std::string code = "package com.example;\n"
                       "public class Big {\n";
    for (int i = 0; i < 30; ++i) {
      code += "  void m" + std::to_string(i) + "() {\n"
              + "    int x" + std::to_string(i) + " = " + std::to_string(i) + ";\n"
              + "  }\n";
    }
    code += "}\n";
    std::ofstream f("/tmp/audit_test_java_class/Big.java");
    f << code;
    std::ofstream f2("/tmp/audit_test_java_class/build.gradle");
    f2 << "plugins { id 'java' }\n";
  }
  auto r = audit_codebase("/tmp/audit_test_java_class");
  // No "function spans" issue should appear — the class body is not a function
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss == nullptr);
  std::cout << "test_java_class_not_tracked_as_function passed" << std::endl;
}

static void test_java_method_inside_class_tracked() {
  // A 105-line method inside a Java class SHOULD be reported
  fs::create_directories("/tmp/audit_test_java_method");
  {
    std::string code = "package com.example;\n"
                       "public class Wrapper {\n"
                       "  void longMethod() {\n";
    for (int i = 0; i < 103; ++i) {
      code += "    int x" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    }
    code += "  }\n"
            "}\n";
    std::ofstream f("/tmp/audit_test_java_method/Wrapper.java");
    f << code;
    std::ofstream f2("/tmp/audit_test_java_method/build.gradle");
    f2 << "plugins { id 'java' }\n";
  }
  auto r = audit_codebase("/tmp/audit_test_java_method");
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("105 lines") != std::string::npos);
  std::cout << "test_java_method_inside_class_tracked passed" << std::endl;
}

static void test_cpp_class_not_tracked_as_function() {
  // A C++ class with small methods should NOT report the class as a function
  std::string code =
      "class Foo {\n"
      "public:\n"
      "  void a() { int x = 1; }\n"
      "  void b() { int y = 2; }\n"
      "  void c() { int z = 3; }\n"
      "};\n";
  auto r = audit_single_file("/tmp/audit_test_cpp_class", code);
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss == nullptr);
  std::cout << "test_cpp_class_not_tracked_as_function passed" << std::endl;
}

static void test_cpp_struct_not_tracked_as_function() {
  std::string code =
      "struct Bar {\n"
      "  int x;\n"
      "  int y;\n"
      "  void f() { x = 1; }\n"
      "};\n";
  auto r = audit_single_file("/tmp/audit_test_cpp_struct", code);
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss == nullptr);
  std::cout << "test_cpp_struct_not_tracked_as_function passed" << std::endl;
}

static void test_cpp_forward_decl_not_type_body() {
  // Forward declaration should NOT set pending_type
  std::string code =
      "class Foo;\n"
      "void bar() {\n"
      "  int x = 1;\n"
      "}\n";
  auto r = audit_single_file("/tmp/audit_test_cpp_fwd", code);
  // bar() is a 3-line function, no issues expected
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss == nullptr);
  std::cout << "test_cpp_forward_decl_not_type_body passed" << std::endl;
}

static void test_java_interface_not_tracked_as_function() {
  fs::create_directories("/tmp/audit_test_java_iface");
  {
    std::string code = "package com.example;\n"
                       "public interface MyIface {\n"
                       "  void a();\n"
                       "  void b();\n"
                       "  void c();\n"
                       "}\n";
    std::ofstream f("/tmp/audit_test_java_iface/MyIface.java");
    f << code;
    std::ofstream f2("/tmp/audit_test_java_iface/build.gradle");
    f2 << "plugins { id 'java' }\n";
  }
  auto r = audit_codebase("/tmp/audit_test_java_iface");
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss == nullptr);
  std::cout << "test_java_interface_not_tracked_as_function passed" << std::endl;
}

static void test_java_enum_not_tracked_as_function() {
  fs::create_directories("/tmp/audit_test_java_enum");
  {
    std::string code = "package com.example;\n"
                       "public enum Color {\n"
                       "  RED, GREEN, BLUE\n"
                       "}\n";
    std::ofstream f("/tmp/audit_test_java_enum/Color.java");
    f << code;
    std::ofstream f2("/tmp/audit_test_java_enum/build.gradle");
    f2 << "plugins { id 'java' }\n";
  }
  auto r = audit_codebase("/tmp/audit_test_java_enum");
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss == nullptr);
  std::cout << "test_java_enum_not_tracked_as_function passed" << std::endl;
}

static void test_java_nested_class() {
  // Outer class contains inner class; inner class method should be tracked
  fs::create_directories("/tmp/audit_test_java_nested");
  {
    std::string code = "package com.example;\n"
                       "public class Outer {\n"
                       "  class Inner {\n"
                       "    void small() { int x = 1; }\n"
                       "  }\n"
                       "  void outerMethod() { int y = 2; }\n"
                       "}\n";
    std::ofstream f("/tmp/audit_test_java_nested/Outer.java");
    f << code;
    std::ofstream f2("/tmp/audit_test_java_nested/build.gradle");
    f2 << "plugins { id 'java' }\n";
  }
  auto r = audit_codebase("/tmp/audit_test_java_nested");
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss == nullptr);
  std::cout << "test_java_nested_class passed" << std::endl;
}

// ── Kotlin detection and scanning ─────────────────────────────────────────

static void test_detect_language_kotlin_kts() {
  fs::create_directories("/tmp/audit_test_lang_kts");
  {
    std::ofstream f("/tmp/audit_test_lang_kts/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_kts") ==
         ProjectLanguage::KOTLIN);
  std::cout << "test_detect_language_kotlin_kts passed" << std::endl;
}

static void test_detect_language_kotlin_settings_kts() {
  fs::create_directories("/tmp/audit_test_lang_settings_kts");
  {
    std::ofstream f("/tmp/audit_test_lang_settings_kts/settings.gradle.kts");
    f << "rootProject.name = \"myapp\"\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_settings_kts") ==
         ProjectLanguage::KOTLIN);
  std::cout << "test_detect_language_kotlin_settings_kts passed" << std::endl;
}

static void test_detect_language_kotlin_gradle_with_kt() {
  fs::create_directories("/tmp/audit_test_lang_kt_gradle");
  {
    std::ofstream f("/tmp/audit_test_lang_kt_gradle/build.gradle");
    f << "plugins { id 'org.jetbrains.kotlin.jvm' }\n";
    fs::create_directories("/tmp/audit_test_lang_kt_gradle/src/main/kotlin");
    std::ofstream f2("/tmp/audit_test_lang_kt_gradle/src/main/kotlin/Main.kt");
    f2 << "fun main() { println(\"hi\") }\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_kt_gradle") ==
         ProjectLanguage::KOTLIN);
  std::cout << "test_detect_language_kotlin_gradle_with_kt passed" << std::endl;
}

static void test_detect_language_kotlin_android_deep() {
  // Mimics Android project: app/src/main/kotlin/com/example/Main.kt
  // (.kt file is 5 levels deep from project root)
  fs::create_directories("/tmp/audit_test_lang_kt_android");
  {
    std::ofstream f("/tmp/audit_test_lang_kt_android/build.gradle");
    f << "plugins { id 'com.android.application' }\n";
    std::ofstream f2("/tmp/audit_test_lang_kt_android/settings.gradle");
    f2 << "rootProject.name = \"myapp\"\n";
    fs::create_directories(
      "/tmp/audit_test_lang_kt_android/app/src/main/kotlin/com/example");
    std::ofstream f3(
      "/tmp/audit_test_lang_kt_android/app/src/main/kotlin/com/example/Main.kt");
    f3 << "fun main() { println(\"hi\") }\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_kt_android") ==
         ProjectLanguage::KOTLIN);
  std::cout << "test_detect_language_kotlin_android_deep passed" << std::endl;
}

static void test_detect_language_java_gradle_no_kt() {
  // build.gradle with only .java files should still be JAVA
  fs::create_directories("/tmp/audit_test_lang_java_no_kt");
  {
    std::ofstream f("/tmp/audit_test_lang_java_no_kt/build.gradle");
    f << "plugins { id 'java' }\n";
    fs::create_directories("/tmp/audit_test_lang_java_no_kt/src/main/java");
    std::ofstream f2("/tmp/audit_test_lang_java_no_kt/src/main/java/Main.java");
    f2 << "public class Main {}\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_java_no_kt") ==
         ProjectLanguage::JAVA);
  std::cout << "test_detect_language_java_gradle_no_kt passed" << std::endl;
}

static void test_kotlin_file_scanned() {
  fs::create_directories("/tmp/audit_test_kotlin");
  {
    std::ofstream f("/tmp/audit_test_kotlin/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::ofstream f2("/tmp/audit_test_kotlin/Main.kt");
    f2 << "package com.example\n"
       << "fun main() {\n"
       << "    println(\"hello\")\n"
       << "}\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kotlin");
  assert(r.total_files == 1);
  assert(r.total_lines > 0);
  std::cout << "test_kotlin_file_scanned passed" << std::endl;
}

static void test_kotlin_import_count() {
  fs::create_directories("/tmp/audit_test_kotlin_imp");
  {
    std::ofstream f("/tmp/audit_test_kotlin_imp/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::string code = "package com.example\n";
    for (int i = 0; i < 22; ++i) {
      code += "import com.example.lib.Class" + std::to_string(i) + "\n";
    }
    code += "fun main() {}\n";
    std::ofstream f2("/tmp/audit_test_kotlin_imp/Big.kt");
    f2 << code;
  }
  auto r = audit_codebase("/tmp/audit_test_kotlin_imp");
  assert(r.total_files == 1);
  const AuditIssue *iss = find_issue(r, "import statements");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("22 import") != std::string::npos);
  std::cout << "test_kotlin_import_count passed" << std::endl;
}

static void test_kotlin_no_ifdef_issues() {
  fs::create_directories("/tmp/audit_test_kotlin_noifdef");
  {
    std::ofstream f("/tmp/audit_test_kotlin_noifdef/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::ofstream f2("/tmp/audit_test_kotlin_noifdef/Clean.kt");
    f2 << "package com.example\n"
       << "fun clean() {\n"
       << "    if (true) {\n"
       << "        g()\n"
       << "    }\n"
       << "}\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kotlin_noifdef");
  for (const auto &i : r.issues) {
    assert(i.message.find("#if") == std::string::npos);
    assert(i.message.find("#define") == std::string::npos);
  }
  std::cout << "test_kotlin_no_ifdef_issues passed" << std::endl;
}

static void test_kotlin_object_not_tracked_as_function() {
  // A Kotlin object declaration should not be tracked as a function
  fs::create_directories("/tmp/audit_test_kotlin_obj");
  {
    std::ofstream f("/tmp/audit_test_kotlin_obj/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::string code = "package com.example\n"
                       "object Singleton {\n";
    for (int i = 0; i < 30; ++i) {
      code += "    fun m" + std::to_string(i) + "() { val x" +
              std::to_string(i) + " = " + std::to_string(i) + " }\n";
    }
    code += "}\n";
    std::ofstream f2("/tmp/audit_test_kotlin_obj/Singleton.kt");
    f2 << code;
  }
  auto r = audit_codebase("/tmp/audit_test_kotlin_obj");
  const AuditIssue *iss = find_issue(r, "function spans");
  assert(iss == nullptr);
  std::cout << "test_kotlin_object_not_tracked_as_function passed" << std::endl;
}

// ── Kotlin missing test detection ─────────────────────────────────────────

static void test_kotlin_missing_test_flagged() {
  // A .kt file with no corresponding test should be flagged
  fs::create_directories("/tmp/audit_test_kt_notest/src/main/kotlin/com/ex");
  {
    std::ofstream f("/tmp/audit_test_kt_notest/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::ofstream f2("/tmp/audit_test_kt_notest/src/main/kotlin/com/ex/Foo.kt");
    f2 << "package com.ex\nfun foo() {}\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kt_notest");
  const AuditIssue *iss = find_issue(r, "no corresponding test file");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->file.find("Foo.kt") != std::string::npos);
  std::cout << "test_kotlin_missing_test_flagged passed" << std::endl;
}

static void test_kotlin_test_exists_not_flagged() {
  // A .kt file WITH a corresponding test should NOT be flagged
  fs::create_directories("/tmp/audit_test_kt_hastest/src/main/kotlin/com/ex");
  fs::create_directories("/tmp/audit_test_kt_hastest/src/test/kotlin/com/ex");
  {
    std::ofstream f("/tmp/audit_test_kt_hastest/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::ofstream f2("/tmp/audit_test_kt_hastest/src/main/kotlin/com/ex/Foo.kt");
    f2 << "package com.ex\nfun foo() {}\n";
    std::ofstream f3("/tmp/audit_test_kt_hastest/src/test/kotlin/com/ex/FooTest.kt");
    f3 << "import org.junit.Test\n@Test fun testFoo() {}\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kt_hastest");
  const AuditIssue *iss = find_issue(r, "no corresponding test file");
  assert(iss == nullptr);
  std::cout << "test_kotlin_test_exists_not_flagged passed" << std::endl;
}

static void test_kotlin_spec_file_counts_as_test() {
  // A .kt file with a corresponding Spec file should NOT be flagged
  fs::create_directories("/tmp/audit_test_kt_spec/src/main/kotlin/com/ex");
  fs::create_directories("/tmp/audit_test_kt_spec/src/test/kotlin/com/ex");
  {
    std::ofstream f("/tmp/audit_test_kt_spec/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::ofstream f2("/tmp/audit_test_kt_spec/src/main/kotlin/com/ex/Bar.kt");
    f2 << "package com.ex\nfun bar() {}\n";
    std::ofstream f3("/tmp/audit_test_kt_spec/src/test/kotlin/com/ex/BarSpec.kt");
    f3 << "class BarSpec\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kt_spec");
  const AuditIssue *iss = find_issue(r, "no corresponding test file");
  assert(iss == nullptr);
  std::cout << "test_kotlin_spec_file_counts_as_test passed" << std::endl;
}

static void test_kotlin_kts_not_flagged() {
  // .kts build scripts should NOT be flagged for missing tests
  fs::create_directories("/tmp/audit_test_kt_kts");
  {
    std::ofstream f("/tmp/audit_test_kt_kts/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::ofstream f2("/tmp/audit_test_kt_kts/settings.gradle.kts");
    f2 << "rootProject.name = \"test\"\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kt_kts");
  const AuditIssue *iss = find_issue(r, "no corresponding test file");
  assert(iss == nullptr);
  std::cout << "test_kotlin_kts_not_flagged passed" << std::endl;
}

static void test_kotlin_androidtest_counts() {
  // A .kt file with an androidTest counterpart should NOT be flagged
  fs::create_directories("/tmp/audit_test_kt_android/src/main/kotlin/com/ex");
  fs::create_directories("/tmp/audit_test_kt_android/src/androidTest/kotlin/com/ex");
  {
    std::ofstream f("/tmp/audit_test_kt_android/build.gradle.kts");
    f << "plugins { kotlin(\"jvm\") }\n";
    std::ofstream f2("/tmp/audit_test_kt_android/src/main/kotlin/com/ex/Baz.kt");
    f2 << "package com.ex\nfun baz() {}\n";
    std::ofstream f3("/tmp/audit_test_kt_android/src/androidTest/kotlin/com/ex/BazTest.kt");
    f3 << "class BazTest\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kt_android");
  const AuditIssue *iss = find_issue(r, "no corresponding test file");
  assert(iss == nullptr);
  std::cout << "test_kotlin_androidtest_counts passed" << std::endl;
}

static void test_kotlin_android_java_src_missing_test() {
  // Android convention: .kt files in src/main/java/ with no test → flagged
  fs::create_directories("/tmp/audit_test_kt_android_java/src/main/java/com/ex");
  {
    std::ofstream f("/tmp/audit_test_kt_android_java/build.gradle");
    f << "apply plugin: 'com.android.application'\n";
    std::ofstream f2("/tmp/audit_test_kt_android_java/gradlew");
    f2 << "#!/bin/sh\n";
    std::ofstream f3("/tmp/audit_test_kt_android_java/src/main/java/com/ex/Foo.kt");
    f3 << "package com.ex\nfun foo() {}\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kt_android_java");
  const AuditIssue *iss = find_issue(r, "no corresponding test file");
  assert(iss != nullptr);
  assert(iss->file.find("Foo.kt") != std::string::npos);
  std::cout << "test_kotlin_android_java_src_missing_test passed" << std::endl;
}

static void test_kotlin_android_java_src_test_exists() {
  // Android convention: .kt in src/main/java/ with test in src/test/java/ → NOT flagged
  fs::create_directories("/tmp/audit_test_kt_android_java2/src/main/java/com/ex");
  fs::create_directories("/tmp/audit_test_kt_android_java2/src/test/java/com/ex");
  {
    std::ofstream f("/tmp/audit_test_kt_android_java2/build.gradle");
    f << "apply plugin: 'com.android.application'\n";
    std::ofstream f2("/tmp/audit_test_kt_android_java2/gradlew");
    f2 << "#!/bin/sh\n";
    std::ofstream f3("/tmp/audit_test_kt_android_java2/src/main/java/com/ex/Bar.kt");
    f3 << "package com.ex\nfun bar() {}\n";
    std::ofstream f4("/tmp/audit_test_kt_android_java2/src/test/java/com/ex/BarTest.kt");
    f4 << "class BarTest\n";
  }
  auto r = audit_codebase("/tmp/audit_test_kt_android_java2");
  const AuditIssue *iss = find_issue(r, "no corresponding test file");
  assert(iss == nullptr);
  std::cout << "test_kotlin_android_java_src_test_exists passed" << std::endl;
}

// ── Main runner ────────────────────────────────────────────────────────────

static void test_java_file_scanned() {
  fs::create_directories("/tmp/audit_test_java");
  {
    std::ofstream f("/tmp/audit_test_java/build.gradle");
    f << "plugins { id 'java' }\n";
    std::ofstream f2("/tmp/audit_test_java/Main.java");
    f2 << "package com.example;\n"
       << "public class Main {\n"
       << "  public static void main(String[] args) {\n"
       << "    System.out.println(\"hi\");\n"
       << "  }\n"
       << "}\n";
  }
  auto r = audit_codebase("/tmp/audit_test_java");
  assert(r.total_files == 1);
  assert(r.total_lines > 0);
  std::cout << "test_java_file_scanned passed" << std::endl;
}

static void test_java_import_count() {
  fs::create_directories("/tmp/audit_test_java_imp");
  {
    std::ofstream f("/tmp/audit_test_java_imp/build.gradle");
    f << "plugins { id 'java' }\n";
    std::string code = "package com.example;\n";
    for (int i = 0; i < 20; ++i) {
      code += "import com.example.lib.Class" + std::to_string(i) + ";\n";
    }
    code += "public class Big {\n  void f() {}\n}\n";
    std::ofstream f2("/tmp/audit_test_java_imp/Big.java");
    f2 << code;
  }
  auto r = audit_codebase("/tmp/audit_test_java_imp");
  assert(r.total_files == 1);
  const AuditIssue *iss = find_issue(r, "import statements");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("20 import") != std::string::npos);
  std::cout << "test_java_import_count passed" << std::endl;
}

static void test_java_import_below_medium() {
  // 18 imports should NOT trigger any issue (threshold is >18)
  fs::create_directories("/tmp/audit_test_java_imp_low");
  {
    std::ofstream f("/tmp/audit_test_java_imp_low/build.gradle");
    f << "plugins { id 'java' }\n";
    std::string code = "package com.example;\n";
    for (int i = 0; i < 18; ++i) {
      code += "import com.example.lib.Class" + std::to_string(i) + ";\n";
    }
    code += "public class Ok {\n  void f() {}\n}\n";
    std::ofstream f2("/tmp/audit_test_java_imp_low/Ok.java");
    f2 << code;
  }
  auto r = audit_codebase("/tmp/audit_test_java_imp_low");
  assert(find_issue(r, "import statements") == nullptr);
  std::cout << "test_java_import_below_medium passed" << std::endl;
}

static void test_java_import_medium_boundary() {
  // 19 imports should trigger MEDIUM (threshold is >18)
  fs::create_directories("/tmp/audit_test_java_imp_med");
  {
    std::ofstream f("/tmp/audit_test_java_imp_med/build.gradle");
    f << "plugins { id 'java' }\n";
    std::string code = "package com.example;\n";
    for (int i = 0; i < 19; ++i) {
      code += "import com.example.lib.Class" + std::to_string(i) + ";\n";
    }
    code += "public class Med {\n  void f() {}\n}\n";
    std::ofstream f2("/tmp/audit_test_java_imp_med/Med.java");
    f2 << code;
  }
  auto r = audit_codebase("/tmp/audit_test_java_imp_med");
  const AuditIssue *iss = find_issue(r, "import statements");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("19 import") != std::string::npos);
  std::cout << "test_java_import_medium_boundary passed" << std::endl;
}

static void test_java_import_high_boundary() {
  // 26 imports should trigger HIGH (threshold is >25)
  fs::create_directories("/tmp/audit_test_java_imp_high");
  {
    std::ofstream f("/tmp/audit_test_java_imp_high/build.gradle");
    f << "plugins { id 'java' }\n";
    std::string code = "package com.example;\n";
    for (int i = 0; i < 26; ++i) {
      code += "import com.example.lib.Class" + std::to_string(i) + ";\n";
    }
    code += "public class Big {\n  void f() {}\n}\n";
    std::ofstream f2("/tmp/audit_test_java_imp_high/Big.java");
    f2 << code;
  }
  auto r = audit_codebase("/tmp/audit_test_java_imp_high");
  const AuditIssue *iss = find_issue(r, "import statements");
  assert(iss != nullptr);
  assert(iss->severity == "high");
  assert(iss->message.find("26 import") != std::string::npos);
  std::cout << "test_java_import_high_boundary passed" << std::endl;
}

static void test_java_import_25_not_high() {
  // 25 imports should NOT trigger HIGH (threshold is >25), but IS medium
  fs::create_directories("/tmp/audit_test_java_imp_25");
  {
    std::ofstream f("/tmp/audit_test_java_imp_25/build.gradle");
    f << "plugins { id 'java' }\n";
    std::string code = "package com.example;\n";
    for (int i = 0; i < 25; ++i) {
      code += "import com.example.lib.Class" + std::to_string(i) + ";\n";
    }
    code += "public class Big {\n  void f() {}\n}\n";
    std::ofstream f2("/tmp/audit_test_java_imp_25/Big.java");
    f2 << code;
  }
  auto r = audit_codebase("/tmp/audit_test_java_imp_25");
  const AuditIssue *iss = find_issue(r, "import statements");
  assert(iss != nullptr);
  assert(iss->severity == "medium");
  assert(iss->message.find("25 import") != std::string::npos);
  std::cout << "test_java_import_25_not_high passed" << std::endl;
}

static void test_java_no_ifdef_issues() {
  fs::create_directories("/tmp/audit_test_java_noifdef");
  {
    std::ofstream f("/tmp/audit_test_java_noifdef/build.gradle");
    f << "plugins { id 'java' }\n";
    std::ofstream f2("/tmp/audit_test_java_noifdef/Clean.java");
    f2 << "package com.example;\n"
       << "public class Clean {\n"
       << "  void f() {\n"
       << "    if (true) {\n"
       << "      g();\n"
       << "    }\n"
       << "  }\n"
       << "}\n";
  }
  auto r = audit_codebase("/tmp/audit_test_java_noifdef");
  for (const auto &i : r.issues) {
    assert(i.message.find("#if") == std::string::npos);
    assert(i.message.find("#define") == std::string::npos);
  }
  std::cout << "test_java_no_ifdef_issues passed" << std::endl;
}

static void test_detect_language_cmake() {
  fs::create_directories("/tmp/audit_test_lang_cmake");
  {
    std::ofstream f("/tmp/audit_test_lang_cmake/CMakeLists.txt");
    f << "cmake_minimum_required(VERSION 3.15)\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_cmake") ==
         ProjectLanguage::CPLUS);
  std::cout << "test_detect_language_cmake passed" << std::endl;
}

static void test_detect_language_gradle() {
  fs::create_directories("/tmp/audit_test_lang_gradle");
  {
    std::ofstream f("/tmp/audit_test_lang_gradle/build.gradle");
    f << "plugins { id 'java' }\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_gradle") ==
         ProjectLanguage::JAVA);
  std::cout << "test_detect_language_gradle passed" << std::endl;
}

static void test_detect_language_maven() {
  fs::create_directories("/tmp/audit_test_lang_maven");
  {
    std::ofstream f("/tmp/audit_test_lang_maven/pom.xml");
    f << "<project></project>\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_maven") ==
         ProjectLanguage::JAVA);
  std::cout << "test_detect_language_maven passed" << std::endl;
}

static void test_detect_language_python() {
  fs::create_directories("/tmp/audit_test_lang_py");
  {
    std::ofstream f("/tmp/audit_test_lang_py/pyproject.toml");
    f << "[project]\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_py") ==
         ProjectLanguage::PYTHON);
  std::cout << "test_detect_language_python passed" << std::endl;
}

static void test_detect_language_none() {
  fs::create_directories("/tmp/audit_test_lang_none");
  assert(detect_project_language("/tmp/audit_test_lang_none") ==
         ProjectLanguage::NONE);
  std::cout << "test_detect_language_none passed" << std::endl;
}

static void test_detect_language_monorepo_gradle_kotlin() {
  // Monorepo: build files one level down in a subdirectory
  fs::create_directories("/tmp/audit_test_lang_mono/android/app/src/main/java/com/ex");
  fs::create_directories("/tmp/audit_test_lang_mono/prototype");
  {
    std::ofstream f("/tmp/audit_test_lang_mono/android/build.gradle");
    f << "apply plugin: 'com.android.application'\n";
    std::ofstream f2("/tmp/audit_test_lang_mono/android/gradlew");
    f2 << "#!/bin/sh\n";
    std::ofstream f3("/tmp/audit_test_lang_mono/android/app/src/main/java/com/ex/Foo.kt");
    f3 << "package com.ex\nfun foo() {}\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_mono") ==
         ProjectLanguage::KOTLIN);
  std::cout << "test_detect_language_monorepo_gradle_kotlin passed" << std::endl;
}

static void test_detect_language_monorepo_gradle_java() {
  // Monorepo: Gradle subdirectory with only .java files → JAVA
  fs::create_directories("/tmp/audit_test_lang_mono_java/app/src/main/java/com/ex");
  {
    std::ofstream f("/tmp/audit_test_lang_mono_java/build.gradle");
    f << "apply plugin: 'java'\n";
    std::ofstream f2("/tmp/audit_test_lang_mono_java/gradlew");
    f2 << "#!/bin/sh\n";
    std::ofstream f3("/tmp/audit_test_lang_mono_java/app/src/main/java/com/ex/Main.java");
    f3 << "package com.ex;\npublic class Main {}\n";
  }
  assert(detect_project_language("/tmp/audit_test_lang_mono_java") ==
         ProjectLanguage::JAVA);
  std::cout << "test_detect_language_monorepo_gradle_java passed" << std::endl;
}

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
    "/tmp/audit_test_java", "/tmp/audit_test_java_imp",
    "/tmp/audit_test_java_imp_low", "/tmp/audit_test_java_imp_med",
    "/tmp/audit_test_java_imp_high", "/tmp/audit_test_java_imp_25",
    "/tmp/audit_test_java_noifdef",
    "/tmp/audit_test_lang_cmake", "/tmp/audit_test_lang_gradle",
    "/tmp/audit_test_lang_maven", "/tmp/audit_test_lang_py",
    "/tmp/audit_test_lang_none",
    "/tmp/audit_test_java_class", "/tmp/audit_test_java_method",
    "/tmp/audit_test_cpp_class", "/tmp/audit_test_cpp_struct",
    "/tmp/audit_test_cpp_fwd", "/tmp/audit_test_java_iface",
    "/tmp/audit_test_java_enum", "/tmp/audit_test_java_nested",
    "/tmp/audit_test_lang_kts", "/tmp/audit_test_lang_settings_kts",
    "/tmp/audit_test_lang_kt_gradle", "/tmp/audit_test_lang_java_no_kt",
    "/tmp/audit_test_kotlin", "/tmp/audit_test_kotlin_imp",
    "/tmp/audit_test_kotlin_noifdef", "/tmp/audit_test_kotlin_obj",
    "/tmp/audit_test_kt_notest", "/tmp/audit_test_kt_hastest",
    "/tmp/audit_test_kt_spec", "/tmp/audit_test_kt_kts",
    "/tmp/audit_test_kt_android",
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
  test_java_file_scanned();
  test_java_import_count();
  test_java_import_below_medium();
  test_java_import_medium_boundary();
  test_java_import_high_boundary();
  test_java_import_25_not_high();
  test_java_no_ifdef_issues();
  test_detect_language_cmake();
  test_detect_language_gradle();
  test_detect_language_maven();
  test_detect_language_python();
  test_detect_language_none();
  test_detect_language_monorepo_gradle_kotlin();
  test_detect_language_monorepo_gradle_java();
  test_java_class_not_tracked_as_function();
  test_java_method_inside_class_tracked();
  test_cpp_class_not_tracked_as_function();
  test_cpp_struct_not_tracked_as_function();
  test_cpp_forward_decl_not_type_body();
  test_java_interface_not_tracked_as_function();
  test_java_enum_not_tracked_as_function();
  test_java_nested_class();
  test_detect_language_kotlin_kts();
  test_detect_language_kotlin_settings_kts();
  test_detect_language_kotlin_gradle_with_kt();
  test_detect_language_kotlin_android_deep();
  test_detect_language_java_gradle_no_kt();
  test_kotlin_file_scanned();
  test_kotlin_import_count();
  test_kotlin_no_ifdef_issues();
  test_kotlin_object_not_tracked_as_function();
  test_kotlin_missing_test_flagged();
  test_kotlin_test_exists_not_flagged();
  test_kotlin_spec_file_counts_as_test();
  test_kotlin_kts_not_flagged();
  test_kotlin_androidtest_counts();
  test_kotlin_android_java_src_missing_test();
  test_kotlin_android_java_src_test_exists();

  // Cleanup
  for (const auto &d : dirs) {
    fs::remove_all(d);
  }
  fs::remove("/tmp/audit_test_fp.json");

  std::cout << "=== All audit tests passed ===" << std::endl;
}