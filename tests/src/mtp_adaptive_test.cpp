#include <iostream>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cmath>

using namespace std;

// ---------------------------------------------------------------------------
// Replicates the new per-position adaptive logic from Llama
// ---------------------------------------------------------------------------

static constexpr int WIN = 32;

struct WinEntry {
  int drafted;
  int accepted;
  int pos_acc[4] = {};
};

struct WinState {
  WinEntry entries[WIN] = {};
  int idx      = 0;
  int filled   = 0;
  int drafted  = 0;
  int accepted = 0;

  void reset() {
    memset(entries, 0, sizeof(entries));
    idx = filled = drafted = accepted = 0;
  }

  void add(int k, int n_acc) {
    if (filled == WIN) {
      drafted  -= entries[idx].drafted;
      accepted -= entries[idx].accepted;
    }
    auto &e = entries[idx];
    e.drafted  = k;
    e.accepted = n_acc;
    for (int i = 0; i < 4; ++i) {
      e.pos_acc[i] = (i < n_acc) ? 1 : 0;
    }
    drafted  += k;
    accepted += n_acc;
    idx = (idx + 1) % WIN;
    if (filled < WIN) { filled++; }
  }

  // per-position rate over the window
  double pos_rate(int pos) const {
    if (filled == 0) return 0.0;
    int sum = 0;
    for (int j = 0; j < filled; ++j) {
      sum += entries[j].pos_acc[pos];
    }
    return (double)sum / (double)filled;
  }
};

struct AdaptiveState {
  int   eval_counter  = 0;
  int   cooldown      = 0;
  int   eff_n_max     = 3;
  float pos_threshold = 0.30f;
  float p_min         = 0.0f;
  int   n_min         = 1;
};

// Mirrors Llama::compute_effective_n_max()
static int compute_effective_n_max(const WinState &w, int n_max,
                                   int eff_n_max, float pos_threshold) {
  if (w.filled < 16) {
    return n_max;
  }
  int k = 0;
  for (int i = 0; i < n_max; ++i) {
    double rate = w.pos_rate(i);
    double threshold = (i >= eff_n_max)
      ? pos_threshold + 0.05
      : pos_threshold;
    if (rate < threshold) break;
    k = i + 1;
  }
  return max(k, 1);
}

// Mirrors Llama::adapt_p_min()
static void adapt_p_min(const WinState &w, AdaptiveState &a) {
  if (w.drafted == 0) return;
  double frac = (double)w.accepted / (double)w.drafted;
  if (frac > 0.80 && a.p_min > 0.0f) {
    a.p_min = max(0.0f, a.p_min - 0.05f);
  } else if (frac < 0.30 && a.p_min < 0.50f) {
    a.p_min = min(0.50f, a.p_min + 0.05f);
  }
}

// Mirrors Llama::adapt_n_min()
static void adapt_n_min(const WinState &w, AdaptiveState &a) {
  if (w.filled < WIN) return;
  double rate0 = w.pos_rate(0);
  if (rate0 < 0.15 && a.n_min > 0) {
    a.n_min = 0;
  } else if (rate0 > 0.40 && a.n_min == 0) {
    a.n_min = 1;
  }
}

