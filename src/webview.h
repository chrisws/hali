// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org

#pragma once

#include <string>

namespace webview {
  bool start(const std::string &root, int port);
  void broadcast_message(const std::string &message);
  void broadcast_reload();
  void stop();
  bool is_running();
  bool has_message();
  std::string get_message();
}
