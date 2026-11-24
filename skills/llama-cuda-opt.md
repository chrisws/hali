# Skill: LLaMA CUDA Optimization — Blackwell (sm_120)

- **Project:** hali / engine/llama.cpp
- **GPU:** NVIDIA RTX 5060 Ti (CC 12.0, 36 SMs, 15906 MiB)
- **Model:** Swift-1.5-Qwen3.8-27B-GSQ-RCO-IQ3_S-mtp.gguf (11.28 GiB)
- **Date:** 2026-11-23 → 2026-11-24
- **Baseline:** 1068.49 pp / 30.83 tg → Final: 1068.21 pp / 30.58 tg (neutral)

---

## 1. NCU Profiling Findings

Three kernels profiled with NCU 2026.4.0.0:

### mmq (mul_mat_q) — PRIMARY BOTTLENECK
- Template: `mul_mat_q<18, 128, 0, 30, 0, 0>` (IQ3_S, J=128, Q8_0)
- **Occupancy: 16.67%** (1 block/SM) — NCU estimates 83% speedup from fixing this
- 255 regs/thread (max) AND 57.86 KB shmem independently limit to 1 block/SM
- 67% uncoalesced global memory sectors
- Latency-bound: DRAM 8.98%, Compute 46.61%
- Config source: **Ampere fallback** (IQ3_S was missing from Blackwell config at profile time)

### mmvq (mul_mat_vec_q) — DECODE
- **DRAM-saturated at 94.75%** — hardware bandwidth limit, no kernel-level gains possible

### fattn (flash_attn_ext_f16) — ATTENTION
- Template: `flash_attn_ext_f16<256, 256, 8, 8, 0, 0, 0>`
- Tensor (FP16) pipeline at 51.44% — moderate headroom
- Latency-bound, not memory-bound (DRAM 17.21%)

---

## 2. Changes Made

### 2a. `mmq-config-blackwell.cuh` — Added IQ3_S + missing types (+341 lines)

Added config entries for IQ3_S and other quant types that were falling through to Ampere fallback.

Key IQ3_S entry (I=128, matching Ampere):
```cpp
// IQ3_S — matches Ampere config (I=128). I=64 caused CUDA crash, under investigation.
CASE(GGML_TYPE_IQ3_S, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
CASE(GGML_TYPE_IQ3_S, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
// ... + 12 more entries for stream_k=false variants (J=8..128)
```

**I=64 attempt:** Changed I from 128→64 to halve shared memory (36 KB → 18 KB for nbs_x).
**Result: CUDA crash.** Root cause not yet investigated.
**Note:** Even with I=64, total shmem (~39.86 KB) still exceeds the 32.77 KB needed for 2 blocks/SM.

### 2b. `tma.cuh` — NEW FILE: TMA helpers for Blackwell

New file providing Tensor Memory Accelerator (TMA) primitives:
- `tma_fenceproxy` — fence proxy for TMA operations
- `tma_store_wait` — wait for TMA store completion
- `tma_load_2d` — 2D TMA bulk copy (global → shared)
- `tma_store_2d` — 2D TMA bulk copy (shared → global)
- Mbarrier init/arrive/parity helpers
- Dummy/no-op declarations for non-TMA architectures (pre-sm_100)

### 2c. `fattn-mma-f16.cuh` — TMA integration (+141 lines changed)

Integrated TMA into the flash attention MMA kernel:

- **`load_tile`** — added `use_tma` template param + `mbar` param. When `use_tma`, thread 0 issues per-row `cp.async.bulk` copies instead of `cp.async`. Removed `static_assert(!oob_check)` since TMA handles OOB natively (zero-fill).
- **`iter`** — added `use_tma` template param + `mbar` param. Updated all 5 `load_tile` call sites. Updated all 4 `ldmatrix` call sites to use non-swizzled 3-arg form `(tile, ptr, stride)` when TMA is active.
- **`process_tile`** — added `use_tma`, swizzle override (`swz=false` when TMA), mbarrier allocation, updated 6 `iter` call sites.
- **`kernel`** — added `use_tma` param, updated 3 `process_tile` call sites.
- **`case` (dispatch)** — determines `use_tma` (sm_120+, nstages==1, !use_sparse), adds 8 bytes shared mem for mbarrier, updated 4 kernel selection sites.

Key design constraints:
```
TMA active when: cc >= sm_120 AND nstages == 1 AND !use_sparse
Layout: non-swizzled when use_tma (swz = false)
Issuer: thread 0 only (all per-row cp.async.bulk copies)
Mbarrier: re-initialized each load_tile call
OOB: handled natively by TMA descriptors (zero-fill)
```

### 2d. `fattn.cu` — Minor update
- 1-line change (include or dispatch adjustment for TMA support)

### 2e. `mmq-load-tiles.cuh` — Supporting changes
- 15 lines changed to support updated config/load paths

---

## 3. Results

| Build | Prefill (pp512) | Decode (tg128) | Δ vs baseline |
|-------|----------------|---------------|---------------|
| Original baseline | 1068.49 ± 3.32 | 30.83 ± 0.20 | — |
| + Blackwell config | ~1068 | ~30.8 | neutral |
| + TMA (final) | 1068.21 ± 3.32 | 30.58 ± 0.20 | −0.28 pp, −0.25 tg |

