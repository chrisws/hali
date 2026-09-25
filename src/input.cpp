// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "input.h"
#include "logging.h"
#include "string_utils.h"

Input::Input()
  : mouse_mode_(false)
  , insert_mode_(false)
  , ctrl_x_mode_(false)
  , cursor_pos_(0)
  , scroll_offset_(0) {
}

//
// Find the start of the word containing pos.
// Returns the index of the first character of that word.
//
int Input::pos_of_word_start(const int pos) const {
  int start = pos;
  while (start > 0 && !isspace(static_cast<unsigned char>(input_buf_[start - 1]))) {
    --start;
  }
  return start;
}

//
// Find the end of the word containing pos.
// Returns the index of the last character of that word.
//
int Input::pos_of_word_end(const int pos) const {
  int end = pos;
  while (end < static_cast<int>(input_buf_.size()) && !isspace(static_cast<unsigned char>(input_buf_[end]))) {
    ++end;
  }
  return end - 1;
}

//
// Move cursor to the start of the previous word.
// A "word" is a contiguous run of non-whitespace characters.
// If already at the start of a word, move to its beginning.
// If at the start of the buffer, return 0.
//
int Input::move_to_prev_word(const int pos) const {
  if (pos == 0) return 0;
  // Find the start of the current word.
  int start = pos;
  while (start > 0 && !isspace(static_cast<unsigned char>(input_buf_[start - 1]))) {
    --start;
  }
  // If we're already at the start, move to the beginning of the word.
  if (start == pos) {
    while (start > 0 && utils::is_word_char(static_cast<unsigned char>(input_buf_[start - 1]))) {
      --start;
    }
  }
  return start;
}

//
// Move cursor to the end of the next word.
// A "word" is a contiguous run of non-whitespace characters.
// If already at the end of a word, move to its end.
// If at the end of the buffer, return size.
//
int Input::move_to_next_word(const int pos) const {
  if (pos >= static_cast<int>(input_buf_.size())) return static_cast<int>(input_buf_.size());
  // Find the end of the current word.
  int end = pos;
  while (end < static_cast<int>(input_buf_.size()) && !isspace(static_cast<unsigned char>(input_buf_[end]))) {
    ++end;
  }
  // If we're already at the end, move to the end of the word.
  if (end == pos) {
    while (end > 0 && utils::is_word_char(static_cast<unsigned char>(input_buf_[end - 1]))) {
      --end;
    }
  }
  return end;
}

//
// Delete the word before the cursor.
// A "word" is a contiguous run of non-whitespace characters.
// Returns the new cursor position.
//
int Input::delete_word_before(const int pos) {
  int start = move_to_prev_word(pos);
  const int end = pos;
  while (start > 0 && utils::is_word_char(static_cast<unsigned char>(input_buf_[start - 1]))) {
    --start;
  }
  input_buf_.erase(start, end - start);
  return start;
}

//
// Kill the word backward.
// A "word" is a contiguous run of non-whitespace characters.
// Returns the new cursor position.
//
int Input::kill_word_backward(const int pos) {
  int start = move_to_prev_word(pos);
  const int end = pos;
  while (start > 0 && utils::is_word_char(static_cast<unsigned char>(input_buf_[start - 1]))) {
    --start;
  }
  input_buf_.erase(start, end - start);
  return start;
}

//
// Delete the word at the cursor (Emacs-style Alt-D).
// If the cursor is at the end of a word, delete the whole word.
// If the cursor is in the middle of a word, delete from cursor to end.
// If the cursor is at the start of a word, delete the whole word.
// Returns the new cursor position.
//
int Input::delete_word_at_cursor() {
  const int start = pos_of_word_start(cursor_pos_);
  const int end = pos_of_word_end(cursor_pos_);
  input_buf_.erase(start, end - start);
  return start;
}

