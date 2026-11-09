// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
// Unit tests for LlamaIter::_ignore_stop flag behavior

#include <cassert>
#include <cstdio>
#include "llama_sb.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
  do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
  } while (0)

// --- test cases -----------------------------------------------------------

static bool test_default_ignore_stop_false() {
  LlamaIter iter;
  assert(iter._ignore_stop == false);
  return true;
}

static bool test_default_has_next_false() {
  LlamaIter iter;
  assert(iter._has_next == false);
  return true;
}

static bool test_set_ignore_stop_true() {
  LlamaIter iter;
  iter._ignore_stop = true;
  assert(iter._ignore_stop == true);
  assert(iter._has_next == false);  // independent of _ignore_stop
  return true;
}

static bool test_set_ignore_stop_back_false() {
  LlamaIter iter;
  iter._ignore_stop = true;
  iter._ignore_stop = false;
  assert(iter._ignore_stop == false);
  return true;
}

static bool test_move_preserves_ignore_stop_true() {
  LlamaIter a;
  a._ignore_stop = true;
  a._has_next = false;
  a._tokens_generated = 42;
  LlamaIter b(std::move(a));
  assert(b._ignore_stop == true);
  assert(b._has_next == false);
  assert(b._tokens_generated == 42);
  return true;
}

static bool test_move_preserves_ignore_stop_false() {
  LlamaIter a;
  // default: _ignore_stop is false
  a._tokens_generated = 7;
  LlamaIter b(std::move(a));
  assert(b._ignore_stop == false);
  assert(b._tokens_generated == 7);
  return true;
}

static bool test_ignore_stop_independent_of_has_next() {
  LlamaIter iter;
  // Both start false by default
  assert(iter._ignore_stop == false);
  assert(iter._has_next == false);
  // Set _ignore_stop without affecting _has_next
  iter._ignore_stop = true;
  assert(iter._has_next == false);
  // Set _has_next without affecting _ignore_stop
  iter._has_next = true;
  assert(iter._ignore_stop == true);
  // Reset both
  iter._ignore_stop = false;
  iter._has_next = false;
  assert(iter._ignore_stop == false);
  assert(iter._has_next == false);
  return true;
}

static bool test_ignore_stop_with_tail() {
  // Simulate the scenario from the bug: during fetch_tool(),
  // _ignore_stop is true so stop sequences in _tail are ignored.
  // We verify the flag state that token_to_string would check.
  LlamaIter iter;
  iter._has_next = true;
  iter._ignore_stop = true;
  iter._tail = "some content with HALI_END_TOOL in it";
  // The guard in token_to_string is:
  //   if (iter._has_next && !iter._ignore_stop && !_stop_sequences.empty())
  // With _ignore_stop=true, this condition is false, so _tail
  // would NOT be checked for stop sequences.
  assert(iter._has_next == true);
  assert(iter._ignore_stop == true);
  // The stop-sequence check would be skipped
  bool would_check_stop = iter._has_next && !iter._ignore_stop;
  assert(would_check_stop == false);
  return true;
}

static bool test_ignore_stop_false_allows_stop_check() {
  // With _ignore_stop=false (default) and _has_next=true,
  // the stop-sequence check IS active
  LlamaIter iter;
  iter._has_next = true;
  assert(iter._ignore_stop == false);
  bool would_check_stop = iter._has_next && !iter._ignore_stop;
  assert(would_check_stop == true);
  return true;
}

// --- entry point -------------------------------------------------------------

void llama_iter_test() {
  printf("\nLlamaIter::_ignore_stop tests\n");
  printf("=============================\n");

  TEST(default_ignore_stop_false);
  TEST(default_has_next_false);
  TEST(set_ignore_stop_true);
  TEST(set_ignore_stop_back_false);
  TEST(move_preserves_ignore_stop_true);
  TEST(move_preserves_ignore_stop_false);
  TEST(ignore_stop_independent_of_has_next);
  TEST(ignore_stop_with_tail);
  TEST(ignore_stop_false_allows_stop_check);

  printf("\n%d/%d tests passed\n", tests_passed, tests_run);
}