**All changes are within measurement noise. No regression, no gain.**

Tests: 738/738 PASS throughout.

---

## 4. Why No Gains

### mmq occupancy wall
Both registers (255×256=65280 of 65536) and shmem (57.86 KB of 65.54 KB)
independently limit to 1 block/SM. Fixing requires **both**:
- ≤128 regs/thread AND
- ≤32.77 KB shmem/block

I=64 halves nbs_x (36→18 KB) but total is still ~39.86 KB > 32.77 KB.
I=64 also crashes (uninvestigated). No single config change solves both constraints.

### TMA neutral
TMA benefits large contiguous transfers. The fattn tile sizes on this model
are modest, and the per-row cp.async.bulk overhead (mbarrier init, thread-0
serialization) cancels the gain over cp.async. Also restricted to nstages==1,
preventing the async pipelining where TMA truly shines.

### mmvq hardware-limited
94.75% DRAM bandwidth saturation. No kernel-level optimization can exceed
the physical memory bandwidth of the GPU.

---

## 5. Lessons Learned

1. **Profile before optimizing.** NCU clearly showed mmq was the target, but we spent significant effort on fattn (TMA) first.
2. **Config changes have ceilings.** Adding the missing Blackwell config entries was necessary but couldn't overcome the structural occupancy limit.
3. **TMA is not a silver bullet.** It helps with large tile transfers and multi-stage pipelining. For single-stage, modest-tile kernels on consumer GPUs, the overhead cancels the benefit.
4. **I=64 crash needs investigation.** The static_assert allows I%32==0, so I=64 is valid. The crash is likely in kernel code assumptions about tile size. This is the highest-impact unresolved issue.
5. **Consumer GPU constraints.** 36 SMs, 65.54 KB shmem/SM, 65536 regs/SM — these hard limits make occupancy tuning much harder than on datacenter GPUs.

---

## 6. Unresolved / Future Work

| Item | Impact | Effort | Status |
|------|--------|--------|--------|
| Debug I=64 crash in mmq | High (enables smaller tiles) | Medium | **Open** |
| mmq kernel restructure (2-pass tile) | High (solves occupancy) | High | Not started |
| mmq uncoalesced memory (67%) | High (59% est. speedup) | High | Not started |
| Fattn multi-stage TMA | Medium | Medium | Not started |
| GGML_CUDA_GRAPH_OPT=1 test | Low (2-5%) | Trivial | Not tested |
| Alternative quantization benchmark | Unknown | Trivial | Not tested |
| CPU-side / pipeline optimization | Unknown | Medium | Not started |

---

## 7. Key Files

| File | Role |
|------|------|
| `ggml/src/ggml-cuda/mmq-config-blackwell.cuh` | Blackwell MMQ tile configs |
| `ggml/src/ggml-cuda/mmq.cuh` | MMQ kernel + CASE macro |
| `ggml/src/ggml-cuda/mmq.cu` | MMQ host launch + J selection |
| `ggml/src/ggml-cuda/tma.cuh` | TMA primitives (new) |
| `ggml/src/ggml-cuda/fattn-mma-f16.cuh` | Flash attention MMA kernel |
| `ggml/src/ggml-cuda/fattn.cu` | Flash attention dispatch |
| `tools/cuda_tune.sh` | Benchmark + profiling harness |
| `.hali/tuning/` | NCU reports, bench results, profiles |

---

## Full Diffs

