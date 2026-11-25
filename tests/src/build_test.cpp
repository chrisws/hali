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
  write_file(SANDBOX + "/CMakeLists.txt", "cmake_minimum_required(VERSION 3.10)\n");
  write_file(SANDBOX + "/setup.py", "from setuptools import setup\nsetup()\n");
  // Use clean mode: CMake clean should succeed (with fake Makefiles),
  // proving CMake was detected over Python
  fs::create_directories(SANDBOX + "/build");
  write_file(SANDBOX + "/build/Makefile", "clean:\n\techo 'cmake cleaned'\n");
  fs::create_directories(SANDBOX + "/tests/build");
  write_file(SANDBOX + "/tests/build/Makefile", "clean:\n\techo 'cmake tests cleaned'\n");

  auto r = build_run(SANDBOX, "clean", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("cmake cleaned") != string::npos);
  assert(r.output_.find("python") == string::npos);
  teardown_sandbox();
}

//
// test: Maven detection when pom.xml is in a subdirectory
//
static void test_maven_detection_subdir() {
  setup_sandbox();
  fs::create_directories(SANDBOX + "/server");
  write_file(SANDBOX + "/server/pom.xml", "<project></project>\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Maven build with pom.xml in subdirectory is detected and attempted
//
static void test_maven_build_subdir() {
  setup_sandbox();
  fs::create_directories(SANDBOX + "/server");
  write_file(SANDBOX + "/server/pom.xml", "<project></project>\n");
  auto r = build_run(SANDBOX, "build", [](const string &) {});
  // Should NOT say "no recognised build system" - it should detect Maven
  assert(r.output_.find("no recognised build system") == string::npos);
  // Should have attempted Maven compilation
  assert(r.output_.find("Compiling with Maven") != string::npos);
  teardown_sandbox();
}

//
// test: root pom.xml takes priority over subdirectory pom.xml
//
static void test_maven_root_pom_priority() {
  setup_sandbox();
  write_file(SANDBOX + "/pom.xml", "<project><artifactId>root</artifactId></project>\n");
  fs::create_directories(SANDBOX + "/sub");
  write_file(SANDBOX + "/sub/pom.xml", "<project><artifactId>sub</artifactId></project>\n");
  // Use invalid mode to just verify detection
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: no pom.xml anywhere means no Maven detection
//
static void test_no_pom_no_maven() {
  setup_sandbox();
  fs::create_directories(SANDBOX + "/server");
  write_file(SANDBOX + "/server/main.cpp", "int main(){return 0;}\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") != string::npos);
  teardown_sandbox();
}

//
// test: scan_test_count finds Java tests in src/test/java (Maven root layout)
//
static void test_maven_test_dir_root() {
  setup_sandbox();
  // Maven-style test dir at sandbox root
  fs::create_directories(SANDBOX + "/src/test/java");
  write_file(SANDBOX + "/src/test/java/MyTest.java",
    "import org.junit.Test;\n"
    "public class MyTest {\n"
    "  @Test\n"
    "  public void testFoo() {}\n"
    "  @Test\n"
    "  public void testBar() {}\n"
    "}\n");

  int count = scan_test_count(SANDBOX);
  assert(count == 2);
  teardown_sandbox();
}

//
// test: scan_test_count finds Java tests in server/src/test/java (subdir Maven)
//
static void test_maven_test_dir_subdir() {
  setup_sandbox();
  // Maven test dir one level deep: server/src/test/java
  fs::create_directories(SANDBOX + "/server/src/test/java");
  write_file(SANDBOX + "/server/src/test/java/ServerTest.java",
    "import org.junit.Test;\n"
    "public class ServerTest {\n"
    "  @Test\n"
    "  public void testStart() {}\n"
    "}\n");

  int count = scan_test_count(SANDBOX);
  assert(count == 1);
  teardown_sandbox();
}

//
// test: recursive iteration finds Java tests in nested packages
//
static void test_maven_test_dir_nested_packages() {
  setup_sandbox();
  // Nested Java package: src/test/java/com/example/
  fs::create_directories(SANDBOX + "/src/test/java/com/example");
  write_file(SANDBOX + "/src/test/java/com/example/AppTest.java",
    "import org.junit.Test;\n"
    "public class AppTest {\n"
    "  @Test\n"
    "  public void testAlpha() {}\n"
    "  @Test\n"
    "  public void testShutdown() {}\n"
    "}\n");

  int count = scan_test_count(SANDBOX);
  assert(count == 2);
  teardown_sandbox();
}

//
// test: Gradle detection via settings.gradle
//
static void test_gradle_settings_detection() {
  setup_sandbox();
  write_file(SANDBOX + "/settings.gradle", "rootProject.name = 'myapp'\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle detection via settings.gradle.kts
//
static void test_gradle_settings_kts_detection() {
  setup_sandbox();
  write_file(SANDBOX + "/settings.gradle.kts", "rootProject.name = \"myapp\"\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: Kotlin file fallback detection (bare .kt file, no build files)
//
static void test_kotlin_file_detection() {
  setup_sandbox();
  write_file(SANDBOX + "/Main.kt", "fun main() { println(\"hello\") }\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: CMake takes priority over Kotlin files
//
static void test_cmake_over_kotlin() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt", "cmake_minimum_required(VERSION 3.15)\n");
  write_file(SANDBOX + "/Main.kt", "fun main() {}\n");
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
// test: Gradle takes priority over Kotlin file fallback
//
static void test_gradle_over_kotlin_fallback() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'kotlin'\n");
  write_file(SANDBOX + "/Main.kt", "fun main() {}\n");
  auto r = build_run(SANDBOX, "invalid", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("no recognised build system") == string::npos);
  assert(r.output_.find("unknown build mode") != string::npos);
  teardown_sandbox();
}

//
// test: scan_test_count finds Kotlin tests with @Test annotation
//
static void test_kotlin_test_scan_at_test() {
  setup_sandbox();
  fs::create_directories(SANDBOX + "/tests/src");
  write_file(SANDBOX + "/tests/src/MyTest.kt",
    "import org.junit.Test\n"
    "class MyTest {\n"
    "  @Test\n"
    "  fun testFoo() {}\n"
    "  @Test\n"
    "  fun testBar() {}\n"
    "}\n");

  int count = scan_test_count(SANDBOX);
  assert(count == 2);
  teardown_sandbox();
}

//
// test: scan_test_count finds Kotlin tests with fun test pattern
//
static void test_kotlin_test_scan_fun_test() {
  setup_sandbox();
  fs::create_directories(SANDBOX + "/tests/src");
  write_file(SANDBOX + "/tests/src/MyTest.kt",
    "class MyTest {\n"
    "  fun testAlpha() {}\n"
    "  fun testBeta() {}\n"
    "}\n");

  int count = scan_test_count(SANDBOX);
  assert(count == 2);
  teardown_sandbox();
}

//
// test: scan_test_count finds mixed Java and Kotlin tests
//
static void test_mixed_java_kotlin_test_scan() {
  setup_sandbox();
  fs::create_directories(SANDBOX + "/tests/src");
  write_file(SANDBOX + "/tests/src/JavaTest.java",
    "import org.junit.Test;\n"
    "public class JavaTest {\n"
    "  @Test\n"
    "  public void testOne() {}\n"
    "}\n");
  write_file(SANDBOX + "/tests/src/KotlinTest.kt",
    "import org.junit.Test\n"
    "class KotlinTest {\n"
    "  @Test\n"
    "  fun testTwo() {}\n"
    "}\n");

  int count = scan_test_count(SANDBOX);
  assert(count == 2);
  teardown_sandbox();
}

//
// test: Gradle clean with Android project uses --console=plain
//
static void test_gradle_android_clean() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle android clean: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "clean", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Cleaning with Gradle") != string::npos);
  assert(r.output_.find("Clean complete") != string::npos);
  assert(r.output_.find("--console=plain") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle build with Android project uses assembleDebug
//
static void test_gradle_android_build() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle build: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "build", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Android: assembleDebug") != string::npos);
  assert(r.output_.find("assembleDebug") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle test with Android project uses testDebugUnitTest
//
static void test_gradle_android_test() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle test: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "test", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Android: testDebugUnitTest") != string::npos);
  assert(r.output_.find("testDebugUnitTest") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle build with non-Android project uses build
//
static void test_gradle_non_android_build() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'org.jetbrains.kotlin.jvm'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle build: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "build", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Android") == string::npos);
  assert(r.output_.find("build") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle test result parsing from XML
//
static void test_gradle_test_result_parsing() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'java'\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'BUILD SUCCESSFUL'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  // Create fake test results XML
  fs::create_directories(SANDBOX + "/build/test-results/test");
  write_file(SANDBOX + "/build/test-results/test/TEST-com.example.MyTest.xml",
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<testsuite name=\"com.example.MyTest\" tests=\"5\" failures=\"1\" errors=\"0\" skipped=\"0\">\n"
    "  <testcase name=\"testOne\" classname=\"com.example.MyTest\"/>\n"
    "  <testcase name=\"testTwo\" classname=\"com.example.MyTest\">\n"
    "    <failure message=\"assertion failed\"/>\n"
    "  </testcase>\n"
    "  <testcase name=\"testThree\" classname=\"com.example.MyTest\"/>\n"
    "  <testcase name=\"testFour\" classname=\"com.example.MyTest\"/>\n"
    "  <testcase name=\"testFive\" classname=\"com.example.MyTest\"/>\n"
    "</testsuite>\n");

  auto r = build_run(SANDBOX, "test", [](const string &) {});
  // Should report test results from XML
  assert(r.output_.find("Test results: 4/5 passed") != string::npos);
  assert(r.output_.find("1 failed") != string::npos);
  // Should be marked as failed due to test failure
  assert(!r.success_);
  teardown_sandbox();
}

//
// test: Gradle test with all passing XML results
//
static void test_gradle_test_all_passing() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'java'\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'BUILD SUCCESSFUL'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  fs::create_directories(SANDBOX + "/build/test-results/test");
  write_file(SANDBOX + "/build/test-results/test/TEST-com.example.AllPass.xml",
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<testsuite name=\"com.example.AllPass\" tests=\"3\" failures=\"0\" errors=\"0\" skipped=\"0\">\n"
    "  <testcase name=\"testA\" classname=\"com.example.AllPass\"/>\n"
    "  <testcase name=\"testB\" classname=\"com.example.AllPass\"/>\n"
    "  <testcase name=\"testC\" classname=\"com.example.AllPass\"/>\n"
    "</testsuite>\n");

  auto r = build_run(SANDBOX, "test", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Test results: 3/3 passed") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle lint mode with no Kotlin files
//
static void test_gradle_lint_no_kotlin() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'java'\n");
  write_file(SANDBOX + "/src/Main.java", "public class Main {}\n");
  auto r = build_run(SANDBOX, "lint", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("No Kotlin source files found") != string::npos);
  assert(r.output_.find("Lint complete") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle lint mode with Kotlin files (ktlint not installed)
//
static void test_gradle_lint_with_kotlin() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'kotlin'\n");
  fs::create_directories(SANDBOX + "/src");
  write_file(SANDBOX + "/src/Main.kt", "fun main() { println(\"hello\") }\n");
  auto r = build_run(SANDBOX, "lint", [](const string &) {});
  assert(r.success_);
  assert(r.output_.find("Kotlin file(s) to lint") != string::npos);
  assert(r.output_.find("Lint complete") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle lint mode is not available for CMake
//
static void test_cmake_lint_still_works() {
  setup_sandbox();
  write_file(SANDBOX + "/CMakeLists.txt", "cmake_minimum_required(VERSION 3.15)\n");
  // CMake lint should work (may fail due to no compile_commands.json,
  // but should not say "only supported for CMake and Gradle")
  auto r = build_run(SANDBOX, "lint", [](const string &) {});
  // Should NOT get the "only supported" error
  assert(r.output_.find("only supported for CMake and Gradle") == string::npos);
  teardown_sandbox();
}

//
// test: Gradle lint mode is not available for Python
//
static void test_python_lint_not_supported() {
  setup_sandbox();
  write_file(SANDBOX + "/pyproject.toml", "[project]\nname = \"test\"\n");
  auto r = build_run(SANDBOX, "lint", [](const string &) {});
  assert(!r.success_);
  assert(r.output_.find("only supported for CMake and Gradle") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle setup with Android project checks local.properties
//
static void test_gradle_setup_android() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/settings.gradle", "rootProject.name = 'myapp'\n");
  write_file(SANDBOX + "/gradlew", "#!/bin/sh\necho gradlew\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
  // No local.properties - should warn
  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.output_.find("Android project detected") != string::npos);
  assert(r.output_.find("local.properties") != string::npos);
  assert(r.output_.find("settings.gradle found") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle setup with Kotlin source files reports count
//
static void test_gradle_setup_kotlin_count() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle", "apply plugin: 'kotlin'\n");
  write_file(SANDBOX + "/gradlew", "#!/bin/sh\necho gradlew\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
  fs::create_directories(SANDBOX + "/src");
  write_file(SANDBOX + "/src/Main.kt", "fun main() {}\n");
  write_file(SANDBOX + "/src/Util.kt", "fun helper() {}\n");

  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.output_.find("2 Kotlin source file") != string::npos);
  teardown_sandbox();
}

//
// test: Gradle setup with AndroidManifest.xml detection
//
static void test_gradle_setup_android_manifest() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew", "#!/bin/sh\necho gradlew\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
  fs::create_directories(SANDBOX + "/app/src/main");
  write_file(SANDBOX + "/app/src/main/AndroidManifest.xml",
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<manifest package=\"com.example.app\"/>\n");
  write_file(SANDBOX + "/local.properties", "sdk.dir=/opt/android-sdk\n");

  auto r = build_run(SANDBOX, "setup", [](const string &) {});
  assert(r.output_.find("Android project detected") != string::npos);
  assert(r.output_.find("AndroidManifest.xml found") != string::npos);
  assert(r.output_.find("local.properties found") != string::npos);
  teardown_sandbox();
}

//
// test: Android build with APK present triggers adb install
//
static void test_android_adb_deploy() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle build: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  // Create a fake APK at the expected location
  fs::create_directories(SANDBOX + "/app/build/outputs/apk/debug");
  write_file(SANDBOX + "/app/build/outputs/apk/debug/app-debug.apk", "fake-apk");

  auto r = build_run(SANDBOX, "build", [](const string &) {}, "", "adb");
  assert(r.success_);
  assert(r.output_.find("Deploying to device via adb") != string::npos);
  assert(r.output_.find("adb install -r") != string::npos);
  teardown_sandbox();
}

//
// test: Android build without APK skips adb deploy gracefully
//
static void test_android_adb_no_apk() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle build: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  // No APK file present
  auto r = build_run(SANDBOX, "build", [](const string &) {}, "", "adb");
  assert(r.success_);
  assert(r.output_.find("APK not found") != string::npos);
  assert(r.output_.find("Skipping adb deploy") != string::npos);
  teardown_sandbox();
}

//
// test: Android build uses custom adb path from config
//
static void test_android_adb_custom_path() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle build: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  // Create a fake APK
  fs::create_directories(SANDBOX + "/app/build/outputs/apk/debug");
  write_file(SANDBOX + "/app/build/outputs/apk/debug/app-debug.apk", "fake-apk");

  // Use a custom adb path
  auto r = build_run(SANDBOX, "build", [](const string &) {}, "",
                     "/opt/android/platform-tools/adb");
  assert(r.success_);
  assert(r.output_.find("/opt/android/platform-tools/adb install -r") != string::npos);
  teardown_sandbox();
}

//
// test: Android build launches app after successful adb install
//
static void test_android_adb_launch() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle build: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  // Create a fake APK
  fs::create_directories(SANDBOX + "/app/build/outputs/apk/debug");
  write_file(SANDBOX + "/app/build/outputs/apk/debug/app-debug.apk", "fake-apk");

  // Create AndroidManifest.xml with a package name
  fs::create_directories(SANDBOX + "/app/src/main");
  write_file(SANDBOX + "/app/src/main/AndroidManifest.xml",
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
    "    package=\"com.example.myapp\">\n"
    "</manifest>\n");

  // Fake adb that reports Success for install
  write_file(SANDBOX + "/fake_adb",
    "#!/bin/sh\n"
    "if [ \"$1\" = \"install\" ]; then\n"
    "  echo 'Performing Streamed Install\n"
    "  echo 'Success'\n"
    "else\n"
    "  echo 'monkey launched ok'\n"
    "fi\n");
  fs::permissions(SANDBOX + "/fake_adb",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "build", [](const string &) {}, "",
                     SANDBOX + "/fake_adb");
  assert(r.success_);
  assert(r.output_.find("APK installed successfully") != string::npos);
  assert(r.output_.find("Launching com.example.myapp") != string::npos);
  assert(r.output_.find("monkey -p com.example.myapp") != string::npos);
  assert(r.output_.find("android.intent.category.LAUNCHER") != string::npos);
  assert(r.output_.find("App launched") != string::npos);
  teardown_sandbox();
}

//
// test: Android build skips launch when no manifest / package found
//
static void test_android_adb_launch_no_manifest() {
  setup_sandbox();
  write_file(SANDBOX + "/build.gradle",
    "plugins {\n"
    "    id 'com.android.application'\n"
    "}\n");
  write_file(SANDBOX + "/gradlew",
    "#!/bin/sh\necho 'gradle build: $@'\n");
  fs::permissions(SANDBOX + "/gradlew",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  // Create a fake APK but NO AndroidManifest.xml
  fs::create_directories(SANDBOX + "/app/build/outputs/apk/debug");
  write_file(SANDBOX + "/app/build/outputs/apk/debug/app-debug.apk", "fake-apk");

  // Fake adb that reports Success
  write_file(SANDBOX + "/fake_adb",
    "#!/bin/sh\n"
    "echo 'Performing Streamed Install\n"
    "echo 'Success'\n");
  fs::permissions(SANDBOX + "/fake_adb",
                   fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);

  auto r = build_run(SANDBOX, "build", [](const string &) {}, "",
                     SANDBOX + "/fake_adb");
  assert(r.success_);
  assert(r.output_.find("APK installed successfully") != string::npos);
  assert(r.output_.find("could not determine package name") != string::npos);
  assert(r.output_.find("Skipping launch") != string::npos);
  teardown_sandbox();
}

void build_test() {
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
  RUN_TEST(test_maven_detection_subdir);
  RUN_TEST(test_maven_build_subdir);
  RUN_TEST(test_maven_root_pom_priority);
  RUN_TEST(test_no_pom_no_maven);
  RUN_TEST(test_maven_test_dir_root);
  RUN_TEST(test_maven_test_dir_subdir);
  RUN_TEST(test_maven_test_dir_nested_packages);
  // Kotlin/Android tests
  RUN_TEST(test_gradle_settings_detection);
  RUN_TEST(test_gradle_settings_kts_detection);
  RUN_TEST(test_kotlin_file_detection);
  RUN_TEST(test_cmake_over_kotlin);
  RUN_TEST(test_gradle_over_kotlin_fallback);
  RUN_TEST(test_kotlin_test_scan_at_test);
  RUN_TEST(test_kotlin_test_scan_fun_test);
  RUN_TEST(test_mixed_java_kotlin_test_scan);
  RUN_TEST(test_gradle_android_clean);
  RUN_TEST(test_gradle_android_build);
  RUN_TEST(test_gradle_android_test);
  RUN_TEST(test_gradle_non_android_build);
  RUN_TEST(test_gradle_test_result_parsing);
  RUN_TEST(test_gradle_test_all_passing);
  RUN_TEST(test_gradle_lint_no_kotlin);
  RUN_TEST(test_gradle_lint_with_kotlin);
  RUN_TEST(test_cmake_lint_still_works);
  RUN_TEST(test_python_lint_not_supported);
  RUN_TEST(test_gradle_setup_android);
  RUN_TEST(test_gradle_setup_kotlin_count);
  RUN_TEST(test_gradle_setup_android_manifest);
  RUN_TEST(test_android_adb_deploy);
  RUN_TEST(test_android_adb_no_apk);
  RUN_TEST(test_android_adb_custom_path);
  RUN_TEST(test_android_adb_launch);
  RUN_TEST(test_android_adb_launch_no_manifest);
}