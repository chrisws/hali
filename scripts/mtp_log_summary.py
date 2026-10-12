#!/usr/bin/env python3
"""Parse hali.log and summarise MTP speculative decoding performance.

Splits the log into sessions at 'hali starting' markers and, within each
session, into segments at 'MTP ready' setting changes.  When multiple
segments are present, prints a comparison table with deltas to highlight
improvements or regressions between configurations.

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
SESSION_RE = re.compile(r'hali starting', re.IGNORECASE)
MTP_READY_RE = re.compile(r'HALI: MTP ready: n_max=(\d+) n_min=(\d+) p_min=([\d.]+)')


def split_sessions(lines):
    """Split log lines into sessions at 'hali starting' markers."""
    sessions = []
    current = None
    for line in lines:
        if SESSION_RE.search(line):
            if current is not None:
                sessions.append(current)
            current = []
        if current is not None:
            current.append(line)
    if current is not None:
        sessions.append(current)
    if not sessions:
        sessions = [lines]
    return sessions


def parse_session(lines):
    """Parse one session's lines into segments split at MTP setting changes."""
    segments = []
    current = _new_segment()

    for line in lines:
        m = LOG_RE.match(line.strip())
        if not m:
            continue
        ts, level, msg = m.groups()
        current['timestamps'].append(ts)

        # Check for MTP settings change
        rm = MTP_READY_RE.search(msg)
        if rm:
            # Close current segment if it has data
            if current['accepts'] or current['summaries']:
                segments.append(current)
            # Start new segment with the new settings
            current = _new_segment()
            current['settings'] = {
                'n_max': int(rm.group(1)),
                'n_min': int(rm.group(2)),
                'p_min': float(rm.group(3)),
            }
            current['mtp_info'].append((ts, level, msg))
            continue

        am = ACCEPT_RE.search(msg)
        if am:
            current['accepts'].append({
                'acc': int(am.group(1)),
                'drafted': int(am.group(2)),
                'limit': int(am.group(3)),
                'gpu': int(am.group(4)),
                'cpu': int(am.group(5)),
            })
            continue

        am_old = ACCEPT_RE_OLD.search(msg)
        if am_old:
            current['accepts'].append({
                'acc': int(am_old.group(1)),
                'drafted': int(am_old.group(2)),
                'limit': None,
                'gpu': None,
                'cpu': None,
            })
            continue

        sm = SUMMARY_RE.search(msg)
        if sm:
            current['summaries'].append(sm.groups())
            continue

        if 'HALI: MTP' in msg and (level == 'DEBUG' or ERROR_RE.search(msg)):
            if 'accepted' not in msg:
                current['mtp_info'].append((ts, level, msg))

        if ERROR_RE.search(msg) and 'HALI: MTP' in msg:
            current['errors'].append((ts, msg))

    # Close final segment
    if current['accepts'] or current['summaries'] or current['mtp_info']:
        segments.append(current)

    return segments


def _new_segment():
    return {
        'accepts': [],
        'summaries': [],
        'errors': [],
        'mtp_info': [],
        'timestamps': [],
        'settings': None,
    }


def compute_metrics(data):
    """Compute summary metrics from parsed segment data."""
    accepts = data['accepts']
    timestamps = data['timestamps']
    m = {}

    m['rounds'] = len(accepts)
    if accepts:
        m['total_drafted'] = sum(r['drafted'] for r in accepts)
        m['total_accepted'] = sum(r['acc'] for r in accepts)
        m['accept_pct'] = (100.0 * m['total_accepted'] / m['total_drafted']
                           if m['total_drafted'] else 0.0)
        m['tok_per_round'] = 1.0 + (m['total_accepted'] / m['rounds']
                                     if m['rounds'] else 0.0)
        gpu_vals = [r['gpu'] for r in accepts if r['gpu'] is not None]
        cpu_vals = [r['cpu'] for r in accepts if r['cpu'] is not None]
        m['gpu_avg'] = sum(gpu_vals) / len(gpu_vals) if gpu_vals else None
        m['cpu_avg'] = sum(cpu_vals) / len(cpu_vals) if cpu_vals else None
    else:
        m['total_drafted'] = 0
        m['total_accepted'] = 0
        m['accept_pct'] = 0.0
        m['tok_per_round'] = 0.0
        m['gpu_avg'] = None
        m['cpu_avg'] = None

    if len(timestamps) >= 2 and accepts:
        t0 = datetime.strptime(timestamps[0], '%Y-%m-%d %H:%M:%S')
        t1 = datetime.strptime(timestamps[-1], '%Y-%m-%d %H:%M:%S')
        dur = (t1 - t0).total_seconds()
        m['duration'] = dur
        total_tokens = m['total_accepted'] + m['rounds']
        m['tok_per_sec'] = total_tokens / dur if dur > 0 else 0.0
    else:
        m['duration'] = None
        m['tok_per_sec'] = None

    return m


def fmt(val, fmt_str, default='—'):
    """Format a value, returning default if None."""
    if val is None:
        return default
    return fmt_str.format(val)