//
// Uppercase the word under the cursor (Emacs-style Alt-L).
// Returns the new cursor position.
//
int Input::uppercase_word(const int pos) {
  const int start = pos_of_word_start(pos);
  const int end = pos_of_word_end(pos);
  for (int i = start; i < end; ++i) {
    if (input_buf_[i] >= 'a' && input_buf_[i] <= 'z') {
      input_buf_[i] = static_cast<char>(input_buf_[i] - ('a' - 'A'));
    }
  }
  return start;
}

//
// Lowercase the word under the cursor (Emacs-style Alt-d).
// Returns the new cursor position.
//
int Input::lowercase_word(const int pos) {
  const int start = pos_of_word_start(pos);
  const int end = pos_of_word_end(pos);
  for (int i = start; i < end; ++i) {
    if (input_buf_[i] >= 'A' && input_buf_[i] <= 'Z') {
      input_buf_[i] = static_cast<char>(input_buf_[i] - ('A' - 'a'));
    }
  }
  return start;
}

// ─── Input::readline_blocking (bash-like editing) ─────────────────────────────────
//
// Integrates InputHistory_:  Up/Down arrows navigate the history_ stack.
// On submit the entry is pushed to history_, and nav is reset.
//
// New features (bash-like):
//   Ctrl-Left/Right  : word-wise navigation
//   Ctrl-Home/End    : jump to start/end
//   Ctrl-a/e         : move to start/end (muscle memory)
//   Ctrl-i (Tab)     : insert space
//   Ctrl-Backspace   : delete word before cursor
//   Alt-d            : delete word at cursor (Emacs-style)
//   Alt-l            : lowercase word under cursor
//   Alt-u            : uppercase word under cursor
//   Ctrl-c           : cancel input (discard draft)
//   Ctrl-d           : delete-char (kill next char)
//   Ctrl-e           : kill word backward
//   Ctrl-g           : quit (cancel input)
//   Ctrl-h           : kill-backward-char (backspace char)
//   Ctrl-k           : clear to end of line
//   Ctrl-l           : clear screen and redraw
//   Ctrl-u           : unix-line-discard (clear to beginning)
//   Ctrl-v           : enter insert mode for the next character
//   Ctrl-x Ctrl+k    : kill-line (clear to end)
//   Ctrl-x Ctrl+l    : list-buffers (show history_)
//   Ctrl-x Ctrl+p    : previous-history_ (go up in history_)
//   Ctrl-x Ctrl+r    : recent-files (show recent entries)
//   Ctrl-x Ctrl+s    : save-buffer (push to history_ as "saved")
//   Ctrl-x Ctrl+t    : transient-mark-mode (select region)
//   Ctrl-x Ctrl+u    : universal-argument (repeat next command x4)
//   Ctrl-x Ctrl+v    : insert-file (paste from clipboard)
//   Ctrl-x Ctrl+y    : yank (paste last killed text)
//   Ctrl-x Ctrl+z    : suspend (cancel and return to prompt)
//
std::string Input::readline(TuiContext &tui) {
  auto redraw_input = [&]() -> void {
    tui.redraw_input();
    tui.render();
  };

  auto redraw_chat = [&]() -> void {
    tui.redraw_chat();
    tui.render();
  };

  input_buf_.clear();
  cursor_pos_ = 0;
  history_.reset_nav();
  redraw_input();

  std::string draft;

  for (;;) {
    auto ev = tui.get_event();
    if (ev.is(Key::ENTER) || ev.is(Key::NL) || ev.is(Key::CR)) {
      log_write(LEVEL_DEBUG, "Key ENTER/NL/CR pressed");
      std::string result = input_buf_;
      if (!result.empty()) {
        history_.push(result);
      }
      input_buf_.clear();
      cursor_pos_ = 0;
      scroll_offset_ = 0;
      redraw_input();
      return result;
    }

    if (ev.is_ctrl() && ev.is(Key::X)) {
      log_write(LEVEL_DEBUG, "Ctrl+X key combination");
      ctrl_x_mode_ = true;
      // read next key combination
      continue;
    } else if (ctrl_x_mode_ && ev.is_ctrl()) {
      // exit control-x mode
      ctrl_x_mode_ = false;
    }

    if (ev.is(Key::F2)) {
      log_write(LEVEL_DEBUG, "F2 key pressed (toggle mouse mode)");
      mouse_mode_ = !mouse_mode_;
      tui.enable_mouse(mouse_mode_);
      continue;
    }

    if (ev.is(Key::F12)) {
      log_write(LEVEL_DEBUG, "F12 key pressed (toggle theme)");
      tui.toggle_theme();
      continue;
    }

    if (ev.is(Key::PAGE_UP)) {
      log_write(LEVEL_DEBUG, "Page Up key pressed");
      const int term_rows = tui.get_term_rows();
      scroll_offset_ += std::max(1, term_rows - 4);
      redraw_chat();
      continue;
    }

    if (ev.is(Key::PAGE_DOWN)) {
      log_write(LEVEL_DEBUG, "Page Down key pressed");
      const int term_rows = tui.get_term_rows();
      scroll_offset_ = std::max(0, scroll_offset_ - std::max(1, term_rows - 4));
      redraw_chat();
      continue;
    }

    if (ev.is(Key::UP)) {
      log_write(LEVEL_DEBUG, "Up arrow key pressed");
      std::string hist_entry;
      if (history_.up(hist_entry)) {
        if (!input_buf_.empty() && hist_entry != input_buf_) {
          draft = input_buf_;
        }
        input_buf_  = hist_entry;
        cursor_pos_ = input_buf_.size();
      }
      redraw_input();
      continue;
    }

    if (ev.is(Key::DOWN)) {
      log_write(LEVEL_DEBUG, "Down arrow key pressed");
      std::string hist_entry;
      if (history_.down(hist_entry)) {
        input_buf_  = hist_entry;
        cursor_pos_ = input_buf_.size();
      } else {
        input_buf_  = draft;
        cursor_pos_ = input_buf_.size();
        draft.clear();
      }
      redraw_input();
      continue;
    }

    // Ctrl-L: clear screen and redraw
    if (ev.is_ctrl() && ev.is(Key::L)) {
      log_write(LEVEL_DEBUG, "Ctrl+L key pressed (clear screen)");
      cursor_pos_ = 0;
      scroll_offset_ = 0;
      input_buf_.clear();
      redraw_input();
      continue;
    }

    // Insert mode handling
    if (insert_mode_) {
      log_write(LEVEL_DEBUG, "Insert mode handling");
      if (ev.is(Key::BACKSPACE) || ev.is(Key::ESCAPE)) {
        if (cursor_pos_ > 0) {
          input_buf_.erase(cursor_pos_ - 1, 1); --cursor_pos_;
        }
      } else if (ev.is(Key::LEFT)) {
        if (cursor_pos_ > 0) {
          --cursor_pos_;
        }
      } else if (ev.is(Key::RIGHT)) {
        if (cursor_pos_ < input_buf_.size()) {
          ++cursor_pos_;
        }
      } else if (ev.is_edit()) {
        input_buf_.insert(cursor_pos_, 1, ev.val());
        ++cursor_pos_;
      }
      redraw_input();
      continue;
    }

    // Ctrl-Left / Ctrl-Right: word-wise navigation
    if (ev.is_ctrl() && ev.is(Key::LEFT)) {
      cursor_pos_ = move_to_prev_word(cursor_pos_);
      redraw_input();
      continue;
    } else if (ev.is(Key::RIGHT)) {
      cursor_pos_ = move_to_next_word(cursor_pos_);
      redraw_input();
      continue;
    }

    // Ctrl-Home / Ctrl-End: jump to start/end
    if (ev.is_ctrl() && ev.is(Key::HOME)) {
      cursor_pos_ = 0;
      redraw_input();
      continue;
    } else if (ev.is(Key::END)) {
      redraw_input();
      continue;
      cursor_pos_ = input_buf_.size();
    }

    // Ctrl-A / Ctrl-E: move to start/end (muscle memory)
    if (ev.is_ctrl() && ev.is(Key::A)) {
      cursor_pos_ = 0;
      redraw_input();
      continue;
    } else if (ev.is_ctrl() && ev.is(Key::E)) {
      cursor_pos_ = input_buf_.size();
      redraw_input();
      continue;
    }

    // Ctrl-I (Tab): insert space
    if (ev.is_ctrl() && ev.is(Key::SPACE)) {
      log_write(LEVEL_DEBUG, "Ctrl+space key pressed");
      input_buf_.insert(cursor_pos_, 1, ' ');
      ++cursor_pos_;
      redraw_input();
      continue;
    }

    // Ctrl-Backspace: delete word before cursor
    if (ev.is_ctrl() && ev.is(Key::BACKSPACE)) {
      log_write(LEVEL_DEBUG, "Ctrl+backspace key pressed");
      cursor_pos_ = delete_word_before(cursor_pos_);
      redraw_input();
      continue;
    }

    // Ctrl-W: kill word backward
    if (ev.is_ctrl() && ev.is(Key::W)) {
      log_write(LEVEL_DEBUG, "Ctrl+W key pressed");
      cursor_pos_ = kill_word_backward(cursor_pos_);
      redraw_input();
      continue;
    }

    // Ctrl-U: clear from cursor to beginning
    if (ev.is_ctrl() && ev.is(Key::U)) {
      log_write(LEVEL_DEBUG, "Ctrl+U key pressed");
      input_buf_.erase(0, cursor_pos_);
      cursor_pos_ = 0;
      redraw_input();
      continue;
    }

    // Ctrl-K: clear from cursor to end
    if (ev.is_ctrl() && ev.is(Key::K)) {
      log_write(LEVEL_DEBUG, "Ctrl+K key pressed");
      input_buf_.erase(cursor_pos_);
      redraw_input();
      continue;
    }

    // Ctrl-D: delete-char (kill next char)
    if (ev.is_ctrl() && ev.is(Key::D)) {
      log_write(LEVEL_DEBUG, "Ctrl+D key pressed (delete char)");
      if (cursor_pos_ < input_buf_.size()) {
        input_buf_.erase(cursor_pos_, 1);
        redraw_input();
        continue;
      }
    }

    if (ev.is_ctrl() && ev.is(Key::V)) {
      log_write(LEVEL_DEBUG, "Ctrl+V key pressed (enter insert mode)");
      // Ctrl-V: enter insert mode for the next character
      insert_mode_ = true;
      redraw_input();
      continue;
    }

    // Ctrl-H: kill-backward-char (backspace char)
    if (ev.is_ctrl() && ev.is(Key::H)) {
      log_write(LEVEL_DEBUG, "Ctrl+H key pressed (backspace)");
      if (cursor_pos_ > 0) {
        input_buf_.erase(cursor_pos_ - 1, 1);
        --cursor_pos_;
        redraw_input();
        continue;
      }
    }

    // crtl-D: delete word at cursor (Emacs-style)
    if (ev.is_ctrl() && ev.is(Key::D)) {
      cursor_pos_ = delete_word_at_cursor();
      redraw_input();
      continue;
    }

    // Alt-L: uppercase word under cursor
    if (ev.is_alt() && ev.is(Key::L)) {
      cursor_pos_ = uppercase_word(cursor_pos_);
      redraw_input();
      continue;
    }

    // Alt-d: lowercase word under cursor
    if (ev.is_alt() && ev.is(Key::D)) {
      cursor_pos_ = lowercase_word(cursor_pos_);
      redraw_input();
      continue;
    }

    // Ctrl-G: quit (cancel input)
    if (ev.is_ctrl() && ev.is(Key::G)) {
      input_buf_.clear();
      cursor_pos_ = 0;
      scroll_offset_ = 0;
      redraw_input();
      continue;
    }

    // Ctrl-x Ctrl-S: save (push to history_ as "saved")
    if (ev.is_ctrl() && ev.is(Key::S)) {
      if (!input_buf_.empty()) {
        history_.push("[saved] " + input_buf_);
      }
      input_buf_.clear();
      cursor_pos_ = 0;
      scroll_offset_ = 0;
      redraw_input();
      continue;
    }

    if (ctrl_x_mode_) {
      log_write(LEVEL_DEBUG, "Ctrl-X mode");
      // Ctrl-x Ctrl+K: kill-line (clear to end)
      if (ev.is(Key::K)) {
        input_buf_.erase(cursor_pos_);
      } else if (ev.is(Key::U)) {
        // For now, just ignore — could be used for repeat next command
      } else if (ev.is(Key::V)) {
        // For now, just ignore — could be used for paste from clipboard
      } else if (ev.is(Key::Y)) {
        // For now, just ignore — could be used for yank
      } else if (ev.is(Key::Z)) {
        // Ctrl-x Ctrl+Z: suspend (cancel and return to prompt)
        input_buf_.clear();
        cursor_pos_ = 0;
        scroll_offset_ = 0;
      } else if (ev.is(Key::K)) {
        // Ctrl-x Ctrl+K: kill-line (clear to end)
        input_buf_.erase(cursor_pos_);
      } else if (ev.is(Key::L)) {
        // Ctrl-x Ctrl+L: list-buffers (show history_)
      } else if (ev.is(Key::P)) {
        // Ctrl-x Ctrl+P: previous-history_ (go up in history_)
      } else if (ev.is(Key::R)) {
        // Ctrl-x Ctrl+R: recent-files (show recent entries)
      } else if (ev.is(Key::S)) {
        // Ctrl-x Ctrl+S: save-buffer (push to history_ as "saved")
        if (!input_buf_.empty()) {
          history_.push("[saved] " + input_buf_);
        }
        input_buf_.clear();
        cursor_pos_ = 0;
        scroll_offset_ = 0;
      } else if (ev.is(Key::T)) {
        // Ctrl-x Ctrl+T: transient-mark-mode (select region)
      } else if (ev.is(Key::U)) {
        // Ctrl-x Ctrl+U: universal-argument (repeat next command x4)
      } else if (ev.is(Key::V)) {
        // Ctrl-x Ctrl+V: insert-file (paste from clipboard)
      } else if (ev.is(Key::Y)) {
        // Ctrl-x Ctrl+Y: yank (paste last killed text)
      } else if (ev.is(Key::Z)) {
        // Ctrl-x Ctrl+Z: suspend (cancel and return to prompt)
        input_buf_.clear();
        cursor_pos_ = 0;
        scroll_offset_ = 0;
      } else {
        ctrl_x_mode_ = false;
      }
      redraw_input();
      continue;
    }

    // Standard editing
    if (ev.is(Key::BACKSPACE) || ev.is(Key::ESCAPE)) {
      if (cursor_pos_ > 0) {
        input_buf_.erase(cursor_pos_ - 1, 1); --cursor_pos_;
      }
    } else if (ev.is(Key::LEFT)) {
      if (cursor_pos_ > 0) --cursor_pos_;
    } else if (ev.is(Key::RIGHT)) {
      if (cursor_pos_ < input_buf_.size()) ++cursor_pos_;
    } else if (ev.is(Key::HOME)) {
      cursor_pos_ = 0;
    } else if (ev.is(Key::END)) {
      cursor_pos_ = input_buf_.size();
    } else if (ev.is(Key::DEL)) {
      if (cursor_pos_ < input_buf_.size()) {
        input_buf_.erase(cursor_pos_, 1);
      }
    } else if (ev.is_edit() && !ev.is_ctrl() && !ev.is_alt()) {
      // Any printable character — entering new text clears the nav draft
      // so that Down won't resurrect a stale saved buffer.
      draft.clear();
      history_.reset_nav();
      input_buf_.insert(cursor_pos_, 1, ev.val());
      ++cursor_pos_;
    } else {
      log_write(LEVEL_DEBUG, "val:%d is_shift:%d is_ctrl:%d is_edit:%d is_ascii:%d is_resize:%d",
                ev.val(), ev.is_shift(), ev.is_ctrl(), ev.is_edit(), ev.is_ascii(), ev.is_resize());
      continue;
    }
    redraw_input();
  }
}
