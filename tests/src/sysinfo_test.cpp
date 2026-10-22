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

#include "sysinfo.h"

using namespace std;
namespace fs = std::filesystem;

static const string TEST_DIR = "/tmp/hali_test_sysinfo";

static void setup() {
  fs::create_directories(TEST_DIR);
}

static void cleanup() {
  fs::remove_all(TEST_DIR);
}

static void test_stat_file() {
  setup();
  string path = TEST_DIR + "/test.txt";
  {
    ofstream f(path);
    f << "hello world\nfoo bar baz\n";
  }

  string result = stat_file(path);
  assert(result.find("path:") != string::npos);
  assert(result.find("regular file") != string::npos);
  assert(result.find("permissions:") != string::npos);
  assert(result.find("lines:") != string::npos);
  assert(result.find("2") != string::npos);  // 2 lines
  assert(result.find("words:") != string::npos);
  assert(result.find("5") != string::npos);  // 5 words

  cleanup();
  cout << "test_stat_file passed" << endl;
}

static void test_stat_missing() {
  string result = stat_file("/tmp/hali_nonexistent_xyz");
  assert(result.find("ERROR") != string::npos);
  cout << "test_stat_missing passed" << endl;
}

static void test_stat_directory() {
  setup();
  string result = stat_file(TEST_DIR);
  assert(result.find("directory") != string::npos);
  cleanup();
  cout << "test_stat_directory passed" << endl;
}

static void test_sysinfo() {
  string result = sysinfo_text();
  assert(result.find("os:") != string::npos);
  assert(result.find("uid:gid:") != string::npos);
  assert(result.find("user:") != string::npos);
  assert(result.find("cwd:") != string::npos);
  cout << "test_sysinfo passed" << endl;
}

static void test_hex_dump() {
  setup();
  string path = TEST_DIR + "/test.bin";
  {
    ofstream f(path, ios::binary);
    // Write bytes: 0x41 0x42 0x43 0x00 0xFF
    const char data[] = {'A', 'B', 'C', '\0', '\xff'};
    f.write(data, 5);
  }

  string result = hex_dump(path, "");
  assert(result.find("41") != string::npos);
  assert(result.find("42") != string::npos);
  assert(result.find("43") != string::npos);
  

  cleanup();
  cout << "test_hex_dump passed" << endl;
}

static void test_hex_dump_offset() {
  setup();
  string path = TEST_DIR + "/test2.bin";
  {
    ofstream f(path, ios::binary);
    const char data[] = {'X', 'Y', 'Z'};
    f.write(data, 3);
  }

  string result = hex_dump(path, "1 2");  // offset=1, length=2
  assert(result.find("59") != string::npos);  // 'Y'
  assert(result.find("5A") != string::npos);  // 'Z'
  assert(result.find("58") == string::npos);  // 'X' should not appear

  cleanup();
  cout << "test_hex_dump_offset passed" << endl;
}

static void test_hex_missing() {
  string result = hex_dump("/tmp/hali_nonexistent_xyz", "");
  assert(result.find("ERROR") != string::npos);
  cout << "test_hex_missing passed" << endl;
}

void sysinfo_test() {
  test_stat_file();
  test_stat_missing();
  test_stat_directory();
  test_sysinfo();
  test_hex_dump();
  test_hex_dump_offset();
  test_hex_missing();
  cout << "\nAll sysinfo tests passed!\n" << endl;
}