// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "usage_log.h"
#include "logging.h"
#include "util/json.h"

void UsageLog::record_turn(const TurnMetrics &m) {
  turns.push_back(m);
  total_llm_ms    += m.llm_latency_ms;
  total_tool_calls += m.tool_call_count;
  total_tool_errors += m.tool_error_count;
  total_tokens    += m.completion_tokens;
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
  oss << "  \"total_tokens\": " << total_tokens << ",\n";
  oss << "  \"cumulative_tokens\": " << cumulative_tokens << ",\n";
  oss << "  \"cumulative_tool_calls\": " << cumulative_tool_calls << ",\n";
  oss << "  \"cumulative_tool_errors\": " << cumulative_tool_errors << ",\n";
  oss << "  \"cumulative_turns\": " << cumulative_turns << ",\n";
  oss << "  \"cumulative_llm_ms\": " << cumulative_llm_ms << ",\n";
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
  // Ensure parent directory exists
  auto slash = path.rfind('/');
  if (slash != std::string::npos) {
    std::string dir = path.substr(0, slash);
    std::filesystem::create_directories(dir);
  }
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
  total_tokens = 0;
}

int UsageLog::session_tokens() const {
  return total_tokens;
}

void UsageLog::load_cumulative(const std::string &path) {
  std::ifstream f(path);
  if (!f) {
    return;
  }
  std::ostringstream oss;
  oss << f.rdbuf();
  std::string content = oss.str();

  json::JsonDoc doc = json::JsonDoc::parse(content);
  if (!doc.is_valid()) {
    log_write(LEVEL_WARNING, "usage_log", "failed to parse %s", path.c_str());
    return;
  }

  json::JsonValue root = doc.get_root();
  root.get_int("cumulative_tokens", cumulative_tokens);
  root.get_int("cumulative_tool_calls", cumulative_tool_calls);
  root.get_int("cumulative_tool_errors", cumulative_tool_errors);
  root.get_int("cumulative_turns", cumulative_turns);
  float llm_ms = 0.0f;
  root.get_float("cumulative_llm_ms", llm_ms);
  cumulative_llm_ms = llm_ms;

  log_write(LEVEL_INFO, "usage_log", "loaded cumulative: %d tokens, %d turns, %d tool calls, %d errors, %.0f ms",
            cumulative_tokens, cumulative_turns, cumulative_tool_calls, cumulative_tool_errors, cumulative_llm_ms);
}

std::string UsageLog::badge_svg() const {
  int tokens = cumulative_tokens > 0 ? cumulative_tokens : total_tokens;
  std::string token_str;
  if (tokens >= 1000000) {
    token_str = std::to_string(tokens / 1000000) + "M";
  } else if (tokens >= 1000) {
    token_str = std::to_string(tokens / 1000) + "k";
  } else {
    token_str = std::to_string(tokens);
  }
  token_str += " tokens";

  // GitHub-style badge: label (left) + value (right)
  constexpr int label_w = 120;
  constexpr int value_w = 90;
  constexpr int total_w = label_w + value_w;
  const int h = 20;
  const int r = 3;

  std::ostringstream svg;
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << total_w
      << "\" height=\"" << h << "\" viewBox=\"0 0 " << total_w << " " << h
      << "\" role=\"img\" aria-label=\"made with hali: " << token_str << "\">\n";
  svg << "  <title>made with hali: " << token_str << "</title>\n";
  // Label background (dark)
  svg << "  <rect width=\"" << label_w << "\" height=\"" << h
      << "\" rx=\"" << r << "\" fill=\"#555\"/>\n";
  // Value background (green)
  svg << "  <rect x=\"" << label_w << "\" width=\"" << value_w
      << "\" height=\"" << h << "\" rx=\"" << r << "\" fill=\"#4c1\"/>\n";
  // Mask to round only outer corners
  svg << "  <rect x=\"" << label_w << "\" width=\"" << (value_w - r)
      << "\" height=\"" << h << "\" fill=\"#4c1\"/>\n";
  // Label text
  svg << "  <text x=\"" << (label_w / 2) << "\" y=\"" << (h / 2 + 4)
      << "\" font-family=\"monospace\" font-size=\"11\" fill=\"#fff\" "
      << "text-anchor=\"middle\">made with hali</text>\n";
  // Value text
  svg << "  <text x=\"" << (label_w + value_w / 2) << "\" y=\"" << (h / 2 + 4)
      << "\" font-family=\"monospace\" font-size=\"11\" fill=\"#fff\" "
      << "text-anchor=\"middle\">" << token_str << "</text>\n";
  svg << "</svg>\n";
  return svg.str();
}