// Test stubs for Tui methods
#include "tui.h"
#include "config.h"

Tui::Tui() = default;
Tui::~Tui() = default;

InputEvent::InputEvent(notcurses *) {
  // no-op in tests
}

void Tui::redraw_input() const {
  // no-op in tests
}

void Tui::enable_mouse(bool) {
  // no-op in tests
}

void Tui::redraw_chat() {
  // no-op in tests
}

void Tui::tick_spinner() {
  // no-op in tests
}

void Tui::show_tool(const std::string &msg) {
  // no-op in tests
}

bool Tui::confirm_dialog(const std::string &msg) {
  return true;
}

void Tui::append_line(const std::string &line) {
  // no-op in tests
}

void Tui::append_token(const std::string &token) {
  // no-op in tests
}

void Tui::append_lines(const std::vector<std::string> &lines) {
  // no-op in tests
}

void Tui::set_thinking(bool) {
  // no-op in tests
}

void Tui::redraw_all() {
  // no-op in tests
}

void Tui::show_modal_popup(const std::string &, const std::string &) {
  // no-op in tests
}

void Tui::dismiss_modal_popup() {
  // no-op in tests
}

void Tui::toggle_theme() {
  // no-op in tests
}

std::string Tui::save_chat(const std::string &path) const {
  return "";
}

std::string Tui::save_session(const std::string &file) const {
  return "";
}

std::string Tui::file_picker(const std::string &sandbox, const std::string &title) {
  return "";
}

int Tui::list_picker(const std::vector<std::string> &items, const std::string &title_hint) {
  return -1;
}

std::string Tui::text_editor(const std::string &title, const std::string &initial_text) {
  return "";
}

// ── progress modal stubs (track state for tests) ──────────────
static int stub_progress_total = 0;
static int stub_progress_current = 0;
static int stub_progress_chunks = 0;
static bool stub_progress_active = false;
static bool stub_escape_pressed = false;

void Tui::show_progress_modal(const std::string &title, int total) {
  stub_progress_total = total;
  stub_progress_current = 0;
  stub_progress_chunks = 0;
  stub_progress_active = true;
  stub_escape_pressed = false;
}

void Tui::update_progress(int current, int total, int chunks) {
  stub_progress_current = current;
  stub_progress_total = total;
  stub_progress_chunks = chunks;
}

void Tui::dismiss_progress_modal() {
  stub_progress_active = false;
  stub_progress_total = 0;
  stub_progress_current = 0;
  stub_progress_chunks = 0;
}

bool Tui::poll_escape() {
  return stub_escape_pressed;
}

// Test helpers (declared in tui_progress_test.cpp)
void tui_stub_set_escape(bool pressed) {
  stub_escape_pressed = pressed;
}
int tui_stub_progress_total() { return stub_progress_total; }
int tui_stub_progress_current() { return stub_progress_current; }
int tui_stub_progress_chunks() { return stub_progress_chunks; }
bool tui_stub_progress_active() { return stub_progress_active; }

namespace ui {
void settings(Tui &tui, HaliConfig &cfg) {
  // no-op in tests
}

void help(Tui &tui) {
  // no-op in tests
}
}
void Tui::set_spinner_label(const std::string &) {
  // no-op in tests
}