#include <iostream>
#include <cassert>
#include <algorithm>
#include <cstring>

using namespace std;

// Replicates the adaptive draft limit logic from Llama::mtp_round
// Uses a sliding window of recent rounds instead of cumulative stats.
static int adaptive_limit(int n_max, int n_batch, int room,
                          int win_filled, int win_drafted, int win_accepted) {
  int limit = n_max;
  if (win_filled >= 16 && win_drafted > 0) {
    const double rate = (double)win_accepted / (double)win_drafted;
    if (rate < 0.25) {
      limit = 1;
    } else if (rate < 0.50) {
      limit = 2;
    }
  }
  limit = min({limit, n_batch - 1, room});
  return limit;
}

// --- sliding window ring buffer (mirrors Llama members) ---
static constexpr int MTP_WIN = 32;
struct WinEntry { int drafted; int accepted; };

struct WinState {
  WinEntry entries[MTP_WIN] = {};
  int idx     = 0;
  int filled  = 0;
  int drafted = 0;
  int accepted = 0;

  void reset() {
    memset(entries, 0, sizeof(entries));
    idx = filled = drafted = accepted = 0;
  }

  void add(int k, int n_acc) {
    if (filled == MTP_WIN) {
      drafted  -= entries[idx].drafted;
      accepted -= entries[idx].accepted;
    }
    entries[idx] = {k, n_acc};
    drafted  += k;
    accepted += n_acc;
    idx = (idx + 1) % MTP_WIN;
    if (filled < MTP_WIN) { filled++; }
  }

  int limit(int n_max, int n_batch, int room) const {
    return adaptive_limit(n_max, n_batch, room, filled, drafted, accepted);
  }
};

// ---------------------------------------------------------------------------
static void test_warmup_not_reached() {
  WinState w;
  // 0 rounds: no adaptation
  assert(w.limit(4, 512, 100) == 4);
  // 15 rounds (below 16): no adaptation even with poor rate
  for (int i = 0; i < 15; ++i) { w.add(3, 0); }
  assert(w.filled == 15);
  assert(w.limit(4, 512, 100) == 4);
  // 16 rounds with 0 drafted: still no adaptation
  WinState w2;
  for (int i = 0; i < 16; ++i) { w2.add(0, 0); }
  assert(w2.filled == 16);
  assert(w2.drafted == 0);
  assert(w2.limit(4, 512, 100) == 4);
  cout << "test_warmup_not_reached passed" << endl;
}

static void test_low_rate() {
  // 16 rounds, all 0/3 → rate = 0.0 → limit = 1
  WinState w;
  for (int i = 0; i < 16; ++i) { w.add(3, 0); }
  assert(w.limit(4, 512, 100) == 1);

  // 16 rounds, 10/48 → rate = 0.208 → limit = 1
  WinState w2;
  for (int i = 0; i < 10; ++i) { w2.add(3, 1); }
  for (int i = 0; i < 6; ++i)  { w2.add(3, 0); }
  assert(w2.drafted == 48);
  assert(w2.accepted == 10);
  assert(w2.limit(4, 512, 100) == 1);
  cout << "test_low_rate passed" << endl;
}

static void test_medium_rate() {
  // 16 rounds, 16/48 → rate = 0.333 → limit = 2
  WinState w;
  for (int i = 0; i < 16; ++i) { w.add(3, 1); }
  assert(w.drafted == 48);
  assert(w.accepted == 16);
  assert(w.limit(4, 512, 100) == 2);

  // rate = 0.49 → limit = 2
  WinState w2;
  for (int i = 0; i < 14; ++i) { w2.add(3, 2); }  // 28 accepted
  for (int i = 0; i < 2; ++i)  { w2.add(3, 0); }   // 0 accepted
  // total: 48 drafted, 28 accepted → 0.583 → limit = 4 (n_max)
  // let me recalculate: 14*2 + 2*0 = 28, 16*3 = 48, 28/48 = 0.583 → n_max
  // need a better example for 0.49
  // 12 rounds of 2/3 + 4 rounds of 0/3 = 24/48 = 0.5 → limit = 4
  // 11 rounds of 2/3 + 5 rounds of 0/3 = 22/48 = 0.458 → limit = 2
  WinState w3;
  for (int i = 0; i < 11; ++i) { w3.add(3, 2); }
  for (int i = 0; i < 5; ++i)  { w3.add(3, 0); }
  assert(w3.drafted == 48);
  assert(w3.accepted == 22);
  assert(w3.limit(4, 512, 100) == 2);
  cout << "test_medium_rate passed" << endl;
}

static void test_high_rate() {
  // 16 rounds, all 3/3 → rate = 1.0 → limit = n_max
  WinState w;
  for (int i = 0; i < 16; ++i) { w.add(3, 3); }
  assert(w.limit(4, 512, 100) == 4);
  assert(w.limit(3, 512, 100) == 3);

  // rate = 0.75 → limit = n_max (boundary: not < 0.50)
  WinState w2;
  for (int i = 0; i < 12; ++i) { w2.add(3, 3); }  // 36
  for (int i = 0; i < 4; ++i)  { w2.add(3, 0); }   // 0
  // 36/48 = 0.75 → limit = n_max
  assert(w2.limit(4, 512, 100) == 4);
  cout << "test_high_rate passed" << endl;
}

