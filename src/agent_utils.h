// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>
#include <vector>

bool hasDangerousPatterns(const std::string &command, const std::vector<std::string> &run_allowed);
std::string join_path(const std::string &a, const std::string &b);
std::string unwrap(const std::string &input);
std::string strip_code_fences(const std::string &filename, const std::string &src);