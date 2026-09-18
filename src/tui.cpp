// This file is part of Nitro
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>

#include "tui.h"
#include "string_utils.h"
#include "logging.h"

// max scan for NCTYPE_PRESS in kity-term or NCTYPE_UNKNOWN in gnome terminal
#define MAX_FIND_PRESS 4

InputEvent::InputEvent(notcurses *nc) {
  notcurses_get_blocking(nc, &in_);
  int iter = 0;
  while (in_.evtype != NCTYPE_PRESS && in_.evtype != NCTYPE_UNKNOWN && ++iter < MAX_FIND_PRESS) {
    notcurses_get_blocking(nc, &in_);
  }
}

Tui::Tui()
    : nc_(nullptr)
    , stdpl_(nullptr)
    , header_(nullptr)
    , chatpl_(nullptr)
    , inputpl_(nullptr)
    , modal_plane_(nullptr)
    , current_model_("none")
    , tokens_per_sec_(0.0f)
    , kv_used_(0)
    , kv_total_(1)
    , kv_percent_(0)
    , vram_used_(0)
    , vram_total_(1)
    , term_rows_(0)
    , term_cols_(0)
    , thinking_(false)
    , spinner_frame_(0) {
  chat_lines_.clear();
  current_theme_ = ThemeMode::DARK;
  theme_ = std::make_unique<Color::DarkTheme::Impl>();
}

Tui::~Tui() {
  if (nc_) {
    notcurses_stop(nc_);
    nc_ = nullptr;
  }
}

// ─── Theme management ───────────────────────────────────────────────────
void Tui::set_theme(ThemeMode mode) {
  current_theme_ = mode;
  switch (mode) {
    case ThemeMode::DARK:
      theme_ = std::make_unique<Color::DarkTheme::Impl>();
      break;
    case ThemeMode::LIGHT:
      theme_ = std::make_unique<Color::LightTheme::Impl>();
      break;
    case ThemeMode::NAVY:
      theme_ = std::make_unique<Color::NavyTheme::Impl>();
      break;
  }
  setup_backgrounds();
  redraw_all();
}

void Tui::toggle_theme() {
  switch (current_theme_) {
    case ThemeMode::NAVY:
      set_theme(ThemeMode::DARK);
      break;
    case ThemeMode::DARK:
      set_theme(ThemeMode::LIGHT);
      break;
    case ThemeMode::LIGHT:
      set_theme(ThemeMode::NAVY);
      break;
  }
}

// Apply foreground (text) color to a plane
void Tui::set_plane_fg(struct ncplane *pl, Color::ColorElement elem) const {
  const auto fg = theme_->get_color(elem);
  ncplane_set_fg_rgb8(pl, fg.r, fg.g, fg.b);
}

// Apply background (text) color to a plane
void Tui::set_plane_bg(struct ncplane *pl, Color::ColorElement elem) const {
  const auto bg = theme_->get_color(elem);
  ncplane_set_bg_rgb8(pl, bg.r, bg.g, bg.b);
}

uint64_t Tui::chat_ch(uint32_t r, uint32_t g, uint32_t b) const {
  const auto bg = theme_->get_color(Color::ColorElement::CHAT_BACKGROUND);
  return NCCHANNELS_INITIALIZER(r, g, b, bg.r, bg.g, bg.b);
}

uint64_t Tui::chat_ch(const Color::ColorElement elem) const {
  const auto bg = theme_->get_color(Color::ColorElement::CHAT_BACKGROUND);
  const auto fg = theme_->get_color(elem);
  return NCCHANNELS_INITIALIZER(fg.r, fg.g, fg.b, bg.r, bg.g, bg.b);
}

uint64_t Tui::inp_ch(uint32_t r, uint32_t g, uint32_t b) const {
  const auto bg = theme_->get_color(Color::ColorElement::INPUT_BACKGROUND);
  return NCCHANNELS_INITIALIZER(r, g, b, bg.r, bg.g, bg.b);
}

// Apply background color to a plane
void Tui::set_plane_base(struct ncplane *pl, Color::ColorElement elem) const {
  const auto bg = theme_->get_color(elem);
  uint64_t channels = NCCHANNELS_INITIALIZER(bg.r, bg.g, bg.b, bg.r, bg.g, bg.b);
  ncplane_set_base(pl, " ", 0, channels);
}

