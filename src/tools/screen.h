// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef SCREEN_H
#define SCREEN_H

#include <string>
#include <vector>

// Screen snapshot: save/restore TUI display state.
// a1 = command (save|show|restore|list|clear)
// a2 = name (for save/show/restore/clear)
std::string screen_exec(const std::string &a1, const std::string &a2,
                        const std::vector<std::string> &current_lines,
                        std::vector<std::string> &restore_out);

#endif