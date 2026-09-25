// This file is part of Nitro
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <notcurses/notcurses.h>

// Key Codes enum - Uses notcurses NCKEY_* constants
enum class Key : uint32_t {
  ESCAPE = NCKEY_ESC,
  TAB = NCKEY_TAB,
  HOME = NCKEY_HOME,
  END = NCKEY_END,
  PAGE_UP = NCKEY_PGUP,
  PAGE_DOWN = NCKEY_PGDOWN,
  UP = NCKEY_UP,
  DOWN = NCKEY_DOWN,
  LEFT = NCKEY_LEFT,
  RIGHT = NCKEY_RIGHT,
  ENTER = NCKEY_ENTER,
  SPACE = NCKEY_SPACE,
  BACKSPACE = NCKEY_BACKSPACE,
  DEL = NCKEY_DEL,
  F1 = NCKEY_F01,
  F2 = NCKEY_F02,
  F3 = NCKEY_F03,
  F4 = NCKEY_F04,
  F5 = NCKEY_F05,
  F12 = NCKEY_F12,
  MEDIA_PLAY = NCKEY_MEDIA_PLAY,
  MEDIA_PAUSE = NCKEY_MEDIA_PAUSE,
  MEDIA_FF = NCKEY_MEDIA_FF,
  MEDIA_REWIND = NCKEY_MEDIA_REWIND,
  MEDIA_NEXT = NCKEY_MEDIA_NEXT,
  MEDIA_PREV = NCKEY_MEDIA_PREV,
  MEDIA_RECORD = NCKEY_MEDIA_RECORD,
  MEDIA_MUTE = NCKEY_MEDIA_MUTE,
  CAPS_LOCK = NCKEY_CAPS_LOCK,
  SCROLL_LOCK = NCKEY_SCROLL_LOCK,
  NUM_LOCK = NCKEY_NUM_LOCK,
  PRINT_SCREEN = NCKEY_PRINT_SCREEN,
  PAUSE = NCKEY_PAUSE,
  MENU = NCKEY_MENU,
  INVALID = NCKEY_INVALID,
  NL = '\n',
  CR = '\r',
  A = 'A',
  B = 'B',
  C = 'C',
  D = 'D',
  E = 'E',
  F = 'F',
  G = 'G',
  H = 'H',
  I = 'I',
  J = 'J',
  K = 'K',
  L = 'L',
  M = 'M',
  N = 'N',
  O = 'O',
  P = 'P',
  Q = 'Q',
  R = 'R',
  S = 'S',
  T = 'T',
  U = 'U',
  V = 'V',
  W = 'W',
  X = 'X',
  Y = 'Y',
  Z = 'Z',
};

struct InputEvent {
  explicit InputEvent(notcurses *nc);
  ~InputEvent() = default;

  Key key() const { return static_cast<Key>(in_.id); }
  bool is(Key k) const { return k == key(); }
  uint32_t val() const { return in_.id; }

  // Modifier state checks using notcurses functions
  bool is_shift() const { return ncinput_shift_p(&in_); }
  bool is_ctrl() const { return ncinput_ctrl_p(&in_); }
  bool is_alt() const { return ncinput_alt_p(&in_); }
  bool is_edit() const { return in_.id >= 32 && in_.id < 0xD800; }
  bool is_ascii() const { return in_.id >= 32 && in_.id < 127; }

  // Window resize event
  bool is_resize() const { return in_.id == NCKEY_RESIZE; }

private:
  ncinput in_{};
};
