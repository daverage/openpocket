#!/usr/bin/env python3
"""Early pitch evidence study: when is the correct fundamental first available, and when would each admission rule have accepted it?  (host only, development data)
  python3 tests/fasttrack/early_analysis.py            needs build/fasttrack/early/*.txt (ft_early) and the ablation event logs (ctrl A, legacy FOLLOW-off)"""
import sys, os, glob, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ft_score as S, ft_attrib as A
ROOT = S.ROOT
def cents(a, b): return 1200 * np.log2(a / b) if a > 0 and b > 0 else 1e9
def first_run(flags, ts, k=3, t0=-5.0):
    for i in range(len(flags) - k + 1):
        if ts[i] >= t0 and all(flags[i:i + k]): return ts[i]
    return None
def load(path):
    G = {}; F = collections.defaultdict(list)
    for l in open(path):
        p = l.split()
        if p[0] == 'G': G[int(p[1])] = (float(p[2]), int(p[3]), [float(x) for x in p[4:]])
        elif p[0] == 'F': F[int(p[1])].append([float(x) for x in p[2:]])
    return G, F
def admit_times(variant, name, key):
    ev = A.parse(f'{ROOT}/build/fasttrack/out/gtechs/{variant}/{name}.ev'); return sorted((t, f) for k, t, f in ev if k == key)
rows = []
for tp in sorted(glob.glob(ROOT + '/build/fasttrack/early/*.txt')):
    name = os.path.basename(tp)[:-4]; G, F = load(tp); ons = sorted(G)
    leg = admit_times('synth_cl_f0', name, 'WN'); ctl = admit_times('oA20', name, 'A')
    for gi in ons:
        on, npi, hz = G[gi]
        if npi != 1 or gi not in F: continue                       # single-note groups only: chords have no single "true" fundamental
        gap = on - G[gi - 1][0] if gi - 1 in G else 1e9
        fr = np.array(F[gi]); ts = fr[:, 0]; mono = fr[:, 1]; rank = fr[:, 3]; dtrue = fr[:, 5]; valid = fr[:, 6] > 0
        ok = np.array([abs(cents(m, hz[0])) < 50 for m in mono]); avail = ok | (rank >= 0)
        t_dom = first_run(list(ok), ts); t_av = first_run(list(avail), ts, 2)
        pv = [t for t, d, v in zip(ts, dtrue, valid) if v and d < 0.2]; t_per = pv[0] if pv else None
        # strongest wrong candidates in the first 25 ms
        early = [(m, cents(m, hz[0])) for t, m in zip(ts, mono) if 0 <= t <= 25 and m > 0 and abs(cents(m, hz[0])) >= 50]
        ratio = [m / hz[0] for m, c in early]
        legt = next((t - on for t, f in leg if on - 30 <= t <= on + 600 and abs(cents(f, hz[0])) < 50), None)
        ctlt = next((t - on for t, f in ctl if on - 30 <= t <= on + 600 and abs(cents(f, hz[0])) < 50), None)
        rows.append(dict(name=name, gap=gap, t_dom=t_dom, t_av=t_av, t_per=t_per, nwrong=len(early), ratios=ratio, leg=legt, ctl=ctlt, cat=('fast' if gap < 150 else 'medium' if gap < 300 else 'isolated')))
def q(v):
    v = [x for x in v if x is not None]; return f'{np.median(v):6.1f}/{np.percentile(v,90):6.1f}' if v else '   n/a'
print(f'{len(rows)} single-note groups with probe data')
for cat in ('isolated', 'medium', 'fast', 'all'):
    rs = [r for r in rows if cat == 'all' or r['cat'] == cat]
    if not rs: continue
    n = len(rs); dom = sum(r['t_dom'] is not None for r in rs); av = sum(r['t_av'] is not None for r in rs); per = sum(r['t_per'] is not None for r in rs)
    w = sum(1 for r in rs if r['nwrong'] > 0)
    print(f"{cat:9s} n={n:5d} | correct pitch is the tracker's strongest (held 3 frames, within 60 ms): {dom/n:5.1%} t med/p90 {q([r['t_dom'] for r in rs])} ms"
          f" | present in the candidate set at all: {av/n:5.1%} | waveform repeats at the true period (d<0.2): {per/n:5.1%} t {q([r['t_per'] for r in rs])}"
          f"\n          wrong strongest candidate in the first 25 ms: {w/n:5.1%} of groups | legacy admission (FOLLOW-off) found {sum(r['leg'] is not None for r in rs)/n:5.1%} t {q([r['leg'] for r in rs])} | controller A found {sum(r['ctl'] is not None for r in rs)/n:5.1%} t {q([r['ctl'] for r in rs])}")
# what are the wrong early candidates?
rr = [x for r in rows for x in r['ratios']]
cls = collections.Counter()
for x in rr:
    h = round(x)
    if x < 0.9: cls['below (sub-octave or lower)'] += 1
    elif h in (2, 3, 4, 5, 6) and abs(x - h) < 0.06 * h: cls[f'harmonic x{h}'] += 1
    else: cls['other (non-harmonic)'] += 1
tot = sum(cls.values()); print('\nwrong strongest candidates in the first 25 ms (frames, all groups):', {k: f'{v/tot:.0%}' for k, v in cls.most_common()})
# availability vs admission: correct pitch available early but admitted late / never
late = [r for r in rows if r['t_dom'] is not None and r['ctl'] is not None]
print('\nadmission lag over availability: controller A admit time minus first-strongest time  median/p90 = %s ms (n=%d)' % (q([r['ctl'] - r['t_dom'] for r in late]), len(late)))
lateL = [r for r in rows if r['t_dom'] is not None and r['leg'] is not None]
print('                                 legacy FOLLOW-off admit time minus first-strongest time median/p90 = %s ms (n=%d)' % (q([r['leg'] - r['t_dom'] for r in lateL]), len(lateL)))
# groups never found by the controller: was the pitch available?
miss = [r for r in rows if r['ctl'] is None]; print(f"\ncontroller A missed {len(miss)} single-note groups: correct pitch strongest at some point {sum(r['t_dom'] is not None for r in miss)}, only in the candidate set {sum(r['t_dom'] is None and r['t_av'] is not None for r in miss)}, absent from the estimator {sum(r['t_av'] is None for r in miss)}")
