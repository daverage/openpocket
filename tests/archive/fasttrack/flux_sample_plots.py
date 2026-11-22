#!/usr/bin/env python3
"""Stratified visual sample of flux picks (the review in docs/archive/fasttrack/FASTTRACK-GROUND-TRUTH.md section 3): waveform, 2 ms level, this pick (blue), other picks (cyan), annotated onsets (green) and offsets (orange).
Needs build/fasttrack/flux_picks.csv (flux_reclass.py).   python3 tests/archive/fasttrack/flux_sample_plots.py [seed=7]  ->  build/fasttrack/plots/flux_sample.png"""
import sys, os, csv, random
import numpy as np, soundfile as sf
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ft_score as S
ROOT = S.ROOT; rows = list(csv.DictReader(open(ROOT + '/build/fasttrack/flux_picks.csv'))); random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 7)
want = [('precursor_before_pick', 'scales'), ('precursor_before_pick', 'scales'), ('precursor_before_pick', 'music'), ('unannotated_rise', 'techniques/PalmMute'), ('unannotated_rise', 'techniques/Bendings'),
        ('note_end_transient', 'scales'), ('silence_noise', 'techniques/PinchHarmonics'), ('genuine_misaligned', 'scales'), ('in_note_fluctuation', 'techniques/Vibrato')]
fig, ax = plt.subplots(3, 3, figsize=(17, 10)); ax = ax.ravel()
for a, (cls, tech) in zip(ax, want):
    c = [r for r in rows if r['cls'] == cls and r['tech'] == tech] or [r for r in rows if r['cls'] == cls]; r = random.choice(c); t = float(r['t'])
    x, sr = sf.read(f"{ROOT}/build/stage5/sets/gtechs/{r['name']}.wav"); x = x if x.ndim == 1 else x[:, 0]
    lo, hi = int((t - 200) / 1000 * sr), int((t + 250) / 1000 * sr); seg = x[lo:hi]; tt = np.arange(lo, hi) / sr * 1000
    a.plot(tt, seg, lw=.4, color='#444'); h = int(0.002 * sr); a.plot(tt, np.sqrt(np.convolve(seg ** 2, np.ones(h) / h, 'same')), color='#d33', lw=1); a.axvline(t, color='b', lw=1.5)
    for on, off, m in S.load_truth(f"{ROOT}/build/stage5/sets/gtechs/{r['name']}.csv"):
        if t - 200 <= on <= t + 250: a.axvline(on, color='g', ls='--')
        if t - 200 <= off <= t + 250: a.axvline(off, color='orange', ls=':')
    for l in open(f"{ROOT}/build/fasttrack/early/{r['name']}.txt"):
        if l.startswith('P '):
            q = float(l.split()[1])
            if t - 200 <= q <= t + 250 and abs(q - t) > 1: a.axvline(q, color='c', lw=.8)
    a.set_title(f"{cls} | {r['tech']} | {r['name']} @ {t:.0f} ms | flux {float(r['flux']):.0f} weak {r['weak']} lvl-rise {float(r['lr']):.1f}", fontsize=8)
os.makedirs(ROOT + '/build/fasttrack/plots', exist_ok=True); plt.tight_layout(); plt.savefig(ROOT + '/build/fasttrack/plots/flux_sample.png', dpi=70)
