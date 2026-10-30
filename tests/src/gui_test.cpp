// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <iostream>
#include <cassert>
#include <string>

#include "webview.h"
#include "config.h"

using namespace std;

static void test_broadcast_and_get() {
  webview::stop();  // clean state
  webview::start("/tmp", 19080);

  assert(webview::is_running());
  assert(!webview::has_message());
  assert(webview::get_message() == "");

  webview::broadcast_message("<h1>Hello</h1>");
  assert(webview::has_message());
  assert(webview::get_message() == "<h1>Hello</h1>");
  assert(!webview::has_message());

  webview::stop();
  assert(!webview::is_running());
  cout << "test_broadcast_and_get passed" << endl;
}

static void test_multiple_messages() {
  webview::stop();
  webview::start("/tmp", 19080);

  webview::broadcast_message("first");
  webview::broadcast_message("second");
  webview::broadcast_message("third");

  assert(webview::has_message());
  assert(webview::get_message() == "first");
  assert(webview::get_message() == "second");
  assert(webview::get_message() == "third");
  assert(!webview::has_message());

  webview::stop();
  cout << "test_multiple_messages passed" << endl;
}

static void test_clear_signal() {
  webview::stop();
  webview::start("/tmp", 19080);

  webview::broadcast_message("clear");
  assert(webview::has_message());
  assert(webview::get_message() == "clear");

  webview::stop();
  cout << "test_clear_signal passed" << endl;
}

static void test_config_prompt_has_gui() {
  HaliConfig cfg;
  string prompt = cfg.build_system_prompt();
  assert(prompt.find("TOOL:GUI") != string::npos);
  assert(prompt.find("display HTML, check input, or clear browser") != string::npos);
  cout << "test_config_prompt_has_gui passed" << endl;
}

void gui_test() {
  test_broadcast_and_get();
  test_multiple_messages();
  test_clear_signal();
  test_config_prompt_has_gui();
}