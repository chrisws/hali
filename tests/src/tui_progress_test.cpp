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

#include "tui.h"

// Test helpers from tui_stub.cpp
void tui_stub_set_escape(bool pressed);
int tui_stub_progress_total();
int tui_stub_progress_current();
int tui_stub_progress_chunks();
bool tui_stub_progress_active();

//
// test: show_progress_modal initializes state
//
static void test_progress_show() {
  Tui tui;
  tui.show_progress_modal("Embedding", 100);

  assert(tui_stub_progress_active());
  assert(tui_stub_progress_total() == 100);
  assert(tui_stub_progress_current() == 0);
  assert(tui_stub_progress_chunks() == 0);

  std::cout << "test_progress_show passed" << std::endl;
}

//
// test: update_progress tracks current/total/chunks
//
static void test_progress_update() {
  Tui tui;
  tui.show_progress_modal("Embedding", 200);

  tui.update_progress(50, 200, 150);
  assert(tui_stub_progress_current() == 50);
  assert(tui_stub_progress_total() == 200);
  assert(tui_stub_progress_chunks() == 150);

  tui.update_progress(100, 200, 300);
  assert(tui_stub_progress_current() == 100);
  assert(tui_stub_progress_total() == 200);
  assert(tui_stub_progress_chunks() == 300);

  std::cout << "test_progress_update passed" << std::endl;
}

//
// test: dismiss_progress_modal resets state
//
static void test_progress_dismiss() {
  Tui tui;
  tui.show_progress_modal("Embedding", 50);
  tui.update_progress(25, 50, 75);

  tui.dismiss_progress_modal();

  assert(!tui_stub_progress_active());
  assert(tui_stub_progress_total() == 0);
  assert(tui_stub_progress_current() == 0);
  assert(tui_stub_progress_chunks() == 0);

  std::cout << "test_progress_dismiss passed" << std::endl;
}

//
// test: poll_escape returns false by default
//
static void test_poll_escape_default() {
  Tui tui;
  tui.show_progress_modal("Embedding", 10);
  tui_stub_set_escape(false);

  assert(!tui.poll_escape());

  std::cout << "test_poll_escape_default passed" << std::endl;
}

//
// test: poll_escape returns true when escape is pressed
//
static void test_poll_escape_pressed() {
  Tui tui;
  tui.show_progress_modal("Embedding", 10);
  tui_stub_set_escape(true);

  assert(tui.poll_escape());

  /* Reset for other tests */
  tui_stub_set_escape(false);

  std::cout << "test_poll_escape_pressed passed" << std::endl;
}

//
// test: progress bar calculation (percentage → filled bars)
//
static void test_progress_bar_math() {
  /* Simulate the bar calculation from update_progress */
  auto calc_filled = [](int bar_w, int current, int total) -> int {
    int pct = (total > 0) ? (current * 100) / total : 0;
    return (bar_w * pct) / 100;
  };

  assert(calc_filled(20, 0, 100) == 0);
  assert(calc_filled(20, 25, 100) == 5);
  assert(calc_filled(20, 50, 100) == 10);
  assert(calc_filled(20, 75, 100) == 15);
  assert(calc_filled(20, 100, 100) == 20);

  /* Edge: zero total */
  assert(calc_filled(20, 5, 0) == 0);

  /* Edge: single item */
  assert(calc_filled(10, 0, 1) == 0);
  assert(calc_filled(10, 1, 1) == 10);

  std::cout << "test_progress_bar_math passed" << std::endl;
}

//
// test: full lifecycle show → update → dismiss
//
static void test_progress_lifecycle() {
  Tui tui;

  /* Show */
  tui.show_progress_modal("Indexing", 4);
  assert(tui_stub_progress_active());
  assert(tui_stub_progress_total() == 4);

  /* Step through */
  for (int i = 1; i <= 4; i++) {
    assert(!tui.poll_escape());
    tui.update_progress(i, 4, i * 10);
    assert(tui_stub_progress_current() == i);
    assert(tui_stub_progress_chunks() == i * 10);
  }

  /* Dismiss */
  tui.dismiss_progress_modal();
  assert(!tui_stub_progress_active());

  std::cout << "test_progress_lifecycle passed" << std::endl;
}

void tui_progress_test() {
  test_progress_show();
  test_progress_update();
  test_progress_dismiss();
  test_poll_escape_default();
  test_poll_escape_pressed();
  test_progress_bar_math();
  test_progress_lifecycle();
}