void Tui::setup_backgrounds() const {
  set_plane_base(stdpl_, Color::ColorElement::CHAT_BACKGROUND);
  set_plane_base(header_, Color::ColorElement::HEADER_BACKGROUND);
  set_plane_base(chatpl_, Color::ColorElement::CHAT_BACKGROUND);
  set_plane_base(inputpl_, Color::ColorElement::INPUT_BACKGROUND);
}

// ─── Tui::init ──────────────────────────────────────────────────────────
void Tui::init() {
  notcurses_options opts{};
  opts.flags = NCOPTION_SUPPRESS_BANNERS;
  nc_ = notcurses_init(&opts, nullptr);
  if (!nc_) {
    std::fputs("notcurses_init failed\n", stderr);
    std::exit(1);
  }
  stdpl_ = notcurses_stdplane(nc_);
  notcurses_term_dim_yx(nc_, reinterpret_cast<unsigned*>(&term_rows_), reinterpret_cast<unsigned*>(&term_cols_));

  ncplane_erase(stdpl_);
  ncplane_options hopt{};
  hopt.y = 0;
  hopt.x = 0;
  hopt.rows = 1;
  hopt.cols = static_cast<unsigned>(term_cols_);
  header_ = ncplane_create(stdpl_, &hopt);
  int chat_rows = std::max(1, term_rows_ - 3);
  ncplane_options copt{};
  copt.y = 1;
  copt.x = 0;
  copt.rows = static_cast<unsigned>(chat_rows); copt.cols = static_cast<unsigned>(term_cols_);
  chatpl_ = ncplane_create(stdpl_, &copt);
  ncplane_set_base(chatpl_, " ", 0, 0);  // Will be set by redraw_chat
  ncplane_options iopt{};
  iopt.y = term_rows_ - 2; iopt.x = 0;
  iopt.rows = 2; iopt.cols = static_cast<unsigned>(term_cols_);
  inputpl_ = ncplane_create(stdpl_, &iopt);
  ncplane_set_base(inputpl_, " ", 0, 0);  // Will be set by redraw_input
  notcurses_mice_enable(nc_, NCMICE_BUTTON_EVENT);
  setup_backgrounds();
  redraw_all();
}

void Tui::resize() {
  unsigned rows = 0, cols = 0;
  notcurses_stddim_yx(nc_, &rows, &cols);
  if (static_cast<int>(rows) != term_rows_ || static_cast<int>(cols) != term_cols_) {
    notcurses_term_dim_yx(nc_, reinterpret_cast<unsigned*>(&term_rows_), reinterpret_cast<unsigned*>(&term_cols_));
    ncplane_resize_simple(header_, 1, static_cast<unsigned>(term_cols_));
    int cr = std::max(1, term_rows_ - 3);
    ncplane_resize_simple(chatpl_, static_cast<unsigned>(cr), static_cast<unsigned>(term_cols_));
    ncplane_move_yx(inputpl_, term_rows_ - 2, 0);
    ncplane_resize_simple(inputpl_, 2, static_cast<unsigned>(term_cols_));
    redraw_all();
  }
}

// ─── Tui::redraw ───────────────────────────────────────────────────────
void Tui::redraw_header() const {
  ncplane_erase(header_);

  float kv_pct   = kv_total_   > 0 ? 100.f * static_cast<float>(kv_used_)   / static_cast<float>(kv_total_)   : 0.f;
  float vram_pct = vram_total_  > 0 ? 100.f * static_cast<float>(vram_used_) / static_cast<float>(vram_total_) : 0.f;

  static const char *const SPIN[] = { "⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷" };
  const char *spin_str = thinking_ ? SPIN[spinner_frame_ % 8] : " ";
  char buf[512];
  int n = std::snprintf(buf, sizeof(buf),
                        " ✦ NITRO  │ %-32s │ %5.1f tok/s │ KV %4.1f%%  VRAM %4.1f%%  %s",
                        current_model_.c_str(), static_cast<double>(tokens_per_sec_),
                        static_cast<double>(kv_pct), static_cast<double>(vram_pct), spin_str);
  if (n > term_cols_) buf[term_cols_] = '\0';

  // Apply header text color from theme
  set_plane_fg(header_, Color::ColorElement::HEADER_TEXT);
  ncplane_putstr_yx(header_, 0, 0, buf);
}

