#!/usr/bin/env python3
"""The strongest harmonics of each render: are the formants at fixed frequencies as the pitch changes? (needs numpy) usage: analysis.py OUTDIR"""
import numpy as np, sys
d = sys.argv[1]; sr = 44100
print('CHOIR AAH, target vowel /a/ (Hillenbrand 1995): 768 / 1333 / 2522 Hz')
for f0 in (110, 165, 220, 330):
    y = np.fromfile('%s/voice_%d.f32' % (d, f0), dtype=np.float32); on = int(np.argmax(np.abs(y) > 0.05 * np.abs(y).max())); y = y[on:]
    sp = np.abs(np.fft.rfft(y[sr:sr + 16384] * np.hanning(16384))); fr = np.fft.rfftfreq(16384, 1 / sr)
    hs = [(k * f0, 20 * np.log10(sp[(fr > k * f0 * .985) & (fr < k * f0 * 1.015)].max() + 1e-12)) for k in range(1, int(4500 / f0) + 1)]
    m = max(v for _, v in hs); print('  %3d Hz: harmonic peaks (Hz, dB re the strongest): %s' % (f0, [(round(f), round(v - m)) for i, (f, v) in enumerate(hs) if 0 < i < len(hs) - 1 and v >= hs[i - 1][1] and v >= hs[i + 1][1] and v - m > -25]))
