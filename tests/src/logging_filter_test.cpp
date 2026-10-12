// This file is part of AudioCore
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <iostream>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <filesystem>

#include "logging.h"

using namespace std;

static string read_file(const string& path) {
  ifstream f(path);
  string content((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
  return content;
}

static void cleanup(const string& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

static void test_filter_matching_category() {
  const string path = "/tmp/hali_log_test_match";
  cleanup(path);
  setenv("HALI_LOG_FILE", path.c_str(), 1);
  setenv("HALI_LOG_FILTER", "mtp", 1);

  log_close();
  log_open("0");

  log_write_cat(LEVEL_DEBUG, "mtp", "matching line");
  log_write_cat(LEVEL_DEBUG, "other", "non-matching line");
  log_write(LEVEL_DEBUG, "untagged line");

  log_close();

  string content = read_file(path);
  assert(content.find("matching line") != string::npos);
  assert(content.find("non-matching line") == string::npos);
  assert(content.find("untagged line") == string::npos);

  cleanup(path);
  cout << "test_filter_matching_category passed" << endl;
}

static void test_filter_wildcard() {
  const string path = "/tmp/hali_log_test_wildcard";
  cleanup(path);
  setenv("HALI_LOG_FILE", path.c_str(), 1);
  setenv("HALI_LOG_FILTER", "*", 1);

  log_close();
  log_open("0");

  log_write_cat(LEVEL_DEBUG, "mtp", "categorized line");
  log_write(LEVEL_DEBUG, "untagged line");

  log_close();

  string content = read_file(path);
  assert(content.find("categorized line") != string::npos);
  assert(content.find("untagged line") != string::npos);

  cleanup(path);
  cout << "test_filter_wildcard passed" << endl;
}

static void test_filter_multiple_categories() {
  const string path = "/tmp/hali_log_test_multi";
  cleanup(path);
  setenv("HALI_LOG_FILE", path.c_str(), 1);
  setenv("HALI_LOG_FILTER", "mtp,net", 1);

  log_close();
  log_open("0");

  log_write_cat(LEVEL_DEBUG, "mtp", "mtp line");
  log_write_cat(LEVEL_DEBUG, "net", "net line");
  log_write_cat(LEVEL_DEBUG, "other", "other line");
  log_write(LEVEL_DEBUG, "untagged line");

  log_close();

  string content = read_file(path);
  assert(content.find("mtp line") != string::npos);
  assert(content.find("net line") != string::npos);
  assert(content.find("other line") == string::npos);
  assert(content.find("untagged line") == string::npos);

  cleanup(path);
  cout << "test_filter_multiple_categories passed" << endl;
}

static void test_filter_no_env_var() {
  const string path = "/tmp/hali_log_test_noenv";
  cleanup(path);
  setenv("HALI_LOG_FILE", path.c_str(), 1);
  unsetenv("HALI_LOG_FILTER");

  log_close();
  log_open("0");

  log_write_cat(LEVEL_DEBUG, "mtp", "categorized line");
  log_write(LEVEL_DEBUG, "untagged line");

  log_close();

  string content = read_file(path);
  assert(content.find("categorized line") != string::npos);
  assert(content.find("untagged line") != string::npos);

  cleanup(path);
  cout << "test_filter_no_env_var passed" << endl;
}

void logging_filter_test() {
  cout << "=== Logging Category Filter Tests ===\n\n";
  test_filter_matching_category();
  test_filter_wildcard();
  test_filter_multiple_categories();
  test_filter_no_env_var();
  cout << "\nAll logging filter tests passed!\n" << endl;
}