uint64_t Tui::get_line_color(const std::string &line) const {
  uint64_t result;
  if (line.rfind("[logo_", 0) == 0 && line.size() > 7 && line[7] == ']') {
    // Gradient logo: extract row number and use gradient colors
    int logo_row = line[6] - '0';
    static const uint32_t GRAD_R[] = {  0,  20,  60, 120, 180, 210, 220 };
    static const uint32_t GRAD_G[] = { 230, 255, 255, 255, 200, 130,  80 };
    static const uint32_t GRAD_B[] = { 255, 200, 140,  80, 100, 200, 255 };
    int gi = std::max(0, std::min(logo_row, 6));
    result = chat_ch(GRAD_R[gi], GRAD_G[gi], GRAD_B[gi]);
  } else if (line.rfind("You: ",    0) == 0) {
    result = chat_ch(Color::ColorElement::COLOR_CHAT_USER);
  } else if (line.rfind("Nitro: ",  0) == 0) {
    result = chat_ch(Color::ColorElement::COLOR_CHAT_NITRO);
  } else if (line.rfind(ICON_SYS,   0) == 0) {
    result = chat_ch(Color::ColorElement::COLOR_CHAT_SYSTEM);
  } else if (line.rfind(ICON_TOOL,  0) == 0) {
    result = chat_ch(Color::ColorElement::COLOR_CHAT_TOOL);
  } else if (line.rfind(ICON_ERR,   0) == 0) {
    result = chat_ch(Color::ColorElement::COLOR_CHAT_ERROR);
  } else if (line.rfind(ICON_THINK, 0) == 0) {
    result = chat_ch(Color::ColorElement::COLOR_CHAT_THINKING);
  } else {
    result = chat_ch(Color::ColorElement::COLOR_CHAT_DEFAULT);
  }
  return result;
}

void Tui::redraw_chat() {
  ncplane_erase(chatpl_);
  unsigned rows, cols;
  ncplane_dim_yx(chatpl_, &rows, &cols);

  std::lock_guard lk(lines_mutex_);

  struct VisualLine {
    std::string text;
    uint64_t    ch;
  };

  std::vector<VisualLine> visual;
  visual.reserve(chat_lines_.size());

  for (const std::string &line : chat_lines_) {
    if (utils::is_blank(line)) {
      continue;
    }
    const auto ch = get_line_color(line);
    const auto display = (line.rfind("[logo_", 0) == 0 && line.size() > 8) ? line.substr(8) : line;
    for (const auto &segment : utils::split_utf8_string(display, cols)) {
      visual.push_back({segment + "\n", ch});
    }
  }

  const int total   = static_cast<int>(visual.size());
  const int visible = static_cast<int>(rows);
  const int start   = std::max(0, total - visible - input_.get_scroll_offset());
  const int end     = std::min(total, start + visible);

  for (int i = start, row = 0; i < end; ++i, ++row) {
    ncplane_set_channels(chatpl_, visual[i].ch);
    const int result = ncplane_puttext(chatpl_, row, NCALIGN_LEFT, visual[i].text.c_str(), nullptr);
    if (result > 0 && result > cols) {
      log_write(LEVEL_INFO, "ncplane_puttext wrapped text row:%d cols:%d result:%d", row, cols, result);
      const int extra = (result / cols);
      row += extra;
    }
  }
}

