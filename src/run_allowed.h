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
#include <ctime>
#include <fstream>
#include <sstream>
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
   * @brief Load user-approved names from a file.
   * Format: one entry per line, optionally "name\tYYYYMMDD".
   * Entries older than ttl_days are discarded.
   * Silently succeeds if the file doesn't exist.
   */
  void load(const std::string &path, int ttl_days = 30) {
    entries_.clear();
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    auto today = current_date_int();
    while (std::getline(f, line)) {
      // Strip whitespace
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
      if (line.empty()) continue;
      // Parse optional timestamp: name\tYYYYMMDD
      std::string name = line;
      auto tab = line.find('\t');
      if (tab != std::string::npos) {
        name = line.substr(0, tab);
        std::string ts = line.substr(tab + 1);
        if (ts.size() == 8) {
          int entry_date = 0;
          try { entry_date = std::stoi(ts); } catch (...) {}
          if (entry_date > 0 && date_diff_days(today, entry_date) > ttl_days) {
            continue;  // expired
          }
        }
      }
      if (!name.empty() && !contains(name)) {
        entries_.push_back(name);
      }
    }
  }

  /**
   * @brief Persist user-approved names to disk with timestamps.
   * Format: "name\tYYYYMMDD" per line.
   */
  void save(const std::string &path) const {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    std::string ts = std::to_string(current_date_int());
    for (const auto &name : entries_) {
      f << name << "\t" << ts << "\n";
    }
  }

  /** @brief Returns a formatted summary for startup display. */
  std::string summary() const {
    if (entries_.empty()) return "";
    std::string s = std::to_string(entries_.size()) + " previously approved RUN command"
      + (entries_.size() > 1 ? "s:" : ":");
    for (const auto &name : entries_) {
      s += "\n  - " + name;
    }
    return s;
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
  /** @brief Returns current date as YYYYMMDD integer. */
  static int current_date_int() {
    std::time_t t = std::time(nullptr);
    std::tm *lt = std::localtime(&t);
    if (!lt) return 0;
    return (lt->tm_year + 1900) * 10000 + (lt->tm_mon + 1) * 100 + lt->tm_mday;
  }

  /**
   * @brief Returns the number of days between two YYYYMMDD dates (a - b).
   * Uses mktime for proper calendar arithmetic.
   */
  static int date_diff_days(int ymd_a, int ymd_b) {
    auto to_days = [](int ymd) -> long {
      int y = ymd / 10000;
      int m = (ymd / 100) % 100;
      int d = ymd % 100;
      std::tm tm = {};
      tm.tm_year = y - 1900;
      tm.tm_mon = m - 1;
      tm.tm_mday = d;
      tm.tm_hour = 12;  // noon to avoid DST edge cases
      std::time_t t = std::mktime(&tm);
      if (t == -1) return 0;
      return static_cast<long>(t / 86400);
    };
    return static_cast<int>(to_days(ymd_a) - to_days(ymd_b));
  }

  std::vector<std::string> entries_;
};