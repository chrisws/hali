// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <fstream>
#include <sstream>
#include <algorithm>

#include "usage_log.h"
#include "logging.h"

void UsageLog::record_turn(const TurnMetrics &m) {
  turns.push_back(m);
  total_llm_ms    += m.llm_latency_ms;
  total_tool_calls += m.tool_call_count;
  total_tool_errors += m.tool_error_count;
  if (m.kv_usage_pct > peak_kv_pct) {
    peak_kv_pct = m.kv_usage_pct;
  }
}

std::string UsageLog::summary() const {
  if (turns.empty()) {
    return "usage: no turns recorded";
  }
  const auto &last = turns.back();
  std::ostringstream oss;
  oss << "usage: turn " << last.turn_number
      << "  " << last.llm_latency_ms << " ms"
      << "  " << last.completion_tokens << " tokens"
      << "  " << last.tool_call_count << " tool calls"
      << "  " << last.tool_error_count << " errors"
      << "  KV " << last.kv_usage_pct << "%"
      << "  files R" << last.files_read << "/W" << last.files_written;
  return oss.str();
}

bool UsageLog::kv_trending_high() const {
  if (turns.size() < 3) {
    return false;
  }
  auto it = turns.end() - 3;
  return std::all_of(it, turns.end(),
    [](const TurnMetrics &m) { return m.kv_usage_pct > 80.0; });
}

std::string UsageLog::to_json() const {
  std::ostringstream oss;
  oss << "{\n";
  oss << "  \"total_turns\": " << turns.size() << ",\n";
  oss << "  \"total_llm_ms\": " << total_llm_ms << ",\n";
  oss << "  \"total_tool_calls\": " << total_tool_calls << ",\n";
  oss << "  \"total_tool_errors\": " << total_tool_errors << ",\n";
  oss << "  \"peak_kv_pct\": " << peak_kv_pct << ",\n";
  oss << "  \"turns\": [\n";
  for (size_t i = 0; i < turns.size(); ++i) {
    const auto &t = turns[i];
    oss << "    {\"turn\": " << t.turn_number
        << ", \"llm_ms\": " << t.llm_latency_ms
        << ", \"tokens\": " << t.completion_tokens
        << ", \"tool_calls\": " << t.tool_call_count
        << ", \"tool_errors\": " << t.tool_error_count
        << ", \"kv_pct\": " << t.kv_usage_pct
        << ", \"files_read\": " << t.files_read
        << ", \"files_written\": " << t.files_written << "}";
    if (i + 1 < turns.size()) {
      oss << ",";
    }
    oss << "\n";
  }
  oss << "  ]\n";
  oss << "}\n";
  return oss.str();
}

void UsageLog::dump(const std::string &path) const {
  std::ofstream f(path);
  if (!f) {
    log_write(LEVEL_WARNING, "usage_log", "cannot write %s", path.c_str());
    return;
  }
  f << to_json();
  log_write(LEVEL_INFO, "usage_log", "wrote %zu turns to %s", turns.size(), path.c_str());
}

void UsageLog::clear() {
  turns.clear();
  total_llm_ms = 0.0;
  total_tool_calls = 0;
  total_tool_errors = 0;
  peak_kv_pct = 0.0;
}