#!/usr/bin/env python3
"""Parse hali.log and summarise MTP speculative decoding performance.

Splits the log into sessions at 'hali starting' markers and, within each
session, into segments at 'MTP ready' setting changes.  Outputs a compact
JSON file with all metrics, histograms, and comparison data.

Usage:
  python3 scripts/mtp_log_summary.py <logfile> [-o OUTPUT] [--stdout]

Options:
  -o, --output FILE   Write JSON to FILE (default: mtp_summary.json)
  --stdout            Also print JSON to stdout
"""

import argparse
import json
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
SUFFIX_ENTER_RE = re.compile(r'HALI: MTP: entering suffix mode \(win_frac=([\d.]+)\)')
SUFFIX_EXIT_RE = re.compile(r'HALI: MTP: exiting suffix mode \(win_frac=([\d.]+)\)')
SUFFIX_FALLBACK_RE = re.compile(r'HALI: MTP: suffix fallback: (\d+) draft tokens')
SUFFIX_HIT_RATE_RE = re.compile(r'HALI: MTP: suffix hit rate ([\d.]+)% over (\d+) lookups \(streak=(\d+)\)')
SUFFIX_DISABLED_RE = re.compile(r'HALI: MTP: suffix DISABLED')
SUFFIX_MAX_DRAFT_RE = re.compile(
    r'HALI: MTP: suffix pos_acc\[1\]=([\d.]+) over (\d+) entries'
    r' — (dropping|restoring) suffix_max_draft to (\d+)')
ADAPT_RE = re.compile(
    r'HALI: MTP: adaptive (limit|p_min|n_min|n_max|pos_threshold) '
    r'([\d.]+) -> ([\d.]+)')
ADAPT_LIMIT_RE = re.compile(r'HALI: MTP: adaptive limit (\d+) -> (\d+) \(eff_n_max=(\d+)\)')
ADAPT_NMIN_RE = re.compile(r'HALI: MTP: adaptive n_min (\d+) -> (\d+)')


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

        rm = MTP_READY_RE.search(msg)
        if rm:
            if current['accepts'] or current['summaries']:
                segments.append(current)
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

        # --- suffix mode events ---
        se = SUFFIX_ENTER_RE.search(msg)
        if se:
            current['suffix_events'].append(
                {'ts': ts, 'type': 'enter', 'win_frac': float(se.group(1))})
            continue
        sx = SUFFIX_EXIT_RE.search(msg)
        if sx:
            current['suffix_events'].append(
                {'ts': ts, 'type': 'exit', 'win_frac': float(sx.group(1))})
            continue
        sf = SUFFIX_FALLBACK_RE.search(msg)
        if sf:
            current['suffix_events'].append(
                {'ts': ts, 'type': 'fallback', 'drafts': int(sf.group(1))})
            continue
        sh = SUFFIX_HIT_RATE_RE.search(msg)
        if sh:
            current['suffix_events'].append({
                'ts': ts, 'type': 'hit_rate',
                'rate': float(sh.group(1)),
                'lookups': int(sh.group(2)),
                'streak': int(sh.group(3)),
            })
            continue
        sd = SUFFIX_DISABLED_RE.search(msg)
        if sd:
            current['suffix_events'].append({'ts': ts, 'type': 'disabled'})
            continue
        smd = SUFFIX_MAX_DRAFT_RE.search(msg)
        if smd:
            current['suffix_events'].append({
                'ts': ts, 'type': 'max_draft',
                'pos_acc1': float(smd.group(1)),
                'entries': int(smd.group(2)),
                'action': smd.group(3),
                'new_val': int(smd.group(4)),
            })
            continue

        # --- adaptive parameter changes ---
        al = ADAPT_LIMIT_RE.search(msg)
        if al:
            current['adaptive_events'].append({
                'ts': ts, 'param': 'limit',
                'from': int(al.group(1)), 'to': int(al.group(2)),
                'eff_n_max': int(al.group(3)),
            })
            continue
        ap = ADAPT_RE.search(msg)
        if ap:
            current['adaptive_events'].append({
                'ts': ts, 'param': ap.group(1),
                'from': float(ap.group(2)), 'to': float(ap.group(3)),
            })
            continue

        if 'HALI: MTP' in msg and (level == 'DEBUG' or ERROR_RE.search(msg)):
            if 'accepted' not in msg:
                current['mtp_info'].append((ts, level, msg))

        if ERROR_RE.search(msg) and 'HALI: MTP' in msg:
            current['errors'].append((ts, msg))

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
        'suffix_events': [],
        'adaptive_events': [],
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


def settings_label(settings):
    """Format a settings dict as a short label."""
    if settings is None:
        return 'default'
    return f"n_max={settings['n_max']} n_min={settings['n_min']} p_min={settings['p_min']:.2f}"


