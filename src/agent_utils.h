// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef AGENT_UTILS_H
#define AGENT_UTILS_H

#include <functional>
#include <string>
#include <vector>

struct Tui;

//
// Run a callable in a background thread, ticking the TUI spinner while waiting.
// Use for any tool operation that may block for >2 seconds.
//
std::string run_with_spinner(Tui &tui, const std::function<std::string()> &fn);

bool hasDangerousPatterns(const std::string &command, const std::vector<std::string> &run_allowed);
std::string join_path(const std::string &a, const std::string &b);
std::string unwrap(const std::string &input);
std::string strip_code_fences(const std::string &filename, const std::string &src);

//
// Path helpers
//
std::string resolve_path(const std::string &sandbox, const std::string &p);
bool path_in_sandbox(const std::string &sandbox, const std::string &path);
bool path_allowed(const std::string &sandbox, const std::vector<std::string> &extra_dirs, const std::string &path);

//
// Tool argument splitting
//
struct ToolArgs {
  std::string arg1;
  std::string arg2;
};

ToolArgs split_tool_args(const std::string &rest);

//
// TOOL:SEARCH
//
struct SearchFlags {
  bool recursive = false;
  bool line_numbers = false;
  bool count = false;
  bool line_count = false;
  bool files_only = false;
  bool name = false;
  std::string include_glob;
  int context = 0;
  int lines_start = 0;
  int lines_end = 0;
};

struct SearchArgs {
  std::string pattern;
  std::string path_and_flags;
};

SearchArgs parse_search_args(const std::string &arg1, const std::string &arg2);
SearchFlags parse_search_flags(const std::string &flags_str);
bool matches_include(const std::string &filename, const std::string &glob);
std::string search_single_file(const std::string &filepath, const std::string &pattern, const SearchFlags &flags);
std::string tool_search(const std::string &sandbox, const std::string &pattern, const std::string &path_arg, const std::string &flags_str, const std::vector<std::string> &extra_dirs = {});

//
// TOOL:REPLACE
//
struct ReplaceArgs {
  std::string find_str;
  std::string replace_str;
  bool all = false;
  bool count = false;
};

ReplaceArgs parse_replace_args(const std::string &arg2);
std::string tool_replace(const std::string &sandbox, const std::string &file, const std::string &arg2);

//
// Shared constants
//
inline constexpr const char *SESSION_MD = ".hali/SESSION.md";

//
// TOOL:RUN / backup / broadcast helpers
//
struct HaliConfig;
std::string tool_run(HaliConfig &cfg, Tui &tui, const std::string &arg1, const std::string &arg2);
void backup_file(const HaliConfig &cfg, Tui &tui, const std::string &path);
void broadcast_reload(const HaliConfig &cfg, Tui &tui);

//
// TOOL:TABLE
//
struct TableResult {
  std::string error;
  std::vector<std::string> rows;
};

TableResult tool_table(const std::string &dims, const std::string &data);

//
// Think-tag state machine for parsing model output
//
class ThinkParser {
public:
  enum Mode { Init, Think, Thunk };

  struct EndResult {
    bool found = false;
    size_t pos = 0;
    size_t tag_len = 0;
    std::string thought;
    bool clear_buffer = false;
  };

  ThinkParser(bool thinking_enabled, bool is_gemma_4 = false);

  // Returns tag length if buffer starts with a think-open tag (transitions Init→Think), else 0.
  size_t check_start(const std::string &buffer);

  // Returns EndResult if buffer contains a think-close tag (transitions Think→Thunk).
  EndResult check_end(const std::string &buffer);

  Mode mode() const;
  void set_mode(Mode m);
  void reset();

  // Assemble a tag from single-char tokens starting with '<'.
  // Stops at first '>' or when max_len is reached.
  static std::string assemble_tag(const std::vector<std::string> &tokens, size_t max_len = 32);

private:
  Mode mode_;
  bool is_gemma_4_;
};

#endif