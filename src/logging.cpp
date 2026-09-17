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
#include <string>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

static FILE *g_logfile = nullptr;
static LogLevel g_level = LEVEL_DEBUG;
static bool g_log_console = false;
static std::mutex g_log_mutex;

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
  g_level = get_level(level);
  if (g_logfile == nullptr) {
    const char *home = getenv("HOME");
    const auto path = std::string(home ? home : ".") + "/.config/nitro/nitro.log";
    std::error_code ec;
    fs::path dir = fs::path(path).parent_path();
    fs::create_directories(dir, ec);
    g_logfile = fopen(path.c_str(), "a");
  }
}

void log_open_console() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_logfile = nullptr;
  g_level = LEVEL_ERROR;
  g_log_console = true;
}

void log_close() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_logfile != nullptr) {
    fclose(g_logfile);
    g_logfile = nullptr;
  }
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

  if (!g_logfile || level < g_level) {
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