// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <poll.h>

#include <notcurses/notcurses.h>
#include "llama_sb.h"
#include "tui_context.h"
#include "input.h"

//
// icons
//
constexpr std::string ICON_ERR   = " ⚡ ▏";
constexpr std::string ICON_THINK = " 🤔 ▏";
constexpr std::string ICON_TOOL  = " 🔧 ▏";
constexpr std::string ICON_SYS   = " ✨ ▏";

// Theme enum for switching
enum class ThemeMode {
  DARK = 0,
  LIGHT = 1,
  NAVY = 2  // Original Hali color scheme
};

//
// Color theme system ───────────────────────────────────────────────────
// Solarized-inspired palette with Dark and Light themes
//
namespace Color {
  struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
  };

  enum class ColorElement {
    INPUT_BORDER,
    INPUT_PROMPT,
    INPUT_TEXT,
    INPUT_CURSOR,
    CHAT_BACKGROUND,
    INPUT_BACKGROUND,
    HEADER_BACKGROUND,
    HEADER_TEXT,
    POPUP_BACKGROUND,
    POPUP_BORDER,
    POPUP_TEXT,
    COLOR_CHAT_LOGO_GRADIENT,
    COLOR_CHAT_USER,
    COLOR_CHAT_HALI,
    COLOR_CHAT_SYSTEM,
    COLOR_CHAT_TOOL,
    COLOR_CHAT_ERROR,
    COLOR_CHAT_THINKING,
    COLOR_CHAT_DEFAULT,
  };
}

// Color theme base class
class ColorTheme {
  public:
  virtual ~ColorTheme() = default;
  virtual Color::RGB get_color(Color::ColorElement element) const = 0;
  virtual Color::RGB get_popup_color() const = 0;
  virtual Color::RGB get_popup_background() const = 0;
  virtual Color::RGB get_popup_border() const = 0;
};

// Solarized Dark theme
namespace Color {
  namespace Chat {
    constexpr RGB COLOR_CHAT_HALI = {180, 255, 180};
    constexpr RGB COLOR_CHAT_SYSTEM = {160,  82,  45};
    constexpr RGB COLOR_CHAT_TOOL = {255, 180,  80};
    constexpr RGB COLOR_CHAT_ERROR = {255,  80,  80};
    constexpr RGB COLOR_CHAT_THINKING = {140, 140, 200};
    constexpr RGB COLOR_CHAT_DEFAULT = {210, 210, 210};
  }

  namespace DarkTheme {
    constexpr RGB INPUT_BORDER              = {80, 120, 160};
    constexpr RGB INPUT_PROMPT              = {100, 210, 255};
    constexpr RGB INPUT_TEXT                = {230, 230, 230};
    constexpr RGB INPUT_CURSOR              = {180, 230, 255};
    constexpr RGB CHAT_BACKGROUND           = {  0, 29, 38 };
    constexpr RGB INPUT_BACKGROUND          = {  6, 35, 45 };
    constexpr RGB HEADER_BACKGROUND         = {  0, 35, 45 };
    constexpr RGB HEADER_TEXT               = {147, 161, 161};
    constexpr RGB POPUP_BACKGROUND          = { 40, 65, 74 };
    constexpr RGB POPUP_BORDER              = {181, 137,   0};
    constexpr RGB POPUP_TEXT                = {253, 246, 227};

