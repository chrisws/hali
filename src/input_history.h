// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <fstream>

namespace fs = std::filesystem;

//
// InputHistory — up/down arrow navigation through submitted inputs
//
class InputHistory {
  public:
  explicit InputHistory() = default;
  ~InputHistory() = default;
  InputHistory(const InputHistory &) = delete;
  InputHistory &operator=(const InputHistory &) = delete;

  /**
   * @brief Adds a new command string to the history stack.
   * Resets navigation index upon adding a new item.
   * Deduplicates consecutive identical entries.
   */
  void push(const std::string &input) {
    if (input.empty()) return;
    if (!history_stack.empty() && history_stack.back() == input) {
      // Don't push duplicate of last entry; just reset nav position.
      current_index = static_cast<int>(history_stack.size());
      return;
    }
    history_stack.push_back(input);
    current_index = static_cast<int>(history_stack.size());
  }

  /**
   * @brief Navigates to an earlier entry.
   * @param out Set to the selected entry on success.
   * @return true if an item was successfully retrieved.
   */
  bool up(std::string &out) {
    if (history_stack.empty() || current_index <= 0) return false;
    --current_index;
    out = history_stack[current_index];
    return true;
  }

  /**
   * @brief Navigates to a later entry, or clears when past the newest.
   * @param out Set to the selected entry, or cleared if past the end.
   * @return true if a history entry was retrieved (false means "clear input").
   */
  bool down(std::string &out) {
    if (history_stack.empty()) return false;
    ++current_index;
    if (current_index >= static_cast<int>(history_stack.size())) {
      current_index = static_cast<int>(history_stack.size());
      out.clear();
      return false; // signal: restore blank input
    }
    out = history_stack[current_index];
    return true;
  }

  /** Reset navigation position without modifying the stack. */
  void reset_nav() {
    current_index = static_cast<int>(history_stack.size());
  }

  /**
   * @brief Load history from ~/.config/hali/hali.history (one entry per line).
   * Silently succeeds if the file doesn't exist.
   */
  void load(const std::string &path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
      if (!line.empty()) history_stack.push_back(line);
    }
    current_index = static_cast<int>(history_stack.size());
  }

  /**
   * @brief Persist history to disk (most-recent last, one entry per line).
   * Caps at MAX_PERSIST entries so the file never grows unbounded.
   */
  void save(const std::string &path) const {
    // Ensure parent directory exists.
    fs::path dir = fs::path(path).parent_path();
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::ofstream f(path, std::ios::trunc);
    if (!f) return;

    static constexpr int MAX_PERSIST = 500;
    int start = std::max(0, static_cast<int>(history_stack.size()) - MAX_PERSIST);
    for (int i = start; i < static_cast<int>(history_stack.size()); ++i) {
      // Escape embedded newlines so each entry stays on one line.
      for (char c : history_stack[i]) {
        if (c == '\n') f << "\\n";
        else           f << c;
      }
      f << '\n';
    }
  }

  private:
  std::vector<std::string> history_stack;
  int current_index = 0;
};
