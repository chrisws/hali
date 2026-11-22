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
#include <filesystem>
#include <fstream>
#include <vector>

#include "agent_utils.h"

namespace fs = std::filesystem;

//
// The same pattern table used by slash_security_audit.
// Kept in sync manually - if you change one, change the other.
//
struct SecPattern {
  const char *category;
  const char *pattern;
};

static const SecPattern SEC_PATTERNS[] = {
  {"network",     "curl|wget|socket|connect\\(|http://|https://|fetch\\(|request\\(|send\\(|recv\\("},
  {"telemetry",   "telemetry|analytics|tracking|beacon|metrics|upload|exfiltrat"},
  {"exfiltration","base64|obfuscat|eval\\(|exec\\(|system\\(|popen\\("},
  {"sensitive",   "/etc/passwd|\\.ssh|\\.aws|id_rsa|credential|api_key"},
  {"process",     "fork\\(|spawn|subprocess|popen"},
  {"crypto",      "encrypt|decrypt|cipher|obfusc"},
};

static const int NUM_PATTERNS = sizeof(SEC_PATTERNS) / sizeof(SEC_PATTERNS[0]);

//
// Helper: write a file to a temp dir and return the path.
//
static std::string write_test_file(const std::string &dir,
                                   const std::string &name,
                                   const std::string &content) {
  fs::create_directories(dir);
  std::string path = dir + "/" + name;
  std::ofstream f(path);
  f << content;
  f.close();
  return path;
}

//
// Helper: run tool_search on a dir and return the raw output.
//
static std::string scan_dir(const std::string &dir, const std::string &pattern) {
  return tool_search(dir, pattern, dir,
                     "--recursive --line-numbers --include=*.cpp --include=*.java");
}

// ── Pattern coverage tests ───────────────────────────────────────────────

static void test_network_patterns() {
  std::string dir = "/tmp/sec_scan_test_network";
  fs::remove_all(dir);

  write_test_file(dir, "net.cpp",
    "#include <cstdio>\n"
    "int main() {\n"
    "  // curl a URL\n"
    "  system(\"curl http://example.com\");\n"
    "  // socket connect\n"
    "  int s = socket(2, 1, 0);\n"
    "  connect(s, &addr, len);\n"
    "  return 0;\n"
    "}\n");

  std::string result = scan_dir(dir, SEC_PATTERNS[0].pattern);
  assert(result != "no matches");
  assert(result.find("curl") != std::string::npos);
  assert(result.find("socket") != std::string::npos);
  assert(result.find("connect(") != std::string::npos);
  assert(result.find("http://") != std::string::npos);
  std::cout << "test_network_patterns passed" << std::endl;
  fs::remove_all(dir);
}

static void test_telemetry_patterns() {
  std::string dir = "/tmp/sec_scan_test_telemetry";
  fs::remove_all(dir);

  write_test_file(dir, "tele.cpp",
    "void track() {\n"
    "  // send telemetry data\n"
    "  upload_metrics(\"beacon\");\n"
    "  analytics_report();\n"
    "}\n");

  std::string result = scan_dir(dir, SEC_PATTERNS[1].pattern);
  assert(result != "no matches");
  assert(result.find("telemetry") != std::string::npos);
  assert(result.find("upload") != std::string::npos);
  assert(result.find("metrics") != std::string::npos);
  assert(result.find("beacon") != std::string::npos);
  assert(result.find("analytics") != std::string::npos);
  std::cout << "test_telemetry_patterns passed" << std::endl;
  fs::remove_all(dir);
}

static void test_exfiltration_patterns() {
  std::string dir = "/tmp/sec_scan_test_exfil";
  fs::remove_all(dir);

  write_test_file(dir, "exfil.cpp",
    "void leak() {\n"
    "  auto encoded = base64_encode(data);\n"
    "  system(\"cat /etc/passwd\");\n"
    "  eval(payload);\n"
    "}\n");

  std::string result = scan_dir(dir, SEC_PATTERNS[2].pattern);
  assert(result != "no matches");
  assert(result.find("base64") != std::string::npos);
  assert(result.find("system(") != std::string::npos);
  assert(result.find("eval(") != std::string::npos);
  std::cout << "test_exfiltration_patterns passed" << std::endl;
  fs::remove_all(dir);
}

static void test_sensitive_patterns() {
  std::string dir = "/tmp/sec_scan_test_sensitive";
  fs::remove_all(dir);

  write_test_file(dir, "creds.cpp",
    "const char *path = \"~/.ssh/id_rsa\";\n"
    "const char *aws = \"~/.aws/credentials\";\n"
    "std::string api_key = \"sk-12345\";\n");

  std::string result = scan_dir(dir, SEC_PATTERNS[3].pattern);
  assert(result != "no matches");
  assert(result.find(".ssh") != std::string::npos);
  assert(result.find("id_rsa") != std::string::npos);
  assert(result.find(".aws") != std::string::npos);
  assert(result.find("credential") != std::string::npos);
  assert(result.find("api_key") != std::string::npos);
  std::cout << "test_sensitive_patterns passed" << std::endl;
  fs::remove_all(dir);
}