void Tui::redraw_input() const {
  ncplane_erase(inputpl_);
  set_plane_bg(inputpl_, Color::ColorElement::INPUT_BACKGROUND);

  if (thinking_) {
    static constexpr const char *BLOCKS[] = { "-", "~", "≈", "~", "-" };
    static constexpr int    N_BLOCKS = 5;
    static constexpr double FREQ     = 0.25;  // gentler wave
    static constexpr double SPEED    = 0.15;  // slower scroll
    static constexpr int    DELAY    = 12;    // frames before animation starts

    if (spinner_frame_ < DELAY) {
      // still just a plain separator during the pause
      ncplane_set_channels(inputpl_, inp_ch(80, 120, 160));
      std::string sep(term_cols_, '-');
      ncplane_putstr_yx(inputpl_, 0, 0, sep.c_str());
    } else {
      int frame = spinner_frame_ - DELAY;  // animation frame relative to start
      for (int col = 0; col < term_cols_; ++col) {
        double phase = (col * FREQ) - (frame * SPEED);
        int idx = static_cast<int>(((std::sin(phase) + 1.0) * 0.5 * (N_BLOCKS - 1)));
        idx = std::max(0, std::min(idx, N_BLOCKS - 1));
        // subtle brightness shift — blue-grey, not full glow
        int brightness = 80 + idx * 20;
        ncplane_set_channels(inputpl_, inp_ch(brightness, brightness + 20, brightness + 40));
        ncplane_putstr_yx(inputpl_, 0, col, BLOCKS[idx]);
      }
    }
    ncplane_set_channels(inputpl_, inp_ch(140, 140, 180));
    ncplane_putstr_yx(inputpl_, 1, 2, "thinking…");
  } else {
    // draw the border
    set_plane_fg(inputpl_, Color::ColorElement::INPUT_BORDER);
    std::string sep(term_cols_, '-');
    ncplane_putstr_yx(inputpl_, 0, 0, sep.c_str());

    // draw the prompt character
    const std::string prompt = " ❯ ";
    const int prompt_cols = 4;
    set_plane_fg(inputpl_, Color::ColorElement::INPUT_PROMPT);
    ncplane_putstr_yx(inputpl_, 1, 0, prompt.c_str());

    // draw the input text before the cursor
    int max_w = std::max(0, term_cols_ - prompt_cols - 1);
    std::string visible = input_.get_input_buf();
    int view_offset = 0;
    if (visible.size() > max_w && max_w > 0) {
      view_offset = static_cast<int>(visible.size() - max_w);
      visible = visible.substr(view_offset);
    }
    int cur_in_view = std::max(0, static_cast<int>(input_.get_cursor_pos() - view_offset));
    cur_in_view = std::min(cur_in_view, static_cast<int>(visible.size()));
    std::string before = visible.substr(0, cur_in_view);
    std::string after  = cur_in_view < static_cast<int>(visible.size()) ? visible.substr(cur_in_view + 1) : "";
    char cursor_ch_val = cur_in_view < static_cast<int>(visible.size()) ? visible[cur_in_view] : ' ';
    set_plane_fg(inputpl_, Color::ColorElement::INPUT_TEXT);
    ncplane_putstr_yx(inputpl_, 1, prompt_cols, before.c_str());

    // draw the cursor
    int cx = prompt_cols + cur_in_view;
    set_plane_fg(inputpl_, Color::ColorElement::INPUT_BACKGROUND);
    set_plane_bg(inputpl_, Color::ColorElement::INPUT_CURSOR);
    char cbuf[2] = { cursor_ch_val, '\0' };
    ncplane_putstr_yx(inputpl_, 1, cx, cbuf);

    // reset colors
    set_plane_bg(inputpl_, Color::ColorElement::INPUT_BACKGROUND);
    set_plane_fg(inputpl_, Color::ColorElement::INPUT_TEXT);

    // draw any text following the cursor
    if (!after.empty()) {
      ncplane_putstr_yx(inputpl_, 1, cx + 1, after.c_str());
    }
  }
}

void Tui::redraw_all() {
  redraw_header();
  redraw_chat();
  redraw_input();
  notcurses_render(nc_);
}

void Tui::tick_spinner() {
  ++spinner_frame_;
  redraw_header();
  redraw_input();
  notcurses_render(nc_);
}

void Tui::set_thinking(bool on) {
  thinking_ = on;
  if (!on) spinner_frame_ = 0;
  redraw_header();
  redraw_input();
  notcurses_render(nc_);
}

void Tui::update_usage(float tokens_sec, const LlamaMemoryInfo &mem) {
  tokens_per_sec_ = tokens_sec;
  kv_used_    = mem.kv_used;
  kv_total_   = mem.kv_total;
  kv_percent_ = mem.kv_percent;
  vram_used_  = mem.vram_used;
  vram_total_ = mem.vram_total;
}

