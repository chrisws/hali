// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <iostream>
#include <string>
#include <functional>

#include "logging.h"

//
// Hali Test Protocol (HTP) v1
//
// This test runner emits a standardized line-based protocol so that
// build.cpp (or any external tool) can parse results generically.
//
// Format:
//   PASS <test_name>
//   FAIL <test_name> <message>
//
// Lines starting with # are comments. All other lines are informational.
// Counting and summary are handled by build.cpp.
//

static int g_failures = 0;

//
// Run a single test function, emitting HTP result line.
//
static void run_htp_test(const std::string &name, const std::function<void()> &fn) {
  try {
    fn();
    std::cout << "PASS " << name << "\n";
  } catch (const std::exception &e) {
    std::cout << "FAIL " << name << " " << e.what() << "\n";
    g_failures++;
  } catch (...) {
    std::cout << "FAIL " << name << " unknown exception\n";
    g_failures++;
  }
}

// Forward declarations
void file_test();
void graph_test();
void string_utils_test();
void mcp_format_test();
void sha1_test();
void utf8_test();
void agent_utils_test();
void input_test();
void run_allowed_test();
void curl_test();
void mtp_adaptive_test();
void logging_filter_test();
void replace_test();
void build_test();
void table_test();
void usage_log_test();
void suffix_drafter_test();
void audio_test();
void config_test();
void audit_test();
void think_parser_test();
void planner_test();
void sysinfo_test();
void git_tool_test();

int main() {
  log_open_console();

  std::cout << "# Hali Test Protocol v1\n";
  std::cout << "# Running hali unit tests\n\n";

  run_htp_test("file", file_test);
  run_htp_test("graph", graph_test);
  run_htp_test("string_utils", string_utils_test);
  run_htp_test("mcp_format", mcp_format_test);
  run_htp_test("sha1", sha1_test);
  run_htp_test("utf8", utf8_test);
  run_htp_test("agent_utils", agent_utils_test);
  run_htp_test("input", input_test);
  run_htp_test("run_allowed", run_allowed_test);
  run_htp_test("curl", curl_test);
  run_htp_test("mtp_adaptive", mtp_adaptive_test);
  run_htp_test("logging_filter", logging_filter_test);
  run_htp_test("replace", replace_test);
  run_htp_test("config", config_test);
  run_htp_test("audit", audit_test);
  run_htp_test("build", build_test);
  run_htp_test("table", table_test);
  run_htp_test("usage_log", usage_log_test);
  run_htp_test("suffix_drafter", suffix_drafter_test);
  run_htp_test("audio", audio_test);
  run_htp_test("think_parser", think_parser_test);
  run_htp_test("planner", planner_test);
  run_htp_test("sysinfo", sysinfo_test);
  run_htp_test("git_tool", git_tool_test);

  return g_failures ? 1 : 0;
}