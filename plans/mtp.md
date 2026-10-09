# MTP (Multi-Token Prediction) Support for Hali

## Overview

Add speculative decoding via MTP to hali's `llama_sb` backend. MTP uses a model's built-in
NextN layers (or a separate draft model) to predict multiple tokens ahead, which the
target model then verifies in a single pass — reducing latency for token generation.

## Current State

- `src/llama_sb.cpp` creates a single `llama_context` via `llama_init_from_model()`
- No speculative decoding, no MTP context, no nextn embeddings
- `llama_sb.h` exposes a simple `process()` / `sample()` interface

## Key APIs (from llama.cpp)

| API | Header | Purpose |
|-----|--------|---------|
| `LLAMA_CONTEXT_TYPE_MTP` | `llama.h` | Context type enum |
| `llama_model_params.load_mtp` | `llama.h` | Load MTP layers at model load |
| `llama_context_params.ctx_type` | `llama.h` | Set MTP context type |
| `llama_model_n_layer_nextn()` | `llama.h` | Count MTP layers in model |
| `llama_set_embeddings_nextn(ctx, val, masked)` | `llama-ext.h` | Enable nextn hidden-state output |
| `llama_set_nextn_layer_offset(ctx, offset)` | `llama-ext.h` | Select which NextN head (chained) |
| `llama_get_embeddings_nextn(ctx)` | `llama-ext.h` | Get nextn embeddings buffer |
| `llama_get_embeddings_nextn_ith(ctx, i)` | `llama-ext.h` | Get ith nextn row |
| `llama_batch_ext_set_embd_state(batch, idx, embd)` | `llama.h` | Inject hidden state into batch |

## Implementation Phases

### Phase 1: Detection & Context Setup

**Goal:** Detect MTP capability and create the MTP draft context.

1. After `llama_model_load_from_file()`, call `llama_model_n_layer_nextn(model)`.
   - If result > 0, the model has built-in MTP layers.
   - Store `n_mtp_layers` in `llama_sb` state.

2. Add a `bool mtp_enabled` flag to `llama_sb` (default: auto-detect).

3. Create the MTP draft context:
   ```cpp
   llama_context_params mtp_cparams = cparams;
   mtp_cparams.ctx_type = LLAMA_CONTEXT_TYPE_MTP;
   mtp_cparams.n_ctx = llama_n_ctx(_ctx);
   _ctx_mtp = llama_init_from_model(_model, mtp_cparams);
   ```
   - Reuse the same model (built-in MTP mode).
   - Store `_ctx_mtp` alongside `_ctx`.

4. Enable nextn embeddings on the target context:
   ```cpp
   llama_set_embeddings_nextn(_ctx, true, true);
   ```

5. Add cleanup: free `_ctx_mtp` in destructor alongside `_ctx`.

### Phase 2: Hidden State Capture

**Goal:** After each target `llama_process()` call, capture the nextn hidden states.

1. After `llama_process(_ctx, ...)` succeeds, read hidden states:
   ```cpp
   float * h_nextn = llama_get_embeddings_nextn(_ctx);
   ```
2. Store per-row hidden states for the batch (size: `n_tokens * n_embd`).
3. Track which rows correspond to which sequence positions.
4. The last row's hidden state is the "pending" state for the next draft step.

### Phase 3: Draft Generation

**Goal:** Use the MTP context to generate draft tokens.

1. Build a `llama_batch_ext` for the MTP context:
   - Input: last accepted token + captured hidden state
   - Use `llama_batch_ext_set_embd_state()` to inject the hidden state
   - For chained heads: call `llama_set_nextn_layer_offset(_ctx_mtp, step)` per draft step

2. Process the batch on `_ctx_mtp`:
   ```cpp
   llama_process(_ctx_mtp, LLAMA_PROCESS_TYPE_DECODE, batch);
   ```

3. Sample from the MTP context logits to get draft token IDs.
   - Repeat up to `n_max` times (configurable, default 3-5).
   - For chained heads: increment layer offset each iteration.

4. Return the draft token sequence.

### Phase 4: Verification & Acceptance

**Goal:** Verify draft tokens against the target model and accept/reject.

