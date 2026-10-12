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

  cout << "\n=== All TOOL:BUILD tests completed ===\n";
}