static void test_process_patterns() {
  std::string dir = "/tmp/sec_scan_test_process";
  fs::remove_all(dir);

  write_test_file(dir, "proc.cpp",
    "void run() {\n"
    "  fork();\n"
    "  spawn(\"/bin/sh\");\n"
    "  subprocess.call([\"ls\"]);\n"
    "}\n");

  std::string result = scan_dir(dir, SEC_PATTERNS[4].pattern);
  assert(result != "no matches");
  assert(result.find("fork(") != std::string::npos);
  assert(result.find("spawn") != std::string::npos);
  assert(result.find("subprocess") != std::string::npos);
  std::cout << "test_process_patterns passed" << std::endl;
  fs::remove_all(dir);
}

static void test_crypto_patterns() {
  std::string dir = "/tmp/sec_scan_test_crypto";
  fs::remove_all(dir);

  write_test_file(dir, "crypto.cpp",
    "void sec() {\n"
    "  encrypt(data, key);\n"
    "  decrypt(buf, key);\n"
    "  auto c = cipher_transform(data);\n"
    "}\n");

  std::string result = scan_dir(dir, SEC_PATTERNS[5].pattern);
  assert(result != "no matches");
  assert(result.find("encrypt") != std::string::npos);
  assert(result.find("decrypt") != std::string::npos);
  assert(result.find("cipher") != std::string::npos);
  std::cout << "test_crypto_patterns passed" << std::endl;
  fs::remove_all(dir);
}

// ── Clean file should produce no matches ──────────────────────────────────

static void test_clean_file_no_matches() {
  std::string dir = "/tmp/sec_scan_test_clean";
  fs::remove_all(dir);

  write_test_file(dir, "clean.cpp",
    "#include <iostream>\n"
    "int main() {\n"
    "  std::cout << \"hello\" << std::endl;\n"
    "  return 0;\n"
    "}\n");

  for (int i = 0; i < NUM_PATTERNS; i++) {
    std::string result = scan_dir(dir, SEC_PATTERNS[i].pattern);
    assert(result == "no matches");
  }
  std::cout << "test_clean_file_no_matches passed" << std::endl;
  fs::remove_all(dir);
}

// ── Multiple files in subdirectories ──────────────────────────────────────

static void test_recursive_scan() {
  std::string dir = "/tmp/sec_scan_test_recursive";
  fs::remove_all(dir);

  write_test_file(dir + "/src", "a.cpp",
    "void f() { curl(\"http://x.com\"); }\n");
  write_test_file(dir + "/src/deep", "b.cpp",
    "void g() { telemetry_send(); }\n");

  std::string net = scan_dir(dir, SEC_PATTERNS[0].pattern);
  assert(net.find("a.cpp") != std::string::npos);

  std::string tele = scan_dir(dir, SEC_PATTERNS[1].pattern);
  assert(tele.find("b.cpp") != std::string::npos);

  std::cout << "test_recursive_scan passed" << std::endl;
  fs::remove_all(dir);
}

// ── Java files are scanned ───────────────────────────────────────────────

static void test_java_files_scanned() {
  std::string dir = "/tmp/sec_scan_test_java";
  fs::remove_all(dir);

  write_test_file(dir, "Net.java",
    "import java.net.URL;\n"
    "class Net {\n"
    "  void fetch() throws Exception {\n"
    "    URL u = new URL(\"http://example.com\");\n"
    "    u.openStream();\n"
    "  }\n"
    "}\n");

  std::string result = scan_dir(dir, SEC_PATTERNS[0].pattern);
  assert(result != "no matches");
  assert(result.find("Net.java") != std::string::npos);
  assert(result.find("http://") != std::string::npos);
  std::cout << "test_java_files_scanned passed" << std::endl;
  fs::remove_all(dir);
}

// ── Pattern table integrity ───────────────────────────────────────────────

static void test_pattern_table_integrity() {
  for (int i = 0; i < NUM_PATTERNS; i++) {
    assert(SEC_PATTERNS[i].category != nullptr);
    assert(SEC_PATTERNS[i].pattern != nullptr);
    assert(std::string(SEC_PATTERNS[i].category).size() > 0);
    assert(std::string(SEC_PATTERNS[i].pattern).size() > 0);
  }
  assert(NUM_PATTERNS == 6);
  std::cout << "test_pattern_table_integrity passed" << std::endl;
}

void security_scan_test() {
  test_pattern_table_integrity();
  test_network_patterns();
  test_telemetry_patterns();
  test_exfiltration_patterns();
  test_sensitive_patterns();
  test_process_patterns();
  test_crypto_patterns();
  test_clean_file_no_matches();
  test_recursive_scan();
  test_java_files_scanned();
  std::cout << "All security scan tests passed." << std::endl;
}