1. Build a target batch containing all draft tokens at their expected positions.
2. Process on `_ctx` (target):
   ```cpp
   llama_process(_ctx, LLAMA_PROCESS_TYPE_DECODE, verify_batch);
   ```
3. Compare target logits at each draft position against the draft model's predictions.
4. Accept the longest prefix of matching tokens.
5. On mismatch:
   - Roll back the MTP context KV cache to the last accepted position.
   - Sample the correct token from the target at the mismatch position.
6. Update hidden state capture for the next round.

### Phase 5: Integration with Hali Loop

**Goal:** Wire MTP into hali's main generation loop.

1. Modify the generation loop in `llama_sb.cpp`:
   ```
   loop:
     a. Capture hidden states from last target process
     b. Generate draft tokens via MTP context
     c. Verify drafts on target
     d. Accept N tokens (N >= 1 always, since target always produces 1)
     e. Append accepted tokens to output
     f. Repeat until EOS or max tokens
   ```

2. Expose MTP config via `llama_sb` parameters:
   - `mtp_enabled` (bool, default auto)
   - `mtp_n_max` (int, default 3)
   - `mtp_n_min` (int, default 1)
   - `mtp_p_min` (float, default 0.9)

3. Add logging: track acceptance rate, tokens/sec improvement.

### Phase 6: Edge Cases & Hardening

1. **KV cache rollback**: On partial acceptance, roll back `_ctx_mtp` KV to the
   accepted position. Use `llama_kv_cache_seq_rm()` or context reset.

2. **Multi-sequence**: If hali supports multiple sequences, MTP state must be
   per-sequence (mirror `pending_h[seq_id]` pattern from speculative.cpp).

3. **Memory**: MTP context adds KV cache memory. Ensure `n_ctx` is reasonable
   and document the memory overhead.

4. **Fallback**: If MTP context creation fails, log a warning and continue
   without speculative decoding (graceful degradation).

5. **Model compatibility**: Not all models have MTP layers. The auto-detect
   in Phase 1 handles this. Also check vocab compatibility if using a
   separate draft model (future).

## File Changes

| File | Changes |
|------|---------|
| `src/llama_sb.h` | Add MTP state members, config params, new methods |
| `src/llama_sb.cpp` | Context creation, hidden state capture, draft/verify loop |
| `src/llama_sb.cpp` | Include `llama-ext.h` for staging APIs |
| `CMakeLists.txt` | Link against `llama` (already done), ensure `llama-ext.h` is accessible |

## Testing Strategy

1. **Unit**: Load a model with MTP layers (e.g. Qwen3.5), verify
   `llama_model_n_layer_nextn() > 0`, verify MTP context creation succeeds.

2. **Integration**: Run a short generation with MTP enabled, verify:
   - Output is identical to non-MTP (same seed, same params)
   - Acceptance rate is logged
   - No memory leaks (valgrind or ASAN)

3. **Performance**: Benchmark tokens/sec with MTP on/off for a fixed prompt.
   Expect 1.3x-2x speedup depending on acceptance rate.

4. **Edge cases**:
   - Model without MTP layers → should silently skip
   - Very short generation (1-2 tokens) → MTP overhead not worth it
   - Context overflow → graceful error

## Open Questions

- Should hali use the `common_speculative` framework from `llama.cpp/common/`
  directly, or implement a lighter-weight MTP loop natively?
  - **Recommendation**: Implement natively in `llama_sb.cpp` for now.
    The `common_speculative` framework is tightly coupled to `common_params`
    and the server tool. A native implementation is ~200 lines and gives
    full control.
- Should we support separate draft models (e.g. a smaller MTP model)?
  - **Recommendation**: Defer. Built-in MTP (same model) covers the common case.
    Separate draft models add model management complexity.

## References

- `llama.cpp/include/llama.h` — public API
- `llama.cpp/src/llama-ext.h` — staging API (nextn embeddings)
- `llama.cpp/common/speculative.cpp` — reference MTP implementation
- `llama.cpp/common/speculative.h` — speculative interface
- `llama.cpp/common/common.h` — `common_speculative_type` enum