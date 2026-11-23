# llama.cpp CUDA Optimization — Blackwell sm_120 (RTX 5060 Ti Super)

## Overview

This skill documents the process for optimizing llama.cpp's CUDA backend for NVIDIA Blackwell GPUs (compute capability 12.0, sm_120), specifically the RTX 5060 Ti Super. It covers build configuration, kernel tuning, quantization paths, flash attention, and memory subsystem optimization.

## 1. Build Configuration (CMake)

### Required CMake Flags

```bash
cmake -B build \
  -DCMAKE_CUDA_ARCHITECTURES=120a-real \
  -DGGML_CUDA_FORCE_MMQ=ON \
  -DGGML_CUDA_FA=ON \
  -DCMAKE_BUILD_TYPE=Release
```

### Key CMake Options (from `ggml/src/ggml-cuda/CMakeLists.txt`)

| Option | Purpose |
|--------|---------|
| `CMAKE_CUDA_ARCHITECTURES=120a-real` | Target Blackwell sm_120 with FP4 tensor core instructions (`a` suffix) |
| `GGML_CUDA_FORCE_MMQ` | Force matrix-matrix quantized GEMM path (bypasses cuBLAS for quantized ops) |
| `GGML_CUDA_FORCE_CUBLAS` | Force cuBLAS for float GEMM (use with care on Blackwell) |
| `GGML_CUDA_NO_VMM` | Disable virtual memory management (useful for debugging) |
| `GGML_CUDA_FA` | Enable flash attention kernels |
| `GGML_CUDA_COMPRESSION_MODE` | CUDA >= 12.8: `none`, `speed`, `balance`, `size` (PTXAS compression) |

### Architecture Notes

- `120a-real` is the correct target for RTX 5060 Ti Super (sm_120)
- The `a` suffix enables FP4 tensor core instructions (NVFP4, MXFP4)
- `121a-real` is for GB10 (sm_121) — NOT the 5060 Ti
- `103-real` is for a different Blackwell variant (sm_103)
- CMake auto-replaces `12X` with `12Xa` for FP4 tensor core support when CUDA >= 12.8

### CUDA Version Requirements

- **Minimum:** CUDA 12.8 for `120a-real` and compression modes
- **Recommended:** CUDA 12.9+ for full Blackwell feature set
- Verify: `nvcc --version` and `nvidia-smi`

## 2. MMQ (Matrix-Matrix Quantized) Kernel Tuning

### Current State of Blackwell Support

File: `ggml/src/ggml-cuda/mmq-config-blackwell.cuh`

**Critical Gap:** The Blackwell mmq config file ONLY covers:
- `MXFP4` quantization
- `NVFP4` quantization

All other quant types (Q4_0, Q4_1, Q5_0, Q5_1, Q8_0, Q8_1, etc.) **fall back to Ampere-tuned configs** via `ggml_cuda_mmq_get_config_ampere()`. This is the single biggest performance gap.

### Config Structure

```cpp
// Config params: (type, J, 1, 128, K, SRAM_LAYOUT, ITER_K, bool, bool)
// J = number of threads in x-dimension
// K = number of elements per thread in k-dimension
// SRAM_LAYOUT = shared memory tiling strategy
// ITER_K = k-loop iteration count
```

### Action: Add Missing Blackwell Configs

Add tuned configs for standard quant types in `mmq-config-blackwell.cuh`:

```cpp
// Example pattern — tune J, K, SRAM_LAYOUT for sm_120
case GGML_TYPE_Q4_0:
    // Blackwell has 256KB shared memory per SM (vs 100KB on Ampere)
    // Larger tiles are viable
    return {GGML_TYPE_Q4_0, 128, 1, 128, 32, SRAM_LAYOUT_2D, 4, true, false};
    // ^ Tune J=128 (wider), K=32 (deeper), larger SRAM tiles

case GGML_TYPE_Q4_1:
    return {GGML_TYPE_Q4_1, 128, 1, 128, 32, SRAM_LAYOUT_2D, 4, true, false};

case GGML_TYPE_Q5_0:
    return {GGML_TYPE_Q5_0, 128, 1, 128, 24, SRAM_LAYOUT_2D, 4, true, false};

case GGML_TYPE_Q5_1:
    return {GGML_TYPE_Q5_1, 128, 1, 128, 24, SRAM_LAYOUT_2D, 4, true, false};

case GGML_TYPE_Q8_0:
    return {GGML_TYPE_Q8_0, 64, 1, 128, 16, SRAM_LAYOUT_2D, 4, true, false};
```

