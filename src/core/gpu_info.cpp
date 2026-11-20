// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "gpu_info.h"

#include <algorithm>
#include <cstddef>

namespace gpu_info {
  namespace {
    // Override slot: 0 means "not set, use ggml backend query".
    // Set by set_vram_bytes() when an external engine (e.g. Basalt)
    // provides its own VRAM figure via the process protocol.
    static uint64_t s_vram_override = 0;
  } // namespace

  void set_vram_bytes(uint64_t bytes) {
    s_vram_override = bytes;
  }

  uint64_t vram_bytes() {
    return s_vram_override;
  }

  int kv_restart_threshold() {
    uint64_t vram = vram_bytes();
    if (vram == 0) {
      return 75;  // CPU-only fallback
    }
    double gb = static_cast<double>(vram) / (1024.0 * 1024.0 * 1024.0);
    // 8 GB → 75, 16 GB → 85, 32 GB → 90, cap at 92
    int threshold = 75 + static_cast<int>((gb - 8.0) * 2.5);
    return std::clamp(threshold, 75, 92);
  }
} // namespace gpu_info
