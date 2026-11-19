#!/usr/bin/env python3
"""OpenPocket Synth, stage 5: a minimal two-voice allocator on the shared tracker's notes, run causally frame by frame
on the cached tracker output of tools/research/poly_compare.py (build/stage5/poly/<set>/tracker), written as the
candidate "alloc" (or alloc_<tag>) and scored by poly_compare.py report.

  python3 tools/research/poly_alloc.py --set dev [--confirm 3] [--hold 15] [--max 2] [--tag NAME]

Per frame (1.45 ms): a sounding voice keeps its note while the tracker reports a note within 120 cents of it (it
follows it: bends, vibrato), and is released after HOLD frames without one. A new note (not within 120 cents of a
voice) that the tracker reports CONFIRM frames running takes a free voice; with none free it is ignored. Lower notes
first when two arrive together. No harmonic rules, no level rules: the tracker's own salience decides what it reports.
"""
import argparse, glob, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import poly_compare as pc


def near(a, b, c): return a > 0 and b > 0 and abs(1200 * np.log2(a / b)) < c


def allocate(rows, confirm, hold, nmax):
    voices = []                                             # [f, missed]
    cand = {}                                               # rounded midi -> (frames seen, last f)
    out = []
    for t, notes in rows:
        notes = sorted(notes)
        used = set()
        for v in voices:
            j = next((k for k, n in enumerate(notes) if k not in used and near(n, v[0], 120)), None)
            if j is None: v[1] += 1
            else: used.add(j); v[0] += 0.6 * (notes[j] - v[0]); v[1] = 0
        voices = [v for v in voices if v[1] < hold]
        seen = {}
        for k, n in enumerate(notes):
            if k in used: continue
            m = int(round(69 + 12 * np.log2(n / 440)))
            seen[m] = (cand.get(m, (0, n))[0] + 1, n)
        cand = seen
        for m in sorted(cand):
            c, n = cand[m]
            if c >= confirm and len(voices) < nmax and not any(near(n, v[0], 120) for v in voices):
                voices.append([n, 0]); del cand[m]
                if len(voices) >= nmax: break
        out.append((t, [v[0] for v in voices if v[1] == 0 or v[1] < hold]))
    return out


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--set', default='dev'); ap.add_argument('--confirm', type=int, default=3)
    ap.add_argument('--hold', type=int, default=15); ap.add_argument('--max', type=int, default=2); ap.add_argument('--tag')
    a = ap.parse_args()
    name = 'alloc' + (f'_{a.tag}' if a.tag else '')
    d = os.path.join(pc.OUT, a.set, name); os.makedirs(d, exist_ok=True)
    n = 0
    for take, wav, ref in pc.takes(a.set):
        src = pc.cache(a.set, 'tracker', take)
        if not os.path.exists(src): continue
        t, fs = pc.load(src)
        rows = allocate(list(zip(t, fs)), a.confirm, a.hold, a.max)
        with open(os.path.join(d, take + '.csv'), 'w') as f:
            for ti, v in rows: f.write(','.join([f'{ti:.4f}'] + [f'{x:.2f}' for x in v]) + '\n')
        n += 1
    if name not in pc.CANDS: pass
    print(f'{name}: {n} takes (confirm {a.confirm}, hold {a.hold}, max {a.max})')
