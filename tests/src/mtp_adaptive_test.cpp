#include <iostream>
#include <cassert>
#include <algorithm>

using namespace std;

// Replicates the adaptive draft limit logic from Llama::mtp_round
// rate = _mtp_accepted / _mtp_drafted
// Warmup gate: rounds >= 16 && drafted > 0
static int adaptive_limit(int n_max, int n_batch, int room,
                          int rounds, int drafted, int accepted) {
  int limit = min({n_max, n_batch - 1, room});
  if (rounds >= 16 && drafted > 0) {
    const double rate = (double)accepted / (double)drafted;
    if (rate < 0.25) {
      limit = 1;
    } else if (rate < 0.50) {
      limit = 2;
    } else if (rate < 0.75) {
      limit = min(limit, 3);
    }
  }
  return limit;
}

static void test_warmup_not_reached() {
  // Before 16 rounds, no adaptation: limit stays at the structural max
  assert(adaptive_limit(4, 512, 100, 0, 0, 0) == 4);
  assert(adaptive_limit(4, 512, 100, 15, 50, 10) == 4);
  // drafted == 0 also blocks adaptation
  assert(adaptive_limit(4, 512, 100, 20, 0, 0) == 4);
  cout << "test_warmup_not_reached passed" << endl;
}

static void test_low_rate() {
  // rate < 0.25 → limit = 1
  // 10/64 = 0.156
  assert(adaptive_limit(4, 512, 100, 20, 64, 10) == 1);
  // 0/32 = 0.0
  assert(adaptive_limit(4, 512, 100, 20, 32, 0) == 1);
  // 15/64 = 0.234
  assert(adaptive_limit(4, 512, 100, 20, 64, 15) == 1);
  cout << "test_low_rate passed" << endl;
}

static void test_medium_rate() {
  // 0.25 <= rate < 0.50 → limit = 2
  // 16/64 = 0.25
  assert(adaptive_limit(4, 512, 100, 20, 64, 16) == 2);
  // 30/64 = 0.469
  assert(adaptive_limit(4, 512, 100, 20, 64, 30) == 2);
  cout << "test_medium_rate passed" << endl;
}

static void test_high_rate() {
  // 0.50 <= rate < 0.75 → limit = min(n_max, 3)
  // 32/64 = 0.50
  assert(adaptive_limit(4, 512, 100, 20, 64, 32) == 3);
  // 45/64 = 0.703
  assert(adaptive_limit(4, 512, 100, 20, 64, 45) == 3);
  // n_max=2 caps at 2
  assert(adaptive_limit(2, 512, 100, 20, 64, 32) == 2);
  cout << "test_high_rate passed" << endl;
}

static void test_full_rate() {
  // rate >= 0.75 → limit = n_max (structural max)
  // 48/64 = 0.75
  assert(adaptive_limit(4, 512, 100, 20, 64, 48) == 4);
  // 64/64 = 1.0
  assert(adaptive_limit(4, 512, 100, 20, 64, 64) == 4);
  cout << "test_full_rate passed" << endl;
}

static void test_structural_caps() {
  // n_batch - 1 caps the limit
  assert(adaptive_limit(4, 3, 100, 20, 64, 48) == 2);  // min(4, 2, 100) = 2
  // room caps the limit
  assert(adaptive_limit(4, 512, 1, 20, 64, 48) == 1);  // min(4, 511, 1) = 1
  // room = 0 → limit = 0
  assert(adaptive_limit(4, 512, 0, 20, 64, 48) == 0);
  cout << "test_structural_caps passed" << endl;
}

static void test_boundary_rates() {
  // Exact boundaries
  // rate = 0.25 exactly → medium (limit=2)
  assert(adaptive_limit(4, 512, 100, 20, 4, 1) == 2);
  // rate = 0.50 exactly → high (limit=3)
  assert(adaptive_limit(4, 512, 100, 20, 2, 1) == 3);
  // rate = 0.75 exactly → full (limit=n_max)
  assert(adaptive_limit(4, 512, 100, 20, 4, 3) == 4);
  cout << "test_boundary_rates passed" << endl;
}

void mtp_adaptive_test() {
  cout << "=== MTP Adaptive Draft Limit Tests ===\n\n";
  test_warmup_not_reached();
  test_low_rate();
  test_medium_rate();
  test_high_rate();
  test_full_rate();
  test_structural_caps();
  test_boundary_rates();
  cout << "\nAll MTP adaptive tests passed!\n" << endl;
}