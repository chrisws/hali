// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <string>
#include <cstddef>
#include <functional>
#include <set>
#include <vector>
#include <utility>

#include "build.h"
#include "pom.h"
#include "util/project.h"
#include "util/shell_util.h"
#include "util/file_ext.h"

namespace fs = std::filesystem;

enum class BuildSystem {
  NONE,
  CMAKE,
  GRADLE,
  MAVEN,
  PYTHON,
};

// Search for pom.xml in the sandbox root or one level of subdirectories.
// Returns the full path if found, empty string otherwise.
std::string find_pom(const std::string &sandbox) {
  std::string root_pom = sandbox + "/pom.xml";
  if (fs::exists(root_pom)) {
    return root_pom;
  }
  // Check immediate subdirectories
  if (!fs::exists(sandbox)) return "";
  for (const auto &entry : fs::directory_iterator(sandbox)) {
    if (entry.is_directory()) {
      std::string sub_pom = entry.path().string() + "/pom.xml";
      if (fs::exists(sub_pom)) {
        return sub_pom;
      }
    }
  }
  return "";
}

static BuildSystem detect_build_system(const std::string &sandbox) {
  if (fs::exists(sandbox + "/CMakeLists.txt")) {
    return BuildSystem::CMAKE;
  }
  if (fs::exists(sandbox + "/build.gradle") ||
      fs::exists(sandbox + "/build.gradle.kts") ||
      fs::exists(sandbox + "/settings.gradle") ||
      fs::exists(sandbox + "/settings.gradle.kts") ||
      fs::exists(sandbox + "/gradlew")) {
    return BuildSystem::GRADLE;
  }
  if (!find_pom(sandbox).empty()) {
    return BuildSystem::MAVEN;
  }
  if (fs::exists(sandbox + "/pyproject.toml") ||
      fs::exists(sandbox + "/setup.py") ||
      fs::exists(sandbox + "/requirements.txt")) {
    return BuildSystem::PYTHON;
  }
  // Fall back: if there are .py files in the sandbox root, treat as Python
  if (fs::exists(sandbox) && fs::is_directory(sandbox)) {
    for (const auto &entry : fs::directory_iterator(sandbox)) {
      if (entry.is_regular_file() && entry.path().extension() == ".py") {
        return BuildSystem::PYTHON;
      }
    }
  }
  // Fall back: if there are .kt files in the sandbox root, treat as Gradle/Kotlin
  if (fs::exists(sandbox) && fs::is_directory(sandbox)) {
    for (const auto &entry : fs::directory_iterator(sandbox)) {
      if (entry.is_regular_file() && entry.path().extension() == ".kt") {
        return BuildSystem::GRADLE;
      }
    }
  }
  return BuildSystem::NONE;
}

static constexpr size_t kMaxOutputSize = 32768;
static constexpr size_t kTruncHeadSize = 2048;
static constexpr size_t kTruncTailSize = 30720;
static constexpr size_t kQuietMaxOutputSize = 8192;
static constexpr size_t kQuietTruncHeadSize = 1024;
static constexpr size_t kQuietTruncTailSize = 7168;

