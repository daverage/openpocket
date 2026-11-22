#!/usr/bin/env python3
"""IDMT-SMT-Guitar benchmark + ranked manifest (host only).  Run after idmt_inventory.py.
  python3 tests/archive/fasttrack/idmt_bench.py manifest              ranked manifest -> build/fasttrack/idmt/manifest.csv (+ top lists on stdout)
  python3 tests/archive/fasttrack/idmt_bench.py pitched VARIANT...    licks/pieces (datasets 2,3), pitch-aware scoring (ft_score), stratified by recording type
  python3 tests/archive/fasttrack/idmt_bench.py d4 VARIANT...         dataset 4 direct-guitar recordings, ONSET-ONLY scoring (no pitch annotations exist)
Annotation caveat: hand-annotated onsets sit within roughly -22..+32 ms of the audio energy rise (median +20 ms, 999 notes of the licks):
absolute latencies against IDMT truth are only good to about +-25 ms; compare variants against each other on the same truth."""
import sys, os, glob, json, csv
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ft_score as S
ROOT = S.ROOT; INV = ROOT + '/build/fasttrack/idmt/inventory.json'

def manifest():
    rows = [r for r in json.load(open(INV)) if r['path'] == 'direct' and r['kind'] in ('lick', 'piece', 'd4_fast')]
    for r in rows:
        mins = max(r['dur'], 1) / 60.0
        r['score_fast'] = r.get('notes_in_runs_8.0', 0) * 3 + r.get('max_rate_5', 0)
        r['score_strings'] = r.get('strings_changes', 0) / mins
        r['score_repeat'] = r.get('repeats', 0) / mins
        r['score_bend'] = r.get('n_bend', 0) + r.get('n_vib', 0)
        r['score_mute'] = r.get('n_muted', 0) + r.get('n_dead', 0)
    cols = ['name', 'set', 'kind', 'guitar', 'path', 'dur', 'groups', 'poly_groups', 'max_rate_5', 'notes_in_runs_8.0', 'notes_in_runs_12.5', 'notes_in_runs_20.0', 'longest_8.0',
            'strings_changes', 'repeats', 'n_bend', 'n_slide', 'n_vib', 'n_muted', 'n_dead', 'n_harm', 'score_fast', 'score_strings', 'score_repeat', 'score_bend', 'score_mute', 'wav']
    with open(ROOT + '/build/fasttrack/idmt/manifest.csv', 'w', newline='') as f:
        w = csv.writer(f); w.writerow(['rank_fast'] + cols)
        for i, r in enumerate(sorted(rows, key=lambda r: -r['score_fast'])): w.writerow([i + 1] + [round(r.get(c, 0), 2) if isinstance(r.get(c, 0), float) else r.get(c, '') for c in cols])
    for key, title, pool in (('score_fast', 'FAST RUNS (notes in >=8/s runs x3 + fastest 5-note rate)', rows), ('score_strings', 'STRING CHANGES per minute (licks/pieces)', [r for r in rows if r['pitched']]),
                             ('score_repeat', 'REPEATED NOTES per minute', [r for r in rows if r['pitched']]), ('score_bend', 'BENDS+VIBRATO notes', [r for r in rows if r['pitched']]), ('score_mute', 'MUTED+DEAD notes', [r for r in rows if r['pitched']])):
        print(f'\n{title}')
        for r in sorted(pool, key=lambda r: -r[key])[:6]: print(f"  {r[key]:7.1f}  {r['name']:42s} max5={r.get('max_rate_5',0):.1f}/s groups={r['groups']} poly={r['poly_groups']}")

def pitched(variants):
    inv = {('idmt_' + r['name']): r for r in json.load(open(INV)) if r['kind'] in ('lick', 'piece')}
    for v in variants:
        for sset in ('idmt_licks', 'idmt_pieces'):
            res = S.run(v, sset)
            if not res: continue
            def sel(f): return [r for n, r in res.items() if f(inv.get(n, {}))]
            print(f"{v:14s} {sset:12s} ALL      {S.fmt(S.pool(list(res.values())))}")
            for lab, f in (('mono-only', lambda i: i.get('poly_groups', 1) == 0), ('has chords', lambda i: i.get('poly_groups', 0) > 0), ('bend/vib', lambda i: i.get('n_bend', 0) + i.get('n_vib', 0) > 0),
                           ('muted/dead', lambda i: i.get('n_muted', 0) + i.get('n_dead', 0) > 0), ('slides', lambda i: i.get('n_slide', 0) > 0), ('harmonics', lambda i: i.get('n_harm', 0) > 0)):
                rs = sel(f)
                if rs: print(f"{'':14s} {'':12s} {lab:9s} {S.fmt(S.pool(rs))}  [{len(rs)} rec]")

def d4(variants):
    for v in variants:
        d = f'{ROOT}/build/fasttrack/out/idmt_d4/{v}'
        lat, tot_on, found, nN, dur, fast_on, fast_found = [], 0, 0, 0, 0.0, 0, 0
        for cp in sorted(glob.glob(ROOT + '/build/stage5/sets/idmt_d4/*.csv')):
            name = os.path.basename(cp)[:-4]; evp = f'{d}/{name}.ev'
            if not os.path.exists(evp): continue
            on = np.array([float(l.split(',')[0]) * 1000 for l in open(cp) if l.strip()]); ev = S.load_ev(evp)
            N = np.array([t for k, t, a in ev if k == 'N'] + [t for k, t, a in ev if k == 'L']); N.sort()
            for i, o in enumerate(on):
                nxt = on[i + 1] if i + 1 < len(on) else 1e12
                c = N[(N >= o - 30) & (N < min(nxt - 10, o + 250))]
                fastc = i > 0 and o - on[i - 1] < 150
                tot_on += 1; fast_on += fastc
                if len(c): found += 1; fast_found += fastc; lat.append(c[0] - o)
            nN += len(N); dur += (on[-1] - on[0]) / 60000
        lat = np.array(lat)
        print(f"{v:14s} d4-direct onsets={tot_on} recall={found/max(1,tot_on):.3f} (fast<150ms: {fast_found}/{fast_on}={fast_found/max(1,fast_on):.3f}) latency med/p90/p99={np.median(lat):.1f}/{np.percentile(lat,90):.1f}/{np.percentile(lat,99):.1f} ms  note-ons={nN} ({nN/max(1,tot_on):.2f} per annotated onset)")

if __name__ == '__main__':
    {'manifest': manifest, 'pitched': lambda: pitched(sys.argv[2:]), 'd4': lambda: d4(sys.argv[2:])}[sys.argv[1]]()
