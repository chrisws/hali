// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>

#include "build.h"

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

static std::string run_command(const std::string &cmd,
                               const std::function<void(const std::string &)> &status) {
  status("build: " + cmd);
  const std::string full_cmd = cmd + " 2>&1";
  FILE *fp = popen(full_cmd.c_str(), "r");
  if (!fp) {
    return "ERROR: popen failed for: " + cmd;
  }
  std::string out;
  char buf[256];
  while (fgets(buf, sizeof(buf), fp)) {
    out += buf;
  }
  pclose(fp);
  if (out.size() > 4096) {
    const size_t head = 1024;
    const size_t tail = 3072;
    out = out.substr(0, head) + "\n…(truncated)…" +
          out.substr(out.size() - tail);
  }
  return out;
}

static BuildResult build_cmake(const std::string &sandbox, const std::string &mode,
                               const std::function<void(const std::string &)> &status) {
  const int nproc = std::thread::hardware_concurrency();
  const std::string build_dir = sandbox + "/build";
  const std::string tests_build_dir = sandbox + "/tests/build";
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

  if (mode == "test" || mode == "all") {
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
    result += run_command("make -C " + tests_build_dir + " clean", status);
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
  auto out = run_command("cmake -S " + sandbox + " -B " + sandbox + "/build -G \"Unix Makefiles\"", status);
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

BuildResult build_run(const std::string &sandbox, const std::string &mode,
                      const std::function<void(const std::string &)> &status) {
  const auto sys = detect_build_system(sandbox);
  if (sys == BuildSystem::NONE) {
    return {false, "ERROR: no recognised build system found in sandbox (looked for CMakeLists.txt, build.gradle, pom.xml, pyproject.toml, setup.py, requirements.txt, *.py)"};
  }

  std::string m = mode;
  if (m.empty()) {
    m = "all";
  }

  if (m == "setup") {
    switch (sys) {
      case BuildSystem::CMAKE:  return setup_cmake(sandbox, status);
      case BuildSystem::MAVEN:  return setup_maven(sandbox, status);
      case BuildSystem::GRADLE: return setup_gradle(sandbox, status);
      case BuildSystem::PYTHON: return setup_python(sandbox, status);
      default:                  return {false, "ERROR: unsupported build system"};
    }
  }

  if (m != "build" && m != "test" && m != "clean" && m != "all") {
    return {false, "ERROR: unknown build mode: " + m + " (expected: setup, build, test, clean, or all)"};
  }

  switch (sys) {
    case BuildSystem::CMAKE:
      return build_cmake(sandbox, m, status);
    case BuildSystem::GRADLE:
      return build_gradle(sandbox, m, status);
    case BuildSystem::MAVEN:
      return build_maven(sandbox, m, status);
    case BuildSystem::PYTHON:
      return build_python(sandbox, m, status);
    default:
      return {false, "ERROR: unsupported build system"};
  }
}