//
// TuiState content helpers
//
void Tui::append_line(const std::string &line) {
  std::lock_guard<std::mutex> lk(lines_mutex_);
  chat_lines_.push_back(line);
}

void Tui::append_lines(const std::vector<std::string> &lines) {
  for (const auto &line : lines) {
    append_line(line);
  }
  redraw_all();
}

void Tui::append_token(const std::string &token) {
  append_line(token);
  redraw_chat();
  notcurses_render(nc_);
}

//
// Creates a centred floating plane with a border and a status message.
// The popup sits above all other planes and blocks until explicitly dismissed.
//
void Tui::show_modal_popup(const std::string &message) {
  // Dismiss any previous popup first.
  dismiss_modal_popup();

  // Clamp popup size to terminal.
  int popup_w = std::min(static_cast<int>(message.size()) + 8, term_cols_ - 4);
  popup_w = std::max(popup_w, 20);
  int popup_h = 5;
  int py = std::max(0, (term_rows_ - popup_h) / 2);
  int px = std::max(0, (term_cols_ - popup_w) / 2);

  ncplane_options opts{};
  opts.y    = py; opts.x    = px;
  opts.rows = static_cast<unsigned>(popup_h);
  opts.cols = static_cast<unsigned>(popup_w);
  modal_plane_ = ncplane_create(stdpl_, &opts);
  if (!modal_plane_) return;

  // Background: deep navy.
  static constexpr uint32_t PBG_R = 20, PBG_G = 28, PBG_B = 50;
  ncplane_set_base(modal_plane_, " ", 0,
                   NCCHANNELS_INITIALIZER(PBG_R, PBG_G, PBG_B, PBG_R, PBG_G, PBG_B));
  ncplane_erase(modal_plane_);

  // Border — bright cyan.
  uint64_t border_ch = NCCHANNELS_INITIALIZER(80, 220, 255, PBG_R, PBG_G, PBG_B);
  ncplane_set_channels(modal_plane_, border_ch);

  // Draw corners and edges manually so we don't require nccell border helpers.
  // Top row
  ncplane_putstr_yx(modal_plane_, 0, 0, "╔");
  for (int c = 1; c < popup_w - 1; ++c)
    ncplane_putstr_yx(modal_plane_, 0, c, "═");
  ncplane_putstr_yx(modal_plane_, 0, popup_w - 1, "╗");
  // Middle rows
  for (int r = 1; r < popup_h - 1; ++r) {
    ncplane_putstr_yx(modal_plane_, r, 0, "║");
    ncplane_putstr_yx(modal_plane_, r, popup_w - 1, "║");
  }
  // Bottom row
  ncplane_putstr_yx(modal_plane_, popup_h - 1, 0, "╚");
  for (int c = 1; c < popup_w - 1; ++c)
    ncplane_putstr_yx(modal_plane_, popup_h - 1, c, "═");
  ncplane_putstr_yx(modal_plane_, popup_h - 1, popup_w - 1, "╝");

  // Title bar.
  uint64_t title_ch = NCCHANNELS_INITIALIZER(255, 220, 80, PBG_R, PBG_G, PBG_B);
  ncplane_set_channels(modal_plane_, title_ch);
  ncplane_putstr_yx(modal_plane_, 1, 2, "⏳ Loading…");

  // Message.
  uint64_t msg_ch = NCCHANNELS_INITIALIZER(200, 200, 200, PBG_R, PBG_G, PBG_B);
  ncplane_set_channels(modal_plane_, msg_ch);
  // Truncate message to fit inside border.
  int max_msg = popup_w - 4;
  std::string display = message.size() > static_cast<size_t>(max_msg)
    ? message.substr(0, max_msg)
    : message;
  ncplane_putstr_yx(modal_plane_, 2, 2, display.c_str());

  notcurses_render(nc_);
}

void Tui::dismiss_modal_popup() {
  if (modal_plane_) {
    ncplane_destroy(modal_plane_);
    modal_plane_ = nullptr;
    notcurses_render(nc_);
  }
}

