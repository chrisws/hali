// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//
// Unit tests for SuffixDrafter (plan 08)

#include <cassert>
#include <cstdio>
#include <vector>
#include "llama_sb_suffix.h"

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

static bool test_init() {
  SuffixDrafter d;
  d.init(4, 1024);
  assert(d.size() == 0);
  return true;
}

static bool test_add_context_basic() {
  SuffixDrafter d;
  d.init(2, 1024);
  // tokens: A B C D E
  // ngram=2: (A,B)->C, (B,C)->D, (C,D)->E
  llama_token toks[] = {10, 20, 30, 40, 50};
  d.add_context(toks, 5);
  assert(d.size() == 3);
  return true;
}

static bool test_draft_single() {
  SuffixDrafter d;
  d.init(2, 1024);
  llama_token toks[] = {10, 20, 30, 40, 50};
  d.add_context(toks, 5);
  // Look up (A,B) -> should return C
  llama_token recent[] = {10, 20};
  auto result = d.draft(recent, 2, 1);
  assert(result.size() == 1);
  assert(result[0] == 30);
  return true;
}

static bool test_draft_chained() {
  SuffixDrafter d;
  d.init(2, 1024);
  llama_token toks[] = {10, 20, 30, 40, 50};
  d.add_context(toks, 5);
  // Look up (A,B) with max_draft=3:
  //   (A,B)->C, (B,C)->D, (C,D)->E
  llama_token recent[] = {10, 20};
  auto result = d.draft(recent, 2, 3);
  assert(result.size() == 3);
  assert(result[0] == 30);
  assert(result[1] == 40);
  assert(result[2] == 50);
  return true;
}

static bool test_draft_no_match() {
  SuffixDrafter d;
  d.init(2, 1024);
  llama_token toks[] = {10, 20, 30};
  d.add_context(toks, 3);
  // (99, 88) was never in context
  llama_token recent[] = {99, 88};
  auto result = d.draft(recent, 2, 2);
  assert(result.empty());
  return true;
}

static bool test_draft_partial_chain() {
  SuffixDrafter d;
  d.init(2, 1024);
  llama_token toks[] = {10, 20, 30};
  d.add_context(toks, 3);
  // (A,B)->C exists, but (B,C) does not (no token after C)
  llama_token recent[] = {10, 20};
  auto result = d.draft(recent, 2, 3);
  assert(result.size() == 1);
  assert(result[0] == 30);
  return true;
}

static bool test_clear() {
  SuffixDrafter d;
  d.init(2, 1024);
  llama_token toks[] = {10, 20, 30};
  d.add_context(toks, 3);
  assert(d.size() > 0);
  d.clear();
  assert(d.size() == 0);
  return true;
}

static bool test_ngram_too_long() {
  SuffixDrafter d;
  d.init(4, 1024);
  // Only 3 tokens, less than ngram=4
  llama_token toks[] = {10, 20, 30};
  d.add_context(toks, 3);
  assert(d.size() == 0);
  return true;
}

static bool test_draft_ngram_mismatch() {
  SuffixDrafter d;
  d.init(4, 1024);
  llama_token toks[] = {10, 20, 30, 40, 50};
  d.add_context(toks, 5);
  // Request ngram_len=2 but drafter was init'd with ngram=4
  // The table has 4-gram keys, so a 2-gram lookup won't match
  llama_token recent[] = {10, 20};
  auto result = d.draft(recent, 2, 2);
  assert(result.empty());
  return true;
}

static bool test_duplicate_ngram_keeps_first() {
  SuffixDrafter d;
  d.init(2, 1024);
  // (A,B) appears twice: first followed by C, then by X
  llama_token toks[] = {10, 20, 30, 10, 20, 99};
  d.add_context(toks, 6);
  llama_token recent[] = {10, 20};
  auto result = d.draft(recent, 2, 1);
  assert(result.size() == 1);
  assert(result[0] == 30);  // first occurrence wins
  return true;
}

static bool test_max_draft_zero() {
  SuffixDrafter d;
  d.init(2, 1024);
  llama_token toks[] = {10, 20, 30};
  d.add_context(toks, 3);
  llama_token recent[] = {10, 20};
  auto result = d.draft(recent, 2, 0);
  assert(result.empty());
  return true;
}

static bool test_large_context() {
  SuffixDrafter d;
  d.init(4, 65536);
  // Build a large context with repeating patterns
  std::vector<llama_token> toks;
  for (int i = 0; i < 1000; ++i) {
    toks.push_back((llama_token)(i % 100));
  }
  d.add_context(toks.data(), (int)toks.size());
  assert(d.size() > 0);
  // Look up a known pattern
  llama_token recent[4];
  for (int i = 0; i < 4; ++i) { recent[i] = toks[i]; }
  auto result = d.draft(recent, 4, 2);
  // Should find a continuation (the pattern repeats)
  assert(!result.empty());
  return true;
}

// --- entry point -------------------------------------------------------------

void suffix_drafter_test() {
  printf("\nSuffixDrafter tests\n");
  printf("===================\n");

  TEST(init);
  TEST(add_context_basic);
  TEST(draft_single);
  TEST(draft_chained);
  TEST(draft_no_match);
  TEST(draft_partial_chain);
  TEST(clear);
  TEST(ngram_too_long);
  TEST(draft_ngram_mismatch);
  TEST(duplicate_ngram_keeps_first);
  TEST(max_draft_zero);
  TEST(large_context);

  printf("\n%d/%d tests passed\n", tests_passed, tests_run);
}