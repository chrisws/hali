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
#include <iostream>
#include <string>

#include "git_tool.h"

using namespace std;
namespace fs = std::filesystem;

static const string TEST_DIR = "/tmp/hali_test_git";

static void setup_git_repo() {
  fs::remove_all(TEST_DIR);
  fs::create_directories(TEST_DIR);
  // Initialize a git repo
  system(("git -C " + TEST_DIR + " init --quiet 2>&1").c_str());
  system(("git -C " + TEST_DIR + " config user.email test@test.com 2>&1").c_str());
  system(("git -C " + TEST_DIR + " config user.name Test 2>&1").c_str());
  // Create a file and commit
  {
    ofstream f(TEST_DIR + "/hello.txt");
    f << "hello\n";
  }
  system(("git -C " + TEST_DIR + " add hello.txt 2>&1").c_str());
  system(("git -C " + TEST_DIR + " commit --quiet -m init 2>&1").c_str());
}

static void cleanup() {
  fs::remove_all(TEST_DIR);
}

static void test_git_status() {
  setup_git_repo();
  string result = git_run("status", "", TEST_DIR);
  assert(result.find("nothing to commit") != string::npos ||
         result.find("working tree clean") != string::npos);
  cleanup();
  cout << "test_git_status passed" << endl;
}

static void test_git_log() {
  setup_git_repo();
  string result = git_run("log", "--oneline", TEST_DIR);
  assert(result.find("init") != string::npos);
  cleanup();
  cout << "test_git_log passed" << endl;
}

static void test_git_whitelist_reject() {
  string result = git_run("push", "", TEST_DIR);
  assert(result.find("ERROR") != string::npos);
  assert(result.find("not permitted") != string::npos);

  result = git_run("rebase", "", TEST_DIR);
  assert(result.find("ERROR") != string::npos);

  result = git_run("", "", TEST_DIR);
  assert(result.find("ERROR") != string::npos);

  cout << "test_git_whitelist_reject passed" << endl;
}

static void test_git_unsafe_args() {
  string result = git_run("log", "; rm -rf /", TEST_DIR);
  assert(result.find("ERROR") != string::npos);
  assert(result.find("unsafe") != string::npos);

  result = git_run("log", "$(evil)", TEST_DIR);
  assert(result.find("ERROR") != string::npos);

  cout << "test_git_unsafe_args passed" << endl;
}

void git_tool_test() {
  test_git_status();
  test_git_log();
  test_git_whitelist_reject();
  test_git_unsafe_args();
  cout << "\nAll git_tool tests passed!\n" << endl;
}