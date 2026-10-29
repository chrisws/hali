// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef USAGE_LOG_H
#define USAGE_LOG_H

#include <string>
#include <vector>

struct TurnMetrics {
  int        turn_number       = 0;
  double     llm_latency_ms    = 0.0;
  int        completion_tokens = 0;
  int        tool_call_count   = 0;
  int        tool_error_count  = 0;
  double     kv_usage_pct      = 0.0;
  int        files_read        = 0;
  int        files_written     = 0;
};

struct UsageLog {
  std::vector<TurnMetrics> turns;
  double total_llm_ms     = 0.0;
  int    total_tool_calls = 0;
  int    total_tool_errors = 0;
  double peak_kv_pct      = 0.0;
  int    total_tokens     = 0;

  // Cumulative values persist across sessions (loaded from disk at startup)
  int    cumulative_tokens      = 0;
  int    cumulative_tool_calls  = 0;
  int    cumulative_tool_errors = 0;
  int    cumulative_turns       = 0;
  double cumulative_llm_ms      = 0.0;

  void record_turn(const TurnMetrics &m);
  std::string summary() const;
  bool kv_trending_high() const;
  std::string to_json() const;
  void dump(const std::string &path) const;
  void clear();
  int session_tokens() const;
  void load_cumulative(const std::string &path);
  std::string badge_svg() const;
};

#endif