    struct Impl : ColorTheme {
      Color::RGB get_color(Color::ColorElement element) const override {
        switch (element) {
          case Color::ColorElement::INPUT_BORDER:
            return INPUT_BORDER;
          case Color::ColorElement::INPUT_TEXT:
            return INPUT_TEXT;
          case Color::ColorElement::INPUT_PROMPT:
            return INPUT_PROMPT;
          case Color::ColorElement::INPUT_CURSOR:
            return INPUT_CURSOR;
          case Color::ColorElement::CHAT_BACKGROUND:
            return CHAT_BACKGROUND;
          case Color::ColorElement::INPUT_BACKGROUND:
            return INPUT_BACKGROUND;
          case Color::ColorElement::HEADER_BACKGROUND:
            return HEADER_BACKGROUND;
          case Color::ColorElement::HEADER_TEXT:
            return HEADER_TEXT;
          case Color::ColorElement::POPUP_BACKGROUND:
            return POPUP_BACKGROUND;
          case Color::ColorElement::POPUP_BORDER:
            return POPUP_BORDER;
          case Color::ColorElement::POPUP_TEXT:
            return POPUP_TEXT;
          case Color::ColorElement::COLOR_CHAT_LOGO_GRADIENT:
            return CHAT_BACKGROUND;
          case Color::ColorElement::COLOR_CHAT_USER:
            return INPUT_PROMPT;
          case Color::ColorElement::COLOR_CHAT_HALI:
            return Color::Chat::COLOR_CHAT_HALI;
          case Color::ColorElement::COLOR_CHAT_SYSTEM:
            return Color::Chat::COLOR_CHAT_SYSTEM;
          case Color::ColorElement::COLOR_CHAT_TOOL:
            return Color::Chat::COLOR_CHAT_TOOL;
          case Color::ColorElement::COLOR_CHAT_ERROR:
            return Color::Chat::COLOR_CHAT_ERROR;
          case Color::ColorElement::COLOR_CHAT_THINKING:
            return Color::Chat::COLOR_CHAT_THINKING;
          case Color::ColorElement::COLOR_CHAT_DEFAULT:
            return Color::Chat::COLOR_CHAT_DEFAULT;
          default:
            return INPUT_PROMPT;
        }
      }

      Color::RGB get_popup_color() const override { return POPUP_TEXT; }
      Color::RGB get_popup_background() const override { return POPUP_BACKGROUND; }
      Color::RGB get_popup_border() const override { return POPUP_BORDER; }
    };
  }

  // Solarized Light theme
  namespace LightTheme {
    constexpr RGB INPUT_BORDER              = {80, 120, 160};
    constexpr RGB INPUT_PROMPT              = {100, 210, 255};
    constexpr RGB INPUT_TEXT                = {108, 117, 125};
    constexpr RGB INPUT_CURSOR              = {180, 230, 255};
    constexpr RGB CHAT_BACKGROUND           = {253, 246, 227};
    constexpr RGB INPUT_BACKGROUND          = {238, 232, 213};
    constexpr RGB HEADER_BACKGROUND         = {238, 232, 213};
    constexpr RGB HEADER_TEXT               = {108, 117, 125};
    constexpr RGB POPUP_BACKGROUND          = {245, 245, 245};
    constexpr RGB POPUP_BORDER              = {194, 178, 128};
    constexpr RGB POPUP_TEXT                = {108, 117, 125};

    struct Impl : ColorTheme {
      Color::RGB get_color(Color::ColorElement element) const override {
        switch (element) {
          case Color::ColorElement::INPUT_BORDER:
            return INPUT_BORDER;
          case Color::ColorElement::INPUT_TEXT:
            return INPUT_TEXT;
          case Color::ColorElement::INPUT_PROMPT:
            return INPUT_PROMPT;
          case Color::ColorElement::INPUT_CURSOR:
            return INPUT_CURSOR;
          case Color::ColorElement::CHAT_BACKGROUND:
            return CHAT_BACKGROUND;
          case Color::ColorElement::INPUT_BACKGROUND:
            return INPUT_BACKGROUND;
          case Color::ColorElement::HEADER_BACKGROUND:
            return HEADER_BACKGROUND;
          case Color::ColorElement::HEADER_TEXT:
            return HEADER_TEXT;
          case Color::ColorElement::POPUP_BACKGROUND:
            return POPUP_BACKGROUND;
          case Color::ColorElement::POPUP_BORDER:
            return POPUP_BORDER;
          case Color::ColorElement::POPUP_TEXT:
            return POPUP_TEXT;
          case Color::ColorElement::COLOR_CHAT_LOGO_GRADIENT:
            return CHAT_BACKGROUND;
          case Color::ColorElement::COLOR_CHAT_USER:
            return INPUT_PROMPT;
          case Color::ColorElement::COLOR_CHAT_HALI:
            return Color::Chat::COLOR_CHAT_HALI;
          case Color::ColorElement::COLOR_CHAT_SYSTEM:
            return Color::Chat::COLOR_CHAT_SYSTEM;
          case Color::ColorElement::COLOR_CHAT_TOOL:
            return Color::Chat::COLOR_CHAT_TOOL;
          case Color::ColorElement::COLOR_CHAT_ERROR:
            return Color::Chat::COLOR_CHAT_ERROR;
          case Color::ColorElement::COLOR_CHAT_THINKING:
            return Color::Chat::COLOR_CHAT_THINKING;
          case Color::ColorElement::COLOR_CHAT_DEFAULT:
            return Color::Chat::COLOR_CHAT_DEFAULT;
          default:
            return INPUT_PROMPT;
        }
      }

