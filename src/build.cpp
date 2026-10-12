// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <filesystem>
#include <format>
#include <sstream>
#include <thread>

#include "build.h"

namespace fs = std::filesystem;

enum class BuildSystem {
  NONE,
  CMAKE,
  GRADLE,
  MAVEN,
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
    out = out.substr(0, 4096) + "\n…(truncated)";
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

BuildResult build_run(const std::string &sandbox, const std::string &mode,
                      const std::function<void(const std::string &)> &status) {
  const auto sys = detect_build_system(sandbox);
  if (sys == BuildSystem::NONE) {
    return {false, "ERROR: no recognised build system found in sandbox (looked for CMakeLists.txt, build.gradle, pom.xml)"};
  }

  std::string m = mode;
  if (m.empty()) {
    m = "all";
  }

  if (m != "build" && m != "test" && m != "clean" && m != "all") {
    return {false, "ERROR: unknown build mode: " + m + " (expected: build, test, clean, or all)"};
  }

  switch (sys) {
    case BuildSystem::CMAKE:
      return build_cmake(sandbox, m, status);
    case BuildSystem::GRADLE:
      return build_gradle(sandbox, m, status);
    case BuildSystem::MAVEN:
      return build_maven(sandbox, m, status);
    default:
      return {false, "ERROR: unsupported build system"};
  }
}
