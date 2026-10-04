// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "line_list.h"

//
// RunAllowed — persistent list of user-approved TOOL:RUN program names.
//
// Only stores commands the user explicitly approved at runtime.
// The hardcoded ALLOWED_TOOLS defaults live in HaliConfig and are
// merged with this list at startup.
//
class RunAllowed {
  public:
  explicit RunAllowed() = default;
  ~RunAllowed() = default;
  RunAllowed(const RunAllowed &) = delete;
  RunAllowed &operator=(const RunAllowed &) = delete;

  /**
   * @brief Canonical persistence path: ~/.config/hali/user-approved.txt
   */
  static std::string default_path() {
    const char *home = std::getenv("HOME");
    std::string base = home ? std::string(home) : ".";
    return base + "/.config/hali/user-approved.txt";
  }

  /**
   * @brief Adds a program name to the list.
   * @return true if added, false if already present or name is empty.
   */
  bool add(const std::string &name) {
    if (name.empty()) return false;
    if (contains(name)) return false;
    entries_.push_back(name);
    return true;
  }

  /**
   * @brief Removes a program name from the list.
   * @return true if found and removed, false otherwise.
   */
  bool remove(const std::string &name) {
    auto it = std::find(entries_.begin(), entries_.end(), name);
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
  }

  /** @brief Returns true if the name is in the list. */
  bool contains(const std::string &name) const {
    return std::find(entries_.begin(), entries_.end(), name) != entries_.end();
  }

  /** @brief Number of user-approved entries. */
  size_t size() const { return entries_.size(); }

  /** @brief True if no user-approved entries. */
  bool empty() const { return entries_.empty(); }

  /**
   * @brief Load user-approved names from a file (one per line).
   * Silently succeeds if the file doesn't exist.
   */
  void load(const std::string &path) {
    entries_.clear();
    load_lines(path, entries_);
  }

  /**
   * @brief Persist user-approved names to disk (one per line).
   */
  void save(const std::string &path) const {
    save_lines(path, entries_);
  }

  /**
   * @brief Merge all entries into a target vector (e.g. cfg.run_allowed_).
   * Skips names already present in the target.
   */
  void merge_into(std::vector<std::string> &target) const {
    for (const auto &name : entries_) {
      if (std::find(target.begin(), target.end(), name) == target.end()) {
        target.push_back(name);
      }
    }
  }

  private:
  std::vector<std::string> entries_;
};