// This file is part of AudioCore
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef LOGGING_H
#define LOGGING_H

#include <string>

enum LogLevel {
  LEVEL_DEBUG = 0,
  LEVEL_INFO = 1,
  LEVEL_WARNING = 2,
  LEVEL_ERROR = 3
};

void log_write(LogLevel level, const char* format, ...);
void log_open(const std::string& level);
void log_open_console();
void log_close();

#endif // LOGGING_H
