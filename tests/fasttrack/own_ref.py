#!/usr/bin/env python3
"""Pseudo-reference for unlabelled recordings (NOT ground truth): offline onset detection (librosa, with look-ahead) and the median offline pYIN pitch
60-160 ms after each onset. Used only to ask, per detected pick, whether the real-time tracker's note-on agrees with a slow offline estimate and how late it is.
  python3 tests/fasttrack/own_ref.py build/fasttrack/own/bleep3_di.wav VARIANT [VARIANT..]"""
import sys, warnings; warnings.filterwarnings('ignore')
import numpy as np, soundfile as sf, librosa
sys.path.insert(0, __file__.rsplit('/', 1)[0]); import ft_score as S
wav = sys.argv[1]; x, sr = sf.read(wav, dtype='float32'); x = x.mean(1) if x.ndim > 1 else x
oenv = librosa.onset.onset_strength(y=x, sr=sr, hop_length=64, aggregate=np.median, fmax=3000)
on = librosa.onset.onset_detect(onset_envelope=oenv, sr=sr, hop_length=64, backtrack=False, delta=0.15, wait=int(0.035 * sr / 64), units='time') * 1000
f0, vf, vp = librosa.pyin(x, fmin=60, fmax=1000, sr=sr, frame_length=2048, hop_length=64)
tf = librosa.times_like(f0, sr=sr, hop_length=64) * 1000
ref = []
for o in on:
    m = (tf >= o + 60) & (tf <= o + 160) & vf
    ref.append(np.nanmedian(f0[m]) if m.sum() >= 5 else np.nan)
ref = np.array(ref)
print(f'{len(on)} offline onsets, {np.isfinite(ref).sum()} with a pitch')
for v in sys.argv[2:]:
    base = wav.rsplit('.', 1)[0] + '.' + v
    ev = S.load_ev(base + '.ev'); N = [(t, a[0]) for k, t, a in ev if k == 'N' and a]; L = [(t, a[0]) for k, t, a in ev if k == 'L' and a]
    lat, ok, wrong, miss = [], 0, [], 0
    for i, (o, r) in enumerate(zip(on, ref)):
        if not np.isfinite(r): continue
        nxt = on[i + 1] if i + 1 < len(on) else 1e12
        cand = [(t, f) for t, f in N + L if o - 20 <= t < min(nxt, o + 400)]
        good = [t for t, f in cand if abs(S.cents(f, r)) < 50]
        if good: ok += 1; lat.append(min(good) - o)
        else:
            miss += 1; wrong.append((round(o), round(r, 1), [(round(t), round(f, 1)) for t, f in cand][:3]))
    extra = sum(1 for t, f in N if not any(o - 20 <= t < o + 400 for o in on))
    print(f'{v:14s} picks={ok+miss} found={ok} missed={miss} lat med/p90={np.median(lat):.1f}/{np.percentile(lat,90):.1f} ms   note-ons not near any offline onset={extra}  total N={len(N)}')
    if v == sys.argv[2]:
        for w in wrong: print('   miss: onset', w[0], 'ref', w[1], 'Hz; tracker said', w[2])
