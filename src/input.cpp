// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "input.h"
#include "logging.h"
#include "slash_commands.h"

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

Input::Input()
  : mouse_mode_(false)
  , insert_mode_(false)
  , ctrl_x_mode_(false)
  , cursor_pos_(0)
  , scroll_offset_(0) {
  build_dispatch();
}

void Input::build_dispatch() {
  dispatch_["F2"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "F2 key pressed (toggle mouse mode)");
    mouse_mode_ = !mouse_mode_;
    tui.enable_mouse(mouse_mode_);
    return true;
  };

  dispatch_["F12"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "F12 key pressed (toggle theme)");
    tui.toggle_theme();
    return true;
  };

  dispatch_["PGUP"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Page Up key pressed");
    const int term_rows = tui.get_term_rows();
    scroll_offset_ += std::max(1, term_rows - 4);
    tui.redraw_chat();
    tui.render();
    return true;
  };

  dispatch_["PGDN"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Page Down key pressed");
    const int term_rows = tui.get_term_rows();
    scroll_offset_ = std::max(0, scroll_offset_ - std::max(1, term_rows - 4));
    tui.redraw_chat();
    tui.render();
    return true;
  };

  dispatch_["SCROLL_UP"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Mouse wheel up");
    scroll_offset_ += 3;
    tui.redraw_chat();
    tui.render();
    return true;
  };

  dispatch_["SCROLL_DOWN"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Mouse wheel down");
    scroll_offset_ = std::max(0, scroll_offset_ - 3);
    tui.redraw_chat();
    tui.render();
    return true;
  };

  dispatch_["UP"] = [this](const InputEvent &, TuiContext &tui, std::string &draft) -> bool {
    log_write(LEVEL_DEBUG, "input", "Up arrow key pressed");
    std::string hist_entry;
    if (history_.up(hist_entry)) {
      if (!input_buf_.empty() && hist_entry != input_buf_) {
        draft = input_buf_;
      }
      input_buf_  = hist_entry;
      cursor_pos_ = input_buf_.size();
    }
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["DOWN"] = [this](const InputEvent &, TuiContext &tui, std::string &draft) -> bool {
    log_write(LEVEL_DEBUG, "input", "Down arrow key pressed");
    std::string hist_entry;
    if (history_.down(hist_entry)) {
      input_buf_  = hist_entry;
      cursor_pos_ = input_buf_.size();
    } else {
      input_buf_  = draft;
      cursor_pos_ = input_buf_.size();
      draft.clear();
    }
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-L"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+L key pressed (clear screen)");
    cursor_pos_ = 0;
    scroll_offset_ = 0;
    input_buf_.clear();
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-Z"] = [this](const InputEvent &, TuiContext &, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+Z key pressed (ignored)");
    return true;
  };

  dispatch_["C-LEFT"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+left pressed [%d]", cursor_pos_);
    cursor_pos_ = move_to_prev_word(cursor_pos_);
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-RIGHT"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+right pressed [%d]", cursor_pos_);
    cursor_pos_ = move_to_next_word(cursor_pos_);
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-HOME"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    cursor_pos_ = 0;
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-END"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    cursor_pos_ = input_buf_.size();
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-A"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    cursor_pos_ = 0;
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-E"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    cursor_pos_ = input_buf_.size();
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-SPACE"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+space key pressed");
    input_buf_.insert(cursor_pos_, 1, ' ');
    ++cursor_pos_;
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-BS"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+backspace key pressed");
    cursor_pos_ = delete_word_before(cursor_pos_);
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-W"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+W key pressed");
    cursor_pos_ = kill_word_backward(cursor_pos_);
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-U"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+U key pressed");
    input_buf_.erase(0, cursor_pos_);
    cursor_pos_ = 0;
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-K"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+K key pressed");
    input_buf_.erase(cursor_pos_);
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-D"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+D key pressed");
    if (cursor_pos_ < input_buf_.size()) {
      input_buf_.erase(cursor_pos_, 1);
    } else {
      cursor_pos_ = delete_word_at_cursor();
    }
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-V"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+V key pressed (enter insert mode)");
    insert_mode_ = true;
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-H"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    log_write(LEVEL_DEBUG, "input", "Ctrl+H key pressed (backspace)");
    if (cursor_pos_ > 0) {
      input_buf_.erase(cursor_pos_ - 1, 1);
      --cursor_pos_;
      tui.redraw_input();
      tui.render();
      return true;
    }
    return false;
  };

  dispatch_["M-L"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    cursor_pos_ = uppercase_word(cursor_pos_);
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["M-D"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    cursor_pos_ = lowercase_word(cursor_pos_);
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-G"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    input_buf_.clear();
    cursor_pos_ = 0;
    scroll_offset_ = 0;
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["C-S"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    if (!input_buf_.empty()) {
      history_.push("[saved] " + input_buf_);
    }
    input_buf_.clear();
    cursor_pos_ = 0;
    scroll_offset_ = 0;
    tui.redraw_input();
    tui.render();
    return true;
  };

  dispatch_["TAB"] = [this](const InputEvent &, TuiContext &tui, std::string &) -> bool {
    if (completion_active_ && !completion_suffix_.empty()) {
      input_buf_.insert(cursor_pos_, completion_suffix_);
      cursor_pos_ += static_cast<int>(completion_suffix_.size());
      dismiss_completion();
      tui.redraw_input();
      tui.render();
      return true;
    }
    // No active completion → insert a space
    input_buf_.insert(cursor_pos_, 1, ' ');
    ++cursor_pos_;
    tui.redraw_input();
    tui.render();
    return true;
  };
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
// If in the middle of a word, move to its start.
// If at the start of a word, skip spaces and move to the start of the previous word.
// If at the start of the buffer, return 0.
//
int Input::move_to_prev_word(const int pos) const {
  if (pos == 0) {
    return 0;
  }
  // Step 1: If in the middle of a word, walk back to its start.
  int start = pos;
  while (start > 0 && !isspace(static_cast<unsigned char>(input_buf_[start - 1]))) {
    --start;
  }
  // Step 2: If we're at the start of a word (or on a space), skip spaces
  // backward and then find the start of the previous word.
  if (start == pos) {
    while (start > 0 && isspace(static_cast<unsigned char>(input_buf_[start - 1]))) {
      --start;
    }
    while (start > 0 && !isspace(static_cast<unsigned char>(input_buf_[start - 1]))) {
      --start;
    }
  }
  return start;
}

//
// Move cursor to the end of the next word.
// A "word" is a contiguous run of non-whitespace characters.
// If in the middle of a word, move to its end.
// If at the end of a word (on a space), skip spaces and move to the end of the next word.
// If at the end of the buffer, return size.
//
int Input::move_to_next_word(const int pos) const {
  if (pos >= static_cast<int>(input_buf_.size())) return static_cast<int>(input_buf_.size());
  // Step 1: If in the middle of a word, walk forward to its end.
  int end = pos;
  while (end < static_cast<int>(input_buf_.size()) && !isspace(static_cast<unsigned char>(input_buf_[end]))) {
    ++end;
  }
  // Step 2: If we're at the end of a word (on a space), skip spaces
  // forward and then find the end of the next word.
  if (end == pos) {
    while (end < static_cast<int>(input_buf_.size()) && isspace(static_cast<unsigned char>(input_buf_[end]))) {
      ++end;
    }
    while (end < static_cast<int>(input_buf_.size()) && !isspace(static_cast<unsigned char>(input_buf_[end]))) {
      ++end;
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
  const int start = move_to_prev_word(pos);
  const int end = pos;
  input_buf_.erase(start, end - start);
  return start;
}

//
// Kill the word backward.
// A "word" is a contiguous run of non-whitespace characters.
// Returns the new cursor position.
//
int Input::kill_word_backward(const int pos) {
  const int start = move_to_prev_word(pos);
  const int end = pos;
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
  input_buf_.erase(start, end - start + 1);
  return start;
}

//
// Uppercase the word under the cursor (Emacs-style Alt-L).
// Returns the new cursor position.
//
int Input::uppercase_word(const int pos) {
  const int start = pos_of_word_start(pos);
  const int end = pos_of_word_end(pos);
  for (int i = start; i <= end; ++i) {
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
  for (int i = start; i <= end; ++i) {
    if (input_buf_[i] >= 'A' && input_buf_[i] <= 'Z') {
      input_buf_[i] = static_cast<char>(input_buf_[i] - ('A' - 'a'));
    }
  }
  return start;
}

//
// Update the slash command completion state.
// Called after every edit to the input buffer.
// Shows a completion suffix when:
//   - Exactly one command matches → complete the full name
//   - Multiple commands match → complete the longest common prefix
//
void Input::update_completion() {
  if (input_buf_.empty() || input_buf_[0] != '/') {
    dismiss_completion();
    return;
  }
  // Case-insensitive prefix match
  std::string lower_buf = input_buf_;
  for (auto &c : lower_buf) {
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  // Collect all matching command names
  std::vector<std::string> matches;
  for (const auto &cmd : slash_commands()) {
    std::string lower_cmd = cmd.name;
    for (auto &c : lower_cmd) {
      c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (lower_cmd.size() >= lower_buf.size() &&
        lower_cmd.substr(0, lower_buf.size()) == lower_buf) {
      matches.push_back(cmd.name);
    }
  }
  if (matches.empty()) {
    dismiss_completion();
    return;
  }
  std::string suffix;
  if (matches.size() == 1) {
    // Single match: complete the full name
    suffix = matches[0].substr(input_buf_.size());
  } else {
    // Multiple matches: find the longest common prefix
    std::string common = matches[0];
    for (size_t i = 1; i < matches.size(); ++i) {
      size_t j = 0;
      while (j < common.size() && j < matches[i].size() && common[j] == matches[i][j]) {
        ++j;
      }
      common.resize(j);
    }
    // Only show suffix if common prefix extends beyond current buffer
    if (common.size() > input_buf_.size()) {
      suffix = common.substr(input_buf_.size());
    }
  }
  if (!suffix.empty()) {
    completion_suffix_ = suffix;
    completion_active_ = true;
  } else {
    dismiss_completion();
  }
}

//
// Accept the current completion: insert the suffix into the buffer.
//
void Input::accept_completion(TuiContext &tui) {
  if (completion_active_ && !completion_suffix_.empty()) {
    input_buf_.insert(cursor_pos_, completion_suffix_);
    cursor_pos_ += static_cast<int>(completion_suffix_.size());
    dismiss_completion();
    tui.redraw_input();
    tui.render();
  }
}

//
// Clear the completion state.
//
void Input::dismiss_completion() {
  completion_suffix_.clear();
  completion_active_ = false;
}

static std::string make_key_name(const InputEvent &ev) {
  std::string s;
  if (ev.is_ctrl()) s += "C-";
  if (ev.is_alt()) s += "M-";
  if (ev.is_shift()) s += "S-";
  Key k = ev.key();
  if (k >= Key::A && k <= Key::Z) {
    s += static_cast<char>('A' + (static_cast<int>(k) - static_cast<int>(Key::A)));
  } else {
    switch (k) {
      case Key::ESCAPE:      s += "ESC"; break;
      case Key::TAB:         s += "TAB"; break;
      case Key::HOME:        s += "HOME"; break;
      case Key::END:         s += "END"; break;
      case Key::PAGE_UP:     s += "PGUP"; break;
      case Key::PAGE_DOWN:   s += "PGDN"; break;
      case Key::UP:          s += "UP"; break;
      case Key::DOWN:        s += "DOWN"; break;
      case Key::LEFT:        s += "LEFT"; break;
      case Key::RIGHT:       s += "RIGHT"; break;
      case Key::ENTER:       s += "ENTER"; break;
      case Key::SPACE:       s += "SPACE"; break;
      case Key::BACKSPACE:   s += "BS"; break;
      case Key::DEL:         s += "DEL"; break;
      case Key::F1:          s += "F1"; break;
      case Key::F2:          s += "F2"; break;
      case Key::F3:          s += "F3"; break;
      case Key::F4:          s += "F4"; break;
      case Key::F5:          s += "F5"; break;
      case Key::F12:         s += "F12"; break;
      case Key::SCROLL_UP:   s += "SCROLL_UP"; break;
      case Key::SCROLL_DOWN: s += "SCROLL_DOWN"; break;
      default:               s += std::to_string(static_cast<uint32_t>(k)); break;
    }
  }
  return s;
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
  input_buf_.clear();
  cursor_pos_ = 0;
  history_.reset_nav();
  tui.redraw_input();
  tui.render();

  std::string draft;
  for (;;) {
    auto ev = tui.get_event();
    if (ev.is(Key::ENTER) || ev.is(Key::NL) || ev.is(Key::CR)) {
      log_write(LEVEL_DEBUG, "input", "Key ENTER/NL/CR pressed");
      if (completion_active_ && !completion_suffix_.empty()) {
        input_buf_.insert(cursor_pos_, completion_suffix_);
        dismiss_completion();
      }
      std::string result = input_buf_;
      if (!result.empty()) {
        history_.push(result);
      }
      input_buf_.clear();
      cursor_pos_ = 0;
      scroll_offset_ = 0;
      tui.redraw_input();
      tui.render();
      return result;
    }

    if (ev.is_ctrl() && ev.is(Key::X)) {
      log_write(LEVEL_DEBUG, "input", "Ctrl+X key combination");
      ctrl_x_mode_ = true;
      // read next key combination
      continue;
    } else if (ctrl_x_mode_ && ev.is_ctrl()) {
      // exit control-x mode
      ctrl_x_mode_ = false;
    }

    // Insert mode handling
    if (insert_mode_) {
      log_write(LEVEL_DEBUG, "input", "Insert mode handling");
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
      tui.redraw_input();
      tui.render();
      continue;
    }

    if (ctrl_x_mode_) {
      log_write(LEVEL_DEBUG, "input", "Ctrl-X mode");
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
      tui.redraw_input();
      tui.render();
      continue;
    }

    // Dispatch table lookup
    const std::string key = make_key_name(ev);
    auto it = dispatch_.find(key);
    if (it != dispatch_.end() && it->second(ev, tui, draft)) {
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
      log_write(LEVEL_DEBUG, "input", "val:%d is_shift:%d is_ctrl:%d is_edit:%d is_ascii:%d is_resize:%d",
                ev.val(), ev.is_shift(), ev.is_ctrl(), ev.is_edit(), ev.is_ascii(), ev.is_resize());
      continue;
    }
    update_completion();
    tui.redraw_input();
    tui.render();
  }
}