      Color::RGB get_popup_color() const override { return POPUP_TEXT; }
      Color::RGB get_popup_background() const override { return POPUP_BACKGROUND; }
      Color::RGB get_popup_border() const override { return POPUP_BORDER; }
    };
  }

  // Navy theme - the original Hali color scheme
  namespace NavyTheme {
    constexpr RGB INPUT_BORDER              = {80, 120, 160};
    constexpr RGB INPUT_PROMPT              = {100, 210, 255};
    constexpr RGB INPUT_TEXT                = {230, 230, 230};
    constexpr RGB INPUT_CURSOR              = {180, 230, 255};
    constexpr RGB CHAT_BACKGROUND           = {18, 22, 30};
    constexpr RGB INPUT_BACKGROUND          = {22, 28, 38};
    constexpr RGB HEADER_BACKGROUND         = {30, 40, 55};
    constexpr RGB HEADER_TEXT               = {210, 210, 210};
    constexpr RGB POPUP_BACKGROUND          = {25, 30, 40};
    constexpr RGB POPUP_BORDER              = {80, 160, 200};
    constexpr RGB POPUP_TEXT                = {210, 210, 210};

    struct Impl : ColorTheme {
      Color::RGB get_color(Color::ColorElement element) const override {
        switch (element) {
          case Color::ColorElement::INPUT_BORDER:
            return INPUT_BORDER;
          case Color::ColorElement::INPUT_TEXT:
            return INPUT_TEXT;
          case Color::ColorElement::INPUT_PROMPT:
            return INPUT_PROMPT;
          case Color::ColorElement::INPUT_CURSOR:
            return INPUT_CURSOR;
          case Color::ColorElement::CHAT_BACKGROUND:
            return CHAT_BACKGROUND;
          case Color::ColorElement::INPUT_BACKGROUND:
            return INPUT_BACKGROUND;
          case Color::ColorElement::HEADER_BACKGROUND:
            return HEADER_BACKGROUND;
          case Color::ColorElement::HEADER_TEXT:
            return HEADER_TEXT;
          case Color::ColorElement::POPUP_BACKGROUND:
            return POPUP_BACKGROUND;
          case Color::ColorElement::POPUP_BORDER:
            return POPUP_BORDER;
          case Color::ColorElement::POPUP_TEXT:
            return POPUP_TEXT;
          case Color::ColorElement::COLOR_CHAT_LOGO_GRADIENT:
            return CHAT_BACKGROUND;
          case Color::ColorElement::COLOR_CHAT_USER:
            return INPUT_PROMPT;
          case Color::ColorElement::COLOR_CHAT_HALI:
            return Color::Chat::COLOR_CHAT_HALI;
          case Color::ColorElement::COLOR_CHAT_SYSTEM:
            return Color::Chat::COLOR_CHAT_SYSTEM;
          case Color::ColorElement::COLOR_CHAT_TOOL:
            return Color::Chat::COLOR_CHAT_TOOL;
          case Color::ColorElement::COLOR_CHAT_ERROR:
            return Color::Chat::COLOR_CHAT_ERROR;
          case Color::ColorElement::COLOR_CHAT_THINKING:
            return Color::Chat::COLOR_CHAT_THINKING;
          case Color::ColorElement::COLOR_CHAT_DEFAULT:
            return Color::Chat::COLOR_CHAT_DEFAULT;
          default:
            return INPUT_PROMPT;
        }
      }

      Color::RGB get_popup_color() const override { return POPUP_TEXT; }
      Color::RGB get_popup_background() const override { return POPUP_BACKGROUND; }
      Color::RGB get_popup_border() const override { return POPUP_BORDER; }
    };
  }
}

//
// Notcurses TUI
//
class Tui final: TuiContext {
  public:
  Tui();
  virtual ~Tui();

