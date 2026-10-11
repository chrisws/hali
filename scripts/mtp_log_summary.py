#!/usr/bin/env python3
"""Parse hali.log and summarise MTP speculative decoding performance.

Usage: python3 scripts/mtp_log_summary.py <logfile>
"""

import re
import sys
from collections import Counter
from datetime import datetime

LOG_RE = re.compile(r'^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\] \[(\w+)\] (.*)$')
ACCEPT_RE = re.compile(r'HALI: MTP: (\d+)/(\d+) accepted \(limit=(\d+)\) gpu=(\d+) cpu=(\d+)')
ACCEPT_RE_OLD = re.compile(r'HALI: MTP: (\d+)/(\d+) accepted$')
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
                accepts.append({
                    'acc': int(am.group(1)),
                    'drafted': int(am.group(2)),
                    'limit': int(am.group(3)),
                    'gpu': int(am.group(4)),
                    'cpu': int(am.group(5)),
                })
                continue

            am_old = ACCEPT_RE_OLD.search(msg)
            if am_old:
                accepts.append({
                    'acc': int(am_old.group(1)),
                    'drafted': int(am_old.group(2)),
                    'limit': None,
                    'gpu': None,
                    'cpu': None,
                })
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

    # Config / Init
    print("Config / Init:")
    for ts, level, msg in mtp_info:
        if any(k in msg for k in ('detected', 'ready', 'disabled', 'failed', 'available')):
            print(f"  [{ts}] {msg}")
    print()

    # Acceptance
    if accepts:
        total_drafted = sum(r['drafted'] for r in accepts)
        total_accepted = sum(r['acc'] for r in accepts)
        rate = 100.0 * total_accepted / total_drafted if total_drafted else 0
        hist = Counter(f"{r['acc']}/{r['drafted']}" for r in accepts)
        print(f"Acceptance: {total_accepted}/{total_drafted} ({rate:.1f}%) over {len(accepts)} rounds")
        print("Histogram:")
        max_count = max(hist.values()) if hist else 1
        for key in sorted(hist, key=lambda x: int(x.split('/')[0])):
            bar = '#' * (hist[key] * 40 // max_count)
            print(f"  {key:>4}  {hist[key]:>5}  {bar}")
        print()

        # Adaptive limit distribution
        limits = [r['limit'] for r in accepts if r['limit'] is not None]
        if limits and len(set(limits)) > 1:
            limit_hist = Counter(limits)
            print("Adaptive limit distribution:")
            for lim in sorted(limit_hist):
                bar = '#' * (limit_hist[lim] * 40 // max(limit_hist.values()))
                print(f"  limit={lim}  {limit_hist[lim]:>5}  {bar}")
            print()

        # Per-round timing
        gpu_vals = [r['gpu'] for r in accepts if r['gpu'] is not None]
        cpu_vals = [r['cpu'] for r in accepts if r['cpu'] is not None]
        if gpu_vals:
            print(f"Per-round timing (us):")
            print(f"  gpu: avg={sum(gpu_vals)/len(gpu_vals):.0f}  min={min(gpu_vals)}  max={max(gpu_vals)}")
            print(f"  cpu: avg={sum(cpu_vals)/len(cpu_vals):.0f}  min={min(cpu_vals)}  max={max(cpu_vals)}")
            print()

    # Phase timing (from 128-round summaries)
    if summaries:
        print("Phase timing (avg us, from 128-round summaries):")
        for s in summaries:
            labels = ['rounds', 'tok/rnd', 'accept%', 'gpu', 'cpu']
            parts = [f"  {label:>10}: {val}" for label, val in zip(labels, s)]
            print("\n".join(parts))
            print()

    # Errors
    if errors:
        print("Errors / Disables:")
        for ts, msg in errors:
            print(f"  [{ts}] {msg}")
        print()

    # Throughput estimate
    if len(timestamps) >= 2 and accepts:
        t0 = datetime.strptime(timestamps[0], '%Y-%m-%d %H:%M:%S')
        t1 = datetime.strptime(timestamps[-1], '%Y-%m-%d %H:%M:%S')
        dur = (t1 - t0).total_seconds()
        if dur > 0:
            total_tokens = sum(r['acc'] for r in accepts) + len(accepts)  # accepted + 1 bonus per round
            print(f"Duration: {dur:.1f}s | ~{total_tokens} tokens | ~{total_tokens/dur:.1f} tok/s")


if __name__ == '__main__':
    main()

