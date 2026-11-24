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

namespace file_ext {

// All recognised source code extensions (for build system detection).
const std::vector<std::string> &source_extensions();

// Extensions for brace-delimited languages (C, C++, Java, JS, TS).
const std::vector<std::string> &brace_extensions();

// Extensions used in --include= flags for search/grep operations.
const std::vector<std::string> &search_extensions();

// All code file extensions (for code-fence / language detection).
const std::vector<std::string> &code_extensions();

// Check if a file extension is a recognised source extension.
bool is_source(const std::string &ext);

// Check if a file extension is a brace-language extension.
bool is_brace(const std::string &ext);

// Check if a file extension is a code extension.
bool is_code(const std::string &ext);

// Build "--include=*.cpp --include=*.h ..." string from a list of extensions.
std::string build_include_flags(const std::vector<std::string> &exts);

// Build "--include=*.cpp --include=*.h ..." string for search operations.
std::string build_search_include_flags();

// Per-language extension checks.
bool is_cpp(const std::string &ext);
bool is_java(const std::string &ext);
bool is_kotlin(const std::string &ext);
bool is_python(const std::string &ext);

} // namespace file_ext