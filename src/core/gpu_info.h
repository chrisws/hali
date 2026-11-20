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
// Default: cross-platform via the ggml backend API (Metal, CUDA, ROCm, etc.)
// Alternative: set_vram_bytes() allows an external engine (e.g. Basalt)
// to inject its own VRAM figure, bypassing the ggml query.
//
namespace gpu_info {

// Total GPU VRAM in bytes. Returns 0 if no GPU device is detected.
// If set_vram_bytes() has been called, returns that value instead.
uint64_t vram_bytes();

// Override the VRAM value (e.g. from a Basalt engine VRAM protocol reply).
// Pass 0 to clear the override and fall back to the ggml backend query.
void set_vram_bytes(uint64_t bytes);

// Recommended KV-cache restart threshold as a percentage (e.g. 75, 85, 90).
// Scales with detected VRAM: 8 GB → 75 %, 16 GB → 85 %, 32 GB → 90 %, cap 92 %.
int kv_restart_threshold();

} // namespace gpu_info

#endif