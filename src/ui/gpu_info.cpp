// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include "gpu_info.h"
#include "ggml-backend.h"

#include <algorithm>
#include <cstddef>

namespace gpu_info {

uint64_t vram_bytes() {
  ggml_backend_dev_t dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
  if (!dev) {
    return 0;
  }
  size_t free_bytes = 0;
  size_t total_bytes = 0;
  ggml_backend_dev_memory(dev, &free_bytes, &total_bytes);
  return static_cast<uint64_t>(total_bytes);
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
