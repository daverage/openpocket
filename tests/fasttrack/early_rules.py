#!/usr/bin/env python3
"""Causal early-admission evidence study on the ft_early probe data (single-note groups, onset assumed known = oracle onset, so this isolates PITCH evidence).
A rule admits the first moment its evidence holds; the admitted pitch is the tracker's mono at that frame.  Reported per rule and stratum:
  right = admitted within 50 cents of the annotated pitch, wrong = admitted elsewhere (these are the 'wrong initial pitch' notes), none = never admitted in 60 ms; latency of the right ones (ms after the annotated onset; +-10 ms annotation uncertainty).
Evidence terms (no waiting period is imposed: a rule is only 'k frames agree' + 'the waveform repeats'):
  agree k    the last k frames' mono pitches lie within 40 cents of each other
  d<thr      pxt_period at the candidate's period over the last 20 decimated samples (needs those samples to lie after the onset)
  conf>c     the tracker's confidence (share of partial level explained by the strongest note)
  sub        the waveform does NOT repeat better at 2x or 3x the period (a lower fundamental would show there)  [needs the longer window]"""
import sys, os, glob, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ft_score as S
ROOT = S.ROOT; RATE = 44100 / 16.0
def cents(a, b): return 1200 * np.log2(a / b) if a > 0 and b > 0 else 1e9
groups = []
for tp in sorted(glob.glob(ROOT + '/build/fasttrack/early/*.txt')):
    G = {}; F = collections.defaultdict(list)
    for l in open(tp):
        p = l.split()
        if p[0] == 'G': G[int(p[1])] = (float(p[2]), int(p[3]), float(p[4]))
        elif p[0] == 'F': F[int(p[1])].append([float(x) for x in p[2:]])
    for gi, (on, n, hz) in G.items():
        if n != 1 or gi not in F: continue
        gap = on - G[gi - 1][0] if gi - 1 in G else 1e9
        groups.append((gap, hz, np.array(F[gi])))
def run(rule, rs):
    right = wrong = none = 0; lat = []
    for gap, hz, fr in rs:
        res = None
        for i in range(len(fr)):
            t, mono = fr[i, 0], fr[i, 1]
            if t < 0 or mono <= 0: continue
            if rule(fr, i): res = (t, mono); break
        if res is None: none += 1
        elif abs(cents(res[1], hz)) < 50: right += 1; lat.append(res[0])
        else: wrong += 1
    n = len(rs); l = np.array(lat)
    return n, right / n, wrong / n, none / n, (np.median(l) if len(l) else float('nan')), (np.percentile(l, 90) if len(l) else float('nan'))
def agree(fr, i, k):
    if i < k - 1: return False
    m = fr[i - k + 1:i + 1, 1]
    return (m > 0).all() and max(abs(cents(x, m[-1])) for x in m) < 40
def valid_d(fr, i):
    t, mono = fr[i, 0], fr[i, 1]; lag = RATE / mono
    return t > 0 and t * RATE / 1000 >= 20 + lag + 2 and 20 + lag + 2 < 256
RULES = {
    'legacy-like: agree 10, no waveform': lambda fr, i: agree(fr, i, 10) and fr[i, 0] >= 20,
    'agree 3': lambda fr, i: agree(fr, i, 3),
    'agree 5': lambda fr, i: agree(fr, i, 5),
    'agree 3 + d<0.2': lambda fr, i: agree(fr, i, 3) and valid_d(fr, i) and fr[i, 7] < 0.2,
    'agree 3 + d<0.1': lambda fr, i: agree(fr, i, 3) and valid_d(fr, i) and fr[i, 7] < 0.1,
    'agree 5 + d<0.2': lambda fr, i: agree(fr, i, 5) and valid_d(fr, i) and fr[i, 7] < 0.2,
    'agree 5 + d<0.1': lambda fr, i: agree(fr, i, 5) and valid_d(fr, i) and fr[i, 7] < 0.1,
    'agree 3 + d<0.2 + conf>0.2': lambda fr, i: agree(fr, i, 3) and valid_d(fr, i) and fr[i, 7] < 0.2 and fr[i, 2] > 0.2,
    'd<0.2 only (valid)': lambda fr, i: valid_d(fr, i) and fr[i, 7] < 0.2,
    'agree 8 + d<0.2': lambda fr, i: agree(fr, i, 8) and valid_d(fr, i) and fr[i, 7] < 0.2,
}
for stratum, f in (('isolated (previous attack >= 300 ms)', lambda g: g[0] >= 300), ('fast (previous attack < 150 ms)', lambda g: g[0] < 150)):
    rs = [g for g in groups if f(g)]; print(f'\n{stratum}: {len(rs)} single-note groups')
    print(f"  {'rule':34s} {'right':>6s} {'wrong':>6s} {'none':>6s}   latency med/p90 of right (ms)")
    for nm, r in RULES.items():
        n, a, w, z, m, p = run(r, rs); print(f'  {nm:34s} {a:6.1%} {w:6.1%} {z:6.1%}   {m:5.1f}/{p:5.1f}')
