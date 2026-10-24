// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef BUILD_H
#define BUILD_H

#include <functional>
#include <string>
#include <vector>

struct LintWarning {
  std::string file;
  int line;
  std::string check;
  std::string description;
};

// Result of parsing a test run output using the Hali Test Protocol (HTP).
// See parse_test_protocol() in build.cpp for the format specification.
struct TestResult {
  int passed = 0;
  int total = 0;
  bool found = false;  // true if a SUMMARY line was detected
};

struct BuildResult {
  bool success_;
  std::string output_;
  std::vector<LintWarning> lint_details_;
  TestResult test_result_;
};

// mode: "setup" | "build" | "test" | "clean" | "lint" | "all"
// sandbox: root directory of the project
// subdir: optional subdirectory to build as a sub-project (e.g. "tests")
// status: callback for progress messages (e.g. tui.show_tool)
BuildResult build_run(const std::string &sandbox, const std::string &mode,
                      const std::function<void(const std::string &)> &status,
                      const std::string &subdir = "");

//
// Parse test output using the Hali Test Protocol (HTP) v1.
//
// Recognised lines:
//   PASS <test_name>
//   FAIL <test_name> <message>
//   SUMMARY <passed> <total>
//
// Lines starting with '#' are comments. All other lines are ignored.
// If no SUMMARY line is found, counts are derived from PASS/FAIL lines.
//
TestResult parse_test_protocol(const std::string &output);

//
// Parse a JUnit XML file for test results.
// Looks for <testsuite> elements and sums tests/failures/errors attributes.
// Returns TestResult with found=true if at least one testsuite was parsed.
//
TestResult parse_junit_xml(const std::string &path);

#endif