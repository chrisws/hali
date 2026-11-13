// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>

enum class ProjectLanguage {
  NONE,
  CPLUS,
  JAVA,
  PYTHON,
};

// Detect the primary language of a project by inspecting well-known
// build-system files in the sandbox root (and one level of subdirectories).
ProjectLanguage detect_project_language(const std::string &sandbox);