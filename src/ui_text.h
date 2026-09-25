// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <string>

#include "config.h"
#include "tui.h"

namespace ui {
  void help(Tui &tui);
  void settings(Tui &tui, HaliConfig &cfg);
  void usage();
  void no_model(Tui &tui);
  void welcome(Tui &tui, const std::string &sandbox);
}
