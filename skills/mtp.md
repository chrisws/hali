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
- `plans/mtp-perf-1.md` — initial performance analysis and priority table
- `plans/mtp-perf-2.md` — second-round optimisations (adaptive draft length, pre-computed ptrs, reduced timing)

## Log Format

Hali writes to `hali.log` in this format:

```
[YYYY-MM-DD HH:MM:SS] [LEVEL] message
```

MTP-specific log lines (all prefixed `HALI: MTP`):

| Pattern | Level | Meaning |
|---------|-------|---------|
| `MTP detected: N nextn layers` | DEBUG | Model has MTP head |
| `MTP ready: n_max=N n_min=N p_min=N.NN` | DEBUG | Speculative context initialised |
| `MTP available (N layers) but disabled` | DEBUG | MTP present but not enabled in config |
| `MTP speculative init failed, continuing without speculative decoding` | DEBUG | `common_speculative_init` returned null |
| `MTP: N/N accepted (limit=L) gpu=NNNN cpu=NNNN` | DEBUG | Per-round acceptance, adaptive limit, and phase timing (µs) |
| `MTP: NNN rounds, X.XX tok/rnd, accept NN% \| gpu NNN.N cpu NNN.N (us)` | DEBUG | Summary every 128 rounds |
| `MTP: verify decode failed rc=N (k=N pos=N)` | DEBUG | Target decode error |
| `MTP: speculative process (verify) failed: ..., disabling MTP` | DEBUG | Draft/target KV desync |
| `MTP: speculative process failed: ...` | DEBUG | `sync_and_capture_mtp` failure |
| `MTP: ctx_dft pos range [A, B], batch pos [C, D]` | DEBUG | Diagnostic: draft context vs batch position range (on process failure) |
| `MTP: seq_rm failed, disabling MTP` | DEBUG | KV rollback failure |
| `MTP: rolled back N unemitted KV tokens (keep_end=N)` | DEBUG | Early-termination rollback |

## Log Analysis Script

Use `scripts/mtp_log_summary.py` to parse `hali.log` and produce a summary of MTP
performance. Run it from the project root:

```bash
python3 scripts/mtp_log_summary.py hali.log
```

### What the script reports

1. **Session overview** — MTP detected/ready lines, config (n_max, n_min, p_min)
2. **Per-round acceptance histogram** — distribution of `N/N accepted` values
3. **Cumulative stats** — total rounds, total drafted, total accepted, overall acceptance %
4. **Phase timing** — parsed from the 128-round summary lines (gpu, cpu in µs)
5. **Errors and disables** — any lines indicating MTP was disabled or a decode/process failed
6. **Throughput estimate** — tokens/round and approximate tokens/sec from timestamps

### Script location

The script lives at `scripts/mtp_log_summary.py`. If it doesn't exist yet, create it
with the following behaviour:

```python
#!/usr/bin/env python3
"""Parse hali.log and summarise MTP speculative decoding performance."""

import re
import sys
from collections import Counter
from datetime import datetime

LOG_RE = re.compile(r'^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\] \[(\w+)\] (.*)$')
ACCEPT_RE = re.compile(r'HALI: MTP: (\d+)/(\d+) accepted')
SUMMARY_RE = re.compile(
    r'HALI: MTP: (\d+) rounds, ([\d.]+) tok/rnd, accept (\d+)% \| '
    r'gpu ([\d.]+) cpu ([\d.]+) \(us\)'
)
ERROR_RE = re.compile(r'(failed|disabling|disabled)')

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 mtp_log_summary.py <logfile>")
        sys.exit(1)

    accepts = []
    summaries = []
    errors = []
    mtp_info = []
    timestamps = []

    with open(sys.argv[1]) as f:
        for line in f:
            m = LOG_RE.match(line.strip())
            if not m:
                continue
            ts, level, msg = m.groups()
            timestamps.append(ts)

            am = ACCEPT_RE.search(msg)
            if am:
                accepts.append((int(am.group(1)), int(am.group(2))))
                continue

            sm = SUMMARY_RE.search(msg)
            if sm:
                summaries.append(sm.groups())
                continue

            if 'HALI: MTP' in msg and (level == 'DEBUG' or ERROR_RE.search(msg)):
                if 'accepted' not in msg:
                    mtp_info.append((ts, level, msg))

            if ERROR_RE.search(msg) and 'HALI: MTP' in msg:
                errors.append((ts, msg))

    # --- Output ---
    print("=== MTP Log Summary ===\n")

    # Config
    print("Config / Init:")
    for ts, level, msg in mtp_info:
        if 'detected' in msg or 'ready' in msg or 'disabled' in msg or 'failed' in msg:
            print(f"  [{ts}] {msg}")
    print()

    # Acceptance
    if accepts:
        total_drafted = sum(d for _, d in accepts)
        total_accepted = sum(a for a, _ in accepts)
        rate = 100.0 * total_accepted / total_drafted if total_drafted else 0
        hist = Counter(f"{a}/{d}" for a, d in accepts)
        print(f"Acceptance: {total_accepted}/{total_drafted} ({rate:.1f}%) over {len(accepts)} rounds")
        print("Histogram:")
        for key in sorted(hist, key=lambda x: int(x.split('/')[0])):
            bar = '#' * (hist[key] * 40 // max(hist.values()))
            print(f"  {key:>4}  {hist[key]:>5}  {bar}")
        print()

    # Phase timing (from 128-round summaries)
    if summaries:
        print("Phase timing (avg µs, from 128-round summaries):")
        last = summaries[-1]
        labels = ['rounds', 'tok/rnd', 'accept%', 'gpu', 'cpu']
        for label, val in zip(labels, last):
            print(f"  {label:>10}: {val}")
        print()

    # Errors
    if errors:
        print("Errors / Disables:")
        for ts, msg in errors:
            print(f"  [{ts}] {msg}")
        print()

    # Throughput estimate
    if len(timestamps) >= 2:
        t0 = datetime.strptime(timestamps[0], '%Y-%m-%d %H:%M:%S')
        t1 = datetime.strptime(timestamps[-1], '%Y-%m-%d %H:%M:%S')
        dur = (t1 - t0).total_seconds()
        if dur > 0 and accepts:
            total_tokens = total_accepted + len(accepts)  # accepted + 1 per round
            print(f"Duration: {dur:.1f}s | ~{total_tokens} tokens | ~{total_tokens/dur:.1f} tok/s")

if __name__ == '__main__':
    main()
```

## Workflow

1. **Reproduce** — run Hali with MTP enabled (`hali.config-mtp.json`) and capture `hali.log`.
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

MTP is controlled via `hali.config-mtp.json`:
- `mtp_enabled` — master switch
- `mtp_n_max` — max draft tokens per round (default 3)
- `mtp_n_min` — min draft tokens (default 0)
- `mtp_p_min` — min probability threshold for draft acceptance (default 0.0)