// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

// console_capture.hpp - ScummVM-style printf interposition for basalt.
// Overrides vprintf/vfprintf so that formatted console output from basalt
// (GPU init, CUDA messages, model loading) is routed to a callback instead
// of corrupting the notcurses terminal.

#ifndef CONSOLE_CAPTURE_H
#define CONSOLE_CAPTURE_H

#include <functional>
#include <string>

namespace console_capture {

/// Set the callback that receives captured console output.
/// Pass nullptr to disable capture (output passes through to the real stdout/stderr).
void set_sink(std::function<void(const std::string &)> sink);

/// Whether capture is currently active.
bool active();

}  // namespace console_capture

#endif