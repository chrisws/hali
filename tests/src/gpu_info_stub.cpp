// Test stub for gpu_info - avoids ggml backend dependencies.
#include "gpu_info.h"

namespace gpu_info {

void set_vram_bytes(uint64_t bytes) {
  (void)bytes;
}

uint64_t vram_bytes() {
  return 0;
}

int kv_restart_threshold() {
  return 75;
}

} // namespace gpu_info