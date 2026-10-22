// Test stubs for Tui methods
#include "tui.h"

void Tui::tick_spinner() {
  // no-op in tests
}

void Tui::show_tool(const std::string &msg) {
  // no-op in tests
}

bool Tui::confirm_dialog(const std::string &msg) {
  return true;
}