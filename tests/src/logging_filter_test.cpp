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

  log_write(LEVEL_DEBUG, "mtp", "matching line");
  log_write(LEVEL_DEBUG, "other", "non-matching line");
  log_write(LEVEL_DEBUG, "untagged", "untagged line");

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

  log_write(LEVEL_DEBUG, "mtp", "categorized line");
  log_write(LEVEL_DEBUG, "untagged", "untagged line");

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

  log_write(LEVEL_DEBUG, "mtp", "mtp line");
  log_write(LEVEL_DEBUG, "net", "net line");
  log_write(LEVEL_DEBUG, "other", "other line");
  log_write(LEVEL_DEBUG, "untagged", "untagged line");

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

  log_write(LEVEL_DEBUG, "mtp", "categorized line");
  log_write(LEVEL_DEBUG, "untagged", "untagged line");

  log_close();

  string content = read_file(path);
  assert(content.find("categorized line") != string::npos);
  assert(content.find("untagged line") != string::npos);

  cleanup(path);
  cout << "test_filter_no_env_var passed" << endl;
}

static void test_named_level_strings() {
  const string path = "/tmp/hali_log_test_named";
  cleanup(path);
  setenv("HALI_LOG_FILE", path.c_str(), 1);
  unsetenv("HALI_LOG_FILTER");

  // "debug" should allow DEBUG messages
  log_close();
  log_open("debug");
  log_write(LEVEL_DEBUG, "cat", "debug_msg");
  log_close();
  string content = read_file(path);
  assert(content.find("debug_msg") != string::npos);

  // "info" should block DEBUG messages
  cleanup(path);
  log_close();
  log_open("info");
  log_write(LEVEL_DEBUG, "cat", "debug_blocked");
  log_write(LEVEL_INFO, "cat", "info_msg");
  log_close();
  content = read_file(path);
  assert(content.find("debug_blocked") == string::npos);
  assert(content.find("info_msg") != string::npos);

  // "warning" should block INFO messages
  cleanup(path);
  log_close();
  log_open("warning");
  log_write(LEVEL_INFO, "cat", "info_blocked");
  log_write(LEVEL_WARNING, "cat", "warn_msg");
  log_close();
  content = read_file(path);
  assert(content.find("info_blocked") == string::npos);
  assert(content.find("warn_msg") != string::npos);

  // "error" should block WARNING messages
  cleanup(path);
  log_close();
  log_open("error");
  log_write(LEVEL_WARNING, "cat", "warn_blocked");
  log_write(LEVEL_ERROR, "cat", "error_msg");
  log_close();
  content = read_file(path);
  assert(content.find("warn_blocked") == string::npos);
  assert(content.find("error_msg") != string::npos);

  cleanup(path);
  cout << "test_named_level_strings passed" << endl;
}

static void test_filter_scoped_to_level() {
  const string path = "/tmp/hali_log_test_scoped";
  cleanup(path);
  setenv("HALI_LOG_FILE", path.c_str(), 1);
  setenv("HALI_LOG_FILTER", "mtp", 1);

  log_close();
  log_open("debug");

  // DEBUG with matching category → logged
  log_write(LEVEL_DEBUG, "mtp", "debug_mtp");
  // DEBUG with non-matching category → filtered
  log_write(LEVEL_DEBUG, "other", "debug_other");
  // INFO with non-matching category → always logged (above configured level)
  log_write(LEVEL_INFO, "other", "info_other");
  // INFO with matching category → always logged
  log_write(LEVEL_INFO, "mtp", "info_mtp");
  // WARNING → always logged
  log_write(LEVEL_WARNING, "other", "warn_other");
  // ERROR → always logged
  log_write(LEVEL_ERROR, "other", "error_other");

  log_close();

  string content = read_file(path);
  assert(content.find("debug_mtp") != string::npos);
  assert(content.find("debug_other") == string::npos);
  assert(content.find("info_other") != string::npos);
  assert(content.find("info_mtp") != string::npos);
  assert(content.find("warn_other") != string::npos);
  assert(content.find("error_other") != string::npos);

  cleanup(path);
  cout << "test_filter_scoped_to_level passed" << endl;
}

void logging_filter_test() {
  cout << "=== Logging Category Filter Tests ===\n\n";
  test_filter_matching_category();
  test_filter_wildcard();
  test_filter_multiple_categories();
  test_filter_no_env_var();
  test_named_level_strings();
  test_filter_scoped_to_level();
  cout << "\nAll logging filter tests passed!\n" << endl;
}