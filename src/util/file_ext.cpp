// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "file_ext.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace file_ext {

//
// ── Extension lists ──────────────────────────────────────────────────────
//

static const std::vector<std::string> SOURCE_EXTENSIONS = {
  ".c", ".cpp", ".cc", ".h", ".hpp",
  ".go", ".py", ".rs", ".java", ".kt", ".ts", ".js",
};

static const std::vector<std::string> BRACE_EXTENSIONS = {
  ".c", ".cpp", ".h", ".hpp",
  ".js", ".jsx", ".ts", ".tsx", ".java",
};

static const std::vector<std::string> SEARCH_EXTENSIONS = {
  ".cpp", ".h", ".hpp", ".c", ".py", ".sh", ".js", ".ts", ".java",
};

static const std::vector<std::string> CODE_EXTENSIONS = {
  ".py", ".c", ".cpp", ".h", ".bas", ".java", ".html", ".js", ".ts",
  ".json", ".yaml", ".toml", ".sh", ".go", ".rs", ".jsx", ".tsx", ".xml", ".svg",
};

const std::vector<std::string> &source_extensions() {
  return SOURCE_EXTENSIONS;
}

const std::vector<std::string> &brace_extensions() {
  return BRACE_EXTENSIONS;
}

const std::vector<std::string> &search_extensions() {
  return SEARCH_EXTENSIONS;
}

const std::vector<std::string> &code_extensions() {
  return CODE_EXTENSIONS;
}

//
// ── Lookup helpers ───────────────────────────────────────────────────────
//

static bool contains(const std::vector<std::string> &list, const std::string &ext) {
  return std::find(list.begin(), list.end(), ext) != list.end();
}

bool is_source(const std::string &ext) {
  return contains(SOURCE_EXTENSIONS, ext);
}

bool is_brace(const std::string &ext) {
  return contains(BRACE_EXTENSIONS, ext);
}

bool is_code(const std::string &ext) {
  return contains(CODE_EXTENSIONS, ext);
}

//
// ── Flag builders ────────────────────────────────────────────────────────
//

std::string build_include_flags(const std::vector<std::string> &exts) {
  std::ostringstream oss;
  for (const auto &ext : exts) {
    oss << " --include=*" << ext;
  }
  return oss.str().substr(1); // strip leading space
}

std::string build_search_include_flags() {
  return build_include_flags(SEARCH_EXTENSIONS);
}

//
// ── Per-language checks ──────────────────────────────────────────────────
//

bool is_cpp(const std::string &ext) {
  return ext == ".cpp" || ext == ".cc" || ext == ".c" ||
         ext == ".h" || ext == ".hpp";
}

bool is_java(const std::string &ext) {
  return ext == ".java";
}

bool is_kotlin(const std::string &ext) {
  return ext == ".kt" || ext == ".kts";
}

bool is_python(const std::string &ext) {
  return ext == ".py";
}

} // namespace file_ext