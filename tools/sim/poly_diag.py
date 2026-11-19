#!/usr/bin/env python3
"""Per-interval dyad accuracy of the poly tracker: raw candidates vs the two voices.

  PYTHONPATH=tools/sim python3 tools/sim/poly_diag.py build/pulse_poly_bench [--set dev|held]
  PYTHONPATH=tools/sim python3 tools/sim/poly_diag.py build/pulse_poly_bench --owner-di 'My Audio'
  PYTHONPATH=tools/sim python3 tools/sim/poly_diag.py build/mono_bench --owner-di 'My Audio' --mono

dev = GuitarSet comping takes 0..23 (tune here); held = 24..47 (report here, as tools/sim/poly_score.py).
"""
import argparse, csv, subprocess, tempfile
from pathlib import Path
import numpy as np
from track_eval import jams_notes, dyad_score
from resonator_track_eval import read_di

p = argparse.ArgumentParser()
p.add_argument('exe'); p.add_argument('--set', default='dev'); p.add_argument('--transpose', type=int, default=0)
p.add_argument('--owner-di', metavar='DIR', type=Path, help='score the two Phase 2b owner recordings')
p.add_argument('--max-ghost-percent', type=float, help='fail if the owner scale exceeds this overtone-ghost rate')
p.add_argument('--mono', action='store_true', help='use mono_bench for the owner DI recordings')
a = p.parse_args()
if a.mono and not a.owner_di:
    p.error('--mono requires --owner-di')
if a.max_ghost_percent is not None and (not a.owner_di or a.mono):
    p.error('--max-ghost-percent requires the poly owner DI score')
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


def score_owner(directory):
    import soundfile as sf

    # pYIN on the DI established the scale labels. Use fixed note interiors for
    # comparisons so a tracker cannot score on a different selection of frames.
    scale = [
        (1.70, 1.99, 45), (2.00, 2.31, 48), (2.31, 2.60, 50),
        (2.64, 2.89, 52), (2.93, 3.17, 55), (3.22, 3.45, 57),
        (3.47, 3.71, 60), (3.75, 3.99, 62), (4.00, 4.25, 64),
        (4.26, 4.49, 67), (4.50, 4.72, 69), (4.74, 4.97, 72),
        (4.98, 5.20, 69), (5.20, 5.42, 67), (5.44, 5.66, 64),
        (5.69, 5.89, 62), (5.90, 6.12, 60), (6.14, 6.34, 57),
        (6.37, 6.62, 55), (6.65, 6.87, 52), (6.89, 7.10, 50),
        (7.11, 7.31, 48), (7.34, 7.70, 45),
    ]
    harmonics = 12 * np.log2(np.arange(2, 7))

    def render(name):
        samples, rate = sf.read(directory / name, dtype='float32', always_2d=True)
        if rate != 44100:
            raise ValueError(f'{name}: expected 44.1 kHz')
        conversion = ', PCM16 for mono bench' if a.mono else ''
        print(f'{name} (left channel, 44100 Hz, original gain{conversion})')
        if a.mono:
            with tempfile.TemporaryDirectory() as temp:
                wav, trace = Path(temp) / 'di.wav', Path(temp) / 'trace.csv'
                sf.write(wav, samples[:, 0], rate, subtype='PCM_16')
                subprocess.run([a.exe, str(wav), str(trace)], check=True)
                with trace.open() as f:
                    rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]
            hz = np.asarray([[r['voice']] for r in rows])
            sounding = hz > 0
        else:
            rows = run(samples[:, 0])
            hz = np.asarray([[r['v1'], r['v2']] for r in rows])
            gain = np.asarray([[r['gain1'], r['gain2']] for r in rows])
            sounding = (gain > 0.065) & (hz > 0)
        times = np.asarray([r['t'] for r in rows])
        pitch = np.where(sounding, 69 + 12 * np.log2(np.maximum(hz, 1) / 440), np.nan)
        return times, pitch, sounding.sum(axis=1)

    def singles(label, time, pitch, count, notes):
        expected = np.full(time.shape, np.nan)
        for start, stop, note in notes:
            expected[(time >= start) & (time < stop)] = note
        valid = np.isfinite(expected)
        offset = pitch - expected[:, None]
        correct = np.any(np.abs(offset) <= 0.6, axis=1)
        overtone = np.any(np.abs(offset[:, :, None] - harmonics) <= 0.6, axis=(1, 2))
        ghosts = (count == 2) & overtone & valid
        ghost_pct = 100 * np.mean(ghosts[valid])
        neighbour = np.any((np.abs(offset) > 0.6) & (np.abs(offset) <= 1.6), axis=1)
        if a.mono:
            below = np.any(np.abs(offset + 12) <= 0.6, axis=1)
            print(f'  {label}: {valid.sum()} frames, correct {100 * correct[valid].mean():.2f}%, '
                  f'upper harmonics {100 * overtone[valid].mean():.2f}%, '
                  f'octave below {100 * below[valid].mean():.2f}%, '
                  f'neighbour slips {100 * neighbour[valid].mean():.2f}%, '
                  f'silent {100 * np.mean(count[valid] == 0):.2f}%')
            return 0
        print(f'  {label}: {valid.sum()} frames, two voices {100 * np.mean(count[valid] == 2):.2f}%, '
              f'ghosts {ghost_pct:.2f}% ({ghosts.sum() * 128 / 44100:.3f} s), '
              f'correct note absent {100 * np.mean(~correct[valid]):.2f}%, '
              f'neighbour slips {100 * np.mean(neighbour[valid]):.2f}%, '
              f'silent {100 * np.mean(count[valid] == 0):.2f}%')
        return ghost_pct

    time, pitch, count = render('scale + duo + power chords di.wav')
    ghost_pct = singles('scale 1.70–7.70 s', time, pitch, count, scale)
    # The chord interiors below were checked against their DI partials. Leave
    # ambiguous changes and muted gaps out of the per-chord denominators.
    for label, start, stop, root, fifth in [
        ('duo A2/E3', 8.20, 8.37, 45, 52),
        ('power A2/E3', 9.48, 9.85, 45, 52),
        ('power D3/A3', 10.25, 10.57, 50, 57),
        ('power E2/B2', 11.20, 13.85, 40, 47),
    ]:
        pick = (time >= start) & (time < stop)
        lo = np.any(np.abs(pitch[pick] - root) <= 0.6, axis=1)
        hi = np.any(np.abs(pitch[pick] - fifth) <= 0.6, axis=1)
        match = f'either {100 * (lo | hi).mean():.2f}%' if a.mono else f'both {100 * (lo & hi).mean():.2f}%'
        print(f'  {label} {start:.2f}–{stop:.2f} s: root {100 * lo.mean():.2f}%, {match}')

    time, pitch, count = render('low e open di.wav')
    for start, stop in [(2.60, 12.50), (13.47, 24.40)]:
        singles(f'E2 {start:.2f}–{stop:.2f} s', time, pitch, count, [(start, stop, 40)])
        in_note = (time >= start) & (time < stop)
        correct = in_note & np.any(np.abs(pitch - 40) <= 0.6, axis=1)
        if correct.any():
            sounding = time[in_note & (count > 0)]
            print(f'    first correct {time[correct][0]:.3f} s, '
                  f'last correct {time[correct][-1]:.3f} s, last audible frame {sounding[-1]:.3f} s')
    return ghost_pct


if a.owner_di:
    ghost_pct = score_owner(a.owner_di)
    raise SystemExit(int(a.max_ghost_percent is not None and ghost_pct > a.max_ghost_percent))

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