**Tuning methodology:**
1. Start with Ampere configs as baseline
2. Increase shared memory tile sizes (Blackwell has ~2.5x more SRAM)
3. Profile with Nsight Compute (`ncu`)
4. Look for: shared memory bank conflicts, occupancy, memory throughput
5. Iterate on J, K, SRAM_LAYOUT parameters

### Key Files for MMQ

- `mmq.cu` / `mmq.cuh` — main quantized GEMM kernel
- `mmq-config-blackwell.cuh` — Blackwell-specific configs (PRIMARY TARGET)
- `mmq-config-ampere.cuh` — fallback configs (reference)
- `template-instances/mmq*.cu` — template specializations

## 3. FP8 / NVFP4 / MXFP4 Quantization Paths

### FP8 (E4M3, E5M2)

File: `fp8.cu` / `fp8.cuh`

- Blackwell tensor cores natively support FP8 operations
- Ensure `GGML_CUDA_FA` is enabled for FP8 flash attention
- FP8 GEMM should use tensor core MMA instructions (not SIMT)
- Check that `__nv_fp8_e4m3` and `__nv_fp8_e5m2` types are used in kernels

### NVFP4 (4-bit floating point)

- Native to Blackwell tensor cores
- Already has Blackwell-specific mmq config
- Verify the `120a-real` architecture flag is active (enables FP4 instructions)
- Profile: compare NVFP4 vs Q4_0 throughput — NVFP4 should win on Blackwell

### MXFP4 (Microscaling FP4)

- Also native to Blackwell
- Already has Blackwell-specific mmq config
- Use for weight-only quantization with per-block scaling

### Action Items

1. Verify FP8/NVFP4/MXFP4 kernels compile with `120a-real`
2. Benchmark all three against Q4_0/Q4_1 baselines
3. Check for correct tensor core instruction selection in SASS
4. Ensure quantization kernels (`quantize.cu`) produce correct formats

## 4. Flash Attention Optimization

### Key Files

- `fattn.cu` / `fattn.cuh` — main flash attention kernel
- `fattn-mma-f16.cuh` — MMA-based flash attention (FP16)
- `fattn-tile.cuh` — tile-based flash attention
- `template-instances/fattn-tile*.cu` — tile specializations
- `template-instances/fattn-mma*.cu` — MMA specializations

### Blackwell-Specific Considerations

1. **Larger shared memory:** 256KB per SM allows larger attention tiles
   - Increase tile sizes in `fattn-tile.cuh`
   - More heads can be processed per CTA

2. **Tensor core MMA:** Blackwell has enhanced MMA units
   - `fattn-mma-f16.cuh` should use the widest available MMA shapes
   - Check for `mma.sync` vs `wgmma` instruction selection

3. **Async memory operations:** Blackwell supports enhanced `cp.async`
   - Overlap Q/K/V loads with computation
   - Use `cp.async.bulk` for large tile transfers

4. **Tuning parameters:**
   - Tile dimensions (M, N, K for Q, K, V matrices)
   - Number of pipeline stages
   - Shared memory swizzle patterns to avoid bank conflicts

### Action Items

1. Profile flash attention with `ncu` — look for memory stalls
2. Increase tile sizes to exploit larger SRAM
3. Verify async copy pipelines are fully utilized
4. Compare `fattn-tile` vs `fattn-mma` paths — pick winner per head count

## 5. GDDR7 Memory Subsystem Tuning

### RTX 5060 Ti Super Memory Specs

- GDDR7, 256-bit bus
- Higher bandwidth than GDDR6 (RTX 40xx)
- Lower latency than HBM (datacenter)

### Optimization Strategies

1. **Memory coalescing:** Ensure global memory accesses are fully coalesced
   - Check `ncu` metrics: `l1tex__t_sectors_pipe_lsu_mem_global_op_ld`
   - Target: 1 sector per 32-byte access

2. **Shared memory tiling:** Larger tiles reduce global memory traffic
   - Blackwell's 256KB SRAM allows 2-3x larger tiles than Ampere
   - Update all kernel tile sizes accordingly

3. **L2 cache utilization:**
   - GDDR7 has higher bandwidth but L2 is still the bottleneck for small working sets
   - Use `__ldg()` (read-only cache) for weight matrices
   - Consider L2 persistence hints for frequently-accessed weights