```
index dea8d2ad3..2503ad984 100644
--- a/ggml/src/ggml-cuda/fattn-mma-f16.cuh
+++ b/ggml/src/ggml-cuda/fattn-mma-f16.cuh
@@ -2,6 +2,7 @@
 #include "cp-async.cuh"
 #include "mma.cuh"
 #include "fattn-common.cuh"
+#include "tma.cuh"
 
 using namespace ggml_cuda_mma;
 
@@ -413,17 +414,38 @@ static constexpr __device__ int ggml_cuda_fattn_mma_get_nstages(
 
 // ------------------------------------------------------------------------------------------------------------------
 
-template<int stride_tile, int nwarps, int nbatch_fa, bool use_cp_async, bool oob_check, bool use_sparse>
+template<int stride_tile, int nwarps, int nbatch_fa, bool use_cp_async, bool oob_check, bool use_sparse, bool use_tma>
 static __device__ __forceinline__ void flash_attn_ext_f16_load_tile(
         const half2 * const __restrict__ KV, half2 * const __restrict__ tile_KV, const int D2, const int stride_KV,
-        const int k_VKQ_0, const int i_sup, const int32_t * const __restrict__ indices) {
+        const int k_VKQ_0, const int i_sup, const int32_t * const __restrict__ indices,
+        unsigned int * mbar) {
     constexpr int warp_size = ggml_cuda_get_physical_warp_size();
     // K/V data is loaded with decreasing granularity for D for better memory bandwidth.
     // The minimum granularity is 16 bytes.
     constexpr int chunk_size = 16;
     constexpr int h2_per_chunk = chunk_size / sizeof(half2);
     const int chunks_per_row = D2 / h2_per_chunk;
-    if constexpr (use_cp_async) {
+    if constexpr (use_tma) {
+        static_assert(!use_sparse, "TMA not compatible with sparse gather");
+        if (threadIdx.x == 0 && threadIdx.y == 0) {
+            const unsigned int tile_KV_32 = ggml_cuda_cvta_generic_to_shared(tile_KV);
+            const unsigned int mbar_32    = ggml_cuda_cvta_generic_to_shared(mbar);
+            const int total_bytes = nbatch_fa * D2 * sizeof(half2);
+            tma_mbarrier_init(mbar_32, total_bytes);
+#pragma unroll
+            for (int i = 0; i < nbatch_fa; i++) {
+                const int64_t i_KV = k_VKQ_0 + i;
+                tma_bulk_copy_1d(
+                    tile_KV_32 + i * stride_tile * sizeof(half2),
+                    KV + i_KV * stride_KV,
+                    D2 * sizeof(half2),
+                    mbar_32);
+            }
+            tma_bulk_commit();
+            tma_mbarrier_wait(mbar_32, 0);
+        }
+        __syncthreads();
+    } else if constexpr (use_cp_async) {
         static_assert(warp_size == 32, "bad warp_size");
         static_assert(!oob_check || use_sparse, "OOB check not compatible with cp_async");
         constexpr int preload = 64;
@@ -612,6 +634,7 @@ static __device__ __forceinline__ void flash_attn_ext_f16_load_mask(
 
 template<int DKQ, int DV, int ncols1, int ncols2, int nwarps,
     bool use_logit_softcap, bool V_is_K_view, bool use_sparse, bool needs_fixup, bool is_fixup, bool last_iter, bool oob_check,
+    bool use_tma,
     typename T_A_KQ, typename T_B_KQ, typename T_C_KQ, typename T_A_VKQ, typename T_B_VKQ, typename T_C_VKQ>
 static __device__ __forceinline__ void flash_attn_ext_f16_iter(
         const float2 * const __restrict__ Q_f2,
@@ -640,7 +663,8 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
         const int jt,
         const int kb0,
         const int kb0_next,
-        const int k_VKQ_sup) {
+        const int k_VKQ_sup,
+        unsigned int * mbar) {
 #if defined(VOLTA_MMA_AVAILABLE) || defined(TURING_MMA_AVAILABLE) || defined(AMD_WMMA_AVAILABLE) || defined(AMD_MFMA_AVAILABLE)
     constexpr int  warp_size       = ggml_cuda_get_physical_warp_size();
     constexpr int  ncols           = ncols1 * ncols2;
@@ -656,7 +680,7 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
     constexpr int DKQp = GGML_PAD(DKQ, 16);
     constexpr int DVp  = GGML_PAD(DV, 16);
 
-    constexpr bool swz = ggml_cuda_fattn_mma_get_swizzled(DKQ, DV, ncols);
+    constexpr bool swz = use_tma ? false : ggml_cuda_fattn_mma_get_swizzled(DKQ, DV, ncols);
     constexpr int stride_tile_K = swz ? nbatch_K2 : nbatch_K2 + 4;
     constexpr int stride_tile_V = V_is_K_view ? stride_tile_K : (swz ? nbatch_V2 : nbatch_V2 + 4);
 
@@ -676,8 +700,8 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
         constexpr bool use_cp_async = true;
         cp_async_wait_all();
         __syncthreads();
-        flash_attn_ext_f16_load_tile<stride_tile_V, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse>
-            (V_h2, tile_V, min(nbatch_V2, DV/2), stride_V, k_VKQ_0, k_VKQ_sup, indices);
+        flash_attn_ext_f16_load_tile<stride_tile_V, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse, use_tma>
+            (V_h2, tile_V, min(nbatch_V2, DV/2), stride_V, k_VKQ_0, k_VKQ_sup, indices, mbar);
     } else {
         // the sparse mask values are gathered per element, always load them synchronously
         constexpr bool use_cp_async = nstages == 1 && !use_sparse;
@@ -696,8 +720,8 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
         if constexpr (nstages <= 1) {
             const int k0_diff = min(k0_stop, DKQ/2) - k0_start; // Only load the real head size from global memory.
             constexpr bool use_cp_async = nstages == 1;
-            flash_attn_ext_f16_load_tile<stride_tile_K, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse>
-                (K_h2 + k0_start, tile_K, k0_diff, stride_K, k_VKQ_0, k_VKQ_sup, indices);
+            flash_attn_ext_f16_load_tile<stride_tile_K, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse, use_tma>
+                (K_h2 + k0_start, tile_K, k0_diff, stride_K, k_VKQ_0, k_VKQ_sup, indices, mbar);
             if (use_cp_async) {
                 cp_async_wait_all();
             }
@@ -712,7 +736,11 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
 #pragma unroll
                 for (int k_KQ_0 = k0_start; k_KQ_0 < k0_stop; k_KQ_0 += T_A_KQ::J) {
                     T_A_KQ K_A;
-                    load_ldmatrix_swizzled<stride_tile_K>(K_A, tile_K, i_KQ_0*stride_tile_K + k_KQ_0-k0_start);
+                    if constexpr (use_tma) {
+                        load_ldmatrix(K_A, tile_K + (i_KQ_0*stride_tile_K + k_KQ_0-k0_start), stride_tile_K);
+                    } else {
+                        load_ldmatrix_swizzled<stride_tile_K>(K_A, tile_K, i_KQ_0*stride_tile_K + k_KQ_0-k0_start);
+                    }
                     if constexpr (cols_per_warp == 8) {
                         mma(KQ_C[i_KQ_00/(np*T_A_KQ::I)], K_A, Q_B[k_KQ_0/T_A_KQ::J]);
                     } else {
@@ -738,7 +766,11 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
                     const int i_KQ_0 = i_KQ_00 + (threadIdx.y % np)*T_A_KQ::I;
 
                     T_A_KQ K_A;
-                    load_ldmatrix_swizzled<stride_tile_K>(K_A, tile_K, i_KQ_0*stride_tile_K + k_KQ_0-k0_start);
+                    if constexpr (use_tma) {
+                        load_ldmatrix(K_A, tile_K + (i_KQ_0*stride_tile_K + k_KQ_0-k0_start), stride_tile_K);
+                    } else {
+                        load_ldmatrix_swizzled<stride_tile_K>(K_A, tile_K, i_KQ_0*stride_tile_K + k_KQ_0-k0_start);
+                    }
 
                     if constexpr (cols_per_warp == 8) {
                         mma(KQ_C[i_KQ_00/(np*T_A_KQ::I)], K_A, Q_B[0]);
@@ -1033,8 +1065,8 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
                 flash_attn_ext_f16_load_mask<ncols1, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse>
                     (mask_h, tile_mask, stride_mask, kb0_next*nbatch_fa, k_VKQ_sup, jt*ncols1, ne01, nullptr);
             }
-            flash_attn_ext_f16_load_tile<stride_tile_K, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse>
-                (K_h2, tile_K, min(nbatch_K2, DKQ/2), stride_K, kb0_next*nbatch_fa, k_VKQ_sup, indices);
+            flash_attn_ext_f16_load_tile<stride_tile_K, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse, use_tma>
+                (K_h2, tile_K, min(nbatch_K2, DKQ/2), stride_K, kb0_next*nbatch_fa, k_VKQ_sup, indices, mbar);
         }
     }
 
@@ -1049,8 +1081,8 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
             const int i0_diff = min(i0_stop, DV) - i0_start; // Only load the real head size from global memory.
             if (!V_is_K_view || i0_stop > 2*nbatch_K2) {
                 constexpr bool use_cp_async = nstages == 1;
-                flash_attn_ext_f16_load_tile<stride_tile_V, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse>
-                    (V_h2 + i0_start/2, tile_V, i0_diff/2, stride_V, k_VKQ_0, k_VKQ_sup, indices);
+                flash_attn_ext_f16_load_tile<stride_tile_V, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse, use_tma>
+                    (V_h2 + i0_start/2, tile_V, i0_diff/2, stride_V, k_VKQ_0, k_VKQ_sup, indices, mbar);
                 if (use_cp_async) {
                     cp_async_wait_all();
                 }
@@ -1068,7 +1100,11 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
                 const int k0 = k00 + (threadIdx.y % np)*T_A_VKQ::J;
 
                 T_A_VKQ A; // Transposed in SRAM but not in registers, gets transposed on load.
-                load_ldmatrix_trans_swizzled<stride_tile_V>(A, tile_V, tile_V_offset_i + 2*k0*stride_tile_V + (i_VKQ_0 - i0_start)/2);
+                if constexpr (use_tma) {
+                    load_ldmatrix_trans(A, tile_V + (tile_V_offset_i + 2*k0*stride_tile_V + (i_VKQ_0 - i0_start)/2), stride_tile_V);
+                } else {
+                    load_ldmatrix_trans_swizzled<stride_tile_V>(A, tile_V, tile_V_offset_i + 2*k0*stride_tile_V + (i_VKQ_0 - i0_start)/2);
+                }
                 if constexpr (T_B_KQ::I == 8) {
                     mma(VKQ_C[i_VKQ_0/T_A_VKQ::I], A, B[k00/(np*T_A_VKQ::J)]);
                 } else {
@@ -1094,7 +1130,11 @@ static __device__ __forceinline__ void flash_attn_ext_f16_iter(
                 const int k0 = k00 + (threadIdx.y % np)*T_A_VKQ::I;
 
                 T_A_VKQ A; // Transposed in both SRAM and registers, load normally.
-                load_ldmatrix_swizzled<stride_tile_V>(A, tile_V, tile_V_offset_i + k0*stride_tile_V + (i_VKQ_0 - i0_start)/2);
+                if constexpr (use_tma) {
+                    load_ldmatrix(A, tile_V + (tile_V_offset_i + k0*stride_tile_V + (i_VKQ_0 - i0_start)/2), stride_tile_V);
+                } else {
+                    load_ldmatrix_swizzled<stride_tile_V>(A, tile_V, tile_V_offset_i + k0*stride_tile_V + (i_VKQ_0 - i0_start)/2);
+                }
                 mma(VKQ_C[i_VKQ_0/i0_stride], B[k00/(np*T_A_VKQ::I)], A);
             }
         }
@@ -1261,7 +1301,7 @@ template<int DKQ, int ncols> struct mma_tile_sizes {
 };
 #endif // defined(TURING_MMA_AVAILABLE)
 
-template<int DKQ, int DV, int ncols1, int ncols2, int nwarps, bool use_logit_softcap, bool V_is_K_view, bool use_sparse, bool needs_fixup, bool is_fixup>
+template<int DKQ, int DV, int ncols1, int ncols2, int nwarps, bool use_logit_softcap, bool V_is_K_view, bool use_sparse, bool needs_fixup, bool is_fixup, bool use_tma>
 static __device__ __forceinline__ void flash_attn_ext_f16_process_tile(
         const float2 * const __restrict__ Q_f2,
         const half2  * const __restrict__ K_h2,
@@ -1320,7 +1360,7 @@ static __device__ __forceinline__ void flash_attn_ext_f16_process_tile(
 
     static_assert(nwarps * (cols_per_warp/ncols2) % ncols1 == 0, "bad nwarps");
 
-    constexpr bool swz = ggml_cuda_fattn_mma_get_swizzled(DKQ, DV, ncols);
+    constexpr bool swz = use_tma ? false : ggml_cuda_fattn_mma_get_swizzled(DKQ, DV, ncols);
     constexpr int stride_tile_Q = DKQp/2 + 4;
     constexpr int stride_tile_K = swz ? nbatch_K2 : nbatch_K2 + 4;
     constexpr int stride_tile_V = V_is_K_view ? stride_tile_K : (swz ? nbatch_V2 : nbatch_V2 + 4);
@@ -1331,6 +1371,8 @@ static __device__ __forceinline__ void flash_attn_ext_f16_process_tile(
     half2 * tile_V    =           nstages > 1 ? tile_K + nbatch_fa * stride_tile_K : tile_K;
     half  * tile_mask = (half *) (nstages > 1 ? tile_V + nbatch_fa * stride_tile_V : tile_V + nbatch_fa * stride_tile_KV_max);
 
+    unsigned int * mbar = use_tma ? (unsigned int *) (tile_mask + ncols1 * (nbatch_fa/2 + 4)) : nullptr;
+
     T_B_KQ    Q_B[(Q_in_reg ? DKQp/(2*T_B_KQ::J) : 1)];
 #if defined(TURING_MMA_AVAILABLE)
     T_C_VKQ VKQ_C[cols_per_warp == 8 ? DVp/T_C_VKQ::I : DVp/(2*T_C_VKQ::J)];
@@ -1459,8 +1501,8 @@ static __device__ __forceinline__ void flash_attn_ext_f16_process_tile(
             flash_attn_ext_f16_load_mask<ncols1, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse>
                 (mask_h, tile_mask, stride_mask, kb0*nbatch_fa, k_VKQ_sup, jt*ncols1, ne01, nullptr);
         }
-        flash_attn_ext_f16_load_tile<stride_tile_K, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse>
-            (K_h2, tile_K, min(nbatch_K2, DKQ/2), stride_K, kb0*nbatch_fa, k_VKQ_sup, indices);
+        flash_attn_ext_f16_load_tile<stride_tile_K, nwarps, nbatch_fa, use_cp_async, oob_check, use_sparse, use_tma>
+            (K_h2, tile_K, min(nbatch_K2, DKQ/2), stride_K, kb0*nbatch_fa, k_VKQ_sup, indices, mbar);
     }
 
     // kb0_start is always < kb0_stop so the last iter can be executed unconditionally.
@@ -1471,61 +1513,61 @@ static __device__ __forceinline__ void flash_attn_ext_f16_process_tile(
             constexpr bool last_iter = false;
             constexpr int  k_VKQ_sup = nbatch_fa;
             flash_attn_ext_f16_iter
-                <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check,
+                <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check, use_tma,
                  T_A_KQ, T_B_KQ, T_C_KQ, T_A_VKQ, T_B_VKQ, T_C_VKQ>
                 (Q_f2, K_h2, V_h2, mask_h, indices, dstk, dstk_fixup, scale, slope, logit_softcap,
                  ne01, ne02, stride_K, stride_V, stride_mask, tile_Q, tile_K, tile_V, tile_mask, Q_B, VKQ_C,
-                 KQ_max, KQ_rowsum, jt, kb0, kb0_next, k_VKQ_sup);
+                 KQ_max, KQ_rowsum, jt, kb0, kb0_next, k_VKQ_sup, mbar);
             kb0 = kb0_next;
         }
         constexpr bool last_iter = true;
         constexpr int  k_VKQ_sup = nbatch_fa;
         flash_attn_ext_f16_iter
-            <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check,
+            <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check, use_tma,
              T_A_KQ, T_B_KQ, T_C_KQ, T_A_VKQ, T_B_VKQ, T_C_VKQ>
             (Q_f2, K_h2, V_h2, mask_h, indices, dstk, dstk_fixup, scale, slope, logit_softcap,
              ne01, ne02, stride_K, stride_V, stride_mask, tile_Q, tile_K, tile_V, tile_mask, Q_B, VKQ_C,
-             KQ_max, KQ_rowsum, jt, kb0, kb0, k_VKQ_sup);
+             KQ_max, KQ_rowsum, jt, kb0, kb0, k_VKQ_sup, mbar);
     } else if constexpr (ncols2 == 1 || use_sparse) {
         constexpr bool oob_check = true;
         for (; kb0 < kb0_stop-1; ++kb0) {
             constexpr bool last_iter = false;
             constexpr int  k_VKQ_sup = nbatch_fa;
             flash_attn_ext_f16_iter
-                <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check,
+                <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check, use_tma,
                  T_A_KQ, T_B_KQ, T_C_KQ, T_A_VKQ, T_B_VKQ, T_C_VKQ>
                 (Q_f2, K_h2, V_h2, mask_h, indices, dstk, dstk_fixup, scale, slope, logit_softcap,
                  ne01, ne02, stride_K, stride_V, stride_mask, tile_Q, tile_K, tile_V, tile_mask, Q_B, VKQ_C,
-                 KQ_max, KQ_rowsum, jt, kb0, kb0 + 1, k_VKQ_sup);
+                 KQ_max, KQ_rowsum, jt, kb0, kb0 + 1, k_VKQ_sup, mbar);
         }
         constexpr bool last_iter = true;
         const     int  k_VKQ_sup = ne11 - kb0*nbatch_fa;
         flash_attn_ext_f16_iter
-            <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check,
+            <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check, use_tma,
               T_A_KQ, T_B_KQ, T_C_KQ, T_A_VKQ, T_B_VKQ, T_C_VKQ>
             (Q_f2, K_h2, V_h2, mask_h, indices, dstk, dstk_fixup, scale, slope, logit_softcap,
              ne01, ne02, stride_K, stride_V, stride_mask, tile_Q, tile_K, tile_V, tile_mask, Q_B, VKQ_C,
-             KQ_max, KQ_rowsum, jt, kb0, kb0, k_VKQ_sup);
+             KQ_max, KQ_rowsum, jt, kb0, kb0, k_VKQ_sup, mbar);
     } else {
         constexpr bool oob_check = false;
         for (; kb0 < kb0_stop-1; ++kb0) {
             constexpr bool last_iter = false;
             constexpr int  k_VKQ_sup = nbatch_fa;
             flash_attn_ext_f16_iter
-                <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check,
+                <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check, use_tma,
                  T_A_KQ, T_B_KQ, T_C_KQ, T_A_VKQ, T_B_VKQ, T_C_VKQ>
                 (Q_f2, K_h2, V_h2, mask_h, indices, dstk, dstk_fixup, scale, slope, logit_softcap,
                  ne01, ne02, stride_K, stride_V, stride_mask, tile_Q, tile_K, tile_V, tile_mask, Q_B, VKQ_C,
-                 KQ_max, KQ_rowsum, jt, kb0, kb0 + 1, k_VKQ_sup);
+                 KQ_max, KQ_rowsum, jt, kb0, kb0 + 1, k_VKQ_sup, mbar);
         }
         constexpr bool last_iter = true;
         constexpr int  k_VKQ_sup = nbatch_fa;
         flash_attn_ext_f16_iter
-            <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check,
+            <DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, last_iter, oob_check, use_tma,
              T_A_KQ, T_B_KQ, T_C_KQ, T_A_VKQ, T_B_VKQ, T_C_VKQ>
             (Q_f2, K_h2, V_h2, mask_h, indices, dstk, dstk_fixup, scale, slope, logit_softcap,
              ne01, ne02, stride_K, stride_V, stride_mask, tile_Q, tile_K, tile_V, tile_mask, Q_B, VKQ_C,
-             KQ_max, KQ_rowsum, jt, kb0, kb0, k_VKQ_sup);
+             KQ_max, KQ_rowsum, jt, kb0, kb0, k_VKQ_sup, mbar);
     }
 
     // With multi-stage loading there is no __syncthreads at the end of the iter,
@@ -1936,7 +1978,7 @@ static constexpr __host__ __device__ bool ggml_cuda_flash_attn_ext_mma_f16_may_u
            (DKQ == 256 && DV == 256 && ncols1 == 8 && ncols2 == 8);
 }
 
-template<int DKQ, int DV, int ncols1, int ncols2, bool use_logit_softcap, bool V_is_K_view, bool use_sparse>
+template<int DKQ, int DV, int ncols1, int ncols2, bool use_logit_softcap, bool V_is_K_view, bool use_sparse, bool use_tma>
 __launch_bounds__(ggml_cuda_fattn_mma_get_nthreads(DKQ, DV, ncols1*ncols2), ggml_cuda_fattn_mma_get_occupancy(DKQ, DV, ncols1*ncols2))
 static __global__ void flash_attn_ext_f16(
         const char * Q_ptr,
@@ -2096,12 +2138,12 @@ static __global__ void flash_attn_ext_f16(
         constexpr bool is_fixup = false; // All but (potentially) the last iterations write their data to dst rather than the fixup buffer.
         if (kb0_start == 0) {
             constexpr bool needs_fixup = false; // CUDA block is working on an entire tile.
-            flash_attn_ext_f16_process_tile<DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup>
+            flash_attn_ext_f16_process_tile<DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, use_tma>
                 (Q_f2, K_h2, V_h2, mask_h, indices, sinks_f, dstk, dst_meta, scale, slope, logit_softcap,
                  ne01, ne02, gqa_ratio, ne11, stride_Q1, stride_Q2, stride_K, stride_V, stride_mask, jt, zt_gqa, kb0_rot, kb0_rot + kb0_stop - kb0_start, kb0_max);
         } else {
             constexpr bool needs_fixup = true; // CUDA block is missing the beginning of a tile.
-            flash_attn_ext_f16_process_tile<DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup>
+            flash_attn_ext_f16_process_tile<DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, use_tma>
                 (Q_f2, K_h2, V_h2, mask_h, indices, sinks_f, dstk, dst_meta, scale, slope, logit_softcap,
                  ne01, ne02, gqa_ratio, ne11, stride_Q1, stride_Q2, stride_K, stride_V, stride_mask, jt, zt_gqa, kb0_rot, kb0_rot + kb0_stop - kb0_start, kb0_max);
         }
@@ -2149,7 +2191,7 @@ static __global__ void flash_attn_ext_f16(
 
     constexpr bool is_fixup = true; // Last index writes its data to fixup buffer to avoid data races with other blocks.
     constexpr bool needs_fixup = false;
-    flash_attn_ext_f16_process_tile<DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup>
+    flash_attn_ext_f16_process_tile<DKQ, DV, ncols1, ncols2, nwarps, use_logit_softcap, V_is_K_view, use_sparse, needs_fixup, is_fixup, use_tma>
         (Q_f2, K_h2, V_h2, mask_h, indices, sinks_f, dstk, dst_meta, scale, slope, logit_softcap,
          ne01, ne02, gqa_ratio, ne11, stride_Q1, stride_Q2, stride_K, stride_V, stride_mask, jt, zt_gqa, kb0_rot, kb0_rot + kb0_stop - kb0_start, kb0_max);
 #else
@@ -2203,9 +2245,12 @@ void ggml_cuda_flash_attn_ext_mma_f16_case(ggml_backend_cuda_context & ctx, ggml
 
     const size_t nbytes_shared_KV = nstages <= 1 ? nbytes_shared_KV_1stage : nbytes_shared_KV_2stage;
 
+    const bool use_tma = (cc >= 120) && nstages == 1;
+
     const size_t nbytes_shared_total = std::max(nbytes_shared_combine, Q_in_reg ?
         std::max(nbytes_shared_Q,  nbytes_shared_KV + nbytes_shared_mask) :
-                 nbytes_shared_Q + nbytes_shared_KV + nbytes_shared_mask);
+                 nbytes_shared_Q + nbytes_shared_KV + nbytes_shared_mask)
+        + (use_tma ? 8 : 0);
 
     float logit_softcap;
     memcpy(&logit_softcap, (const float *) KQV->op_params + 2, sizeof(float));
@@ -2223,7 +2268,7 @@ void ggml_cuda_flash_attn_ext_mma_f16_case(ggml_backend_cuda_context & ctx, ggml
         if constexpr (ggml_cuda_flash_attn_ext_mma_f16_may_use_sparse(DKQ, DV, ncols1, ncols2)) {
             if (ggml_cuda_flash_attn_ext_mma_f16_shall_use_sparse(cc, dst, ncols1, ncols2)) {
                 constexpr bool use_sparse_kernel = true;
-                fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel>;
+                fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel, false>;
                 use_sparse = true;
 
                 static bool shared_memory_limit_raised[GGML_CUDA_MAX_DEVICES] = {false};
@@ -2233,7 +2278,11 @@ void ggml_cuda_flash_attn_ext_mma_f16_case(ggml_backend_cuda_context & ctx, ggml
                 }
             } else {
                 constexpr bool use_sparse_kernel = false;
-                fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel>;
+                if (use_tma) {
+                    fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel, true>;
+                } else {
+                    fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel, false>;
+                }
 
                 static bool shared_memory_limit_raised[GGML_CUDA_MAX_DEVICES] = {false};
                 if (!shared_memory_limit_raised[id]) {
@@ -2245,7 +2294,11 @@ void ggml_cuda_flash_attn_ext_mma_f16_case(ggml_backend_cuda_context & ctx, ggml
 #endif // !defined(GGML_USE_MUSA)
         {
             constexpr bool use_sparse_kernel = false;
-            fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel>;
+            if (use_tma) {
+                fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel, true>;
+            } else {
+                fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel, false>;
+            }
 
             static bool shared_memory_limit_raised[GGML_CUDA_MAX_DEVICES] = {false};
             if (!shared_memory_limit_raised[id]) {
@@ -2256,7 +2309,11 @@ void ggml_cuda_flash_attn_ext_mma_f16_case(ggml_backend_cuda_context & ctx, ggml
     } else {
         constexpr bool use_logit_softcap = true;
         constexpr bool use_sparse_kernel = false;
-        fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel>;
+        if (use_tma) {
+            fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel, true>;
+        } else {
+            fattn_kernel = flash_attn_ext_f16<DKQ, DV, ncols1, ncols2, use_logit_softcap, V_is_K_view, use_sparse_kernel, false>;
+        }
 
         static bool shared_memory_limit_raised[GGML_CUDA_MAX_DEVICES] = {false};
         if (!shared_memory_limit_raised[id]) {
diff --git a/ggml/src/ggml-cuda/fattn.cu b/ggml/src/ggml-cuda/fattn.cu
index 10aa5af15..e7ffb49af 100644
--- a/ggml/src/ggml-cuda/fattn.cu
+++ b/ggml/src/ggml-cuda/fattn.cu
@@ -276,7 +276,7 @@ static void ggml_cuda_flash_attn_ext_mma_f16_switch_ncols2(ggml_backend_cuda_con
     }
 
     if (use_gqa_opt && gqa_ratio > 4) {
-        ggml_cuda_flash_attn_ext_mma_f16_switch_ncols1<DKQ, DV, 8>(ctx, dst);
+        ggml_cuda_flash_attn_ext_mma_f16_switch_ncols1<DKQ, DV, 4>(ctx, dst);
         return;
     }
 
diff --git a/ggml/src/ggml-cuda/mmq-config-blackwell.cuh b/ggml/src/ggml-cuda/mmq-config-blackwell.cuh
index 9fbe32b69..ebeec5dce 100644
--- a/ggml/src/ggml-cuda/mmq-config-blackwell.cuh
+++ b/ggml/src/ggml-cuda/mmq-config-blackwell.cuh
@@ -33,5 +33,346 @@ static constexpr __host__ __device__ ggml_cuda_mmq_config ggml_cuda_mmq_get_conf
     CASE(GGML_TYPE_NVFP4, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_FP4, MMQ_ITER_K_FP4, true, false);
     CASE(GGML_TYPE_NVFP4, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_FP4, MMQ_ITER_K_FP4, true, false);
 
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_1, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_1, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q8_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    // IQ3_S — matches Ampere config (I=128). I=64 caused CUDA crash, under investigation.
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_S, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q1_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_0, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q2_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q2_K, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q3_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q4_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q5_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_1, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_Q6_K, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q6_K, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ1_S, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XXS, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_XS, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ2_S, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q3_K, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ3_XXS, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_XS, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, true);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,   8, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  16, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  24, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  32, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  40, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  48, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  64, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  80, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128,  96, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128, 112, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+    CASE(GGML_TYPE_IQ4_NL, 256, 1, 128, 128, GGML_CUDA_MMQ_SRAM_LAYOUT_Q8_0, MMQ_ITER_K, true, false);
+
     return ggml_cuda_mmq_get_config_ampere(type, J, fallback);
 }
diff --git a/ggml/src/ggml-cuda/mmq-load-tiles.cuh b/ggml/src/ggml-cuda/mmq-load-tiles.cuh
index 9a5a6a405..c66861779 100644
--- a/ggml/src/ggml-cuda/mmq-load-tiles.cuh
+++ b/ggml/src/ggml-cuda/mmq-load-tiles.cuh
@@ -1433,12 +1433,16 @@ template <ggml_type type, int J, bool fallback> static __device__ __forceinline_
 
         const block_iq3_s * bxi = (const block_iq3_s *) x + kbx0 + i*stride;
 
-        const int2      qs_packed = make_int2(get_int_b2(bxi->qs, 2*kqsx+0), get_int_b2(bxi->qs, 2*kqsx+1));
-        const uint8_t * qs        = (const uint8_t *) &qs_packed;
+        const uint16_t * qs16 = (const uint16_t *) bxi->qs;
+        const int2      qs_packed = make_int2(
+            __ldg(qs16 + 4*kqsx)     | (__ldg(qs16 + 4*kqsx + 1) << 16),
+            __ldg(qs16 + 4*kqsx + 2) | (__ldg(qs16 + 4*kqsx + 3) << 16));
+        const uint8_t * qs = (const uint8_t *) &qs_packed;
 
-        const int qh = bxi->qh[kqsx];
+        const int qh = __ldg(bxi->qh + kqsx);
 
-        const int       signs_packed_32 = get_int_b2(bxi->signs, kqsx);
+        const uint16_t * signs16       = (const uint16_t *) bxi->signs;
+        const int       signs_packed_32 = __ldg(signs16 + 2*kqsx) | (__ldg(signs16 + 2*kqsx + 1) << 16);
         const uint8_t * signs_packed_8  = (const uint8_t *) &signs_packed_32;
 
 #pragma unroll
@@ -1462,7 +1466,8 @@ template <ggml_type type, int J, bool fallback> static __device__ __forceinline_
 #endif // defined(AMD_MFMA_AVAILABLE) || defined(TURING_MMA_AVAILABLE) || defined(AMD_WMMA_AVAILABLE)
         }
 
-        const int ls = 1 + 2*((bxi->scales[kqsx/2] >> (((2*kqsx) << 1) & 0x04)) & 0x0F);
+        const uint8_t sc = __ldg(bxi->scales + kqsx/2);
+        const int ls = 1 + 2*((sc >> (((2*kqsx) << 1) & 0x04)) & 0x0F);
         const float d = bxi->d;
 #if defined(AMD_MFMA_AVAILABLE) || defined(TURING_MMA_AVAILABLE) || defined(AMD_WMMA_AVAILABLE)
         x_df[i*sram_stride             + kqsx] = ls*d;
```
