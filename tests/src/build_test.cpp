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
#include <fstream>
#include <filesystem>
#include <vector>
#include "build.h"

namespace fs = std::filesystem;
using namespace std;

#define RUN_TEST(name) do {                         \
    std::cout << "Running " << #name << "... ";     \
    try { name(); std::cout << "PASSED\n"; }        \
    catch (const std::exception& e) {               \
      std::cout << "FAILED: " << e.what() << "\n";  \
    }                                               \
  } while(0)

static const string SANDBOX = "/tmp/hali_test_build_sandbox";

static void setup_sandbox() {
  fs::remove_all(SANDBOX);
  fs::create_directories(SANDBOX);
}

static void teardown_sandbox() {
  fs::remove_all(SANDBOX);
}

static void write_file(const string &path, const string &content) {
  ofstream out(path);
  out << content;
}

//
// test: no build system detected
//
static void test_no_build_system() {
  setup_sandbox();
  auto r = build_run(SANDBOX, "build", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") != string::npos);
  teardown_sandbox();
}

//
// test: unknown mode rejected
//
static void test_unknown_mode() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt", "cmake_minimum_required(VERSION 3.15)\n");
  auto r = build_run(SANDBOX, "invalid_mode", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: status callback is invoked with "build:" prefix
//
static void test_status_callback() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt", "cmake_minimum_required(VERSION 3.15)\n");
  vector<string> messages;
  build_run(SANDBOX, "clean", [&](const string &msg) {
    messages.push_back(msg);
  });
  assert(!messages.empty());
  assert(messages[0].find("build:") == 0);
  teardown_sandbox();
}

//
// test: CMake clean mode with minimal project
//
static void test_cmake_clean() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt",
    "cmake_minimum_required(VERSION 3.15)\n"
    "project(testproj CXX)\n"
    "add_executable(hello main.cpp)\n");
  write_file(SANDBOX + "/main.cpp", "int main(){return 0;}\n");
  // Minimal Makefiles so `make clean` succeeds
  fs::create_directories(SANDBOX + "/build");
  write_file(SANDBOX + "/build/Makefile", "clean:\n\techo 'main cleaned'\n");
  fs::create_directories(SANDBOX + "/tests/build");
  write_file(SANDBOX + "/tests/build/Makefile", "clean:\n\techo 'tests cleaned'\n");

  auto r = build_run(SANDBOX, "clean", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Cleaning build artifacts") != string::npos);
  assert(r.output_.find("Clean complete") != string::npos);
  assert(r.output_.find("main cleaned") != string::npos);
  assert(r.output_.find("tests cleaned") != string::npos);
  teardown_sandbox();
}

//
// test: Maven detection (pom.xml present, no "no build system" error)
//
static void test_maven_detection() {
  setup_sandbox();
  write_file(SANDBOX + "/pom.xml", "<project></project>\n");
  // Use invalid mode to trigger detection without running maven
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: CMake takes priority over Maven when both exist
//
static void test_cmake_priority() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt", "cmake_minimum_required(VERSION 3.15)\n");
  write_file(SANDBOX + "/pom.xml", "<project></project>\n");
  // Clean mode on CMake project should reference CMake paths, not maven
  fs::create_directories(SANDBOX + "/build");
  write_file(SANDBOX + "/build/Makefile", "clean:\n\techo 'cmake cleaned'\n");
  fs::create_directories(SANDBOX + "/tests/build");
  write_file(SANDBOX + "/tests/build/Makefile", "clean:\n\techo 'cmake tests cleaned'\n");

  auto r = build_run(SANDBOX, "clean", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("cmake cleaned") != string::npos);
  assert(r.output_.find("mvn") == string::npos);
  teardown_sandbox();
}

