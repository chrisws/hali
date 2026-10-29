# MTP (Multi-Token Prediction) Support

Use this skill when working on, debugging, or improving Hali's speculative decoding via
`src/llama_sb_mtp.cpp`.

## Context

MTP uses llama.cpp's `common_speculative` API to draft k tokens with the MTP head, verify
them in a single target decode pass, accept the matching prefix, and roll back rejected
positions. The per-round flow is:

```
draft → verify → sync → accept → rollback
```

Key files:
- `src/llama_sb_mtp.cpp` — MTP round logic, init, rollback, sampler config
- `src/llama_sb.h` — member declarations (`_mtp_*`, `_verify_*`, `_draft_toks`, `_spec_batch`)

## Log Format

Hali writes to `hali.log` in this format:

```
[YYYY-MM-DD HH:MM:SS] [LEVEL] message
```

MTP-specific log lines (all prefixed `HALI: MTP`):

| Pattern                                                                 | Level | Meaning                                                                |
|-------------------------------------------------------------------------|-------|------------------------------------------------------------------------|
| `MTP detected: N nextn layers`                                          | DEBUG | Model has MTP head                                                     |
| `MTP ready: n_max=N n_min=N p_min=N.NN`                                 | DEBUG | Speculative context initialised                                        |
| `MTP available (N layers) but disabled`                                 | DEBUG | MTP present but not enabled in config                                  |
| `MTP speculative init failed, continuing without speculative decoding`  | DEBUG | `common_speculative_init` returned null                                |
| `MTP: N/N accepted (limit=L) gpu=NNNN cpu=NNNN`                         | DEBUG | Per-round acceptance, adaptive limit, and phase timing (µs)            |
| `MTP: NNN rounds, X.XX tok/rnd, accept NN% \| gpu NNN.N cpu NNN.N (us)` | DEBUG | Summary every 128 rounds                                               |
| `MTP: verify decode failed rc=N (k=N pos=N)`                            | DEBUG | Target decode error                                                    |
| `MTP: speculative process (verify) failed: ..., disabling MTP`          | DEBUG | Draft/target KV desync                                                 |
| `MTP: speculative process failed: ...`                                  | DEBUG | `sync_and_capture_mtp` failure                                         |
| `MTP: ctx_dft pos range [A, B], batch pos [C, D]`                       | DEBUG | Diagnostic: draft context vs batch position range (on process failure) |
| `MTP: seq_rm failed, disabling MTP`                                     | DEBUG | KV rollback failure                                                    |
| `MTP: rolled back N unemitted KV tokens (keep_end=N)`                   | DEBUG | Early-termination rollback                                             |

## Log Environment Variables

Hali's logging supports two environment variables to control what is written and
where:

| Variable          | Purpose                         | Example                      |
|-------------------|---------------------------------|------------------------------|
| `HALI_LOG_FILE`   | Override the log file path      | `HALI_LOG_FILE=/tmp/mtp.log` |
| `HALI_LOG_FILTER` | Comma-separated category filter | `HALI_LOG_FILTER=mtp`        |

- **`HALI_LOG_FILTER`** — when set, only log lines tagged with a matching category
  are written. Untagged lines (general logging, tool requests, etc.) are excluded.
  Set to `*` to disable filtering (default behaviour).
- **`HALI_LOG_FILE`** — full path to the log file. Parent directories are created
  automatically. Defaults to `~/.config/hali/hali.log`.

### Capturing a clean MTP log

```bash
HALI_LOG_FILTER=mtp HALI_LOG_FILE=/tmp/mtp.log ./hali
```

This produces a log containing **only** MTP-categorised lines, ideal for feeding
into `scripts/mtp_log_summary.py` without noise from tool requests or other
subsystems.

## Log Analysis Script

Use `scripts/mtp_log_summary.py` to parse a log file and produce a summary of MTP
performance. Run it from the project root:

```bash
python3 scripts/mtp_log_summary.py /tmp/mtp.log
```

### What the script reports

1. **Session overview** — MTP detected/ready lines, config (n_max, n_min, p_min)
2. **Per-round acceptance histogram** — distribution of `N/N accepted` values
3. **Cumulative stats** — total rounds, total drafted, total accepted, overall acceptance %
4. **Phase timing** — parsed from the 128-round summary lines (gpu, cpu in µs)
5. **Errors and disables** — any lines indicating MTP was disabled or a decode/process failed
6. **Throughput estimate** — tokens/round and approximate tokens/sec from timestamps

### Script location

The script lives at `scripts/mtp_log_summary.py`

## Workflow

1. **Reproduce** — run Hali with MTP enabled (`.hali/config.json`) and capture a
   clean MTP log using the env-vars (see "Capturing a clean MTP log" above):
   `HALI_LOG_FILTER=mtp HALI_LOG_FILE=/tmp/mtp.log ./hali`
2. **Summarise** — run the log script to get acceptance rate, phase timings, and any errors.
3. **Diagnose** — compare against the baselines in `plans/mtp-perf-1.md`:
   - Acceptance < 50% → check penalty/temperature alignment (plan 1, item 1a/7)
   - Verify phase dominates → check `n_threads_batch`, VRAM, model offload (plan 1, items 2a/2b/2c)
   - Draft phase high → MTP head is slow; check if layers are on CPU
   - Process phase high → draft catch-up decode is expensive; check `n_batch` for draft ctx
4. **Plan** — write a new `plans/mtp-perf-N.md` with specific changes, referencing the
   measurements from the log summary.
5. **Implement** — make changes in `src/llama_sb_mtp.cpp` (and `src/llama_sb.h` for members).
6. **Verify** — rebuild with `./maintain.sh`, re-run, re-parse the log, compare before/after.
7. **Test** — add unit tests in `tests/` for any new logic (e.g. adaptive threshold,
   rollback edge cases).

## Key Invariants

- `mtp_trim_draft(P)` **must** be called before `common_speculative_process` for
  single-head MTP models (`n_mtp_layers == 1`). The internal trim only runs when
  `chain_heads = true` (multi-head).
- The anchor token is never decoded via `decode_anchor` when MTP is active; it is the
  first element of the verify batch.
- `_tokens_physically_used` tracks KV positions and must be updated after every
  accept/rollback.
- On any permanent failure (verify decode error, process failure, seq_rm failure),
  set `_mtp_disabled_permanently = true` so `mtp_reset_state` does not re-enable MTP.

## Configuration

MTP is controlled via `.hali/config.json`:
- `mtp_enabled` — master switch
- `mtp_n_max` — max draft tokens per round (default 3)
- `mtp_n_min` — min draft tokens (default 0)
- `mtp_p_min` — min probability threshold for draft acceptance (default 0.0)