def delta_str(cur, prev, fmt_str, lower_is_better=False):
    """Format a delta with an arrow indicator."""
    if cur is None or prev is None:
        return '—'
    d = cur - prev
    if d == 0:
        return '  0'
    arrow = '↑' if (d < 0) == lower_is_better else '↓'
    sign = '+' if d > 0 else ''
    return f"{sign}{fmt_str.format(d)} {arrow}"


def settings_label(settings):
    """Format a settings dict as a short label."""
    if settings is None:
        return 'default'
    return f"n_max={settings['n_max']} n_min={settings['n_min']} p_min={settings['p_min']:.2f}"


def print_segment_detail(idx, data, metrics):
    """Print detailed stats for one segment."""
    accepts = data['accepts']
    label = settings_label(data['settings'])
    print(f"=== Segment {idx} [{label}] ===\n")

    # Config / Init
    print("Config / Init:")
    for ts, level, msg in data['mtp_info']:
        if any(k in msg for k in ('detected', 'ready', 'disabled', 'failed', 'available')):
            print(f"  [{ts}] {msg}")
    print()

    # Acceptance
    if accepts:
        hist = Counter(f"{r['acc']}/{r['drafted']}" for r in accepts)
        print(f"Acceptance: {metrics['total_accepted']}/{metrics['total_drafted']} "
              f"({metrics['accept_pct']:.1f}%) over {metrics['rounds']} rounds")
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
            print("Per-round timing (us):")
            print(f"  gpu: avg={sum(gpu_vals)/len(gpu_vals):.0f}  "
                  f"min={min(gpu_vals)}  max={max(gpu_vals)}")
            print(f"  cpu: avg={sum(cpu_vals)/len(cpu_vals):.0f}  "
                  f"min={min(cpu_vals)}  max={max(cpu_vals)}")
            print()

    # Phase timing (from 128-round summaries)
    if data['summaries']:
        print("Phase timing (avg us, from 128-round summaries):")
        for s in data['summaries']:
            labels = ['rounds', 'tok/rnd', 'accept%', 'gpu', 'cpu']
            parts = [f"  {label:>10}: {val}" for label, val in zip(labels, s)]
            print("\n".join(parts))
            print()

    # Errors
    if data['errors']:
        print("Errors / Disables:")
        for ts, msg in data['errors']:
            print(f"  [{ts}] {msg}")
        print()

    # Throughput
    if metrics['duration'] is not None:
        print(f"Duration: {metrics['duration']:.1f}s | "
              f"~{metrics['total_accepted'] + metrics['rounds']} tokens | "
              f"~{metrics['tok_per_sec']:.1f} tok/s")
    print()


def print_comparison(all_segments, all_metrics):
    """Print a comparison table across segments with deltas."""
    n = len(all_metrics)
    if n < 2:
        return

    print("=== Settings Comparison ===\n")

    col_w = 14
    header = f"{'Metric':<16}"
    for i in range(n):
        header += f"{'S' + str(i + 1):>{col_w}}"
    header += f"{'Delta':>16}"
    print(header)
    print("-" * len(header))

    # Settings row
    line = f"{'Settings':<16}"
    for seg in all_segments:
        line += f"{settings_label(seg['settings']):>{col_w}}"
    line += f"{'':>16}"
    print(line)
    print("-" * len(header))

    def row(label, vals, fmt_s, lower_better=False):
        line = f"{label:<16}"
        for v in vals:
            line += f"{fmt(v, fmt_s):>{col_w}}"
        if len(vals) >= 2 and vals[-1] is not None and vals[0] is not None:
            line += f"{delta_str(vals[-1], vals[0], fmt_s, lower_better):>16}"
        else:
            line += f"{'—':>16}"
        print(line)

    row('Rounds',       [m['rounds'] for m in all_metrics],       '{:d}')
    row('Acceptance',   [m['accept_pct'] for m in all_metrics],   '{:.1f}%', lower_better=False)
    row('tok/rnd',      [m['tok_per_round'] for m in all_metrics], '{:.2f}')
    row('GPU avg (us)', [m['gpu_avg'] for m in all_metrics],       '{:.0f}', lower_better=True)
    row('CPU avg (us)', [m['cpu_avg'] for m in all_metrics],       '{:.0f}', lower_better=True)
    row('Duration (s)', [m['duration'] for m in all_metrics],      '{:.1f}')
    row('tok/s',        [m['tok_per_sec'] for m in all_metrics],   '{:.1f}')
    print()


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 mtp_log_summary.py <logfile>")
        sys.exit(1)

    with open(sys.argv[1]) as f:
        lines = f.readlines()

    sessions = split_sessions(lines)
    all_segments = []
    for session_lines in sessions:
        all_segments.extend(parse_session(session_lines))

    all_metrics = [compute_metrics(s) for s in all_segments]

    # Print per-segment detail
    for i, (seg, metrics) in enumerate(zip(all_segments, all_metrics), 1):
        print_segment_detail(i, seg, metrics)

    # Print comparison table if multiple segments
    print_comparison(all_segments, all_metrics)


if __name__ == '__main__':
    main()