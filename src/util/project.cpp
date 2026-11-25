// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "project.h"

#include <filesystem>

namespace fs = std::filesystem;

// Returns true if the project contains at least one .kt source file.
// Uses recursive_directory_iterator with an entry cap to avoid excessive
// I/O on large pure-Java projects.
static bool has_kotlin_sources(const std::string &sandbox) {
  std::error_code ec;
  if (!fs::is_directory(sandbox, ec)) {
    return false;
  }
  constexpr int kMaxEntries = 1000;
  int count = 0;
  for (auto it = fs::recursive_directory_iterator(sandbox, ec);
       !ec && it != fs::recursive_directory_iterator() && count < kMaxEntries;
       ++it, ++count) {
    // Skip build output directories — they contain generated artifacts
    // and can exhaust the entry cap before we reach real sources.
    if (it->is_directory() && it->path().filename() == "build") {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file() && it->path().extension() == ".kt") {
      return true;
    }
  }
  return false;
}

ProjectLanguage detect_project_language(const std::string &sandbox) {
  if (fs::exists(sandbox + "/CMakeLists.txt")) {
    return ProjectLanguage::CPLUS;
  }
  // Kotlin DSL is a strong signal for Kotlin
  if (fs::exists(sandbox + "/build.gradle.kts") ||
      fs::exists(sandbox + "/settings.gradle.kts")) {
    return ProjectLanguage::KOTLIN;
  }
  // Groovy DSL or gradlew — check for .kt files to disambiguate
  if (fs::exists(sandbox + "/build.gradle") ||
      fs::exists(sandbox + "/gradlew")) {
    if (has_kotlin_sources(sandbox)) {
      return ProjectLanguage::KOTLIN;
    }
    return ProjectLanguage::JAVA;
  }
  if (fs::exists(sandbox + "/pom.xml")) {
    return ProjectLanguage::JAVA;
  }
  if (fs::exists(sandbox + "/pyproject.toml") ||
      fs::exists(sandbox + "/setup.py") ||
      fs::exists(sandbox + "/requirements.txt")) {
    return ProjectLanguage::PYTHON;
  }
  if (fs::is_directory(sandbox)) {
    for (const auto &entry : fs::directory_iterator(sandbox)) {
      if (entry.is_regular_file() && entry.path().extension() == ".py") {
        return ProjectLanguage::PYTHON;
      }
    }
  }
  return ProjectLanguage::NONE;
}

//
// Return a formatted summary of supported project types.
//
std::string project_types_summary() {
  std::string out;
  out += "=== Supported Project Types ===\n";
  out += "  C/C++     — CMakeLists.txt detected\n";
  out += "  Java      — pom.xml or build.gradle (no .kt sources)\n";
  out += "  Kotlin    — build.gradle.kts, settings.gradle.kts, or .kt sources\n";
  out += "  Python    — pyproject.toml, setup.py, requirements.txt, or .py files\n";
  out += "  None      — no recognised build system found\n";
  return out;
}