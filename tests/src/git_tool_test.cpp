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
  system(("git -C " + TEST_DIR + " init --quiet 2>&1").c_str());
  system(("git -C " + TEST_DIR + " config user.email test@test.com 2>&1").c_str());
  system(("git -C " + TEST_DIR + " config user.name Test 2>&1").c_str());
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

  result = git_run("merge", "", TEST_DIR);
  assert(result.find("ERROR") != string::npos);

  result = git_run("pull", "", TEST_DIR);
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

  result = git_run("log", "a | b", TEST_DIR);
  assert(result.find("ERROR") != string::npos);

  result = git_run("log", "a > b", TEST_DIR);
  assert(result.find("ERROR") != string::npos);

  cout << "test_git_unsafe_args passed" << endl;
}

static void test_git_commit_with_parens() {
  setup_git_repo();
  // Create a new file to commit
  {
    ofstream f(TEST_DIR + "/world.txt");
    f << "world\n";
  }
  system(("git -C " + TEST_DIR + " add world.txt 2>&1").c_str());
  string result = git_run("commit", "-m \"Fix (bug) in parser\"", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  assert(result.find("1 file changed") != string::npos ||
         result.find("insertion") != string::npos);
  cleanup();
  cout << "test_git_commit_with_parens passed" << endl;
}

static void test_git_commit_with_quotes() {
  setup_git_repo();
  {
    ofstream f(TEST_DIR + "/world.txt");
    f << "world\n";
  }
  system(("git -C " + TEST_DIR + " add world.txt 2>&1").c_str());
  string result = git_run("commit", "-m 'Add \"feature\" support'", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  assert(result.find("1 file changed") != string::npos ||
         result.find("insertion") != string::npos);
  cleanup();
  cout << "test_git_commit_with_quotes passed" << endl;
}

static void test_git_rm_requires_cached() {
  setup_git_repo();
  // Bare rm should be rejected
  string result = git_run("rm", "hello.txt", TEST_DIR);
  assert(result.find("ERROR") != string::npos);
  assert(result.find("--cached") != string::npos);
  cleanup();
  cout << "test_git_rm_requires_cached passed" << endl;
}

static void test_git_rm_cached() {
  setup_git_repo();
  // rm --cached should work
  string result = git_run("rm", "--cached hello.txt", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  assert(result.find("hello.txt") != string::npos);
  // File should still exist on disk
  assert(fs::exists(TEST_DIR + "/hello.txt"));
  cleanup();
  cout << "test_git_rm_cached passed" << endl;
}

static void test_git_config() {
  setup_git_repo();
  // Set a config value
  string result = git_run("config", "user.name NewName", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  // Read it back
  result = git_run("config", "user.name", TEST_DIR);
  assert(result.find("NewName") != string::npos);
  cleanup();
  cout << "test_git_config passed" << endl;
}

static void test_git_stash() {
  setup_git_repo();
  // Modify a file to create a stashable change
  {
    ofstream f(TEST_DIR + "/hello.txt");
    f << "modified\n";
  }
  // Stash the change
  string result = git_run("stash", "push -m \"wip\"", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  assert(result.find("Saved working directory") != string::npos);
  // List stashes
  result = git_run("stash", "list", TEST_DIR);
  assert(result.find("wip") != string::npos);
  // Pop the stash
  result = git_run("stash", "pop", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  cleanup();
  cout << "test_git_stash passed" << endl;
}

static void test_git_tag() {
  setup_git_repo();
  // Create a tag
  string result = git_run("tag", "v1.0", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  // List tags
  result = git_run("tag", "", TEST_DIR);
  assert(result.find("v1.0") != string::npos);
  // Delete the tag
  result = git_run("tag", "-d v1.0", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  cleanup();
  cout << "test_git_tag passed" << endl;
}

static void test_git_submodule() {
  setup_git_repo();
  // submodule init on a repo with no submodules should succeed (no-op)
  string result = git_run("submodule", "init", TEST_DIR);
  assert(result.find("ERROR") == string::npos);
  cleanup();
  cout << "test_git_submodule passed" << endl;
}

void git_tool_test() {
  test_git_status();
  test_git_log();
  test_git_whitelist_reject();
  test_git_unsafe_args();
  test_git_commit_with_parens();
  test_git_commit_with_quotes();
  test_git_rm_requires_cached();
  test_git_rm_cached();
  test_git_config();
  test_git_stash();
  test_git_tag();
  test_git_submodule();
  cout << "\nAll git_tool tests passed!\n" << endl;
}