static std::string run_command(const std::string &cmd,
                               const std::function<void(const std::string &)> &status,
                               size_t max_size = kMaxOutputSize) {
  status("build: " + cmd);
  std::string out = run_shell_command(cmd);
  if (out.size() > max_size) {
    size_t head = (max_size / 4);
    size_t tail = max_size - head;
    out = out.substr(0, head) + "\n…(truncated)…" +
          out.substr(out.size() - tail);
  }
  return out;
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

//
// Scan for test functions across common test directories and source
// extensions.  Attempts to recognise test patterns in C/C++, Go,
// Python, Rust, Java, and JS/TS.
// Returns the total count of individual test functions found.
// Returns -1 if no test directory could be located.
//
int scan_test_count(const std::string &sandbox) {
  // Try common test directory layouts, most specific first.
  const char *candidates[] = {"tests/src", "tests", "test", "src/test/java"};
  std::string test_dir;
  for (const auto &c : candidates) {
    auto p = sandbox + "/" + c;
    if (fs::exists(p) && fs::is_directory(p)) {
      test_dir = p;
      break;
    }
  }
  // Also check one level deep for Maven subdirectory layout: */src/test/java
  if (test_dir.empty()) {
    for (const auto &entry : fs::directory_iterator(sandbox)) {
      if (!entry.is_directory()) continue;
      auto p = entry.path().string() + "/src/test/java";
      if (fs::exists(p) && fs::is_directory(p)) {
        test_dir = p;
        break;
      }
    }
  }
  if (test_dir.empty()) return -1;

  // Source extensions we recognise.
  const auto &src_exts = file_ext::source_extensions();

  // Patterns that indicate a test function in various languages.
  // Each entry: {substring to search for, minimum count weight}
  // We use simple substring matching; good enough for counting.
  const std::vector<std::string> test_patterns = {
    // C / C++
    "static void test_",
    "void test_",
    // Go
    "func Test",
    // Python
    "def test_",
    // Rust
    "#[test]",
    // Java / Kotlin
    "@Test",
    // Kotlin
    "fun test",
    // JS / TS
    "test(",
    "it(",
  };

  int count = 0;
  for (const auto &entry : fs::recursive_directory_iterator(test_dir)) {
    if (!entry.is_regular_file()) continue;
    auto ext = entry.path().extension().string();
    if (!file_ext::is_source(ext)) continue;

    std::ifstream f(entry.path());
    if (!f) continue;

    int file_count = 0;
    std::string line;
    while (std::getline(f, line)) {
      bool matched = false;
      for (const auto &pat : test_patterns) {
        if (line.find(pat) != std::string::npos) {
          matched = true;
          break;
        }
      }
      // C++ TEST() macro: line starts with "TEST(" after trimming.
      // Avoids matching RUN_TEST() call sites.
      if (!matched) {
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos &&
            line.compare(start, 5, "TEST(") == 0) {
          matched = true;
        }
      }
      if (matched) file_count++;
    }
    count += file_count;
  }
  return count;
}