4. **Async copies:**
   - `cp.async` for global→shared transfers
   - Multi-stage pipelines to hide memory latency
   - Blackwell supports larger async copy granularity

5. **VMM considerations:**
   - `GGML_CUDA_NO_VMM` disables virtual memory management
   - For large models, VMM helps with memory mapping
   - Test both paths for the target model size

## 6. Profiling Workflow

### Nsight Compute (ncu)

```bash
# Profile a specific kernel
ncu --set full --kernel-name regex:ggml_cuda_mmq \
  --export profile_mmq ./build/bin/llama-bench -m model.gguf -p 512

# Key metrics to watch:
# - sm__throughput.avg.pct_of_peak_sustained_elapsed
# - l1tex__t_sector_hit_rate.pct
# - lts__t_sector_hit_rate.pct (L2)
# - dram__throughput.avg.pct_of_peak_sustained_elapsed
# - launch__occupancy
# - shared memory bank conflicts
```

### Nsight Systems (nsys)

```bash
# System-level profiling
nsys profile -o trace ./build/bin/llama-bench -m model.gguf -p 512
```

### Benchmarking

```bash
# Use llama-bench for token generation speed
./build/bin/llama-bench -m model.gguf -p 512 -n 128

# Compare: prompt processing (prefill) vs generation (decode)
# Prefill: compute-bound → tensor core utilization matters
# Decode: memory-bound → bandwidth and latency matter
```

## 7. Other Kernel Files (Secondary Priority)

| File | Purpose | Blackwell Note |
|------|---------|---------------|
| `mmvf.cu` / `mmvf.cuh` | Float GEMM | Use cuBLAS or tuned MMA |
| `mmvq.cu` / `mmvq.cuh` | Vector quantized GEMM | Decode path — memory bound |
| `quantize.cu` / `quantize.cuh` | Quantization | Ensure FP8/NVFP4 output formats |
| `convert.cu` / `convert.cuh` | Type conversion | FP16↔FP8, BF16↔FP8 |
| `rope.cu` / `rope.cuh` | RoPE embeddings | Usually not bottleneck |
| `norm.cu` / `norm.cuh` | Layer norm | Usually not bottleneck |
| `softmax.cu` / `softmax.cuh` | Softmax | Check for warp-level reductions |

## 8. Testing

### Unit Tests

```bash
# Run ggml-cuda unit tests
cd build && ctest -R ggml-cuda --output-on-failure

# Specific test suites
ctest -R "ggml-cuda.*mmq" --output-on-failure
ctest -R "ggml-cuda.*fattn" --output-on-failure
ctest -R "ggml-cuda.*fp8" --output-on-failure
```

### Validation

1. Compare outputs against CPU reference (bit-exact for FP32, tolerance for quantized)
2. Verify no NaN/Inf in outputs
3. Check quantization round-trip accuracy
4. Benchmark regression: new config must beat Ampere fallback

## 9. Optimization Priority Order

1. **MMQ configs for standard quant types** (biggest gap, easiest win)
2. **Flash attention tile sizes** (compute-bound prefill path)
3. **FP8/NVFP4 path verification** (ensure native tensor core usage)
4. **Shared memory tiling across all kernels** (exploit 256KB SRAM)
5. **Async copy pipelines** (hide GDDR7 latency)
6. **Memory access patterns** (coalescing, L2 utilization)
7. **Secondary kernels** (norm, rope, softmax — usually not bottleneck)

## 10. Reference: Architecture Constants

From `common.cuh`:

```cpp
#define GGML_CUDA_CC_BLACKWELL  1200   // sm_120 (RTX 5060 Ti Super)
#define GGML_CUDA_CC_GB10       1210   // sm_121 (GB10)
#define GGML_CUDA_CC_RUBIN      1300   // sm_130 (Rubin, future)
```

Verify your GPU:
```bash
nvidia-smi --query-gpu=name,compute_cap --format=csv
# Expected: NVIDIA GeForce RTX 5060 Ti Super, 12.0
```

## Changelog

- 2025-07-11: Initial skill created from code inspection of llama.cpp CUDA backend
  - Inspected: CMakeLists.txt, common.cuh, mmq-config-blackwell.cuh
  - Identified: missing mmq configs for standard quant types as primary gap
  - Documented: build flags, tuning methodology, profiling workflow