#!/usr/bin/env python3
"""OpenPocket Synth, stage 5: does an octave-down follow only the lowest note of a chord?

  python3 tools/research/low_bench.py --set dev|gtchords --conf NAME:block:type:p0,p1,p2,p3,p4[:low_sel] ...

Each configuration runs alone (build/pitch_bench) over every take. In each 46 ms frame where two or more notes sound,
the output spectrum is read at the lowest note's octave-down fundamental (wanted) and at the other notes' octave-down
fundamentals that are not harmonics of it (unwanted). Reported: the median wanted/unwanted ratio (dB, higher is cleaner),
the share of frames where the wanted note is the louder (%), and the output level against the input (dB)."""
import argparse, os, subprocess, sys, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import poly_compare as pc
EXE = os.path.join(pc.ROOT, 'build', 'pitch_bench')

def hz(m): return 440 * 2 ** ((m - 69) / 12)

def run(conf, wav):
    name, blk, typ, ps, *sel = conf.split(':')
    out = os.path.join(pc.ROOT, 'build', 'stage5', 'low', name, os.path.basename(wav)[:-4] + '.f32')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    subprocess.run([EXE, wav, out, blk, typ, *ps.split(','), *sel], check=True)
    return np.fromfile(out, dtype=np.float32)

def score(x, y, notes, sr=44100, N=4096, hop=2048):
    win = np.hanning(N); db, win_n, lv = [], 0, []
    for s in range(0, len(y) - N, hop):
        t = (s + N / 2) / sr
        on = sorted({int(round(m)) for a, b, m in notes if a + 0.06 < t < b})
        if len(on) < 2: continue
        Y = np.abs(np.fft.rfft(y[s:s + N] * win)) ** 2
        def e(f):
            k = f * N / sr; i = int(round(k)); return Y[max(1, i - 1):i + 2].sum()
        L = hz(on[0]) / 2
        want = e(L)
        bad = 0.0
        for m in on[1:]:
            f = hz(m) / 2; h = f / L
            if abs(h - round(h)) * 1200 / max(1, round(h)) > 40 or round(h) == 0: bad += e(f)
            elif abs(1200 * np.log2(h / max(1, round(h)))) > 40: bad += e(f)
        if bad <= 0: continue
        db.append(10 * np.log10((want + 1e-12) / bad)); win_n += want > bad
        lv.append(10 * np.log10((np.mean(y[s:s + N] ** 2) + 1e-12) / (np.mean(x[s:s + N] ** 2) + 1e-12)))
    return db, win_n, lv

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--set', default='dev'); ap.add_argument('--conf', nargs='+'); ap.add_argument('--max', type=int, default=99)
    a = ap.parse_args()
    tk = pc.takes(a.set)[:a.max]
    for c in a.conf:
        D, W, LV = [], 0, []
        for take, wav, ref in tk:
            x = pc.read(wav, 44100)[0]
            y = run(c, wav)
            d, w, lv = score(x[:len(y)], y, pc.notes_of(ref)); D += d; W += w; LV += lv
        D = np.array(D)
        print(f'{c.split(":")[0]:22} frames {len(D):5d}  lowest over the rest {np.median(D):+6.1f} dB  lowest louder {100 * W / max(1, len(D)):5.1f}%  level {np.median(LV):+6.1f} dB', flush=True)