def build_segment_json(idx, seg, m, tail=5):
    """Build the JSON dict for one segment."""
    entry = {
        'idx': idx,
        'settings': seg['settings'],
        'metrics': {
            'rounds': m['rounds'],
            'total_drafted': m['total_drafted'],
            'total_accepted': m['total_accepted'],
            'accept_pct': round(m['accept_pct'], 2),
            'tok_per_round': round(m['tok_per_round'], 3),
            'gpu_avg_us': round(m['gpu_avg'], 1) if m['gpu_avg'] is not None else None,
            'cpu_avg_us': round(m['cpu_avg'], 1) if m['cpu_avg'] is not None else None,
            'duration_s': round(m['duration'], 1) if m['duration'] is not None else None,
            'tok_per_sec': round(m['tok_per_sec'], 1) if m['tok_per_sec'] is not None else None,
        },
    }

    accepts = seg['accepts']
    if accepts:
        hist = Counter(f"{r['acc']}/{r['drafted']}" for r in accepts)
        entry['accept_hist'] = dict(
            sorted(hist.items(), key=lambda x: int(x[0].split('/')[0]))
        )

        limits = [r['limit'] for r in accepts if r['limit'] is not None]
        if limits and len(set(limits)) > 1:
            entry['limit_hist'] = dict(sorted(Counter(limits).items()))

        timing = {}
        gpu_vals = [r['gpu'] for r in accepts if r['gpu'] is not None]
        cpu_vals = [r['cpu'] for r in accepts if r['cpu'] is not None]
        if gpu_vals:
            timing['gpu'] = {
                'avg': round(sum(gpu_vals) / len(gpu_vals), 1),
                'min': min(gpu_vals),
                'max': max(gpu_vals),
            }
        if cpu_vals:
            timing['cpu'] = {
                'avg': round(sum(cpu_vals) / len(cpu_vals), 1),
                'min': min(cpu_vals),
                'max': max(cpu_vals),
            }
        if timing:
            entry['timing'] = timing

    # --- suffix stats ---
    suffix_events = seg['suffix_events']
    if suffix_events:
        enters = [e for e in suffix_events if e['type'] == 'enter']
        exits = [e for e in suffix_events if e['type'] == 'exit']
        fallbacks = [e for e in suffix_events if e['type'] == 'fallback']
        hit_rates = [e for e in suffix_events if e['type'] == 'hit_rate']
        disabled = [e for e in suffix_events if e['type'] == 'disabled']
        max_drafts = [e for e in suffix_events if e['type'] == 'max_draft']

        suffix_block = {
            'transitions': [
                {'ts': e['ts'], 'type': e['type'], 'win_frac': e['win_frac']}
                for e in enters + exits
            ],
            'enter_count': len(enters),
            'exit_count': len(exits),
            'fallback_count': len(fallbacks),
            'fallback_total_drafts': sum(e['drafts'] for e in fallbacks),
        }
        if hit_rates:
            suffix_block['hit_rates'] = [
                {'ts': e['ts'], 'rate': e['rate'],
                 'lookups': e['lookups'], 'streak': e['streak']}
                for e in hit_rates
            ]
        if disabled:
            suffix_block['disabled'] = True
            suffix_block['disabled_at'] = disabled[0]['ts']
        if max_drafts:
            suffix_block['max_draft_changes'] = [
                {'ts': e['ts'], 'action': e['action'],
                 'new_val': e['new_val'], 'pos_acc1': e['pos_acc1']}
                for e in max_drafts
            ]
        entry['suffix'] = suffix_block

    # --- adaptive parameter drift ---
    adaptive_events = seg['adaptive_events']
    if adaptive_events:
        entry['adaptive_drift'] = [
            {'ts': e['ts'], 'param': e['param'],
             'from': e['from'], 'to': e['to']}
            | ({'eff_n_max': e['eff_n_max']} if 'eff_n_max' in e else {})
            for e in adaptive_events
        ]

    if seg['summaries']:
        summaries = seg['summaries'][-tail:] if tail > 0 else seg['summaries']
        entry['phase_summaries'] = [
            {
                'rounds': int(s[0]),
                'tok_per_round': float(s[1]),
                'accept_pct': int(s[2]),
                'gpu_us': float(s[3]),
                'cpu_us': float(s[4]),
            }
            for s in summaries
        ]

    if seg['errors']:
        entry['errors'] = [{'ts': ts, 'msg': msg} for ts, msg in seg['errors']]

    if seg['mtp_info']:
        entry['mtp_info'] = [
            {'ts': ts, 'level': level, 'msg': msg}
            for ts, level, msg in seg['mtp_info']
        ]

    return entry


