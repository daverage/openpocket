#!/usr/bin/env python3
"""Compare old and HQ SYN implementations for catalogue consolidation (v63). Host only.

  cc -O2 -Iengine -Ifirmware tools/sim/ensemble_bench.c firmware/px_math.c -lm -o build/ensemble_bench
  PYTHONPATH=tools/sim python3 tools/sim/syn_compare.py [--takes N]

For every annotated single note of at least 150 ms in N GuitarSet solo takes (build/guitarset), the share of the
output's energy (60 Hz .. 4 kHz, middle half of the note) that lies within 30 cents of the harmonics of the note's
expected base (the guitar's pitch times the type's lowest voice: 1/2 for the organs, strings and choir, 1/4 for BASS
SYNTH, 1 for PWM). --comp: the comping takes instead, 93 ms windows where the same two or more notes sound
throughout, the harmonics of any of them. Higher is cleaner: less energy at pitches the player did not play. Also the host render time.
Knobs at noon, all wet (DRY 0; PWM's MIX 100).
"""
import argparse, glob, os, subprocess, sys, time
import numpy as np
from track_eval import ROOT, jams_notes
from resonator_track_eval import read_di

PAIRS = [("ORGAN", "ORGAN HQ", 0.5), ("CHURCH ORGAN", "CHURCH ORGAN HQ", 0.5), ("STRINGS", "STRINGS HQ", 0.5),
         ("CHOIR", "CHOIR HQ", 0.5), ("PWM SYNTH", "PWM SYNTH HQ", 1.0), ("BASS SYNTH", "BASS SYNTH HQ", 0.25)]
BENCH = os.path.join(ROOT, 'build', 'ensemble_bench')

def render(name, x):
    knobs = ['50'] * 5
    knobs[4] = '100' if name.startswith('PWM') else '0'      # all wet: PWM's MIX at 100, the others' DRY at 0
    r = subprocess.run([BENCH, name, *knobs], input=x.astype('<f4').tobytes(), capture_output=True, check=True)
    return np.frombuffer(r.stdout, dtype='<f4'), r

def on_pitch(y, notes, base):
    shares = []
    for a, b, m in notes:
        if b - a < 0.15: continue
        i0, i1 = int((a + (b - a) / 4) * 44100), int((b - (b - a) / 4) * 44100)
        seg = y[i0:i1]
        if len(seg) < 2048: continue
        w = np.hanning(len(seg)); X = np.abs(np.fft.rfft(seg * w)) ** 2
        f = np.fft.rfftfreq(len(seg), 1 / 44100)
        band = (f >= 60) & (f <= 4000)
        tot = X[band].sum()
        if tot <= 1e-12: continue
        f0 = 440 * 2 ** ((m - 69) / 12) * base
        k = np.maximum(1, np.round(f / f0))
        near = np.abs(1200 * np.log2(np.maximum(f, 1) / (k * f0))) < 30
        shares.append(X[band & near].sum() / tot)
    return np.array(shares)

def on_chords(y, notes, base, win=4096):
    """Comping: 93 ms windows where the same two or more notes sound throughout; harmonics of any of them."""
    shares = []
    for i0 in range(0, len(y) - win, win):
        t0, t1 = i0 / 44100, (i0 + win) / 44100
        held = {m for a, b, m in notes if a <= t0 - 0.03 and b >= t1}
        if len(held) < 2 or any(t0 - 0.03 < a < t1 for a, b, m in notes): continue
        seg = y[i0:i0 + win]; X = np.abs(np.fft.rfft(seg * np.hanning(win))) ** 2
        f = np.fft.rfftfreq(win, 1 / 44100); band = (f >= 60) & (f <= 4000); tot = X[band].sum()
        if tot <= 1e-12: continue
        near = np.zeros_like(f, dtype=bool)
        for m in held:
            f0 = 440 * 2 ** ((m - 69) / 12) * base; k = np.maximum(1, np.round(f / f0))
            near |= np.abs(1200 * np.log2(np.maximum(f, 1) / (k * f0))) < 30
        shares.append(X[band & near].sum() / tot)
    return np.array(shares)

p = argparse.ArgumentParser(); p.add_argument('--takes', type=int, default=20); p.add_argument('--comp', action='store_true')
a = p.parse_args()
if a.comp: on_pitch = on_chords
files = sorted(glob.glob(os.path.join(ROOT, 'build', 'guitarset', 'mono', '*_comp_mix.wav' if a.comp else '*_solo_mix.wav')))[:a.takes]
items = [(read_di(w), jams_notes(os.path.join(ROOT, 'build', 'guitarset', 'annotation', os.path.basename(w).replace('_mix.wav', '.jams')))) for w in files]
print(f'{"type":18s} {"on-pitch":>9s} {"p25":>6s} {"host s":>7s}')
for old, new, base in PAIRS:
    for name in (old, new):
        shares = []; secs = 0.0
        for x, notes in items:
            t0 = time.perf_counter(); y, _ = render(name, x); secs += time.perf_counter() - t0
            shares.append(on_pitch(y, notes, base))
        s = np.concatenate(shares)
        print(f'{name:18s} {100*s.mean():8.1f}% {100*np.percentile(s,25):5.1f}% {secs:7.2f}')
    print()
