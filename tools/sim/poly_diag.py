#!/usr/bin/env python3
"""Per-interval dyad accuracy of the poly tracker: raw candidates vs the two voices.

  PYTHONPATH=tools/sim python3 tools/sim/poly_diag.py build/pulse_poly_bench [--set dev|held]

dev = GuitarSet comping takes 0..23 (tune here); held = 24..47 (report here, as tools/sim/poly_score.py).
"""
import argparse, csv, subprocess, tempfile
from pathlib import Path
import numpy as np
from track_eval import jams_notes, dyad_score
from resonator_track_eval import read_di

p = argparse.ArgumentParser()
p.add_argument('exe'); p.add_argument('--set', default='dev'); p.add_argument('--transpose', type=int, default=0)
a = p.parse_args()
di = Path('build/guitarset')
files = sorted((di / 'mono').glob('*_comp_mix.wav'))
files = files[:24] if a.set == 'dev' else files[24:48] if a.set == 'held' else files[48:]

def run(x):
    with tempfile.TemporaryDirectory() as t:
        tr = Path(t) / 'trace.csv'
        subprocess.run([a.exe, str(tr)], input=np.asarray(x, dtype='<f4').tobytes(), capture_output=True, check=True)
        with tr.open() as f:
            return [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]

def voices(rows):
    out = []
    for r in rows:
        ps = sorted(r[f'v{k}'] for k in (1, 2) if r[f'gain{k}'] > .065 and r[f'v{k}'] > 0)
        out.append(dict(t=r['t'], nn=len(ps), **{f'n{i+1}': x for i, x in enumerate(ps)}))
    return out

tot = {'raw': [0] * 5, 'voice': [0] * 5}; iv = {'raw': {}, 'voice': {}}
for f in files:
    x = read_di(f)
    notes = jams_notes(di / 'annotation' / (f.stem.removesuffix('_mix') + '.jams'))
    if a.transpose:
        k = 2 ** (a.transpose / 12)
        x = np.interp(np.arange(0, len(x) - 1, k), np.arange(len(x)), x)
        notes = [(s / k, e / k, m + a.transpose) for s, e, m in notes]
    rows = run(x)
    for name, rs in (('raw', rows), ('voice', voices(rows))):
        r = dyad_score(rs, notes, iv[name])
        tot[name] = [u + v for u, v in zip(tot[name], r)]
for name in ('raw', 'voice'):
    d, b, lo, s, e = tot[name]
    print(f'{name:5s} dyads {100*b/d:5.1f}%  lowest {100*lo/d:5.1f}%  extra-on-single {100*e/s:5.1f}%  ({d} dyad, {s} single frames)')
print('interval  frames   raw%  voice%')
for k in sorted(set(iv['raw']) | set(iv['voice'])):
    r = iv['raw'].get(k, [0, 0]); v = iv['voice'].get(k, [0, 0])
    if r[1] >= 200: print(f'{k:8d} {r[1]:7d} {100*r[0]/r[1]:6.1f} {100*v[0]/max(v[1],1):6.1f}')