def build_comparison(all_segments, all_metrics):
    """Build comparison data for multiple segments."""
    n = len(all_metrics)
    if n < 2:
        return None

    def row(label, vals, lower_better=False):
        r = {'metric': label, 'values': vals}
        if len(vals) >= 2 and vals[-1] is not None and vals[0] is not None:
            d = vals[-1] - vals[0]
            r['delta'] = round(d, 2)
            r['improved'] = (d < 0) == lower_better
        return r

    return {
        'settings': [settings_label(s['settings']) for s in all_segments],
        'rows': [
            row('rounds', [m['rounds'] for m in all_metrics]),
            row('accept_pct', [round(m['accept_pct'], 2) for m in all_metrics]),
            row('tok_per_round', [round(m['tok_per_round'], 3) for m in all_metrics]),
            row('gpu_avg_us',
                [round(m['gpu_avg'], 1) if m['gpu_avg'] is not None else None
                 for m in all_metrics], lower_better=True),
            row('cpu_avg_us',
                [round(m['cpu_avg'], 1) if m['cpu_avg'] is not None else None
                 for m in all_metrics], lower_better=True),
            row('duration_s',
                [round(m['duration'], 1) if m['duration'] is not None else None
                 for m in all_metrics]),
            row('tok_per_sec',
                [round(m['tok_per_sec'], 1) if m['tok_per_sec'] is not None else None
                 for m in all_metrics]),
            row('suffix_enters',
                [len([e for e in s['suffix_events'] if e['type'] == 'enter'])
                 for s in all_segments]),
            row('suffix_fallbacks',
                [len([e for e in s['suffix_events'] if e['type'] == 'fallback'])
                 for s in all_segments]),
            row('adaptive_changes',
                [len(s['adaptive_events']) for s in all_segments]),
        ],
    }


def build_json(logfile, session_count, all_segments, all_metrics, tail=5):
    """Build the top-level JSON structure."""
    segments = [
        build_segment_json(i, seg, m, tail=tail)
        for i, (seg, m) in enumerate(zip(all_segments, all_metrics), 1)
    ]

    result = {
        'log_file': logfile,
        'sessions': session_count,
        'segments': segments,
    }

    comp = build_comparison(all_segments, all_metrics)
    if comp:
        result['comparison'] = comp

    return result


def print_brief_summary(all_segments, all_metrics, output_path):
    """Print a short human-readable summary to stdout."""
    n = len(all_metrics)
    print(f"MTP Summary: {n} segment{'s' if n != 1 else ''} from log")
    for i, (seg, m) in enumerate(zip(all_segments, all_metrics), 1):
        label = settings_label(seg['settings'])
        parts = [f"S{i} [{label}]",
                 f"{m['rounds']} rounds",
                 f"{m['accept_pct']:.1f}% accept",
                 f"{m['tok_per_round']:.2f} tok/rnd"]
        if m['tok_per_sec'] is not None:
            parts.append(f"{m['tok_per_sec']:.1f} tok/s")
        print("  " + ", ".join(parts))
        if seg['suffix_events']:
            enters = len([e for e in seg['suffix_events'] if e['type'] == 'enter'])
            fallbacks = len([e for e in seg['suffix_events'] if e['type'] == 'fallback'])
            disabled = any(e['type'] == 'disabled' for e in seg['suffix_events'])
            sfx = f"    suffix: {enters} enter, {fallbacks} fallback"
            if disabled:
                sfx += ", DISABLED"
            print(sfx)
        if seg['adaptive_events']:
            params = set(e['param'] for e in seg['adaptive_events'])
            print(f"    adaptive: {len(seg['adaptive_events'])} changes "
                  f"({', '.join(sorted(params))})")
    print(f"  JSON -> {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description='Parse hali.log and output MTP summary as compact JSON.')
    parser.add_argument('logfile', help='Path to the log file')
    parser.add_argument('-o', '--output', default='mtp_summary.json',
                        help='Output JSON file (default: mtp_summary.json)')
    parser.add_argument('--stdout', action='store_true',
                        help='Also print JSON to stdout')
    parser.add_argument('--tail', type=int, default=5,
                        help='Keep only the last N phase summaries per segment (default: 5)')
    parser.add_argument('--min-rounds', type=int, default=0,
                        help='Skip segments with fewer than N rounds (default: 0)')
    args = parser.parse_args()

    with open(args.logfile) as f:
        lines = f.readlines()

    sessions = split_sessions(lines)
    all_segments = []
    for session_lines in sessions:
        all_segments.extend(parse_session(session_lines))

    all_metrics = [compute_metrics(s) for s in all_segments]

    # Filter out segments below min-rounds threshold
    if args.min_rounds > 0:
        filtered = [(s, m) for s, m in zip(all_segments, all_metrics)
                    if m['rounds'] >= args.min_rounds]
        all_segments = [s for s, _ in filtered]
        all_metrics = [m for _, m in filtered]

    result = build_json(args.logfile, len(sessions), all_segments, all_metrics,
                        tail=args.tail)
    json_str = json.dumps(result, indent=2)

    with open(args.output, 'w') as f:
        f.write(json_str)

    if args.stdout:
        print(json_str)
    else:
        print_brief_summary(all_segments, all_metrics, args.output)


if __name__ == '__main__':
    main()