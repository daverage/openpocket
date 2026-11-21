#!/usr/bin/env python3
"""Poly synth response on single-note playing (v66): how fast a voice reaches the played note, and how often a
voice starts on a pitch nobody played. Host only.

  PYTHONPATH=tools/research python3 tools/legacy-synth/poly_latency.py build/pulse_poly_bench [--set dev|held] [--takes N]

GuitarSet solo takes (dev 0..23, held 24..47). Per note of at least 80 ms: the time from its onset to the first report
with a sounding voice (gain > 0.065) within 50 cents (None if not within 150 ms). False starts: voices that rise
through 0.065 and are not within 50 cents of any note sounding then, per minute of playing.
"""
import argparse, csv, glob, os, subprocess, tempfile
from statistics import median
import numpy as np
from track_eval import ROOT, jams_notes, cents
from resonator_track_eval import read_di

p = argparse.ArgumentParser(); p.add_argument('exe'); p.add_argument('--set', default='dev'); p.add_argument('--takes', type=int, default=24)
p.add_argument('--offset', type=int, help='override the first GuitarSet take for a separate audit')
a = p.parse_args()
files = sorted(glob.glob(os.path.join(ROOT, 'build', 'guitarset', 'mono', '*_solo_mix.wav')))
offset = a.offset if a.offset is not None else 0 if a.set == 'dev' else 24
if offset < 0 or a.takes < 1:
    p.error('expected a non-negative offset and a positive take count')
files = files[offset:offset + a.takes]
if not files:
    p.error('no GuitarSet solo takes at the requested offset')
lat, miss, false_starts, starts, minutes = [], 0, 0, 0, 0.0
for w in files:
    x = read_di(w)
    notes = jams_notes(os.path.join(ROOT, 'build', 'guitarset', 'annotation', os.path.basename(w).replace('_mix.wav', '.jams')))
    with tempfile.TemporaryDirectory() as t:
        tr = os.path.join(t, 'tr.csv')
        subprocess.run([a.exe, tr], input=np.asarray(x, dtype='<f4').tobytes(), capture_output=True, check=True)
        rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(open(tr))]
    T = np.array([r['t'] for r in rows])
    minutes += len(x) / 44100 / 60
    for s, e, m in notes:
        if e - s < 0.08: continue
        f = 440 * 2 ** ((m - 69) / 12)
        hit = None
        for r in rows[np.searchsorted(T, s):np.searchsorted(T, s + 0.15)]:
            if any(r[f'gain{k}'] > .065 and r[f'v{k}'] > 0 and abs(cents(r[f'v{k}'], f)) < 50 for k in (1, 2)):
                hit = 1000 * (r['t'] - s); break
        if hit is None: miss += 1
        else: lat.append(hit)
    prev = [0.0, 0.0]
    for r in rows:
        for k in (1, 2):
            g = r[f'gain{k}']
            if g > .065 >= prev[k - 1]:
                starts += 1
                sounding = [mm for s, e, mm in notes if s - 0.02 <= r['t'] < e + 0.05]
                if not any(abs(cents(r[f'v{k}'], 440 * 2 ** ((mm - 69) / 12))) < 50 for mm in sounding): false_starts += 1
            prev[k - 1] = g
lat.sort()
n = len(lat) + miss
print(f'notes {n}: reached within 150 ms {len(lat)}/{n} ({100*len(lat)/n:.1f}%), '
      f'latency median {median(lat):.1f} ms, p90 {lat[int(.9*len(lat))]:.1f} ms; '
      f'voice starts {starts}, false {false_starts} ({100*false_starts/max(1,starts):.1f}%, {false_starts/minutes:.0f}/min)')