static void test_structural_caps() {
  WinState w;
  for (int i = 0; i < 16; ++i) { w.add(3, 3); }
  // n_batch - 1 caps: min(4, 2, 100) = 2
  assert(w.limit(4, 3, 100) == 2);
  // room caps: min(4, 511, 1) = 1
  assert(w.limit(4, 512, 1) == 1);
  // room = 0 → 0
  assert(w.limit(4, 512, 0) == 0);
  cout << "test_structural_caps passed" << endl;
}

static void test_boundary_rates() {
  // rate = 0.25 exactly → NOT < 0.25 → limit = 2 (medium)
  // 1/4 per round × 16 rounds = 16/64 = 0.25
  WinState w;
  for (int i = 0; i < 16; ++i) { w.add(4, 1); }
  assert(w.drafted == 64);
  assert(w.accepted == 16);
  assert(w.limit(4, 512, 100) == 2);

  // rate = 0.50 exactly → NOT < 0.50 → limit = n_max
  // 2/4 per round × 16 rounds = 32/64 = 0.50
  WinState w2;
  for (int i = 0; i < 16; ++i) { w2.add(4, 2); }
  assert(w2.limit(4, 512, 100) == 4);
  cout << "test_boundary_rates passed" << endl;
}

// The key test: sliding window responds to recent degradation
// that cumulative stats would miss.
static void test_window_responds_to_degradation() {
  WinState w;
  // First 20 rounds: perfect 3/3 acceptance
  for (int i = 0; i < 20; ++i) { w.add(3, 3); }
  assert(w.limit(3, 512, 100) == 3);

  // Next 12 rounds: all 0/3 (window now has 12×0/3 + 20×3/3, but
  // window size is 32 so all 32 fit: 12 zeros + 20 threes)
  for (int i = 0; i < 12; ++i) { w.add(3, 0); }
  // window: 20×(3,3) + 12×(3,0) = 96 drafted, 60 accepted → 0.625 → limit = 3
  assert(w.drafted == 96);
  assert(w.accepted == 60);
  assert(w.limit(3, 512, 100) == 3);

  // 4 more zero rounds: window evicts 4 of the 3/3 entries
  // window: 16×(3,3) + 16×(3,0) = 96 drafted, 48 accepted → 0.50 → limit = 3
  for (int i = 0; i < 4; ++i) { w.add(3, 0); }
  assert(w.drafted == 96);
  assert(w.accepted == 48);
  assert(w.limit(3, 512, 100) == 3);

  // 4 more: 12×(3,3) + 20×(3,0) = 96 drafted, 36 accepted → 0.375 → limit = 2
  for (int i = 0; i < 4; ++i) { w.add(3, 0); }
  assert(w.drafted == 96);
  assert(w.accepted == 36);
  assert(w.limit(3, 512, 100) == 2);

  // 4 more: 8×(3,3) + 24×(3,0) = 96 drafted, 24 accepted → 0.25 → limit = 2
  for (int i = 0; i < 4; ++i) { w.add(3, 0); }
  assert(w.drafted == 96);
  assert(w.accepted == 24);
  assert(w.limit(3, 512, 100) == 2);

  // 4 more: 4×(3,3) + 28×(3,0) = 96 drafted, 12 accepted → 0.125 → limit = 1
  for (int i = 0; i < 4; ++i) { w.add(3, 0); }
  assert(w.drafted == 96);
  assert(w.accepted == 12);
  assert(w.limit(3, 512, 100) == 1);

  // 4 more: 0×(3,3) + 32×(3,0) = 96 drafted, 0 accepted → 0.0 → limit = 1
  for (int i = 0; i < 4; ++i) { w.add(3, 0); }
  assert(w.drafted == 96);
  assert(w.accepted == 0);
  assert(w.limit(3, 512, 100) == 1);
  cout << "test_window_responds_to_degradation passed" << endl;
}

// Window recovers: after bad rounds, good rounds push the rate back up
static void test_window_recovers() {
  WinState w;
  // 32 rounds of 0/3 → rate = 0 → limit = 1
  for (int i = 0; i < 32; ++i) { w.add(3, 0); }
  assert(w.limit(3, 512, 100) == 1);

  // 16 rounds of 3/3 → evicts 16 zeros, window = 16×(3,0) + 16×(3,3)
  // = 96 drafted, 48 accepted → 0.50 → limit = 3
  for (int i = 0; i < 16; ++i) { w.add(3, 3); }
  assert(w.drafted == 96);
  assert(w.accepted == 48);
  assert(w.limit(3, 512, 100) == 3);
  cout << "test_window_recovers passed" << endl;
}

// Reset clears the window
static void test_reset() {
  WinState w;
  for (int i = 0; i < 20; ++i) { w.add(3, 0); }
  assert(w.filled == 20);
  assert(w.drafted == 60);
  w.reset();
  assert(w.filled == 0);
  assert(w.drafted == 0);
  assert(w.accepted == 0);
  assert(w.idx == 0);
  assert(w.limit(4, 512, 100) == 4);  // back to n_max
  cout << "test_reset passed" << endl;
}

void mtp_adaptive_test() {
  cout << "=== MTP Adaptive Draft Limit Tests (sliding window) ===\n\n";
  test_warmup_not_reached();
  test_low_rate();
  test_medium_rate();
  test_high_rate();
  test_structural_caps();
  test_boundary_rates();
  test_window_responds_to_degradation();
  test_window_recovers();
  test_reset();
  cout << "\nAll MTP adaptive tests passed!\n" << endl;
}