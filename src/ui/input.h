// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef INPUT_H
#define INPUT_H

#include <functional>
#include <string>
#include <unordered_map>

#include "input_history.h"
#include "tui_context.h"

class Input {
  public:
  explicit Input();
  ~Input() = default;
  Input(const Input &) = delete;
  Input &operator=(const Input &) = delete;

  std::string readline(TuiContext &tui);
  std::string get_input_buf() const { return input_buf_; }
  int get_scroll_offset() const { return scroll_offset_; }
  void set_scroll_offset(int offset) { scroll_offset_ = offset; }
  int get_cursor_pos() const { return cursor_pos_; }
  bool completion_active() const { return completion_active_; }
  const std::string &completion_suffix() const { return completion_suffix_; }
  void load(const std::string &path) { history_.load(path); }
  void save(const std::string &path) const { history_.save(path); }

  using Handler = std::function<bool(const InputEvent &, TuiContext &, std::string &)>;

  private:
  void build_dispatch();
  int pos_of_word_start(int pos) const;
  int pos_of_word_end(int pos) const;
  int move_to_prev_word(int pos) const;
  int move_to_next_word(int pos) const;
  int delete_word_before(int pos);
  int kill_word_backward(int pos);
  int delete_word_at_cursor();
  int uppercase_word(int pos);
  int lowercase_word(int pos);

  std::string input_buf_;
  bool mouse_mode_;
  bool insert_mode_;
  bool ctrl_x_mode_;
  int cursor_pos_;
  int scroll_offset_;
  InputHistory history_;
  std::unordered_map<std::string, Handler> dispatch_;

  // ── slash command completion ──────────────────────────────────────
  std::string completion_suffix_;
  bool completion_active_ = false;
  void update_completion();
  void accept_completion(TuiContext &tui);
  void dismiss_completion();
};

#endif
