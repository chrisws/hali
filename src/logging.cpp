// This file is part of AudioCore
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "logging.h"

#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <mutex>
#include <atomic>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

namespace fs = std::filesystem;

static FILE *g_logfile = nullptr;
static std::atomic<LogLevel> g_level{LEVEL_DEBUG};
static bool g_log_console = false;
static std::mutex g_log_mutex;
static std::unordered_set<std::string> g_log_categories;
static bool g_log_filter_active = false;

static LogLevel get_level(const std::string& level) {
  static const std::unordered_map<std::string, LogLevel> loggingMap = {
    {"0", LEVEL_DEBUG},
    {"1", LEVEL_DEBUG},
    {"2", LEVEL_INFO},
    {"3", LEVEL_WARNING},
    {"4", LEVEL_ERROR},
    {"5", LEVEL_ERROR},
  };
  LogLevel result = LEVEL_INFO;
  if (!level.empty()) {
    if (const auto it = loggingMap.find(level); it != loggingMap.end()) {
      result = it->second;
    }
  }
  return result;
}

void log_open(const std::string& level) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_level.store(get_level(level));
  g_log_console = false;

  // Read category filter (reset state unconditionally)
  g_log_filter_active = false;
  g_log_categories.clear();
  if (const char *filter = getenv("HALI_LOG_FILTER")) {
    g_log_filter_active = (std::string(filter) != "*");
    if (g_log_filter_active) {
      std::string f(filter);
      size_t start = 0;
      while (start < f.size()) {
        size_t comma = f.find(',', start);
        if (comma == std::string::npos) {
          g_log_categories.insert(f.substr(start));
          break;
        }
        g_log_categories.insert(f.substr(start, comma - start));
        start = comma + 1;
      }
    }
  }

  if (g_logfile == nullptr) {
    std::string path;
    if (const char *env_path = getenv("HALI_LOG_FILE")) {
      path = env_path;
    } else {
      const char *home = getenv("HOME");
      path = std::string(home ? home : ".") + "/.config/hali/hali.log";
    }
    std::error_code ec;
    fs::path dir = fs::path(path).parent_path();
    fs::create_directories(dir, ec);
    g_logfile = fopen(path.c_str(), "a");
  }
}

void log_open_console() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_logfile = nullptr;
  g_level.store(LEVEL_ERROR);
  g_log_console = true;
}

void log_close() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_logfile != nullptr) {
    fclose(g_logfile);
    g_logfile = nullptr;
  }
}

bool log_is_enabled(LogLevel level) {
  return level >= g_level.load();
}

void log_write_cat(LogLevel level, const char* category, const char* format, ...) {
  std::lock_guard<std::mutex> lock(g_log_mutex);

  if (g_log_console) {
    fprintf(stderr, "LOG: ");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    return;
  }

  if (!g_logfile || level < g_level.load()) {
    return;
  }

  if (g_log_filter_active && category &&
      g_log_categories.find(category) == g_log_categories.end()) {
    return;
  }

  const time_t now = time(nullptr);
  char timestamp[20];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

  const char* level_str;
  switch (level) {
    case LEVEL_DEBUG: level_str = "DEBUG"; break;
    case LEVEL_INFO: level_str = "INFO"; break;
    case LEVEL_WARNING: level_str = "WARNING"; break;
    case LEVEL_ERROR: level_str = "ERROR"; break;
    default: level_str = "UNKNOWN"; break;
  }

  fprintf(g_logfile, "[%s] [%s] ", timestamp, level_str);
  if (format) {
    va_list args;
    va_start(args, format);
    vfprintf(g_logfile, format, args);
    va_end(args);
  }
  fprintf(g_logfile, "\n");
  fflush(g_logfile);
}

void log_write(LogLevel level, const char* format, ...) {
  std::lock_guard<std::mutex> lock(g_log_mutex);

  if (g_log_console) {
    fprintf(stderr, "LOG: ");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    return;
  }

  if (!g_logfile || level < g_level.load()) {
    return;
  }

  // When a category filter is active, untagged lines are excluded
  if (g_log_filter_active) {
    return;
  }

  const time_t now = time(nullptr);
  char timestamp[20];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

  const char* level_str;
  switch (level) {
    case LEVEL_DEBUG: level_str = "DEBUG"; break;
    case LEVEL_INFO: level_str = "INFO"; break;
    case LEVEL_WARNING: level_str = "WARNING"; break;
    case LEVEL_ERROR: level_str = "ERROR"; break;
    default: level_str = "UNKNOWN"; break;
  }

  fprintf(g_logfile, "[%s] [%s] ", timestamp, level_str);
  if (format) {
    va_list args;
    va_start(args, format);
    vfprintf(g_logfile, format, args);
    va_end(args);
  }
  fprintf(g_logfile, "\n");
  fflush(g_logfile);
}