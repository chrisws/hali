// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "pom.h"

namespace fs = std::filesystem;

static const std::string kTestDir = "/tmp/hali_pom_test";

static void write_test_pom(const std::string &path) {
  std::ofstream f(path);
  f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    << "<project xmlns=\"http://maven.apache.org/POM/4.0.0\">\n"
    << "  <modelVersion>4.0.0</modelVersion>\n"
    << "  <groupId>com.example</groupId>\n"
    << "  <artifactId>test-app</artifactId>\n"
    << "  <version>1.0-SNAPSHOT</version>\n"
    << "  <packaging>jar</packaging>\n"
    << "  <properties>\n"
    << "    <maven.compiler.source>17</maven.compiler.source>\n"
    << "    <maven.compiler.target>17</maven.compiler.target>\n"
    << "  </properties>\n"
    << "</project>\n";
}

static std::string read_file(const std::string &path) {
  std::ifstream f(path);
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  return content;
}

static void test_pom_show() {
  fs::create_directories(kTestDir);
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "show", [](const std::string &) {});
  assert(result.success_);
  assert(result.output_.find("com.example") != std::string::npos);
  assert(result.output_.find("test-app") != std::string::npos);
  assert(result.output_.find("1.0-SNAPSHOT") != std::string::npos);
  assert(result.output_.find("maven.compiler.source") != std::string::npos);
  printf("  test_pom_show: OK\n");
}

static void test_pom_add_dep() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "add-dep com.google.guava:guava:33.0.0-jre:compile",
                        [](const std::string &) {});
  assert(result.success_);
  assert(result.output_.find("Added dependency") != std::string::npos);

  std::string content = read_file(pom);
  assert(content.find("<groupId>com.google.guava</groupId>") != std::string::npos);
  assert(content.find("<artifactId>guava</artifactId>") != std::string::npos);
  assert(content.find("<version>33.0.0-jre</version>") != std::string::npos);
  assert(content.find("<scope>compile</scope>") != std::string::npos);
  printf("  test_pom_add_dep: OK\n");
}

static void test_pom_add_dep_duplicate() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  pom_run(pom, "add-dep com.google.guava:guava:33.0.0-jre", [](const std::string &) {});
  auto result = pom_run(pom, "add-dep com.google.guava:guava:33.0.0-jre",
                        [](const std::string &) {});
  assert(!result.success_);
  assert(result.output_.find("already exists") != std::string::npos);
  printf("  test_pom_add_dep_duplicate: OK\n");
}

static void test_pom_remove_dep() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  pom_run(pom, "add-dep org.junit.jupiter:junit-jupiter:5.10.0:test",
          [](const std::string &) {});
  auto result = pom_run(pom, "remove-dep org.junit.jupiter:junit-jupiter",
                        [](const std::string &) {});
  assert(result.success_);
  assert(result.output_.find("Removed dependency") != std::string::npos);

  std::string content = read_file(pom);
  assert(content.find("junit-jupiter") == std::string::npos);
  printf("  test_pom_remove_dep: OK\n");
}

static void test_pom_remove_dep_not_found() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "remove-dep com.nonexistent:missing:1.0",
                        [](const std::string &) {});
  assert(!result.success_);
  assert(result.output_.find("not found") != std::string::npos);
  printf("  test_pom_remove_dep_not_found: OK\n");
}

static void test_pom_set_property() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "set-property maven.compiler.source 21",
                        [](const std::string &) {});
  assert(result.success_);
  assert(result.output_.find("Set property") != std::string::npos);

  std::string content = read_file(pom);
  assert(content.find("<maven.compiler.source>21</maven.compiler.source>") != std::string::npos);
  printf("  test_pom_set_property: OK\n");
}

static void test_pom_set_property_new() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "set-property spring.version 6.1.0",
                        [](const std::string &) {});
  assert(result.success_);

  std::string content = read_file(pom);
  assert(content.find("<spring.version>6.1.0</spring.version>") != std::string::npos);
  printf("  test_pom_set_property_new: OK\n");
}

static void test_pom_set_version() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "set-version 2.0.0", [](const std::string &) {});
  assert(result.success_);

  std::string content = read_file(pom);
  assert(content.find("<version>2.0.0</version>") != std::string::npos);
  printf("  test_pom_set_version: OK\n");
}

static void test_pom_set_group_id() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "set-group-id com.neworg", [](const std::string &) {});
  assert(result.success_);

  std::string content = read_file(pom);
  assert(content.find("<groupId>com.neworg</groupId>") != std::string::npos);
  printf("  test_pom_set_group_id: OK\n");
}

static void test_pom_set_artifact_id() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "set-artifact-id new-app", [](const std::string &) {});
  assert(result.success_);

  std::string content = read_file(pom);
  assert(content.find("<artifactId>new-app</artifactId>") != std::string::npos);
  printf("  test_pom_set_artifact_id: OK\n");
}

static void test_pom_add_plugin() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "add-plugin org.apache.maven.plugins:maven-compiler-plugin:3.13.0",
                        [](const std::string &) {});
  assert(result.success_);
  assert(result.output_.find("Added plugin") != std::string::npos);

  std::string content = read_file(pom);
  assert(content.find("<plugins>") != std::string::npos);
  assert(content.find("maven-compiler-plugin") != std::string::npos);
  printf("  test_pom_add_plugin: OK\n");
}

static void test_pom_unknown_op() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "bogus-op", [](const std::string &) {});
  assert(!result.success_);
  assert(result.output_.find("unknown pom operation") != std::string::npos);
  printf("  test_pom_unknown_op: OK\n");
}

static void test_pom_add_dep_no_scope() {
  std::string pom = kTestDir + "/pom.xml";
  write_test_pom(pom);

  auto result = pom_run(pom, "add-dep org.slf4j:slf4j-api:2.0.9",
                        [](const std::string &) {});
  assert(result.success_);

  std::string content = read_file(pom);
  assert(content.find("<scope>compile</scope>") != std::string::npos);
  printf("  test_pom_add_dep_no_scope: OK\n");
}

void run_pom_tests() {
  printf("Running pom tests...\n");
  test_pom_show();
  test_pom_add_dep();
  test_pom_add_dep_duplicate();
  test_pom_remove_dep();
  test_pom_remove_dep_not_found();
  test_pom_set_property();
  test_pom_set_property_new();
  test_pom_set_version();
  test_pom_set_group_id();
  test_pom_set_artifact_id();
  test_pom_add_plugin();
  test_pom_unknown_op();
  test_pom_add_dep_no_scope();
  printf("All pom tests passed.\n");

  // Cleanup
  fs::remove_all(kTestDir);
}