static std::string make_test_badge(const std::string &sandbox, bool ok) {
  int tc = scan_test_count(sandbox);
  if (tc > 0) {
    if (ok) {
      generate_test_badge(sandbox, tc, tc);
      return "==> " + std::to_string(tc) + "/" + std::to_string(tc) +
             " tests passed\n"
             "==> Badge: " + sandbox + "/.hali/test_badge.svg\n";
    } else {
      generate_test_badge(sandbox, 0, tc);
      return "==> Tests failing\n"
             "==> Badge: " + sandbox + "/.hali/test_badge.svg\n";
    }
  }
  return "==> No test functions found in tests/src/\n";
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
    auto out = run_command("make -C " + build_dir + " -j" + std::to_string(nproc), status, kQuietMaxOutputSize);
    if (out.find("Error") != std::string::npos || out.find("error") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Build complete.\n";
  }

  if ((mode == "test" || mode == "all") && has_tests) {
    result += "==> Building unit tests...\n";
    auto out = run_command("make -C " + tests_build_dir + " -j" + std::to_string(nproc), status, kQuietMaxOutputSize);
    if (out.find("Error") != std::string::npos || out.find("error") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Running unit tests...\n";
    std::string junit_path = sandbox + "/.hali/results.xml";
    fs::create_directories(sandbox + "/.hali");
    out = run_command("ctest --test-dir " + tests_build_dir +
                      " --output-on-failure --output-junit " + junit_path, status, kQuietMaxOutputSize);
    if (out.find("Failed") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Tests complete.\n";
    result += make_test_badge(sandbox, ok);
  }

  if (mode == "clean") {
    result += "==> Cleaning build artifacts...\n";
    result += run_command("make -C " + build_dir + " clean", status);
    if (fs::exists(tests_build_dir)) {
      result += run_command("make -C " + tests_build_dir + " clean", status);
    }
    result += "==> Clean complete.\n";
  }

  return {ok, result};
}

static BuildResult build_maven(const std::string &sandbox, const std::string &mode,
                               const std::function<void(const std::string &)> &status) {
  const std::string pom = find_pom(sandbox);
  if (pom.empty()) {
    return {false, "ERROR: no pom.xml found in sandbox or subdirectories"};
  }
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
    auto out = run_command("mvn -f " + pom + " test -q", status, kQuietMaxOutputSize);
    if (out.find("BUILD FAILURE") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Tests complete.\n";
    result += make_test_badge(sandbox, ok);
  }

  if (mode == "clean") {
    result += "==> Cleaning with Maven...\n";
    result += run_command("mvn -f " + pom + " clean -q", status);
    result += "==> Clean complete.\n";
  }

  return {ok, result};
}

//
// Check if a project is an Android project using shared detection.
//
static bool is_android_project(const std::string &sandbox) {
  return detect_project_language(sandbox) == ProjectLanguage::ANDROID;
}

//
// Extract the Android package name from AndroidManifest.xml.
// Returns the package string (e.g. "com.example.app") or "" if not found.
//
static std::string extract_android_package(const std::string &sandbox) {
  const std::string app_dir = sandbox + "/app";
  if (!fs::exists(app_dir) || !fs::is_directory(app_dir)) return "";
  std::string manifest_path;
  for (const auto &entry : fs::recursive_directory_iterator(app_dir)) {
    if (entry.is_regular_file() &&
        entry.path().filename() == "AndroidManifest.xml") {
      manifest_path = entry.path().string();
      break;
    }
  }
  if (manifest_path.empty()) return "";
  std::ifstream f(manifest_path);
  if (!f) return "";
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  f.close();
  const std::string key = "package=\"";
  size_t pos = content.find(key);
  if (pos == std::string::npos) return "";
  pos += key.size();
  size_t end = content.find('"', pos);
  if (end == std::string::npos) return "";
  return content.substr(pos, end - pos);
}

//
// Parse JUnit XML test results from Gradle build/test-results/ directory.
// Returns {total_tests, total_failures, total_errors}.
//
struct GradleTestSummary {
  int total_tests;
  int total_failures;
  int total_errors;
};

static GradleTestSummary parse_gradle_test_results(const std::string &sandbox) {
  GradleTestSummary summary{0, 0, 0};
  // Gradle writes test results to build/test-results/<task>/
  // Common task names: test, testDebugUnitTest, testReleaseUnitTest
  const std::vector<std::string> result_dirs = {
    sandbox + "/build/test-results/test",
    sandbox + "/build/test-results/testDebugUnitTest",
    sandbox + "/build/test-results/testReleaseUnitTest",
  };
  for (const auto &dir : result_dirs) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) continue;
    for (const auto &entry : fs::directory_iterator(dir)) {
      if (!entry.is_regular_file()) continue;
      if (entry.path().extension() != ".xml") continue;
      std::ifstream f(entry.path());
      if (!f) continue;
      std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
      f.close();
      // Parse <testsuite ... tests="N" failures="N" errors="N" ...>
      size_t pos = 0;
      while ((pos = content.find("<testsuite", pos)) != std::string::npos) {
        size_t end = content.find(">", pos);
        if (end == std::string::npos) break;
        std::string tag = content.substr(pos, end - pos);
        auto extract_attr = [&tag](const std::string &name) -> int {
          std::string key = name + "=\"";
          size_t a = tag.find(key);
          if (a == std::string::npos) return 0;
          a += key.size();
          size_t b = tag.find('"', a);
          if (b == std::string::npos) return 0;
          return std::stoi(tag.substr(a, b - a));
        };
        summary.total_tests += extract_attr("tests");
        summary.total_failures += extract_attr("failures");
        summary.total_errors += extract_attr("errors");
        pos = end + 1;
      }
    }
  }
  return summary;
}

