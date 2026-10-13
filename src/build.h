// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <functional>
#include <string>

struct BuildResult {
  bool success_;
  std::string output_;
};

// mode: "setup" | "build" | "test" | "clean" | "all"
// sandbox: root directory of the project
// status: callback for progress messages (e.g. tui.show_tool)
BuildResult build_run(const std::string &sandbox, const std::string &mode,
                      const std::function<void(const std::string &)> &status);
