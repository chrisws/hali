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

#include "usage_log.h"

using namespace std;

static void test_record_turn() {
  UsageLog log;
  TurnMetrics m;
  m.turn_number = 1;
  m.llm_latency_ms = 1500.0;
  m.completion_tokens = 256;
  m.tool_call_count = 3;
  m.tool_error_count = 1;
  m.kv_usage_pct = 45.0;
  m.files_read = 2;
  m.files_written = 1;

  log.record_turn(m);

  assert(log.turns.size() == 1);
  assert(log.total_llm_ms == 1500.0);
  assert(log.total_tool_calls == 3);
  assert(log.total_tool_errors == 1);
  assert(log.peak_kv_pct == 45.0);
  cout << "test_record_turn passed" << endl;
}

static void test_summary() {
  UsageLog log;
  TurnMetrics m;
  m.turn_number = 1;
  m.llm_latency_ms = 1200.0;
  m.completion_tokens = 128;
  m.tool_call_count = 2;
  m.tool_error_count = 0;
  m.kv_usage_pct = 30.0;
  m.files_read = 1;
  m.files_written = 0;

  log.record_turn(m);

  string s = log.summary();
  assert(s.find("turn 1") != string::npos);
  assert(s.find("1200") != string::npos);
  assert(s.find("128") != string::npos);
  assert(s.find("2 tool calls") != string::npos);
  assert(s.find("KV 30") != string::npos);
  cout << "test_summary passed" << endl;
}

static void test_summary_empty() {
  UsageLog log;
  string s = log.summary();
  assert(s.find("no turns") != string::npos);
  cout << "test_summary_empty passed" << endl;
}

static void test_kv_trending_high() {
  UsageLog log;

  // Fewer than 3 turns — should be false
  TurnMetrics m1; m1.kv_usage_pct = 90.0;
  TurnMetrics m2; m2.kv_usage_pct = 95.0;
  log.record_turn(m1);
  log.record_turn(m2);
  assert(!log.kv_trending_high());

  // Third turn also high — should be true
  TurnMetrics m3; m3.kv_usage_pct = 85.0;
  log.record_turn(m3);
  assert(log.kv_trending_high());

  // Reset and add a low turn — should be false
  log.clear();
  TurnMetrics a; a.kv_usage_pct = 90.0;
  TurnMetrics b; b.kv_usage_pct = 95.0;
  TurnMetrics c; c.kv_usage_pct = 50.0;
  log.record_turn(a);
  log.record_turn(b);
  log.record_turn(c);
  assert(!log.kv_trending_high());

  cout << "test_kv_trending_high passed" << endl;
}

static void test_peak_kv() {
  UsageLog log;
  TurnMetrics m1; m1.kv_usage_pct = 30.0;
  TurnMetrics m2; m2.kv_usage_pct = 75.0;
  TurnMetrics m3; m3.kv_usage_pct = 60.0;
  log.record_turn(m1);
  log.record_turn(m2);
  log.record_turn(m3);
  assert(log.peak_kv_pct == 75.0);
  cout << "test_peak_kv passed" << endl;
}

static void test_to_json() {
  UsageLog log;
  TurnMetrics m;
  m.turn_number = 1;
  m.llm_latency_ms = 1000.0;
  m.completion_tokens = 64;
  m.tool_call_count = 1;
  m.tool_error_count = 0;
  m.kv_usage_pct = 20.0;
  m.files_read = 1;
  m.files_written = 0;
  log.record_turn(m);

  string json = log.to_json();
  assert(json.find("\"total_turns\": 1") != string::npos);
  assert(json.find("\"turn\": 1") != string::npos);
  assert(json.find("\"tokens\": 64") != string::npos);
  assert(json.find("\"kv_pct\": 20") != string::npos);
  cout << "test_to_json passed" << endl;
}

static void test_dump() {
  const string path = "/tmp/hali_usage_log_test.json";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  UsageLog log;
  TurnMetrics m;
  m.turn_number = 1;
  m.llm_latency_ms = 500.0;
  m.completion_tokens = 32;
  m.tool_call_count = 0;
  m.tool_error_count = 0;
  m.kv_usage_pct = 10.0;
  m.files_read = 0;
  m.files_written = 0;
  log.record_turn(m);
  log.dump(path);

  assert(std::filesystem::exists(path));
  std::ifstream f(path);
  string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  assert(content.find("\"total_turns\": 1") != string::npos);

  std::filesystem::remove(path, ec);
  cout << "test_dump passed" << endl;
}

static void test_clear() {
  UsageLog log;
  TurnMetrics m;
  m.turn_number = 1;
  m.kv_usage_pct = 50.0;
  log.record_turn(m);
  assert(log.turns.size() == 1);

  log.clear();
  assert(log.turns.empty());
  assert(log.total_llm_ms == 0.0);
  assert(log.total_tool_calls == 0);
  assert(log.total_tool_errors == 0);
  assert(log.peak_kv_pct == 0.0);
  cout << "test_clear passed" << endl;
}

void usage_log_test() {
  cout << "=== Usage Log Tests ===\n\n";
  test_record_turn();
  test_summary();
  test_summary_empty();
  test_kv_trending_high();
  test_peak_kv();
  test_to_json();
  test_dump();
  test_clear();
  cout << "\nAll usage log tests passed!\n" << endl;
}