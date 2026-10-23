// Test stubs for Tui methods
#include "tui.h"
#include "config.h"

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

void Tui::redraw_all() {
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

namespace ui {
void settings(Tui &tui, HaliConfig &cfg) {
  // no-op in tests
}

void help(Tui &tui) {
  // no-op in tests
}
}