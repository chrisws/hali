// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef SHELL_UTIL_H
#define SHELL_UTIL_H

#include <string>

//
// Execute a shell command via popen, capture stdout+stderr.
// Returns the output string, or an error message on failure.
//
std::string run_shell_command(const std::string &cmd);

//
// Check if a string contains shell metacharacters that could
// enable command injection.
//
bool has_shell_metachars(const std::string &pattern);

#endif