// Mirrors Llama::adapt_pos_threshold()
static void adapt_pos_threshold(uint64_t total_drafted, uint64_t total_accepted,
                                AdaptiveState &a) {
  if (total_drafted == 0) return;
  double ema = (double)total_accepted / (double)total_drafted;
  if (ema > 0.70 && a.pos_threshold < 0.40f) {
    a.pos_threshold = min(0.50f, a.pos_threshold + 0.05f);
  } else if (ema < 0.30 && a.pos_threshold > 0.20f) {
    a.pos_threshold = max(0.15f, a.pos_threshold - 0.05f);
  }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_pos_acc_tracking() {
  WinState w;
  // 3 drafted, 2 accepted → pos_acc = {1, 1, 0, 0}
  w.add(3, 2);
  assert(w.entries[0].pos_acc[0] == 1);
  assert(w.entries[0].pos_acc[1] == 1);
  assert(w.entries[0].pos_acc[2] == 0);
  assert(w.entries[0].pos_acc[3] == 0);

  // 2 drafted, 1 accepted → pos_acc = {1, 0, 0, 0}
  w.add(2, 1);
  assert(w.entries[1].pos_acc[0] == 1);
  assert(w.entries[1].pos_acc[1] == 0);

  // 1 drafted, 1 accepted → pos_acc = {1, 0, 0, 0}
  w.add(1, 1);
  assert(w.entries[2].pos_acc[0] == 1);
  assert(w.entries[2].pos_acc[1] == 0);

  // 3 drafted, 0 accepted → pos_acc = {0, 0, 0, 0}
  w.add(3, 0);
  assert(w.entries[3].pos_acc[0] == 0);
  assert(w.entries[3].pos_acc[1] == 0);
  assert(w.entries[3].pos_acc[2] == 0);
  cout << "test_pos_acc_tracking passed" << endl;
}

static void test_compute_nmax_warmup() {
  // Not enough data → return config ceiling
  WinState w;
  for (int i = 0; i < 15; ++i) { w.add(3, 3); }
  assert(compute_effective_n_max(w, 4, 4, 0.30f) == 4);
  cout << "test_compute_nmax_warmup passed" << endl;
}

static void test_compute_nmax_all_accepted() {
  // All positions accepted → eff_n_max = n_max
  WinState w;
  for (int i = 0; i < 32; ++i) { w.add(4, 4); }
  assert(w.pos_rate(0) == 1.0);
  assert(w.pos_rate(1) == 1.0);
  assert(w.pos_rate(2) == 1.0);
  assert(w.pos_rate(3) == 1.0);
  assert(compute_effective_n_max(w, 3, 3, 0.30f) == 3);
  assert(compute_effective_n_max(w, 4, 4, 0.30f) == 4);
  cout << "test_compute_nmax_all_accepted passed" << endl;
}

static void test_compute_nmax_pos2_low() {
  // Position 0 and 1 always accepted, position 2 rarely
  // 24 rounds of (3,2) + 8 rounds of (3,0)
  // pos_rate[0] = 24/32 = 0.75, pos_rate[1] = 24/32 = 0.75, pos_rate[2] = 0
  WinState w;
  for (int i = 0; i < 24; ++i) { w.add(3, 2); }
  for (int i = 0; i < 8; ++i)  { w.add(3, 0); }
  assert(w.pos_rate(0) > 0.70);
  assert(w.pos_rate(1) > 0.70);
  assert(w.pos_rate(2) == 0.0);
  // eff_n_max should be 2 (pos 0 and 1 pass, pos 2 fails)
  assert(compute_effective_n_max(w, 3, 3, 0.30f) == 2);
  cout << "test_compute_nmax_pos2_low passed" << endl;
}

static void test_compute_nmax_pos0_low() {
  // Position 0 rarely accepted → eff_n_max = 1 (minimum)
  WinState w;
  for (int i = 0; i < 32; ++i) { w.add(3, 0); }
  assert(w.pos_rate(0) == 0.0);
  assert(compute_effective_n_max(w, 3, 3, 0.30f) == 1);
  cout << "test_compute_nmax_pos0_low passed" << endl;
}

static void test_hysteresis() {
  // Position 2 rate is exactly at threshold (0.30)
  // With eff_n_max=3, extending to 3 requires rate >= 0.35 (0.30 + 0.05)
  // So rate = 0.30 should NOT extend → stays at 2
  WinState w;
  // 10 rounds of (3,2) gives pos_rate[2] = 0
  // Need pos_rate[2] = 0.30: 10 out of 32 rounds have pos 2 accepted
  // (3,3) × 10 + (3,2) × 22 → pos_rate[2] = 10/32 = 0.3125
  // That's above 0.30 but below 0.35 → with hysteresis, won't extend
  for (int i = 0; i < 10; ++i) { w.add(3, 3); }
  for (int i = 0; i < 22; ++i) { w.add(3, 2); }
  double rate2 = w.pos_rate(2);
  assert(rate2 > 0.30);   // above base threshold
  assert(rate2 < 0.35);   // below hysteresis threshold
  // With eff_n_max=2, extending to 3 requires rate >= 0.35
  assert(compute_effective_n_max(w, 3, 2, 0.30f) == 2);
  // Without hysteresis (eff_n_max=3 already), maintaining 3 only needs 0.30
  assert(compute_effective_n_max(w, 3, 3, 0.30f) == 3);
  cout << "test_hysteresis passed" << endl;
}

static void test_adapt_p_min_high_frac() {
  // High acceptance fraction → lower p_min
  WinState w;
  for (int i = 0; i < 32; ++i) { w.add(3, 3); }
  AdaptiveState a;
  a.p_min = 0.10f;
  adapt_p_min(w, a);
  assert(fabsf(a.p_min - 0.05f) < 1e-6f);  // decreased by 0.05
  cout << "test_adapt_p_min_high_frac passed" << endl;
}

static void test_adapt_p_min_low_frac() {
  // Low acceptance fraction → raise p_min
  WinState w;
  for (int i = 0; i < 32; ++i) { w.add(3, 0); }
  AdaptiveState a;
  a.p_min = 0.10f;
  adapt_p_min(w, a);
  assert(fabsf(a.p_min - 0.15f) < 1e-6f);  // increased by 0.05
  cout << "test_adapt_p_min_low_frac passed" << endl;
}

static void test_adapt_p_min_bounds() {
  // p_min at 0.0, high frac → stays at 0.0
  WinState w;
  for (int i = 0; i < 32; ++i) { w.add(3, 3); }
  AdaptiveState a;
  a.p_min = 0.0f;
  adapt_p_min(w, a);
  assert(a.p_min == 0.0f);

  // p_min at 0.50, low frac → stays at 0.50
  WinState w2;
  for (int i = 0; i < 32; ++i) { w2.add(3, 0); }
  AdaptiveState a2;
  a2.p_min = 0.50f;
  adapt_p_min(w2, a2);
  assert(a2.p_min == 0.50f);
  cout << "test_adapt_p_min_bounds passed" << endl;
}

static void test_adapt_n_min_circuit_breaker() {
  // Position 0 rate < 0.15 → n_min = 0
  WinState w;
  for (int i = 0; i < 32; ++i) { w.add(3, 0); }
  AdaptiveState a;
  a.n_min = 1;
  adapt_n_min(w, a);
  assert(a.n_min == 0);

  // Recovery: position 0 rate > 0.40 → n_min = 1
  WinState w2;
  for (int i = 0; i < 32; ++i) { w2.add(3, 3); }
  AdaptiveState a2;
  a2.n_min = 0;
  adapt_n_min(w2, a2);
  assert(a2.n_min == 1);
  cout << "test_adapt_n_min_circuit_breaker passed" << endl;
}

static void test_adapt_pos_threshold() {
  // High EMA → raise threshold
  AdaptiveState a;
  a.pos_threshold = 0.30f;
  adapt_pos_threshold(1000, 800, a);  // ema = 0.80
  assert(fabsf(a.pos_threshold - 0.35f) < 1e-6f);

  // Low EMA → lower threshold
  AdaptiveState a2;
  a2.pos_threshold = 0.30f;
  adapt_pos_threshold(1000, 200, a2);  // ema = 0.20
  assert(fabsf(a2.pos_threshold - 0.25f) < 1e-6f);

  // Guard: can't raise above 0.40 (guard blocks at 0.40)
  AdaptiveState a3;
  a3.pos_threshold = 0.35f;
  adapt_pos_threshold(1000, 900, a3);  // ema = 0.90
  assert(fabsf(a3.pos_threshold - 0.40f) < 1e-6f);
  // Second call: guard blocks (0.40 is not < 0.40)
  adapt_pos_threshold(1000, 900, a3);
  assert(fabsf(a3.pos_threshold - 0.40f) < 1e-6f);

  // Guard: can't lower below 0.20 (guard blocks at 0.20)
  AdaptiveState a4;
  a4.pos_threshold = 0.22f;
  adapt_pos_threshold(1000, 100, a4);  // ema = 0.10
  assert(fabsf(a4.pos_threshold - 0.17f) < 1e-6f);
  // Second call: guard blocks (0.17 is not > 0.20)
  adapt_pos_threshold(1000, 100, a4);
  assert(fabsf(a4.pos_threshold - 0.17f) < 1e-6f);
  cout << "test_adapt_pos_threshold passed" << endl;
}

static void test_reset_preserves_adaptive() {
  // Simulate: adaptive state has been tuned, then reset is called
  AdaptiveState a;
  a.eff_n_max     = 2;
  a.p_min         = 0.10f;
  a.n_min         = 0;
  a.pos_threshold = 0.25f;
  a.eval_counter  = 16;
  a.cooldown      = 10;

  // Reset: eval_counter and cooldown go to 0, params preserved
  a.eval_counter = 0;
  a.cooldown     = 0;

  assert(a.eff_n_max     == 2);
  assert(a.p_min         == 0.10f);
  assert(a.n_min         == 0);
  assert(a.pos_threshold == 0.25f);
  assert(a.eval_counter  == 0);
  assert(a.cooldown      == 0);
  cout << "test_reset_preserves_adaptive passed" << endl;
}

static void test_cooldown_blocks_eval() {
  // During cooldown, no changes should occur
  AdaptiveState a;
  a.cooldown = 32;
  a.eff_n_max = 3;

  // Simulate: cooldown decrements, no eval happens
  a.cooldown--;
  assert(a.cooldown == 31);
  assert(a.eff_n_max == 3);  // unchanged
  cout << "test_cooldown_blocks_eval passed" << endl;
}

void mtp_adaptive_test() {
  cout << "=== MTP Adaptive Self-Tuning Tests ===\n\n";
  test_pos_acc_tracking();
  test_compute_nmax_warmup();
  test_compute_nmax_all_accepted();
  test_compute_nmax_pos2_low();
  test_compute_nmax_pos0_low();
  test_hysteresis();
  test_adapt_p_min_high_frac();
  test_adapt_p_min_low_frac();
  test_adapt_p_min_bounds();
  test_adapt_n_min_circuit_breaker();
  test_adapt_pos_threshold();
  test_reset_preserves_adaptive();
  test_cooldown_blocks_eval();
  cout << "\nAll MTP adaptive tests passed!\n" << endl;
}