//
// test: Gradle detection via build.gradle
//
static void test_gradle_detection() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'com.android.application'\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle detection via build.gradle.kts (Kotlin DSL)
//
static void test_gradle_kts_detection() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle.kts", "plugins { id(\"com.android.application\") }\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle detection via gradlew wrapper only
//
static void test_gradlew_detection() {
  setup_sandbox();
  write_file(SANDBOX + "/gradlew", "#!/bin/sh\necho gradlew\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle clean mode with fake gradlew
//
static void test_gradle_clean() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'com.android.application'\n");
  // Fake gradlew that echoes a marker
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle clean ran: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "clean", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Cleaning with Gradle") != string::npos);
  assert(r.output_.find("Clean complete") != string::npos);
  assert(r.output_.find("gradle clean ran:") != string::npos);
  assert(r.output_.find("clean") != string::npos);
  teardown_sandbox();
}

//
// test: CMake takes priority over Gradle
//
static void test_cmake_over_gradle() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt", "cmake_minimum_required(VERSION 3.15)\n");
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'java'\n");
  fs::create_directories(SANDBOX + "/build");
  write_file(SANDBOX + "/build/Makefile", "clean:\n\techo 'cmake cleaned'\n");
  fs::create_directories(SANDBOX + "/tests/build");
  write_file(SANDBOX + "/tests/build/Makefile", "clean:\n\techo 'cmake tests cleaned'\n");

  auto r = build_run(SANDBOX, "clean", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("cmake cleaned") != string::npos);
  assert(r.output_.find("gradle") == string::npos);
  teardown_sandbox();
}

//
// test: Gradle takes priority over Maven
//
static void test_gradle_over_maven() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'java'\n");
  write_file(SANDBOX + "/pom.xml", "<project></project>\n");
  // Use invalid mode to verify detection without running gradle
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  // Should be detected as Gradle (not Maven, not "no build system")
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Python detection via pyproject.toml
//
static void test_python_detection_pyproject() {
  setup_sandbox();
  write_file(SANDBOX + "/pyproject.toml", "[project]\nname = \"test\"\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Python detection via setup.py
//
static void test_python_detection_setup_py() {
  setup_sandbox();
  write_file(SANDBOX + "/setup.py", "from setuptools import setup\nsetup()\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Python detection via requirements.txt
//
static void test_python_detection_requirements() {
  setup_sandbox();
  write_file(SANDBOX + "/requirements.txt", "requests\nflask\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Python detection via bare .py script
//
static void test_python_detection_bare_script() {
  setup_sandbox();
  write_file(SANDBOX + "/hello.py", "print('hello')\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: CMake takes priority over Python
//
static void test_cmake_over_python() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt", "cmake_minimum_required(VERSION 3.15)\n");
  write_file(SANDBOX + "/pyproject.toml", "[project]\nname = \"test\"\n");
  fs::create_directories(SANDBOX + "/build");
  write_file(SANDBOX + "/build/Makefile", "clean:\n\techo 'cmake cleaned'\n");
  fs::create_directories(SANDBOX + "/tests/build");
  write_file(SANDBOX + "/tests/build/Makefile", "clean:\n\techo 'cmake tests cleaned'\n");

  auto r = build_run(SANDBOX, "clean", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("cmake cleaned") != string::npos);
  assert(r.output_.find("python3") == string::npos);
  teardown_sandbox();
}

//
// test: Python clean mode removes __pycache__ and .pyc files
//
static void test_python_clean() {
  setup_sandbox();
  write_file(SANDBOX + "/main.py", "print('hi')\n");
  // Create artifacts that clean should remove
  fs::create_directories(SANDBOX + "/__pycache__");
  write_file(SANDBOX + "/__pycache__/main.cpython-311.pyc", "fake");
  write_file(SANDBOX + "/main.pyc", "fake");
  fs::create_directories(SANDBOX + "/.pytest_cache");
  write_file(SANDBOX + "/.pytest_cache/CACHEDIR.TAG", "fake");

  auto r = build_run(SANDBOX, "clean", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Cleaning Python artifacts") != string::npos);
  assert(r.output_.find("Clean complete") != string::npos);
  // Verify artifacts were removed
  assert(!fs::exists(SANDBOX + "/__pycache__"));
  assert(!fs::exists(SANDBOX + "/main.pyc"));
  assert(!fs::exists(SANDBOX + "/.pytest_cache"));
  teardown_sandbox();
}

//
// test: Python build mode runs syntax check
//
static void test_python_build() {
  setup_sandbox();
  write_file(SANDBOX + "/main.py", "print('hello')\n");
  auto r = build_run(SANDBOX, "build", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Syntax-checking Python files") != string::npos);
  assert(r.output_.find("Syntax check complete") != string::npos);
  teardown_sandbox();
}

//
// test: Python build mode fails on syntax error
//
static void test_python_build_syntax_error() {
  setup_sandbox();
  write_file(SANDBOX + "/bad.py", "def broken(:\n  pass\n");
  auto r = build_run(SANDBOX, "build", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("Syntax check complete") != string::npos);
  teardown_sandbox();
}

//
// test: CMake setup with no tests directory
//
static void test_cmake_setup_no_tests() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt",
    "cmake_minimum_required(VERSION 3.15)\n"
    "project(setuptest CXX)\n");
  write_file(SANDBOX + "/main.cpp", "int main(){return 0;}\n");

  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Configuring main build") != string::npos);
  assert(r.output_.find("No tests/ directory found") != string::npos);
  assert(r.output_.find("Setup complete") != string::npos);
  assert(fs::exists(SANDBOX + "/build"));
  teardown_sandbox();
}

//
// test: CMake setup with tests directory
//
static void test_cmake_setup_with_tests() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt",
    "cmake_minimum_required(VERSION 3.15)\n"
    "project(setuptest CXX)\n");
  write_file(SANDBOX + "/main.cpp", "int main(){return 0;}\n");
  fs::create_directories(SANDBOX + "/tests");
  write_file(SANDBOX + "/tests/CMakeLists.txt",
    "cmake_minimum_required(VERSION 3.15)\n"
    "project(setuptest_tests CXX)\n");

  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Configuring main build") != string::npos);
  assert(r.output_.find("Configuring test build") != string::npos);
  assert(r.output_.find("Setup complete") != string::npos);
  assert(fs::exists(SANDBOX + "/build"));
  assert(fs::exists(SANDBOX + "/tests/build"));
  teardown_sandbox();
}

//
// test: Maven setup skips generation when pom.xml already exists
//
static void test_maven_setup_existing_pom() {
  setup_sandbox();
  write_file(SANDBOX + "/pom.xml",
    "<project><modelVersion>4.0.0</modelVersion></project>\n");
  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.output_.find("already exists") != string::npos);
  assert(r.output_.find("Generating basic pom.xml") == string::npos);
  teardown_sandbox();
}

//
// test: Gradle setup succeeds with gradlew and build.gradle
//
static void test_gradle_setup_ok() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'java'\n");
  write_file(SANDBOX + "/gradlew", "#!/bin/sh\necho gradlew\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("gradlew found and made executable") != string::npos);
  assert(r.output_.find("Setup complete") != string::npos);
  auto perms = fs::status(SANDBOX + "/gradlew").permissions();
  assert(perms & fs::perms::owner_exec);
  teardown_sandbox();
}

//
// test: Gradle setup fails when gradlew is missing
//
static void test_gradle_setup_no_gradlew() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'java'\n");

  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no gradlew found") != string::npos);
  assert(r.output_.find("Setup complete") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle setup fails when build.gradle is missing
//
static void test_gradle_setup_no_build_gradle() {
  setup_sandbox();
  write_file(SANDBOX + "/gradlew", "#!/bin/sh\necho gradlew\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no build.gradle found") != string::npos);
  assert(r.output_.find("Setup complete") != string::npos);
  teardown_sandbox();
}

//
// test: Python setup detects pyproject.toml
//
static void test_python_setup_pyproject() {
  setup_sandbox();
  write_file(SANDBOX + "/pyproject.toml", "[project]\nname = \"test\"\n");
  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("pyproject.toml found") != string::npos);
  assert(r.output_.find("Setup complete") != string::npos);
  teardown_sandbox();
}

//
// test: Python setup detects setup.py
//
static void test_python_setup_setup_py() {
  setup_sandbox();
  write_file(SANDBOX + "/setup.py", "from setuptools import setup\nsetup()\n");
  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("setup.py found") != string::npos);
  assert(r.output_.find("Setup complete") != string::npos);
  teardown_sandbox();
}

//
// test: Python setup with no project file (implicit discovery)
//
static void test_python_setup_neither() {
  setup_sandbox();
  write_file(SANDBOX + "/main.py", "print('hi')\n");
  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("implicit discovery") != string::npos);
  assert(r.output_.find("Setup complete") != string::npos);
  teardown_sandbox();
}

//
// test: lint mode on non-CMake project is rejected
//
static void test_lint_non_cmake() {
  setup_sandbox();
  write_file(SANDBOX + "/pyproject.toml", "[project]\nname = \"test\"\n");
  auto r = build_run(SANDBOX, "lint", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("only supported for CMake") != string::npos);
  teardown_sandbox();
}

//
// test: lint mode on CMake project with no source files succeeds
//
static void test_lint_no_sources() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt",
    "cmake_minimum_required(VERSION 3.15)\n"
    "project(linttest CXX)\n");
  // No src/ directory, no .cpp files
  auto r = build_run(SANDBOX, "lint", [](const string &) {});
  // Should fail at compile_db generation (cmake may not be available in test)
  // OR succeed with "No C/C++ source files" if compile_db already exists
  // Either way, it should NOT be "unknown build mode"
  assert(r.output_.find("unknown build mode") == string::npos);
  teardown_sandbox();
}

//
// test: lint mode is recognised (not rejected as unknown)
//
static void test_lint_mode_recognised() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt",
    "cmake_minimum_required(VERSION 3.15)\n"
    "project(linttest CXX)\n");
  auto r = build_run(SANDBOX, "lint", [](const string &) {});
  // Should NOT say "unknown build mode"
  assert(r.output_.find("unknown build mode") == string::npos);
  // Will either succeed (no sources) or fail at cmake/clang-tidy step
  // Both are valid — the point is the mode was recognised
  teardown_sandbox();
}