//
// Deploy an Android APK to a connected device via adb and launch it.
// Returns a status string to append to the result.
//
static std::string deploy_android(const std::string &sandbox,
                                   const std::string &adb_path,
                                   const std::function<void(const std::string &)> &status,
                                   bool &ok) {
  if (!ok) return "";
  const std::string adb = adb_path.empty() ? "adb" : adb_path;
  const std::string apk =
      sandbox + "/app/build/outputs/apk/debug/app-debug.apk";
  if (!fs::exists(apk)) {
    return "==> WARNING: APK not found at " + apk +
           ". Skipping adb deploy.\n";
  }
  std::string result;
  result += "==> Deploying to device via adb...\n";
  auto adb_out = run_command(adb + " install -r " + apk, status);
  if (adb_out.find("Success") != std::string::npos) {
    result += "==> APK installed successfully.\n";
    std::string pkg = extract_android_package(sandbox);
    if (!pkg.empty()) {
      result += "==> Launching " + pkg + "...\n";
      auto launch_out = run_command(
          adb + " shell monkey -p " + pkg +
          " -c android.intent.category.LAUNCHER 1", status);
      if (launch_out.find("Exception") == std::string::npos) {
        result += "==> App launched.\n";
      } else {
        result += "==> WARNING: app launch may have failed.\n";
        result += launch_out;
      }
    } else {
      result += "==> WARNING: could not determine package name. "
                "Skipping launch.\n";
    }
  } else {
    ok = false;
    result += "==> WARNING: adb install may have failed.\n";
    result += adb_out;
  }
  return result;
}

