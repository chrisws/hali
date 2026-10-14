// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef GPU_INFO_H
#define GPU_INFO_H

#include <cstdint>

//
// Platform / hardware introspection helpers.
// Cross-platform via the ggml backend API (Metal, CUDA, ROCm, etc.)
//
namespace gpu_info {

// Total GPU VRAM in bytes. Returns 0 if no GPU device is detected.
uint64_t vram_bytes();

// Recommended KV-cache restart threshold as a percentage (e.g. 75, 85, 90).
// Scales with detected VRAM: 8 GB → 75 %, 16 GB → 85 %, 32 GB → 90 %, cap 92 %.
int kv_restart_threshold();

} // namespace gpu_info

#endif