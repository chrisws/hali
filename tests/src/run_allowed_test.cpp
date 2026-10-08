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

#include "run_allowed.h"

using namespace std;
namespace fs = std::filesystem;

static const string TEST_DIR = "/tmp/hali_test_run_allowed";
static const string TEST_FILE = TEST_DIR + "/user-approved.txt";

static void setup() {
  fs::create_directories(TEST_DIR);
}

static void cleanup() {
  fs::remove_all(TEST_DIR);
}

//
// Test: basic add / contains / remove / size / empty
//
static void test_basic_ops() {
  RunAllowed ra;
  assert(ra.empty());
  assert(ra.size() == 0);

  assert(ra.add("gradle") == true);
  assert(ra.add("python") == true);
  assert(ra.add("npm") == true);
  assert(ra.size() == 3);
  assert(!ra.empty());

  assert(ra.contains("gradle"));
  assert(ra.contains("python"));
  assert(ra.contains("npm"));
  assert(!ra.contains("bash"));

  // Duplicate add returns false
  assert(ra.add("gradle") == false);
  assert(ra.size() == 3);

  // Empty name returns false
  assert(ra.add("") == false);

  // Remove
  assert(ra.remove("python") == true);
  assert(!ra.contains("python"));
  assert(ra.size() == 2);

  // Remove non-existent
  assert(ra.remove("bash") == false);

  cout << "test_basic_ops passed" << endl;
}

//
// Test: save and load round-trip
//
static void test_save_load() {
  setup();

  RunAllowed ra;
  ra.add("gradle");
  ra.add("python");
  ra.save(TEST_FILE);

  // Verify file exists
  assert(fs::exists(TEST_FILE));

  // Load into a new instance
  RunAllowed ra2;
  ra2.load(TEST_FILE);
  assert(ra2.size() == 2);
  assert(ra2.contains("gradle"));
  assert(ra2.contains("python"));

  cleanup();
  cout << "test_save_load passed" << endl;
}

//
// Test: load from non-existent file (should silently succeed)
//
static void test_load_missing_file() {
  RunAllowed ra;
  ra.load("/tmp/hali_nonexistent_dir_xyz/file.txt");
  assert(ra.empty());

  cout << "test_load_missing_file passed" << endl;
}

//
// Test: TTL expiry — entries older than 30 days are discarded
//
static void test_ttl_expiry() {
  setup();

  // Write a file with one fresh entry and one old entry
  // Format: name\tYYYYMMDD
  // Use a date 60 days in the past for the expired entry
  // and today for the fresh entry
  {
    time_t t = time(nullptr);
    tm *lt = localtime(&t);
    int today = (lt->tm_year + 1900) * 10000 + (lt->tm_mon + 1) * 100 + lt->tm_mday;

    // 60 days ago: subtract 60*100 from YYYYMMDD (approximate, good enough for test)
    // Actually, let's just use a fixed old date
    string old_date = "20200101";  // definitely > 30 days ago
    string fresh_date = to_string(today);

    ofstream f(TEST_FILE);
    f << "oldcommand\t" << old_date << "\n";
    f << "newcommand\t" << fresh_date << "\n";
    f.close();
  }

  RunAllowed ra;
  ra.load(TEST_FILE, 30);  // 30-day TTL
  assert(ra.size() == 1);
  assert(ra.contains("newcommand"));
  assert(!ra.contains("oldcommand"));

  cleanup();
  cout << "test_ttl_expiry passed" << endl;
}

//
// Test: entries without timestamps are always kept
//
static void test_no_timestamp() {
  setup();

  {
    ofstream f(TEST_FILE);
    f << "gradle\n";
    f << "python\n";
    f.close();
  }

  RunAllowed ra;
  ra.load(TEST_FILE, 30);
  assert(ra.size() == 2);
  assert(ra.contains("gradle"));
  assert(ra.contains("python"));

  cleanup();
  cout << "test_no_timestamp passed" << endl;
}

//
// Test: summary formatting
//
static void test_summary() {
  RunAllowed ra;
  assert(ra.summary().empty());

  ra.add("gradle");
  string s = ra.summary();
  assert(s.find("1 previously approved RUN command:") != string::npos);
  assert(s.find("  - gradle") != string::npos);

  ra.add("python");
  s = ra.summary();
  assert(s.find("2 previously approved RUN commands:") != string::npos);
  assert(s.find("  - gradle") != string::npos);
  assert(s.find("  - python") != string::npos);

  cout << "test_summary passed" << endl;
}

//
// Test: merge_into — adds only new entries to target
//
static void test_merge_into() {
  RunAllowed ra;
  ra.add("gradle");
  ra.add("python");

  vector<string> target;
  target.push_back("gradle");  // already present
  target.push_back("g++");

  ra.merge_into(target);

  assert(target.size() == 3);
  assert(find(target.begin(), target.end(), "gradle") != target.end());
  assert(find(target.begin(), target.end(), "python") != target.end());
  assert(find(target.begin(), target.end(), "g++") != target.end());

  // No duplicates
  assert(count(target.begin(), target.end(), "gradle") == 1);

  cout << "test_merge_into passed" << endl;
}

//
// Test: save writes timestamps in YYYYMMDD format
//
static void test_save_format() {
  setup();

  RunAllowed ra;
  ra.add("testcmd");
  ra.save(TEST_FILE);

  // Read the file and verify format: "testcmd\tYYYYMMDD"
  ifstream f(TEST_FILE);
  string line;
  getline(f, line);
  f.close();

  assert(line.find("testcmd\t") == 0);
  // Timestamp should be 8 digits
  auto tab = line.find('\t');
  string ts = line.substr(tab + 1);
  assert(ts.size() == 8);
  // Should be a valid year (20xx)
  assert(ts.substr(0, 2) == "20");

  cleanup();
  cout << "test_save_format passed" << endl;
}

void run_allowed_test() {
  test_basic_ops();
  test_save_load();
  test_load_missing_file();
  test_ttl_expiry();
  test_no_timestamp();
  test_summary();
  test_merge_into();
  test_save_format();
  cout << "\nAll RunAllowed tests passed!\n" << endl;
}
