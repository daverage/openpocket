#!/usr/bin/env python3
"""The +-1 semitone early-pitch errors (docs/FASTTRACK-GROUND-TRUTH.md section 4).  Isolated single-note groups (previous attack >= 300 ms) whose first confirmed pitch
(3 agreeing frames + the waveform repeats at that period, d<0.2) is a semitone off: what is the pitch 100-240 ms later?
Needs build/fasttrack/early250 (the probe over 250 ms): the script builds it if missing (about 1 minute).   python3 tests/fasttrack/early_semitone.py"""
import os, glob, collections, subprocess
import numpy as np
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')); RATE = 44100 / 16.0
def cents(a, b): return 1200 * np.log2(a / b) if a > 0 and b > 0 else 1e9
def agree(m, i, k):
    if i < k - 1: return False
    w = m[i - k + 1:i + 1]; return (w > 0).all() and max(abs(cents(x, w[-1])) for x in w) < 40
d = ROOT + '/build/fasttrack/early250'
if not glob.glob(d + '/*.txt'):
    os.makedirs(d, exist_ok=True); subprocess.check_call(['sh', '-c', 'FT_SRC=tests/fasttrack/ft_early.c tests/fasttrack/build.sh early && for w in build/stage5/sets/gtechs/*.wav; do echo $w; done | xargs -P 8 -n 1 sh -c \'b=$(basename $1 .wav); FT_WIN=250 build/fasttrack/ft_early $1 > build/fasttrack/early250/$b.txt\' _'], cwd=ROOT)
res = collections.Counter(); rows = []
for tp in sorted(glob.glob(d + '/*.txt')):
    name = os.path.basename(tp)[:-4]; G = {}; F = collections.defaultdict(list)
    for l in open(tp):
        p = l.split()
        if p[0] == 'G': G[int(p[1])] = (float(p[2]), int(p[3]), float(p[4]))
        elif p[0] == 'F': F[int(p[1])].append([float(x) for x in p[2:]])
    for gi, (on, n, hz) in G.items():
        if n != 1 or gi not in F or gi - 1 not in G or on - G[gi - 1][0] < 300: continue
        fr = np.array(F[gi]); ts = fr[:, 0]; mono = fr[:, 1]; dm = fr[:, 7]; adm = None
        for i in range(len(fr)):
            t = ts[i]
            if t < 0 or mono[i] <= 0 or t > 60: continue
            if agree(mono, i, 3) and t * RATE / 1000 >= 20 + RATE / mono[i] + 2 and dm[i] < 0.2: adm = (t, mono[i]); break
        if adm is None or abs(cents(adm[1], hz)) < 50: continue
        c0 = cents(adm[1], hz)
        if abs(round(c0 / 100)) != 1: continue
        late = mono[(ts >= 100) & (ts <= 240) & (mono > 0)]
        if len(late) < 10: res['too short to judge'] += 1; continue
        lm = np.median([cents(x, hz) for x in late])
        k = 'settles to the annotated pitch' if abs(lm) < 50 else 'stays at the admitted pitch' if abs(lm - c0) < 50 else 'moves elsewhere'
        res[k] += 1; rows.append((name, hz, c0, lm))
tot = sum(res.values()); print('+-1 semitone wrong admits (isolated single notes):', tot)
for k, v in res.most_common(): print(f'  {v:4d} {v/tot:5.1%} {k}')
pers = [r for r in rows if abs(r[3] - r[2]) < 50]
if pers: print('persistent ones: mean offset from the annotation %.0f cents' % np.mean([r[3] for r in pers]))
