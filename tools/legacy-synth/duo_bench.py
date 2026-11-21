#!/usr/bin/env python3
"""OpenPocket Synth, stage 5: what a tracked synth plays, scored as notes (what a player hears), not frames.

  python3 tools/legacy-synth/duo_bench.py --set gtchords|dev|gtechs [--max N] --cand NAME[=exe:mode] ...

Candidates: poly_<exe> (build/<exe>, tools/legacy-synth/pulse_poly_bench.c as STRING DUO: its two voices while their gain is
above 0.065) and mono_<exe> (build/<exe>, tools/legacy-synth/mono_bench.c: the one voice). Each voice's output is cut into
played notes (a new note at a jump over 80 cents or a gap); notes shorter than 30 ms are dropped. A played note is
  right   within 50 cents of a note sounding at its start (+-40 ms),
  octave  12 or 24 semitones from one (an octave jump),
  wrong   anything else (a random beep).
missed = annotated notes no voice plays within their first 300 ms; chord2 = the share of frames with two or more
notes sounding where two voices play two different right notes; bottom = frames with a note sounding where some
voice plays the lowest one."""
import argparse, csv, glob, os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'research'))
import poly_compare as pc

def takes(s):
    if s == 'gtechs':
        return [(os.path.basename(w)[:-4], w, ('csv', w[:-4] + '.csv'))
                for w in sorted(glob.glob(os.path.join(pc.ROOT, 'build', 'stage5', 'sets', 'gtechs', '*.wav')))]
    return pc.takes(s)

def trace(cand, wav):
    kind, exe = cand.split('_', 1)
    x, _ = pc.read(wav, 44100)
    if kind == 'low':                                  # low_<sel>_<exe>: the mono tracker on the lowest bands
        sel, exe = exe.split('_', 1); kind = 'mono'
        import librosa
        with tempfile.NamedTemporaryFile(suffix='.f32', delete=False) as a, tempfile.NamedTemporaryFile(suffix='.f32', delete=False) as b:
            a.write(x.astype('<f4').tobytes())
        subprocess.run([os.path.join(pc.ROOT, 'build', 'lowsig'), a.name, b.name, sel], check=True)
        y = np.fromfile(b.name, dtype='<f4'); os.unlink(a.name); os.unlink(b.name)
        x = librosa.resample(y, orig_sr=11025, target_sr=44100)[:len(x)]
    if kind == 'fix':
        exe0 = exe; exe = exe.replace('solo_', '').replace('nooct_', '')                                  # fix_<mono exe>: the duo's voices, an octave jump from the mono tracker's note snapped to it
        t1, (mo,) = trace(f'mono_{exe}', wav); t2, pv = trace('poly_poly_cur', wav)
        idx = np.clip(np.searchsorted(t1, t2), 0, len(t1) - 1); mo = mo[idx]
        out = []; solo = ((pv[0] > 0).astype(int) + (pv[1] > 0).astype(int)) == 1 if 'solo' in exe0 else True
        for v in pv:
            v = v.copy()
            m = (v > 0) & (mo > 0) & solo
            d = np.zeros_like(v); d[m] = cents(v[m], mo[m])
            for k in (-2400, -1200, 1200, 2400): v[m & (np.abs(d - k) < 60)] = mo[m & (np.abs(d - k) < 60)]
            out.append(v)
        if all(len(o) for o in out):                   # (two voices on one note: the second goes quiet)
            same = (out[0] > 0) & (out[1] > 0) & (np.abs(cents(np.maximum(out[0], 1), np.maximum(out[1], 1))) < 60)
            out[1][same] = 0
            if 'nooct' in exe0:                        # (two voices an octave or two apart: the upper is a harmonic)
                lo_, hi_ = np.minimum(out[0], out[1]), np.maximum(out[0], out[1])
                both = (lo_ > 0)
                d = np.zeros_like(lo_); d[both] = cents(hi_[both], lo_[both])
                octv = both & ((np.abs(d - 1200) < 60) | (np.abs(d - 2400) < 60) | (np.abs(d - 1902) < 60))
                for o in out: o[octv & (o == hi_)] = 0
        return t2, out
    with tempfile.NamedTemporaryFile(suffix='.csv', delete=False) as tf: tr = tf.name
    if kind == 'poly':
        subprocess.run([os.path.join(pc.ROOT, 'build', exe), tr, 'duo'], input=x.astype('<f4').tobytes(), stdout=subprocess.DEVNULL, check=True)
        rows = list(csv.DictReader(open(tr))); os.unlink(tr)
        t = np.array([float(r['t']) for r in rows])
        v = [[float(r[a]) if float(r[g]) > 0.065 else 0.0 for r in rows] for a, g in (('v1', 'gain1'), ('v2', 'gain2'))]
    else:
        import soundfile as sf
        w = tr[:-4] + '.wav'; sf.write(w, x, 44100, subtype='PCM_16')
        subprocess.run([os.path.join(pc.ROOT, 'build', exe), w, tr], check=True); os.unlink(w)
        rows = list(csv.DictReader(open(tr))); os.unlink(tr)
        t = np.array([float(r['t']) for r in rows]); v = [[float(r['voice']) for r in rows]]
    return t, [np.array(a) for a in v]

