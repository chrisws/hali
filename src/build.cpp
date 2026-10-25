// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <filesystem>
#include <fstream>
#include <thread>
#include <string>
#include <cstddef>
#include <functional>
#include <vector>
#include <utility>

#include "build.h"
#include "shell_util.h"

namespace fs = std::filesystem;

enum class BuildSystem {
  NONE,
  CMAKE,
  GRADLE,
  MAVEN,
  PYTHON,
};

static BuildSystem detect_build_system(const std::string &sandbox) {
  if (fs::exists(sandbox + "/CMakeLists.txt")) {
    return BuildSystem::CMAKE;
  }
  if (fs::exists(sandbox + "/build.gradle") ||
      fs::exists(sandbox + "/build.gradle.kts") ||
      fs::exists(sandbox + "/gradlew")) {
    return BuildSystem::GRADLE;
  }
  if (fs::exists(sandbox + "/pom.xml")) {
    return BuildSystem::MAVEN;
  }
  if (fs::exists(sandbox + "/pyproject.toml") ||
      fs::exists(sandbox + "/setup.py") ||
      fs::exists(sandbox + "/requirements.txt")) {
    return BuildSystem::PYTHON;
  }
  // Fall back: if there are .py files in the sandbox root, treat as Python
  for (const auto &entry : fs::directory_iterator(sandbox)) {
    if (entry.is_regular_file() && entry.path().extension() == ".py") {
      return BuildSystem::PYTHON;
    }
  }
  return BuildSystem::NONE;
}

static constexpr size_t kMaxOutputSize = 32768;
static constexpr size_t kTruncHeadSize = 2048;
static constexpr size_t kTruncTailSize = 30720;

static std::string run_command(const std::string &cmd,
                               const std::function<void(const std::string &)> &status) {
  status("build: " + cmd);
  std::string out = run_shell_command(cmd);
  if (out.size() > kMaxOutputSize) {
    out = out.substr(0, kTruncHeadSize) + "\n…(truncated)…" +
          out.substr(out.size() - kTruncTailSize);
  }
  return out;
}

//
// Parse ctest output for "N% tests passed, M tests failed out of T"
// Returns {passed, total}. Returns {0,0} if not found.
//
static std::pair<int,int> parse_test_results(const std::string &output) {
  int passed = 0, total = 0;
  auto pos = output.find("tests passed");
  if (pos == std::string::npos) return {0, 0};
  // Walk backwards past non-digits (space, %) to find the number
  int i = static_cast<int>(pos) - 1;
  while (i >= 0 && (output[i] == ' ' || output[i] == '%')) i--;
  while (i >= 0 && output[i] >= '0' && output[i] <= '9') i--;
  int start = i + 1;
  if (start >= static_cast<int>(pos)) return {0, 0};
  passed = std::stoi(output.substr(start, pos - start));
  // Find "out of N"
  auto out_pos = output.find("out of ", pos);
  if (out_pos == std::string::npos) return {passed, passed};
  out_pos += 7;
  total = 0;
  while (out_pos < static_cast<int>(output.size()) && output[out_pos] >= '0' && output[out_pos] <= '9') {
    total = total * 10 + (output[out_pos] - '0');
    out_pos++;
  }
  return {passed, total};
}

