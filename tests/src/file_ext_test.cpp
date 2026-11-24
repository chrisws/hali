// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cassert>
#include <string>

#include "util/file_ext.h"

void file_ext_test() {
  // ── is_source ──────────────────────────────────────────────────────
  assert(file_ext::is_source(".cpp"));
  assert(file_ext::is_source(".c"));
  assert(file_ext::is_source(".h"));
  assert(file_ext::is_source(".py"));
  assert(file_ext::is_source(".java"));
  assert(file_ext::is_source(".ts"));
  assert(!file_ext::is_source(".md"));
  assert(!file_ext::is_source(".txt"));
  assert(!file_ext::is_source(""));

  // ── is_brace ───────────────────────────────────────────────────────
  assert(file_ext::is_brace(".cpp"));
  assert(file_ext::is_brace(".java"));
  assert(file_ext::is_brace(".js"));
  assert(file_ext::is_brace(".ts"));
  assert(file_ext::is_brace(".tsx"));
  assert(!file_ext::is_brace(".py"));
  assert(!file_ext::is_brace(".go"));
  assert(!file_ext::is_brace(".md"));

  // ── is_code ────────────────────────────────────────────────────────
  assert(file_ext::is_code(".cpp"));
  assert(file_ext::is_code(".py"));
  assert(file_ext::is_code(".json"));
  assert(file_ext::is_code(".yaml"));
  assert(file_ext::is_code(".svg"));
  assert(!file_ext::is_code(".md"));
  assert(!file_ext::is_code(".txt"));
  assert(!file_ext::is_code(".png"));

  // ── Per-language checks ────────────────────────────────────────────
  assert(file_ext::is_cpp(".cpp"));
  assert(file_ext::is_cpp(".cc"));
  assert(file_ext::is_cpp(".c"));
  assert(file_ext::is_cpp(".h"));
  assert(file_ext::is_cpp(".hpp"));
  assert(!file_ext::is_cpp(".java"));
  assert(!file_ext::is_cpp(".py"));

  assert(file_ext::is_java(".java"));
  assert(!file_ext::is_java(".kt"));
  assert(!file_ext::is_java(".cpp"));

  assert(file_ext::is_kotlin(".kt"));
  assert(file_ext::is_kotlin(".kts"));
  assert(!file_ext::is_kotlin(".java"));

  assert(file_ext::is_python(".py"));
  assert(!file_ext::is_python(".rb"));
  assert(!file_ext::is_python(".pyc"));

  // ── build_include_flags ────────────────────────────────────────────
  std::vector<std::string> exts = {".cpp", ".h", ".py"};
  std::string flags = file_ext::build_include_flags(exts);
  assert(flags == "--include=*.cpp --include=*.h --include=*.py");

  // ── build_search_include_flags ─────────────────────────────────────
  std::string search_flags = file_ext::build_search_include_flags();
  assert(search_flags.find("--include=*.cpp") != std::string::npos);
  assert(search_flags.find("--include=*.java") != std::string::npos);
  assert(search_flags.find("--include=*.py") != std::string::npos);
  // Should not have leading space
  assert(search_flags[0] == '-');

  // ── Extension lists are non-empty ──────────────────────────────────
  assert(!file_ext::source_extensions().empty());
  assert(!file_ext::brace_extensions().empty());
  assert(!file_ext::search_extensions().empty());
  assert(!file_ext::code_extensions().empty());
}