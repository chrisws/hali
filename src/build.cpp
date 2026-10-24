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

  if ((mode == "test" || mode == "all") && has_tests) {
    result += "==> Building unit tests...\n";
    auto out = run_command("make -C " + tests_build_dir + " -j" + std::to_string(nproc), status);
    if (out.find("Error") != std::string::npos || out.find("error") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Running unit tests...\n";
    out = run_command("ctest --test-dir " + tests_build_dir + " --output-on-failure", status);
    if (out.find("Failed") != std::string::npos || out.find("failed") != std::string::npos) {
      ok = false;
    }
    result += out;
    result += "==> Tests complete.\n";
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