static BuildResult build_gradle(const std::string &sandbox, const std::string &mode,
                                 const std::function<void(const std::string &)> &status,
                                 const std::string &adb_path = "") {
  // Prefer the Gradle wrapper over a system install
  const std::string gradle = fs::exists(sandbox + "/gradlew")
                                ? sandbox + "/gradlew"
                                : "gradle";
  const bool android = is_android_project(sandbox);
  std::string result;
  bool ok = true;

  if (mode == "build" || mode == "all") {
    std::string task = android ? "assembleDebug" : "build";
    result += "==> Building with Gradle" +
              (android ? " (Android: " + task + ")" : "") + "...\n";
    auto out = run_command(gradle + " -p " + sandbox + " --console=plain " + task, status);
    if (out.find("BUILD FAILED") != std::string::npos) {
      ok = false;
      // Extract compilation errors for better reporting
      size_t err_pos = 0;
      int err_count = 0;
      while ((err_pos = out.find("e: ", err_pos)) != std::string::npos && err_count < 10) {
        size_t eol = out.find('\n', err_pos);
        std::string err_line = (eol == std::string::npos)
                                   ? out.substr(err_pos)
                                   : out.substr(err_pos, eol - err_pos);
        result += "  ERROR: " + err_line + "\n";
        err_count++;
        err_pos = (eol == std::string::npos) ? out.size() : eol + 1;
      }
      if (err_count > 0) {
        result += "==> " + std::to_string(err_count) + " compilation error(s) found.\n";
      }
    }
    result += out;
    result += "==> Build complete.\n";

    // Deploy to device via adb for Android projects
    if (android) {
      result += deploy_android(sandbox, adb_path, status, ok);
    }
  }

  if (mode == "test" || mode == "all") {
    // For Android, ensure a fresh APK exists before tests + deploy
    if (android && mode == "test") {
      result += "==> Building APK (assembleDebug) for deploy...\n";
      auto build_out = run_command(
          gradle + " -p " + sandbox + " --console=plain assembleDebug", status);
      if (build_out.find("BUILD FAILED") != std::string::npos) {
        ok = false;
        result += build_out;
        result += "==> Build failed, skipping tests.\n";
        return {false, result};
      }
    }
    std::string task = android ? "testDebugUnitTest" : "test";
    result += "==> Running Gradle tests" +
              (android ? " (Android: " + task + ")" : "") + "...\n";
    auto out = run_command(gradle + " -p " + sandbox + " --console=plain " + task, status,
                           kQuietMaxOutputSize);
    if (out.find("BUILD FAILED") != std::string::npos) {
      ok = false;
    }
    result += out;

    // Parse structured test results from XML
    auto summary = parse_gradle_test_results(sandbox);
    if (summary.total_tests > 0) {
      int passed = summary.total_tests - summary.total_failures - summary.total_errors;
      result += "==> Test results: " + std::to_string(passed) + "/" +
                std::to_string(summary.total_tests) + " passed";
      if (summary.total_failures > 0) {
        result += ", " + std::to_string(summary.total_failures) + " failed";
      }
      if (summary.total_errors > 0) {
        result += ", " + std::to_string(summary.total_errors) + " errors";
      }
      result += "\n";
      if (summary.total_failures > 0 || summary.total_errors > 0) {
        ok = false;
      }
    }

    result += "==> Tests complete.\n";
    result += make_test_badge(sandbox, ok);

    // Deploy to device after tests for Android projects
    if (android) {
      result += deploy_android(sandbox, adb_path, status, ok);
    }
  }

  if (mode == "deploy") {
    if (!android) {
      result += "==> Deploy is only available for Android projects.\n";
      return {false, result};
    }
    result += deploy_android(sandbox, adb_path, status, ok);
    result += "==> Deploy complete.\n";
  }

  if (mode == "devices") {
    if (!android) {
      result += "==> Devices is only available for Android projects.\n";
      return {false, result};
    }
    const std::string adb = adb_path.empty() ? "adb" : adb_path;
    result += "==> Checking connected devices...\n";
    auto out = run_command(adb + " devices", status);
    result += out;
    // Count connected devices (lines containing "device" but not headers)
    int count = 0;
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
      if (line.empty() || line.rfind("List of devices", 0) == 0 ||
          line.rfind("*)", 0) == 0) {
        continue;
      }
      if (line.find("device") != std::string::npos) {
        count++;
      }
    }
    if (count > 0) {
      result += "==> " + std::to_string(count) +
                " device(s) connected.\n";
    } else {
      result += "==> WARNING: no devices connected. "
                "Start an emulator or connect a device.\n";
      ok = false;
    }
  }

  if (mode == "clean") {
    result += "==> Cleaning with Gradle...\n";
    result += run_command(gradle + " -p " + sandbox + " --console=plain clean", status);
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
    result += make_test_badge(sandbox, ok);
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
  std::string pom = find_pom(sandbox);

  if (pom.empty()) {
    pom = sandbox + "/pom.xml";
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

static BuildResult setup_gradle(const std::string &sandbox) {
  std::string result;
  bool ok = true;

  result += "==> Checking Gradle project structure...\n";

  // Check for build files
  bool has_build_file = fs::exists(sandbox + "/build.gradle") ||
                        fs::exists(sandbox + "/build.gradle.kts");
  if (has_build_file) {
    result += "==> build.gradle found.\n";
  } else {
    result += "==> WARNING: no build.gradle found. This project may need to be\n"
              "==> set up in Android Studio or IntelliJ first.\n";
    ok = false;
  }

  // Check for settings file
  bool has_settings = fs::exists(sandbox + "/settings.gradle") ||
                      fs::exists(sandbox + "/settings.gradle.kts");
  if (has_settings) {
    result += "==> settings.gradle found.\n";
  } else {
    result += "==> NOTE: no settings.gradle found (single-module project).\n";
  }

  // Check for gradlew wrapper
  if (fs::exists(sandbox + "/gradlew")) {
    fs::permissions(sandbox + "/gradlew",
                     fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                     fs::perms::others_read | fs::perms::others_exec);
    result += "==> gradlew found and made executable.\n";
  } else {
    result += "==> WARNING: no gradlew found. If this is an Android Studio project,\n"
              "==> the wrapper should already exist. If not, run: gradle wrapper\n";
    ok = false;
  }

  // Check for gradle wrapper properties
  if (fs::exists(sandbox + "/gradle/wrapper/gradle-wrapper.properties")) {
    result += "==> gradle-wrapper.properties found.\n";
  } else {
    result += "==> NOTE: no gradle/wrapper/gradle-wrapper.properties found.\n";
  }

  // Check for gradle.properties
  if (fs::exists(sandbox + "/gradle.properties")) {
    result += "==> gradle.properties found.\n";
  }

  // Android-specific checks
  if (is_android_project(sandbox)) {
    result += "==> Android project detected.\n";
    if (fs::exists(sandbox + "/local.properties")) {
      result += "==> local.properties found (Android SDK configured).\n";
    } else {
      result += "==> WARNING: no local.properties found.\n"
                "==> Android SDK path may not be configured. Create local.properties\n"
                "==> with: sdk.dir=/path/to/android/sdk\n";
    }
    // Check for AndroidManifest.xml
    bool has_manifest = false;
    auto app_dir = sandbox + "/app";
    if (fs::exists(app_dir) && fs::is_directory(app_dir)) {
      for (const auto &entry : fs::recursive_directory_iterator(app_dir)) {
        if (entry.is_regular_file() &&
            entry.path().filename() == "AndroidManifest.xml") {
          has_manifest = true;
          break;
        }
      }
    }
    if (has_manifest) {
      result += "==> AndroidManifest.xml found.\n";
    } else {
      result += "==> WARNING: no AndroidManifest.xml found in app/.\n";
    }
  }

  // Check for Kotlin source files
  int kt_count = 0;
  auto src_dir = sandbox + "/src";
  if (fs::exists(src_dir) && fs::is_directory(src_dir)) {
    for (const auto &entry : fs::recursive_directory_iterator(src_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".kt") {
        kt_count++;
      }
    }
  }
  if (kt_count > 0) {
    result += "==> Found " + std::to_string(kt_count) + " Kotlin source file(s).\n";
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
        if (file_ext::is_cpp(ext)) {
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

static BuildResult build_gradle_lint(const std::string &sandbox,
                                     const std::function<void(const std::string &)> &status) {
  std::string result;
  std::vector<LintWarning> details;

  // Collect Kotlin source files
  std::vector<std::string> kt_files;
  auto src_dir = sandbox + "/src";
  auto app_dir = sandbox + "/app/src";
  const std::vector<std::string> search_dirs = {src_dir, app_dir};
  for (const auto &dir : search_dirs) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) continue;
    for (const auto &entry : fs::recursive_directory_iterator(dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".kt") {
        kt_files.push_back(entry.path().string());
      }
    }
  }

  if (kt_files.empty()) {
    result += "==> No Kotlin source files found in src/ or app/src/.\n";
    result += "==> Lint complete.\n";
    return {true, result, details};
  }

  result += "==> Found " + std::to_string(kt_files.size()) + " Kotlin file(s) to lint.\n";

  // Try ktlint first
  auto ktlint_check = run_command("which ktlint 2>/dev/null", status);
  if (ktlint_check.find("ktlint") != std::string::npos) {
    result += "==> Running ktlint...\n";
    std::string files_arg;
    for (const auto &f : kt_files) {
      files_arg += f + " ";
    }
    auto out = run_command("ktlint " + files_arg, status);
    result += out;

    // Parse ktlint output: file:line:col: message [rule]
    size_t pos = 0;
    while (pos < out.size()) {
      size_t eol = out.find('\n', pos);
      std::string line = (eol == std::string::npos)
                             ? out.substr(pos)
                             : out.substr(pos, eol - pos);
      pos = (eol == std::string::npos) ? out.size() : eol + 1;

      // ktlint format: /path/to/file.kt:12:34: message [rule-name]
      if (line.find(".kt:") == std::string::npos) continue;
      if (line.find("[") == std::string::npos) continue;

      LintWarning w;
      size_t colon1 = line.find(':');
      size_t colon2 = line.find(':', colon1 + 1);
      size_t colon3 = line.find(':', colon2 + 1);
      if (colon1 == std::string::npos || colon2 == std::string::npos ||
          colon3 == std::string::npos) continue;

      std::string full_path = line.substr(0, colon1);
      size_t slash = full_path.rfind('/');
      w.file = (slash != std::string::npos) ? full_path.substr(slash + 1) : full_path;
      w.line = std::stoi(line.substr(colon1 + 1, colon2 - colon1 - 1));

      size_t bracket = line.find('[');
      size_t bracket_close = line.find(']', bracket);
      if (bracket != std::string::npos && bracket_close != std::string::npos) {
        w.check = line.substr(bracket + 1, bracket_close - bracket - 1);
        w.description = line.substr(colon3 + 1, bracket - colon3 - 1);
      } else {
        w.check = "ktlint";
        w.description = line.substr(colon3 + 1);
      }
      while (!w.description.empty() &&
             (w.description.back() == ' ' || w.description.back() == '\t')) {
        w.description.pop_back();
      }
      details.push_back(std::move(w));
    }

    if (!details.empty()) {
      result += "==> ktlint found " + std::to_string(details.size()) +
                " issue(s).\n";
    } else {
      result += "==> ktlint: no issues found.\n";
    }
  } else {
    result += "==> ktlint not found on PATH. Skipping.\n"
              "==> Install with: curl -sSLO https://github.com/pinterest/ktlint/releases/latest/download/ktlint\n"
              "==> then: chmod +x ktlint && sudo mv ktlint /usr/local/bin/\n";
  }

  // Try detekt if configured
  auto detekt_check = run_command("which detekt 2>/dev/null", status);
  if (detekt_check.find("detekt") != std::string::npos) {
    result += "==> Running detekt...\n";
    auto out = run_command("detekt --input " + sandbox + "/src --report-txt " +
                           sandbox + "/.hali/detekt-report.txt 2>&1", status);
    result += out;
    if (out.find("FAILURE") != std::string::npos) {
      result += "==> detekt found issues.\n";
    } else {
      result += "==> detekt: no issues found.\n";
    }
  }

  // Summary
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
  }

  result += "==> Lint complete.\n";
  return {true, result, details};
}

BuildResult build_run(const std::string &sandbox, const std::string &mode,
                      const std::function<void(const std::string &)> &status,
                      const std::string &subdir,
                      const std::string &adb_path) {
  std::string m = mode;
  if (m.empty()) {
    m = "all";
  }

  // For pom mode, 'subdir' is the operation string, not a directory.
  // Use sandbox directly as the root.
  const std::string root = (m == "pom") ? sandbox : (subdir.empty() ? sandbox : sandbox + "/" + subdir);
  const auto sys = detect_build_system(root);
  if (sys == BuildSystem::NONE) {
    return {false, "ERROR: no recognised build system found in " + root +
                    " (looked for CMakeLists.txt, build.gradle, build.gradle.kts, settings.gradle, "
                    "gradlew, pom.xml, pyproject.toml, setup.py, requirements.txt, *.py, *.kt)"};
  }

  if (m == "setup") {
    switch (sys) {
      case BuildSystem::CMAKE:  return setup_cmake(root, status);
      case BuildSystem::MAVEN:  return setup_maven(root, status);
      case BuildSystem::GRADLE: return setup_gradle(root);
      case BuildSystem::PYTHON: return setup_python(root, status);
      default:                  return {false, "ERROR: unsupported build system"};
    }
  }

  if (m == "pom") {
    if (sys != BuildSystem::MAVEN) {
      return {false, "ERROR: pom mode requires a Maven project (pom.xml)"};
    }
    std::string pom = find_pom(root);
    if (pom.empty()) {
      return {false, "ERROR: no pom.xml found"};
    }
    PomResult pr = pom_run(pom, subdir, status);
    return {pr.success_, pr.output_};
  }

  if (m == "lint") {
    switch (sys) {
      case BuildSystem::CMAKE:
        return build_cmake_lint(root, status);
      case BuildSystem::GRADLE:
        return build_gradle_lint(root, status);
      default:
        return {false, "ERROR: lint mode is only supported for CMake and Gradle/Kotlin projects"};
    }
  }

  if (m != "build" && m != "test" && m != "clean" && m != "all") {
    return {false, "ERROR: unknown build mode: " + m + " (expected: setup, build, test, clean, lint, or all)"};
  }

  switch (sys) {
    case BuildSystem::CMAKE:
      return build_cmake(root, m, status);
    case BuildSystem::GRADLE:
      return build_gradle(root, m, status, adb_path);
    case BuildSystem::MAVEN:
      return build_maven(root, m, status);
    case BuildSystem::PYTHON:
      return build_python(root, m, status);
    default:
      return {false, "ERROR: unsupported build system"};
  }
}
