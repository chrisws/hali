// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include "input_event.h"

class TuiContext {
public:
  TuiContext() = default;
  virtual ~TuiContext() = default;
  TuiContext(const TuiContext &) = delete;
  TuiContext &operator=(const TuiContext &) = delete;

  virtual InputEvent get_event() const = 0;
  virtual void render() = 0;
  virtual void redraw_input() const = 0;
  virtual void enable_mouse(bool enable) = 0;
  virtual void redraw_chat() = 0;
  virtual int get_term_rows() const = 0;
  virtual void toggle_theme() = 0;
};