//
// ─── Tui::file_picker ────────────────────────────────────────────────
// Interactive directory/file browser popup.
// Keyboard:  ↑/↓ navigate,  Enter select/descend,  Backspace go up,
//            's' select current dir for indexing,   Esc cancel.
// Returns the chosen path or "" on cancel.
// ─── Tui::file_picker ────────────────────────────────────────────────
// Unified interactive directory/file browser used by /rag, /model, /embed.
// title_hint appears in the popup header (e.g. "RAG Folder", "Model File").
//
// Keyboard:
//   ↑/↓        navigate list
//   Enter      descend into directory, or select a file
//   Backspace  go up one directory
//   s          select the current directory itself (useful for /rag)
//   Esc        cancel → returns ""
//
// Returns the chosen path, or "" on cancel.
//
std::string Tui::file_picker(const std::string &start_dir,
                             const std::string &title_hint) const {
  std::string current_dir = start_dir;
  {
    std::error_code ec;
    auto canon = fs::canonical(start_dir, ec);
    if (!ec) current_dir = canon.string();
  }
  auto load_entries = [](const std::string &dir,
                         std::vector<std::string> &entries)
  {
    entries.clear();
    std::error_code ec;
    if (fs::path(dir).has_parent_path() &&
        fs::path(dir) != fs::path(dir).root_path())
      entries.emplace_back("..");
    std::vector<std::string> dirs, files;
    for (const auto &e : fs::directory_iterator(dir, ec)) {
      if (ec) break;
      std::string name = e.path().filename().string();
      if (name.empty() || name[0] == '.') continue;
      if (e.is_directory()) dirs.push_back(name);
      else                  files.push_back(name);
    }
    ranges::sort(dirs);
    ranges::sort(files);
    for (auto &d : dirs)  entries.push_back(d + "/");
    for (auto &f : files) entries.push_back(f);
  };

  std::vector<std::string> entries;
  int selected = 0;
  int scroll   = 0;

  // Popup dimensions.
  static constexpr int PW = 60;
  static constexpr int PH = 20;
  int py = std::max(0, (term_rows_ - PH) / 2);
  int px = std::max(0, (term_cols_ - PW) / 2);

  ncplane_options opts{};
  opts.y = py; opts.x = px;
  opts.rows = static_cast<unsigned>(PH); opts.cols = static_cast<unsigned>(PW);
  struct ncplane *picker = ncplane_create(stdpl_, &opts);
  if (!picker) return "";

  static constexpr uint32_t PBG_R = 18, PBG_G = 24, PBG_B = 40;
  ncplane_set_base(picker, " ", 0, NCCHANNELS_INITIALIZER(PBG_R, PBG_G, PBG_B, PBG_R, PBG_G, PBG_B));
  // Build a compact hint line appropriate to the operation.
  // /rag adds 's=select dir'; /model and /embed only need file selection.
  std::string hint_line = "↑↓ navigate  Enter open/select  Esc cancel";
  if (title_hint.find("RAG") != std::string::npos ||
      title_hint.find("Folder") != std::string::npos) {
    hint_line = "↑↓ navigate  Enter open  s=select dir  Esc cancel";
  }
  auto draw_picker = [&]() {
    ncplane_erase(picker);
    uint64_t border_ch = NCCHANNELS_INITIALIZER(100, 180, 255, PBG_R, PBG_G, PBG_B);
    ncplane_set_channels(picker, border_ch);
    ncplane_putstr_yx(picker, 0, 0, "╔");
    for (int c = 1; c < PW - 1; ++c) ncplane_putstr_yx(picker, 0, c, "═");
    ncplane_putstr_yx(picker, 0, PW - 1, "╗");
    for (int r = 1; r < PH - 1; ++r) {
      ncplane_putstr_yx(picker, r, 0,      "║");
      ncplane_putstr_yx(picker, r, PW - 1, "║");
    }
    ncplane_putstr_yx(picker, PH - 1, 0, "╚");
    for (int c = 1; c < PW - 1; ++c) ncplane_putstr_yx(picker, PH - 1, c, "═");
    ncplane_putstr_yx(picker, PH - 1, PW - 1, "╝");

    // Title
    ncplane_set_channels(picker, NCCHANNELS_INITIALIZER(255, 220, 80, PBG_R, PBG_G, PBG_B));
    std::string title_str = " 📂 " + title_hint + " Picker ";
    if (static_cast<int>(title_str.size()) > PW - 4) title_str = title_str.substr(0, PW - 4);
    ncplane_putstr_yx(picker, 0, 2, title_str.c_str());
    // Current path (truncated).
    std::string path_display = current_dir;
    if (static_cast<int>(path_display.size()) > PW - 4) {
      path_display = "…" + path_display.substr(path_display.size() - (PW - 5));
    }
    ncplane_set_channels(picker, NCCHANNELS_INITIALIZER(160, 200, 240, PBG_R, PBG_G, PBG_B));
    ncplane_putstr_yx(picker, 1, 2, path_display.c_str());
    // Hint line (bottom interior row).
    ncplane_set_channels(picker, NCCHANNELS_INITIALIZER(120, 120, 160, PBG_R, PBG_G, PBG_B));
    std::string hint_trunc = hint_line;
    if (static_cast<int>(hint_trunc.size()) > PW - 4) hint_trunc = hint_trunc.substr(0, PW - 4);
    ncplane_putstr_yx(picker, PH - 2, 2, hint_trunc.c_str());
    // Entry list.
    int list_rows = PH - 5;
    if (selected < scroll) {
      scroll = selected;
    }
    if (selected >= scroll + list_rows) {
      scroll = selected - list_rows + 1;
    }
    for (int i = 0; i < list_rows; ++i) {
      int idx = scroll + i;
      if (idx >= static_cast<int>(entries.size())) break;
      bool is_selected = (idx == selected);
      bool is_dir = !entries[idx].empty() && entries[idx].back() == '/';
      uint32_t fr, fg, fb;
      if (is_selected)  { fr = 20;  fg = 20;  fb = 20;  }
      else if (is_dir)   { fr = 120; fg = 200; fb = 255; }
      else               { fr = 200; fg = 200; fb = 200; }
      uint32_t br = is_selected ? 100 : PBG_R;
      uint32_t bg = is_selected ? 180 : PBG_G;
      uint32_t bb = is_selected ? 255 : PBG_B;
      ncplane_set_channels(picker, NCCHANNELS_INITIALIZER(fr, fg, fb, br, bg, bb));
      std::string label = (is_selected ? " ▶ " : "   ") + entries[idx];
      if (static_cast<int>(label.size()) > PW - 2) label = label.substr(0, PW - 2);
      while (static_cast<int>(label.size()) < PW - 2) label += ' ';
      ncplane_putstr_yx(picker, 2 + i, 1, label.c_str());
    }
    notcurses_render(nc_);
  };

  std::string result;
  load_entries(current_dir, entries);
  draw_picker();

  for (;;) {
    InputEvent ev = get_event();
    if (ev.is(Key::ESCAPE)) {
      // cancelled
      break;
    }
    if (ev.is(Key::UP)) {
      if (selected > 0) --selected;
      draw_picker();
      continue;
    }
    if (ev.is(Key::DOWN)) {
      if (selected + 1 < static_cast<int>(entries.size())) {
        ++selected;
      }
      draw_picker();
      continue;
    }
    // 's' — select the current directory (useful for /rag, ignored for file pickers).
    if (ev.is(Key::S)) {
      // Select current directory for RAG indexing.
      result = current_dir;
      break;
    }
    if (ev.is(Key::BACKSPACE) || ev.is(Key::ESCAPE)) {
      // Go up one level.
      fs::path p(current_dir);
      if (p.has_parent_path() && p != p.root_path()) {
        current_dir = p.parent_path().string();
        load_entries(current_dir, entries);
        selected = 0; scroll = 0;
        draw_picker();
      }
      continue;
    }
    if (ev.is(Key::ENTER) || ev.is(Key::NL) || ev.is(Key::CR)) {
      if (entries.empty()) {
        continue;
      }
      const std::string &entry = entries[selected];
      if (entry == "..") {
        fs::path p(current_dir);
        if (p.has_parent_path() && p != p.root_path()) {
          current_dir = p.parent_path().string();
          load_entries(current_dir, entries);
          selected = 0;
          scroll = 0;
          draw_picker();
        }
      } else if (!entry.empty() && entry.back() == '/') {
        current_dir += "/" + entry.substr(0, entry.size() - 1);
        std::error_code ec;
        auto canon = fs::canonical(current_dir, ec);
        if (!ec) current_dir = canon.string();
        load_entries(current_dir, entries);
        selected = 0;
        scroll = 0;
        draw_picker();
      } else {
        result = current_dir + "/" + entry;
        break;
      }
      continue;
    }
  }

  ncplane_destroy(picker);
  notcurses_render(nc_);
  return result;
}