//
// Parse output using the Hali Test Protocol (HTP) v1.
//
// Protocol specification:
//   - Lines starting with "PASS " indicate a passed test:  PASS <test_name>
//   - Lines starting with "FAIL " indicate a failed test:  FAIL <test_name> <message>
//   - A line starting with "SUMMARY " provides the final count: SUMMARY <passed> <total>
//   - Lines starting with "#" are comments (ignored)
//   - All other lines are informational output (ignored by parser)
//
// The SUMMARY line is authoritative. If no SUMMARY line is found, the parser
// falls back to counting PASS/FAIL lines. If neither is found, TestResult.found
// is false.
//
TestResult parse_test_protocol(const std::string &output) {
  TestResult result;
  int pass_count = 0;
  int fail_count = 0;
  bool has_summary = false;

  size_t pos = 0;
  while (pos < output.size()) {
    size_t eol = output.find('\n', pos);
    std::string line = (eol == std::string::npos)
                           ? output.substr(pos)
                           : output.substr(pos, eol - pos);
    pos = (eol == std::string::npos) ? output.size() : eol + 1;

    // Skip empty lines and comments
    if (line.empty() || line[0] == '#') continue;

    if (line.rfind("SUMMARY ", 0) == 0) {
      // SUMMARY <passed> <total>
      size_t sp1 = line.find(' ', 8);
      if (sp1 == std::string::npos) continue;
      size_t sp2 = line.find(' ', sp1 + 1);
      try {
        result.passed = std::stoi(line.substr(8, sp1 - 8));
        if (sp2 != std::string::npos) {
          result.total = std::stoi(line.substr(sp1 + 1, sp2 - sp1 - 1));
        } else {
          // Second number extends to end of line
          result.total = std::stoi(line.substr(sp1 + 1));
        }
        has_summary = true;
      } catch (const std::exception &) {
        // Malformed summary line, ignore
      }
    } else if (line.rfind("PASS ", 0) == 0) {
      pass_count++;
    } else if (line.rfind("FAIL ", 0) == 0) {
      fail_count++;
    }
    // All other lines are informational, ignored
  }

  if (has_summary) {
    result.found = true;
  } else if (pass_count > 0 || fail_count > 0) {
    // No SUMMARY line, but we have individual results — derive counts
    result.passed = pass_count;
    result.total = pass_count + fail_count;
    result.found = true;
  }

  return result;
}

//
// Parse a JUnit XML file for test results.
// Looks for <testsuite> elements and sums up tests/failures/errors attributes.
// Returns a TestResult with found=true if at least one testsuite was parsed.
//
TestResult parse_junit_xml(const std::string &path) {
  TestResult result;
  std::ifstream f(path);
  if (!f) return result;

  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  f.close();

  int total_tests = 0;
  int total_failures = 0;
  int total_errors = 0;
  bool found_any = false;

  size_t pos = 0;
  while ((pos = content.find("<testsuite", pos)) != std::string::npos) {
    // Skip the plural <testsuites> container tag
    if (pos + 10 < content.size() && content[pos + 10] == 's') {
      pos += 10;
      continue;
    }
    // Find the closing > of this tag
    size_t tag_end = content.find('>', pos);
    if (tag_end == std::string::npos) break;
    std::string tag = content.substr(pos, tag_end - pos);

    // Extract tests="N"
    auto extract_attr = [&tag](const std::string &name) -> int {
      std::string needle = name + "=\"";
      size_t a = tag.find(needle);
      if (a == std::string::npos) return 0;
      a += needle.size();
      size_t b = tag.find('"', a);
      if (b == std::string::npos) return 0;
      try { return std::stoi(tag.substr(a, b - a)); }
      catch (...) { return 0; }
    };

    total_tests += extract_attr("tests");
    total_failures += extract_attr("failures");
    total_errors += extract_attr("errors");
    found_any = true;

    pos = tag_end + 1;
  }

  // ctest reports the whole binary as one <testsuite tests="1">.
  // Individual test results live in <system-out> as free-form text.
  // Scan for "passed"/"failed" lines to get a finer-grained count.
  int so_passed = 0, so_failed = 0;
  size_t so_start = content.find("<system-out>");
  if (so_start != std::string::npos) {
    so_start += 12; // len("<system-out>")
    size_t so_end = content.find("</system-out>", so_start);
    if (so_end == std::string::npos) so_end = content.size();
    std::string so = content.substr(so_start, so_end - so_start);
    size_t line_pos = 0;
    while (line_pos < so.size()) {
      size_t eol = so.find('\n', line_pos);
      std::string line = (eol == std::string::npos)
                             ? so.substr(line_pos)
                             : so.substr(line_pos, eol - line_pos);
      line_pos = (eol == std::string::npos) ? so.size() : eol + 1;
      // Match lines like "test_foo passed" or "test_foo failed"
      if (line.size() > 6 && line.substr(line.size() - 6) == "passed") {
        so_passed++;
      } else if (line.size() > 6 && line.substr(line.size() - 6) == "failed") {
        so_failed++;
      }
    }
  }

  if (found_any) {
    result.total = total_tests;
    result.passed = total_tests - total_failures - total_errors;
    if (result.passed < 0) result.passed = 0;
    result.found = true;

    // Prefer the finer-grained system-out count when it's higher
    int so_total = so_passed + so_failed;
    if (so_total > result.total) {
      result.passed = so_passed;
      result.total = so_total;
    }
  }
  return result;
}

