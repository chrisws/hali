// This file is part of AudioCore
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "logging.h"
#include "json.h"

#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <mutex>
#include <atomic>
#include <string>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <system_error>
#include <iterator>
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
    {"1", LEVEL_INFO},
    {"2", LEVEL_WARNING},
    {"3", LEVEL_ERROR},
    {"debug", LEVEL_DEBUG},
    {"info", LEVEL_INFO},
    {"warning", LEVEL_WARNING},
    {"error", LEVEL_ERROR},
  };
  LogLevel result = LEVEL_INFO;
  if (!level.empty()) {
    if (const auto it = loggingMap.find(level); it != loggingMap.end()) {
      result = it->second;
    }
  }
  return result;
}

void log_open(const std::string& level, const std::string& file, const std::string& filter, bool erase) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_log_console = false;

  std::string level_str = level;
  std::string file_path = file;
  std::string filter_str = filter;
  bool has_file = !file_path.empty();
  bool has_filter = !filter_str.empty();

  // Erase the log file before opening if requested
  if (erase && has_file) {
    std::error_code ec_erase;
    fs::remove(file_path, ec_erase);
  }

  // Check if the level argument is actually a path to an existing JSON config file
  std::error_code ec;
  if (!level.empty() && fs::is_regular_file(level, ec)) {
    std::ifstream ifs(level);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    json::JsonDoc doc = json::JsonDoc::parse(content);
    if (doc.is_valid()) {
      json::JsonValue root = doc.get_root();
      std::string val;
      if (root.get_str("level", val)) {
        level_str = val;
      }
      if (root.get_str("file", val) && val.empty() == false) {
        file_path = val;
        has_file = true;
      }
      if (root.get_str("filter", val) && val.empty() == false) {
        filter_str = val;
        has_filter = true;
      }
    }
  }

  g_level.store(get_level(level_str));

  // Category filter: explicit param > JSON config > env var
  g_log_filter_active = false;
  g_log_categories.clear();
  std::string effective_filter;
  if (has_filter) {
    effective_filter = filter_str;
  } else if (const char *env_filter = getenv("HALI_LOG_FILTER")) {
    effective_filter = env_filter;
  }
  if (!effective_filter.empty() && effective_filter != "*") {
    g_log_filter_active = true;
    size_t start = 0;
    while (start < effective_filter.size()) {
      size_t comma = effective_filter.find(',', start);
      if (comma == std::string::npos) {
        g_log_categories.insert(effective_filter.substr(start));
        break;
      }
      g_log_categories.insert(effective_filter.substr(start, comma - start));
      start = comma + 1;
    }
  }

  if (g_logfile == nullptr) {
    std::string path;
    if (has_file) {
      path = file_path;
    } else if (const char *env_path = getenv("HALI_LOG_FILE")) {
      path = env_path;
    } else {
      path = ".hali/hali.log";
    }
    std::error_code ec2;
    fs::path dir = fs::path(path).parent_path();
    fs::create_directories(dir, ec2);
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

void log_write(LogLevel level, const char* category, const char* format, ...) {
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

  if (g_log_filter_active && level == g_level.load() && category &&
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