//
// ─── Tui::confirm_dialog ─────────────────────────────────────────────
//
bool Tui::confirm_dialog(const std::string &prompt) const {
  ncplane_erase(inputpl_);
  ncplane_set_channels(inputpl_, inp_ch(255, 200, 80));
  std::string msg = " " + prompt + " [y/n] ❯ ";
  ncplane_putstr_yx(inputpl_, 1, 0, msg.c_str());
  notcurses_render(nc_);
  std::string answer;
  for (;;) {
    InputEvent ev = get_event();
    if (ev.is(Key::ENTER) || ev.is(Key::NL) || ev.is(Key::CR)) {
      break;
    }
    if (ev.is(Key::BACKSPACE) && !answer.empty()) {
      answer.pop_back();
    } else if (ev.is_ascii()) {
      answer += static_cast<char>(ev.key());
    }
    ncplane_erase(inputpl_);
    ncplane_set_channels(inputpl_, inp_ch(255, 200, 80));
    ncplane_putstr_yx(inputpl_, 1, 0, (msg + answer).c_str());
    notcurses_render(nc_);
  }
  std::string lo = answer;
  ranges::transform(lo, lo.begin(), ::tolower);
  redraw_input();
  notcurses_render(nc_);
  return (lo == "y" || lo == "yes" || lo == "sure" || lo == "k");
}