//
// Generate a GitHub-style SVG badge for unit test results.
// Saved to <sandbox>/.hali/test_badge.svg
//
static void generate_test_badge(const std::string &sandbox, int passed, int total) {
  if (total == 0) return;
  std::string value_str;
  std::string color;
  if (passed == total) {
    value_str = std::to_string(passed) + " passed";
    color = "#4c1";
  } else {
    value_str = std::to_string(total - passed) + " failed";
    color = "#ae0000";
  }
  std::string label = "unit tests";

  constexpr int label_w = 80;
  constexpr int value_w = 80;
  constexpr int total_w = label_w + value_w;
  const int h = 20;
  const int r = 3;

  std::ostringstream svg;
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << total_w
      << "\" height=\"" << h << "\" viewBox=\"0 0 " << total_w << " " << h
      << "\" role=\"img\" aria-label=\"" << label << ": " << value_str << "\">\n";
  svg << "  <title>" << label << ": " << value_str << "</title>\n";
  svg << "  <rect width=\"" << label_w << "\" height=\"" << h
      << "\" rx=\"" << r << "\" fill=\"#555\"/>\n";
  svg << "  <rect x=\"" << label_w << "\" width=\"" << value_w
      << "\" height=\"" << h << "\" rx=\"" << r << "\" fill=\"" << color << "\"/>\n";
  svg << "  <rect x=\"" << label_w << "\" width=\"" << (value_w - r)
      << "\" height=\"" << h << "\" fill=\"" << color << "\"/>\n";
  svg << "  <text x=\"" << (label_w / 2) << "\" y=\"" << (h / 2 + 4)
      << "\" font-family=\"monospace\" font-size=\"11\" fill=\"#fff\" "
      << "text-anchor=\"middle\">" << label << "</text>\n";
  svg << "  <text x=\"" << (label_w + value_w / 2) << "\" y=\"" << (h / 2 + 4)
      << "\" font-family=\"monospace\" font-size=\"11\" fill=\"#fff\" "
      << "text-anchor=\"middle\">" << value_str << "</text>\n";
  svg << "</svg>\n";

  std::string dir = sandbox + "/.hali";
  fs::create_directories(dir);
  std::string path = dir + "/test_badge.svg";
  std::ofstream f(path);
  if (f) {
    f << svg.str();
  }
}