def cents(a, b): return 1200 * np.log2(a / b)

def segments(t, v):
    out, s = [], None
    for i in range(len(t)):
        f = v[i]
        if f > 0 and s is not None and abs(cents(f, s[2])) < 80: s[1] = t[i]; continue
        if s is not None: out.append(tuple(s))
        s = [t[i], t[i], f] if f > 0 else None
    if s is not None: out.append(tuple(s))
    return [x for x in out if x[1] - x[0] >= 0.03]

def score(t, vs, notes):
    hz = lambda m: 440 * 2 ** ((m - 69) / 12)
    r = dict(right=0, octave=0, wrong=0, missed=0, notes=len(notes), c2n=0, c2d=0, bn=0, bd=0)
    for v in vs:
        for a, b, f in segments(t, v):
            snd = [hz(m) for s, e, m in notes if s - 0.04 <= a <= e + 0.04]
            d = [abs(cents(f, x)) for x in snd]
            if any(x < 50 for x in d): r['right'] += 1
            elif any(abs(x - 1200) < 50 or abs(x - 2400) < 50 for x in d): r['octave'] += 1
            else: r['wrong'] += 1
    for s, e, m in notes:
        i0, i1 = np.searchsorted(t, s), np.searchsorted(t, min(e, s + 0.3))
        if not any(abs(cents(v[i], hz(m))) < 50 for v in vs for i in range(i0, i1) if v[i] > 0): r['missed'] += 1
    for i in range(0, len(t), 8):
        snd = sorted(hz(m) for s, e, m in notes if s + 0.05 <= t[i] < e)
        if not snd: continue
        play = [v[i] for v in vs if v[i] > 0]
        ok = {j for f in play for j, x in enumerate(snd) if abs(cents(f, x)) < 50}
        r['bd'] += 1; r['bn'] += 0 in ok
        if len(snd) >= 2: r['c2d'] += 1; r['c2n'] += len(ok) >= 2
    return r

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--set', default='gtchords'); ap.add_argument('--max', type=int, default=999)
    ap.add_argument('--cand', nargs='+', default=['poly_poly_cur', 'mono_mono_cur']); a = ap.parse_args()
    tk = takes(a.set)[:a.max]; mins = 0
    print(f'set {a.set}, {len(tk)} takes. Played notes: right / octave jumps / random (per minute of playing); missed notes; '
          'chord2: both voices on right notes in chords; bottom: the lowest note played')
    for c in a.cand:
        R = {}; mins = 0
        for name, wav, ref in tk:
            notes = pc.notes_of(ref)
            if not notes: continue
            t, vs = trace(c, wav); mins += t[-1] / 60
            for k, v in score(t, vs, notes).items(): R[k] = R.get(k, 0) + v
        n = R['right'] + R['octave'] + R['wrong']
        print(f'{c:18} right {100 * R["right"] / max(1, n):5.1f}%  octave {100 * R["octave"] / max(1, n):5.1f}% ({R["octave"] / mins:5.1f}/min)  '
              f'random {100 * R["wrong"] / max(1, n):5.1f}% ({R["wrong"] / mins:5.1f}/min)  missed {100 * R["missed"] / max(1, R["notes"]):5.1f}%  '
              f'chord2 {100 * R["c2n"] / max(1, R["c2d"]):5.1f}%  bottom {100 * R["bn"] / max(1, R["bd"]):5.1f}%', flush=True)
