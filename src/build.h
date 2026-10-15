// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef BUILD_H
#define BUILD_H

#include <functional>
#include <string>

struct BuildResult {
  bool success_;
  std::string output_;
};

// mode: "setup" | "build" | "test" | "clean" | "all"
// sandbox: root directory of the project
// subdir: optional subdirectory to build as a sub-project (e.g. "tests")
// status: callback for progress messages (e.g. tui.show_tool)
BuildResult build_run(const std::string &sandbox, const std::string &mode,
                      const std::function<void(const std::string &)> &status,
                      const std::string &subdir = "");

#endif