static BuildResult build_cmake(const std::string &sandbox, const std::string &mode,
                               const std::function<void(const std::string &)> &status) {
  const int nproc = std::thread::hardware_concurrency();
  const std::string build_dir = sandbox + "/build";
  const std::string tests_dir = sandbox + "/tests";
  const std::string tests_build_dir = tests_dir + "/build";
  const bool has_tests = fs::exists(tests_dir + "/CMakeLists.txt");
  std::string result;
  bool ok = true;

  if (mode == "build" || mode == "all") {
    result += "==> Building main project...\n";
    auto out = run_command("make -C " + build_dir + " -j" + std::to_string(nproc), status);
    if (out.find("Error") != std::string::npos || out.find("error") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Build complete.\n";
  }

  TestResult test_res;

  if ((mode == "test" || mode == "all") && has_tests) {
    result += "==> Building unit tests...\n";
    auto out = run_command("make -C " + tests_build_dir + " -j" + std::to_string(nproc), status);
    if (out.find("Error") != std::string::npos || out.find("error") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Running unit tests...\n";
    std::string junit_path = sandbox + "/.hali/results.xml";
    fs::create_directories(sandbox + "/.hali");
    out = run_command("ctest --test-dir " + tests_build_dir +
                      " --output-on-failure --output-junit " + junit_path, status);
    if (out.find("Failed") != std::string::npos || out.find("failed") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Tests complete.\n";

    // Parse test results: JUnit XML → HTP → ctest text
    test_res = parse_junit_xml(junit_path);
    if (!test_res.found) {
      test_res = parse_test_protocol(out);
    }
    if (!test_res.found) {
      auto pr = parse_test_results(out);
      test_res.passed = pr.first;
      test_res.total = pr.second;
      test_res.found = (pr.second > 0);
    }
    if (test_res.found) {
      generate_test_badge(sandbox, test_res.passed, test_res.total);
      result += "==> Badge: " + sandbox + "/.hali/test_badge.svg\n";
    }
  }

  if (mode == "clean") {
    result += "==> Cleaning build artifacts...\n";
    result += run_command("make -C " + build_dir + " clean", status);
    if (fs::exists(tests_build_dir)) {
      result += run_command("make -C " + tests_build_dir + " clean", status);
    }
    result += "==> Clean complete.\n";
  }

  BuildResult br;
  br.success_ = ok;
  br.output_ = result;
  br.test_result_ = test_res;
  return br;
}

static BuildResult build_maven(const std::string &sandbox, const std::string &mode,
                               const std::function<void(const std::string &)> &status) {
  const std::string pom = sandbox + "/pom.xml";
  std::string result;
  bool ok = true;

  if (mode == "build" || mode == "all") {
    result += "==> Compiling with Maven...\n";
    auto out = run_command("mvn -f " + pom + " compile -q", status);
    if (out.find("BUILD FAILURE") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Compile complete.\n";
  }

  if (mode == "test" || mode == "all") {
    result += "==> Running Maven tests...\n";
    auto out = run_command("mvn -f " + pom + " test", status);
    if (out.find("BUILD FAILURE") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Tests complete.\n";
  }

  if (mode == "clean") {
    result += "==> Cleaning with Maven...\n";
    result += run_command("mvn -f " + pom + " clean -q", status);
    result += "==> Clean complete.\n";
  }

  return {ok, result};
}

static BuildResult build_gradle(const std::string &sandbox, const std::string &mode,
                                 const std::function<void(const std::string &)> &status) {
  // Prefer the Gradle wrapper over a system install
  const std::string gradle = fs::exists(sandbox + "/gradlew")
                                ? sandbox + "/gradlew"
                                : "gradle";
  std::string result;
  bool ok = true;

  if (mode == "build" || mode == "all") {
    result += "==> Building with Gradle...\n";
    auto out = run_command(gradle + " -p " + sandbox + " build", status);
    if (out.find("BUILD FAILED") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Build complete.\n";
  }

  if (mode == "test" || mode == "all") {
    result += "==> Running Gradle tests...\n";
    auto out = run_command(gradle + " -p " + sandbox + " test", status);
    if (out.find("BUILD FAILED") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Tests complete.\n";
  }

  if (mode == "clean") {
    result += "==> Cleaning with Gradle...\n";
    result += run_command(gradle + " -p " + sandbox + " clean", status);
    result += "==> Clean complete.\n";
  }

  return {ok, result};
}

static BuildResult build_python(const std::string &sandbox, const std::string &mode,
                                 const std::function<void(const std::string &)> &status) {
  std::string result;
  bool ok = true;

  if (mode == "build" || mode == "all") {
    result += "==> Syntax-checking Python files...\n";
    auto out = run_command("python3 -m compileall -q " + sandbox, status);
    if (out.find("SyntaxError") != std::string::npos ||
        out.find("Syntax error") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Syntax check complete.\n";
  }

  if (mode == "test" || mode == "all") {
    // Prefer pytest if available, fall back to unittest
    auto pytest_check = run_command("python3 -m pytest --version", status);
    if (pytest_check.find("usage:") == std::string::npos &&
        pytest_check.find("No module named") == std::string::npos) {
      result += "==> Running pytest...\n";
      auto out = run_command("python3 -m pytest " + sandbox + " -v --tb=short", status);
      if (out.find("FAILED") != std::string::npos ||
          out.find("ERROR") != std::string::npos) {
        ok = false;
      }
      result += out;
    } else {
      result += "==> Running unittest discover...\n";
      auto out = run_command("python3 -m unittest discover -s " + sandbox + " -v", status);
      if (out.find("FAILED") != std::string::npos ||
          out.find("ERROR") != std::string::npos) {
        ok = false;
      }
      result += out;
    }
    result += "==> Tests complete.\n";
  }

  if (mode == "clean") {
    result += "==> Cleaning Python artifacts...\n";
    result += run_command("find " + sandbox + " -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null", status);
    result += run_command("find " + sandbox + " -type f -name '*.pyc' -delete 2>/dev/null", status);
    result += run_command("rm -rf " + sandbox + "/.pytest_cache " + sandbox + "/.mypy_cache " + sandbox + "/.ruff_cache 2>/dev/null", status);
    result += "==> Clean complete.\n";
  }

  return {ok, result};
}

static BuildResult setup_cmake(const std::string &sandbox,
                               const std::function<void(const std::string &)> &status) {
  std::string result;
  bool ok = true;

  result += "==> Configuring main build...\n";
  auto out = run_command("cmake -S " + sandbox + " -B " + sandbox + "/build -G \"Unix Makefiles\" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON", status);
  if (out.find("Error") != std::string::npos || out.find("CMake Error") != std::string::npos) {
    ok = false;
  }
  result += out;

  if (fs::exists(sandbox + "/tests/CMakeLists.txt")) {
    result += "==> Configuring test build...\n";
    out = run_command("cmake -S " + sandbox + "/tests -B " + sandbox + "/tests/build -G \"Unix Makefiles\"", status);
    if (out.find("Error") != std::string::npos || out.find("CMake Error") != std::string::npos) {
      ok = false;
    }
    result += out;

    // Warn if enable_testing() is missing from the test CMakeLists.txt
    {
      std::ifstream f(sandbox + "/tests/CMakeLists.txt");
      std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
      f.close();
      if (content.find("enable_testing") == std::string::npos &&
          content.find("enable_testing()") == std::string::npos) {
        result += "==> WARNING: tests/CMakeLists.txt does not call enable_testing().\n"
                  "==> ctest may not discover tests. Add 'enable_testing()' to your CMakeLists.txt.\n";
      }
    }
  } else {
    result += "==> No tests/ directory found, skipping test configuration.\n";
  }

  result += "==> Setup complete.\n";
  return {ok, result};
}

static BuildResult setup_maven(const std::string &sandbox,
                               const std::function<void(const std::string &)> &status) {
  std::string result;
  bool ok = true;
  const std::string pom = sandbox + "/pom.xml";

  if (!fs::exists(pom)) {
    result += "==> Generating basic pom.xml...\n";
    // Write a minimal pom.xml
    std::ofstream f(pom);
    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<project xmlns=\"http://maven.apache.org/POM/4.0.0\"\n"
      << "         xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
      << "         xsi:schemaLocation=\"http://maven.apache.org/POM/4.0.0\n"
      << "         http://maven.apache.org/xsd/maven-4.0.0.xsd\">\n"
      << "  <modelVersion>4.0.0</modelVersion>\n"
      << "  <groupId>com.example</groupId>\n"
      << "  <artifactId>" + fs::path(sandbox).filename().string() + "</artifactId>\n"
      << "  <version>1.0-SNAPSHOT</version>\n"
      << "  <packaging>jar</packaging>\n"
      << "  <properties>\n"
      << "    <maven.compiler.source>17</maven.compiler.source>\n"
      << "    <maven.compiler.target>17</maven.compiler.target>\n"
      << "    <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>\n"
      << "  </properties>\n"
      << "</project>\n";
    f.close();
    result += "==> pom.xml created.\n";
  } else {
    result += "==> pom.xml already exists, skipping generation.\n";
  }

  result += "==> Compiling with Maven...\n";
  auto out = run_command("mvn -f " + pom + " compile -q", status);
  if (out.find("BUILD FAILURE") != std::string::npos) {
    ok = false;
  }
  result += out;
  result += "==> Setup complete.\n";
  return {ok, result};
}

static BuildResult setup_gradle(const std::string &sandbox,
                                const std::function<void(const std::string &)> &status) {
  std::string result;
  bool ok = true;

  result += "==> Checking Gradle project structure...\n";

  if (fs::exists(sandbox + "/gradlew")) {
    // Ensure gradlew is executable
    fs::permissions(sandbox + "/gradlew",
                     fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                     fs::perms::others_read | fs::perms::others_exec);
    result += "==> gradlew found and made executable.\n";
  } else {
    result += "==> WARNING: no gradlew found. If this is an Android Studio project,\n"
              "==> the wrapper should already exist. If not, run: gradle wrapper\n";
    ok = false;
  }

  if (!fs::exists(sandbox + "/build.gradle") && !fs::exists(sandbox + "/build.gradle.kts")) {
    result += "==> WARNING: no build.gradle found. This project may need to be\n"
              "==> set up in Android Studio first.\n";
    ok = false;
  }

  result += "==> Setup complete.\n";
  return {ok, result};
}

static BuildResult setup_python(const std::string &sandbox,
                                const std::function<void(const std::string &)> &status) {
  std::string result;
  bool ok = true;

  result += "==> Checking Python project structure...\n";

  if (fs::exists(sandbox + "/pyproject.toml")) {
    result += "==> pyproject.toml found.\n";
  } else if (fs::exists(sandbox + "/setup.py")) {
    result += "==> setup.py found.\n";
  } else {
    result += "==> No pyproject.toml or setup.py found. Project will use\n"
              "==> implicit discovery (compileall / unittest discover).\n";
  }

  // Verify python3 is available
  auto out = run_command("python3 --version", status);
  if (out.find("not found") != std::string::npos) {
    result += "==> WARNING: python3 not found on PATH.\n";
    ok = false;
  } else {
    result += "==> " + out;
  }

  result += "==> Setup complete.\n";
  return {ok, result};
}

static BuildResult build_cmake_lint(const std::string &sandbox,
                                     const std::function<void(const std::string &)> &status) {
  const std::string build_dir = sandbox + "/build";
  const std::string compile_db = build_dir + "/compile_commands.json";
  std::string result;

  // Step 1: Ensure compile database exists
  if (!fs::exists(compile_db)) {
    result += "==> No compile_commands.json found, generating...\n";
    auto out = run_command("cmake -S " + sandbox + " -B " + build_dir +
                           " -G \"Unix Makefiles\" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON", status);
    if (out.find("Error") != std::string::npos) {
      return {false, result + out + "\n==> Failed to generate compile database.\n"};
    }
    result += out;
  }

  // Step 2: Find clang-tidy
  auto tidy_check = run_command("which clang-tidy 2>/dev/null", status);
  if (tidy_check.find("clang-tidy") == std::string::npos) {
    return {false, "ERROR: clang-tidy not found on PATH.\n"
                    "Install it with: sudo apt install clang-tidy\n"};
  }

  // Step 3: Check clang-tidy version (need 14+ for misc-include-cleaner)
  auto version_out = run_command("clang-tidy --version 2>&1", status);
  size_t ver_pos = version_out.find("version");
  if (ver_pos != std::string::npos) {
    size_t num_start = ver_pos + 7;
    while (num_start < version_out.size() &&
           (version_out[num_start] < '0' || version_out[num_start] > '9')) {
      num_start++;
    }
    int major = 0;
    while (num_start < version_out.size() &&
           version_out[num_start] >= '0' && version_out[num_start] <= '9') {
      major = major * 10 + (version_out[num_start] - '0');
      num_start++;
    }
    if (major > 0 && major < 14) {
      return {false, "ERROR: clang-tidy version " + std::to_string(major) +
                      " is too old. misc-include-cleaner requires Clang 14+.\n"};
    }
  }

  // Step 4: Find source files
  std::string files_arg;
  int file_count = 0;
  auto src_dir = sandbox + "/src";
  if (fs::exists(src_dir) && fs::is_directory(src_dir)) {
    for (const auto &entry : fs::recursive_directory_iterator(src_dir)) {
      if (entry.is_regular_file()) {
        auto ext = entry.path().extension();
        if (ext == ".cpp" || ext == ".cc" || ext == ".c") {
          files_arg += entry.path().string() + " ";
          file_count++;
        }
      }
    }
  }

  if (file_count == 0) {
    result += "==> No C/C++ source files found in src/.\n";
    result += "==> Lint complete.\n";
    return {true, result};
  }

  // Step 5: Run clang-tidy
  const std::string checks =
      "-*,misc-include-cleaner,"
      "bugprone-unused-return-value,"
      "modernize-use-override,"
      "readability-const-returns,"
      "clang-analyzer-core.NullDereference";
  result += "==> Running clang-tidy on " +
            std::to_string(file_count) + " file(s)...\n";

  auto out = run_command("clang-tidy --checks='" + checks + "' "
                         "-p " + build_dir + " " + files_arg, status);
  result += out;

  // Step 6: Report findings
  if (out.find("error:") != std::string::npos) {
    result += "==> Lint FAILED. See errors above.\n";
    return {false, result};
  }

  // Step 7: Parse warnings into structured details
  std::vector<LintWarning> details;
  {
    size_t pos = 0;
    while (pos < out.size()) {
      size_t eol = out.find('\n', pos);
      std::string line = (eol == std::string::npos)
                             ? out.substr(pos)
                             : out.substr(pos, eol - pos);
      pos = (eol == std::string::npos) ? out.size() : eol + 1;

      if (line.find("warning:") == std::string::npos) {
        continue;
      }

      // Skip "missing include" warnings — only keep redundant ("not used
      // directly") and all non-include-cleaner checks.
      if (line.find("misc-include-cleaner") != std::string::npos &&
          line.find("no header providing") != std::string::npos) {
        continue;
      }

      LintWarning w;
      // file:line:col: warning: description [check]
      size_t first_colon = line.find(':');
      size_t second_colon = line.find(':', first_colon + 1);

      // Full path, then shorten to filename
      std::string full_path = line.substr(0, first_colon);
      size_t slash = full_path.rfind('/');
      w.file = (slash != std::string::npos)
                   ? full_path.substr(slash + 1)
                   : full_path;
      w.line = std::stoi(line.substr(first_colon + 1,
                                     second_colon - first_colon - 1));

      // Description is between "warning: " and " ["
      size_t warn_pos = line.find("warning:");
      size_t desc_start = warn_pos + 9;
      size_t bracket = line.rfind('[');
      if (bracket != std::string::npos && bracket > desc_start) {
        w.description = line.substr(desc_start, bracket - desc_start);
        size_t bracket_close = line.find(']', bracket);
        w.check = line.substr(bracket + 1, bracket_close - bracket - 1);
      } else {
        w.description = line.substr(desc_start);
        w.check = "unknown";
      }
      // Trim trailing whitespace from description
      while (!w.description.empty() &&
             (w.description.back() == ' ' || w.description.back() == '\t')) {
        w.description.pop_back();
      }

      details.push_back(std::move(w));
    }
  }

  // Step 8: Append text summary (first 10)
  if (!details.empty()) {
    result += "==> Summary (" + std::to_string(details.size()) + " warnings):\n";
    int shown = 0;
    for (const auto &w : details) {
      if (shown >= 10) {
        result += "  ... and " + std::to_string(details.size() - 10) + " more\n";
        break;
      }
      result += "  " + std::to_string(shown + 1) + ". " +
                w.file + ":" + std::to_string(w.line) +
                " [" + w.check + "] " + w.description + "\n";
      shown++;
    }
  } else {
    result += "==> No warnings found.\n";
  }

  result += "==> Lint complete.\n";
  return {true, result, details};
}

BuildResult build_run(const std::string &sandbox, const std::string &mode,
                      const std::function<void(const std::string &)> &status,
                      const std::string &subdir) {
  const std::string root = subdir.empty() ? sandbox : sandbox + "/" + subdir;
  const auto sys = detect_build_system(root);
  if (sys == BuildSystem::NONE) {
    return {false, "ERROR: no recognised build system found in " + root +
                    " (looked for CMakeLists.txt, build.gradle, pom.xml, pyproject.toml, setup.py, requirements.txt, *.py)"};
  }

  std::string m = mode;
  if (m.empty()) {
    m = "all";
  }

  if (m == "setup") {
    switch (sys) {
      case BuildSystem::CMAKE:  return setup_cmake(root, status);
      case BuildSystem::MAVEN:  return setup_maven(root, status);
      case BuildSystem::GRADLE: return setup_gradle(root, status);
      case BuildSystem::PYTHON: return setup_python(root, status);
      default:                  return {false, "ERROR: unsupported build system"};
    }
  }

  if (m == "lint") {
    if (sys == BuildSystem::CMAKE) {
      return build_cmake_lint(root, status);
    }
    return {false, "ERROR: lint mode is only supported for CMake projects"};
  }

  if (m != "build" && m != "test" && m != "clean" && m != "all") {
    return {false, "ERROR: unknown build mode: " + m + " (expected: setup, build, test, clean, lint, or all)"};
  }

  switch (sys) {
    case BuildSystem::CMAKE:
      return build_cmake(root, m, status);
    case BuildSystem::GRADLE:
      return build_gradle(root, m, status);
    case BuildSystem::MAVEN:
      return build_maven(root, m, status);
    case BuildSystem::PYTHON:
      return build_python(root, m, status);
    default:
      return {false, "ERROR: unsupported build system"};
  }
}
