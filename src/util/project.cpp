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

ProjectLanguage detect_project_language(const std::string &sandbox) {
  if (fs::exists(sandbox + "/CMakeLists.txt")) {
    return ProjectLanguage::CPLUS;
  }
  if (fs::exists(sandbox + "/build.gradle") ||
      fs::exists(sandbox + "/build.gradle.kts") ||
      fs::exists(sandbox + "/gradlew")) {
    return ProjectLanguage::JAVA;
  }
  if (fs::exists(sandbox + "/pom.xml")) {
    return ProjectLanguage::JAVA;
  }
  if (fs::is_directory(sandbox)) {
    for (const auto &entry : fs::directory_iterator(sandbox)) {
      if (entry.is_directory() &&
          fs::exists(entry.path().string() + "/pom.xml")) {
        return ProjectLanguage::JAVA;
      }
    }
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