  // ── lifecycle ─────────────────────────────────────────────────────
  void init();
  void resize();
  bool is_escape();
  bool has_input();
  void setup_model(std::string &model_name, const LlamaMemoryInfo &mem, bool thinking);
  void tick_spinner();
  void set_thinking(bool on);
  void update_usage(float tokens_sec, const LlamaMemoryInfo &mem);
  void set_tokens_per_sec(float tokens_per_sec) { tokens_per_sec_ = tokens_per_sec; }
  void show_tool(const std::string &tool);
  int get_term_rows() const override { return term_rows_; }
  int get_term_cols() const { return term_cols_; }

  // ── draw ──────────────────────────────────────────────────────────
  void redraw_header() const;
  void redraw_chat() override;
  void redraw_input() const override;
  void redraw_all();

  // ── content helpers ───────────────────────────────────────────────
  void append_line(const std::string &line);
  void append_lines(const std::vector<std::string> &lines);
  void append_token(const std::string &token);

  // ── interaction ───────────────────────────────────────────────────
  bool confirm_dialog(const std::string &prompt) const;
  std::string readline() { return input_.readline(*this); }
  std::string save_chat(const std::string &file) const;

  // Modal popup overlay while a long operation runs.
  void show_modal_popup(const std::string &message);
  void dismiss_modal_popup();

  // ── folder picker popup ───────────────────────────────────────
  std::string file_picker(const std::string &start_dir,
                          const std::string &title_hint = "File") const;
  std::string rag_folder_picker(const std::string &start_dir) const {
    return file_picker(start_dir, "RAG Folder");
  }

  double get_tokens_per_sec() const { return tokens_per_sec_; }
  double get_kv_percent() const { return kv_percent_; }

  // TuiContext
  InputEvent get_event() const override { return InputEvent(nc_); }
  void render() override { notcurses_render(nc_); }
  void enable_mouse(bool enable) override;

  // ── history ────────────────────────────────────────────────────────
  void history_load(const std::string &path) { input_.load(path); }
  void history_save(const std::string &path) const { input_.save(path); }

  // ── search ────────────────────────────────────────────────────────
  void start_search(const std::string &term);
  void find_next();
  void find_previous();
  void clear_search();
  bool is_searching() const { return search_active_; }
  std::string get_search_term() const { return search_term_; }
  int get_match_index() const { return match_index_; }
  int get_total_matches() const { return total_matches_; }

  // ── theme management ───────────────────────────────────────────────
  void toggle_theme() override;

  private:
  // ── notcurses handles ──────────────────────────────────────────────
  notcurses *nc_;
  ncplane *stdpl_;
  ncplane *header_;
  ncplane *chatpl_;
  ncplane *inputpl_;
  ncplane *modal_plane_;

  // ── theme management ───────────────────────────────────────────────
  void set_theme(ThemeMode mode);
  void set_plane_base(ncplane *pl, Color::ColorElement elem) const;
  void set_plane_fg(ncplane *pl, Color::ColorElement elem) const;
  void set_plane_bg(ncplane *pl, Color::ColorElement elem) const;
  uint64_t chat_ch(const Color::ColorElement elem) const;
  uint64_t chat_ch(uint32_t r, uint32_t g, uint32_t b) const;
  uint64_t inp_ch(uint32_t r, uint32_t g, uint32_t b) const;
  void setup_backgrounds() const;

  // ── utils ─────────────────────────────────────────────────────────
  uint64_t get_line_color(const std::string &line) const;

  // ── status bar values ─────────────────────────────────────────────
  std::string current_model_;
  float tokens_per_sec_;
  int kv_used_;
  int kv_total_;
  float kv_percent_;
  size_t vram_used_;
  size_t vram_total_;

  // ── dimensions ────────────────────────────────────────────────────
  int term_rows_;
  int term_cols_;

  // ── chat buffer ───────────────────────────────────────────────────
  std::vector<std::string> chat_lines_;
  std::mutex lines_mutex_;

  // ── streaming accumulator ─────────────────────────────────────────
  std::string token_acc_;

  // ── thinking spinner ──────────────────────────────────────────────
  bool thinking_;
  int spinner_frame_;

  // ── input ─────────────────────────────────────────────────────────
  Input input_;

  // ── search ────────────────────────────────────────────────────────
  bool search_active_ = false;
  std::string search_term_;
  int match_index_ = 0;
  int total_matches_ = 0;
  std::vector<std::pair<int, int>> match_positions_;

  // ── theme ─────────────────────────────────────────────────────────
  ThemeMode current_theme_;
  std::unique_ptr<ColorTheme> theme_;
};