void build_test() {
  cout << "=== Running TOOL:BUILD Unit Tests ===\n\n";

  RUN_TEST(test_no_build_system);
  RUN_TEST(test_unknown_mode);
  RUN_TEST(test_status_callback);
  RUN_TEST(test_cmake_clean);
  RUN_TEST(test_maven_detection);
  RUN_TEST(test_cmake_priority);
  RUN_TEST(test_gradle_detection);
  RUN_TEST(test_gradle_kts_detection);
  RUN_TEST(test_gradlew_detection);
  RUN_TEST(test_gradle_clean);
  RUN_TEST(test_cmake_over_gradle);
  RUN_TEST(test_gradle_over_maven);
  RUN_TEST(test_python_detection_pyproject);
  RUN_TEST(test_python_detection_setup_py);
  RUN_TEST(test_python_detection_requirements);
  RUN_TEST(test_python_detection_bare_script);
  RUN_TEST(test_cmake_over_python);
  RUN_TEST(test_python_clean);
  RUN_TEST(test_python_build);
  RUN_TEST(test_python_build_syntax_error);
  RUN_TEST(test_cmake_setup_no_tests);
  RUN_TEST(test_cmake_setup_with_tests);
  RUN_TEST(test_maven_setup_existing_pom);
  RUN_TEST(test_gradle_setup_ok);
  RUN_TEST(test_gradle_setup_no_gradlew);
  RUN_TEST(test_gradle_setup_no_build_gradle);
  RUN_TEST(test_python_setup_pyproject);
  RUN_TEST(test_python_setup_setup_py);
  RUN_TEST(test_python_setup_neither);
  RUN_TEST(test_lint_non_cmake);
  RUN_TEST(test_lint_no_sources);
  RUN_TEST(test_lint_mode_recognised);

  cout << "\n=== All TOOL:BUILD tests completed ===\n";
}