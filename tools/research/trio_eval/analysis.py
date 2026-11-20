#!/usr/bin/env python3
"""Synth2 stage D analysis of trio_eval's renders: spectral peak, the single-resonance finding (LPC), off-harmonic energy. needs numpy. usage: analysis.py OUTDIR"""
import numpy as np, sys
d = sys.argv[1]; sr = 44100; F = [82.41, 164.81, 329.63, 659.26, 987.77]
def lpc(seg, order):
    r = np.correlate(seg, seg, 'full')[len(seg) - 1:len(seg) + order]; a = np.array([1.0]); e = r[0]
    for i in range(1, order + 1):
        k = -(r[i] + sum(a[j] * r[i - j] for j in range(1, i))) / e; a = np.append(a, 0.0); a = a + k * a[::-1]; e *= (1 - k * k)
    return a
def formants(y):
    y = y - np.mean(y); y = np.append(y[0], y[1:] - 0.97 * y[:-1]); a = lpc(y[sr // 2:sr // 2 + 4096] * np.hamming(4096), 14)
    return sorted(round(np.angle(r) * sr / (2 * np.pi)) for r in np.roots(a) if np.imag(r) > 0.01 and abs(r) > 0.9 and 150 < np.angle(r) * sr / (2 * np.pi) < 5000)
for tone in (50, 25, 8):
    y = np.fromfile('%s/note_1_tone%d.f32' % (d, tone), dtype=np.float32)   # 164.81 Hz
    print('TONE %3d: estimated formants of a 164.8 Hz note (Hz): %s' % (tone, formants(y)))
print('off the harmonic series (TONE 50):')
for k, f in enumerate(F):
    y = np.fromfile('%s/note_%d_tone50.f32' % (d, k), dtype=np.float32)[sr // 2:]; sp = np.abs(np.fft.rfft(y * np.hanning(len(y)))) ** 2; fr = np.fft.rfftfreq(len(y), 1 / sr)
    m = (fr > 100) & (fr < 20000); r = fr[m] / f; near = np.abs(r - np.round(r)) < 0.012 * np.maximum(np.round(r), 1)
    print('  %7.2f Hz: %.2f %%' % (f, 100 * sp[m][~near].sum() / sp[m].sum()))
