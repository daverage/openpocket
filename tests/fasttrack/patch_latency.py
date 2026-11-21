#!/usr/bin/env python3
"""How long from a pluck to an AUDIBLE synth note, per patch? (the tracker's note-on is only the start of the envelope)  python3 tests/fasttrack/patch_latency.py
A synthetic pluck (165 Hz, harmonic series, 3 ms attack, 1.2 s decay, peak about -12 dBFS) starts at exactly 500.0 ms and goes through the real legacy chain (tracker + adapter + Felucca) via ft_synth.
Reported per synth type and patch setting: note-on (the envelope starts), first audible (output RMS over 5 ms above -40 dB re the note's own peak), and -6 dB (half of the peak: what the ear takes as 'the note').
Needs build/fasttrack/ft_synth_base (tests/fasttrack/regress.sh)."""
import os, subprocess, tempfile
import numpy as np, soundfile as sf
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')); SR = 44100
t = np.arange(int(1.8 * SR)) / SR; on = 0.5; ph = 2 * np.pi * 165.0 * t
x = sum(np.sin(h * ph) / h for h in range(1, 7)) * np.where(t >= on, (1 - np.exp(-(t - on) / 0.003)) * np.exp(-(t - on) / 1.2), 0.0) * 0.18
tmp = tempfile.mkdtemp(); inp = os.path.join(tmp, 'pluck.wav'); sf.write(inp, x.astype('float32'), SR, subtype='PCM_16')
NAMES = ['ANALOG', 'ARCADE', 'INSTRUMENT', 'ENSEMBLE']; rows = []
for ty in range(4):
    for patch in range(0, 101, 5):
        out = os.path.join(tmp, 'o.wav'); env = dict(os.environ, FT_TYPE=str(ty), FT_PATCH=str(patch), FT_WAV=out)
        lines = subprocess.run([ROOT + '/build/fasttrack/ft_synth_base', inp], env=env, capture_output=True, text=True).stdout.splitlines()
        att = [float(l.split()[1]) for l in lines if l.startswith('A ')]
        if not os.path.exists(out): continue
        y, _ = sf.read(out); h = int(0.005 * SR); e = np.sqrt(np.convolve(y ** 2, np.ones(h) / h, 'same')); seg = e[int(on * SR):int((on + 1.0) * SR)]
        if seg.max() < 1e-4: continue
        pk = seg.max(); i40 = np.argmax(seg > pk * 10 ** (-40 / 20)); i6 = np.argmax(seg > pk * 0.5)
        rows.append((NAMES[ty], patch, (att[0] - on * 1000) if att else None, i40 / SR * 1000, i6 / SR * 1000))
os.remove(inp)
print(f'{"type":11s} {"patch":>5s} {"note-on ms":>10s} {"audible ms":>10s} {"half-level ms":>13s}')
for r in rows: print(f'{r[0]:11s} {r[1]:5d} {"-" if r[2] is None else f"{r[2]:.1f}":>10s} {r[3]:10.1f} {r[4]:13.1f}')
print()
for nm in NAMES:
    rs = [r for r in rows if r[0] == nm]
    if rs: print(f'{nm:11s} patches {len(rs):2d}: half-level latency median {np.median([r[4] for r in rs]):6.1f} ms, p90 {np.percentile([r[4] for r in rs], 90):6.1f}, max {max(r[4] for r in rs):6.1f}; audible-onset median {np.median([r[3] for r in rs]):5.1f}')