bool Tui::has_input() {
  // 10ms block
  int fd = notcurses_inputready_fd(nc_);
  struct pollfd pfd = { .fd = fd, .events = POLLIN };
  return poll(&pfd, 1, 10) > 0 && (pfd.revents & POLLIN);
}

bool Tui::is_escape() {
  ncinput ni{};
  notcurses_get_nblock(nc_, &ni);
  if (ni.id == NCKEY_ESC) {
    set_thinking(false);
    append_line(ICON_ERR + "Generation cancelled by user (Escape)");
    redraw_all();
  }
  return ni.id == NCKEY_ESC;
}

void Tui::setup_model(std::string &model_name, const LlamaMemoryInfo &mem, bool thinking) {
  update_usage(0.0f, mem);
  current_model_ = model_name;
  append_line(ICON_SYS + "Model ready: " + current_model_);
  append_line(ICON_SYS + "" + mem.advice);
  append_line(ICON_SYS + "Thinking mode: " + (thinking ? "enabled" : "disabled"));
  redraw_all();
}

void Tui::enable_mouse(bool enable) {
  if (enable) {
    notcurses_mice_enable(nc_, NCMICE_BUTTON_EVENT);
  } else {
    notcurses_mice_disable(nc_);
  }
}

void Tui::show_tool(const std::string &tool) {
  append_line(ICON_TOOL + "→ " + tool);
  redraw_all();
};

std::string Tui::save_chat(const std::string &file) const {
  // Check if file exists
  std::error_code ec;
  if (fs::exists(file, ec) && !ec) {
    return std::format("Cannot save, file exists");
  }

  // Filter lines to remove icons at the start
  std::vector<std::string> filtered;
  filtered.reserve(chat_lines_.size());

  for (const std::string &line : chat_lines_) {
    if (line.empty() == false &&
        line.rfind(ICON_SYS,   0) != 0 &&
        line.rfind("[logo_",   0) != 0) {
      const auto text = utils::trim(line);
      if (!utils::is_blank(text)) {
        filtered.push_back(text);
      }
    }
  }

  // Save to file
  std::ofstream ofs(file);
  if (!ofs) {
    return std::format("Failed to save chat to {}", file);
  }

  for (const std::string &line : filtered) {
    ofs << line << "\n";
  }

  ofs.close();
  return std::format("Transcript saved to {}", file);
}

