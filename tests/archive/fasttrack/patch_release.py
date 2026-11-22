#!/usr/bin/env python3
"""How long does a synth note hang on after the string is MUTED (default) or after it has died away (--natural), per patch?  python3 tests/archive/fasttrack/patch_release.py [--natural] [--csv OUT]
A 165 Hz harmonic note rings (a slow natural decay) and is muted hard (a 3 ms fade, as a palm mute or a hand on the string) at exactly 1.000 s; it goes through the real legacy chain (tracker + adapter + Felucca) via ft_synth.
Reported per synth type and patch setting: the time from the mute to the output falling 20 dB and 40 dB below its level at the mute (output RMS over 5 ms). A pad that rings for seconds after a muted note is what 'slow release' sounds like.
--natural: the string rings out (decay time constant 0.45 s, no mute); reported per patch: the time the synth keeps sounding after the GUITAR has fallen 50 dB below its peak (the synth to -20 and -40 dB of its own peak, measured from that moment). Negative: already quiet.
Needs build/fasttrack/ft_synth_base (tests/archive/fasttrack/regress.sh, or the ft_synth_base line of it)."""
import os, subprocess, sys, tempfile
import numpy as np, soundfile as sf
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..')); SR = 44100
T_ON, T_MUTE = 0.3, 1.0
NAT = '--natural' in sys.argv
t = np.arange(int(4.0 * SR)) / SR; ph = 2 * np.pi * 165.0 * t
fade = np.ones_like(t) if NAT else np.clip(1.0 - (t - T_MUTE) / 0.003, 0.0, 1.0)
x = sum(np.sin(h * ph) / h for h in range(1, 7)) * np.where(t >= T_ON, (1 - np.exp(-(t - T_ON) / 0.003)) * np.exp(-(t - T_ON) / (0.45 if NAT else 3.0)), 0.0) * fade * 0.18
tmp = tempfile.mkdtemp(); inp = os.path.join(tmp, 'muted.wav'); sf.write(inp, x.astype('float32'), SR, subtype='PCM_16')
NAMES = ['ANALOG', 'ARCADE', 'INSTRUMENT', 'ENSEMBLE']; rows = []
for ty in range(4):
    for patch in range(0, 101, 5):
        out = os.path.join(tmp, 'o.wav'); env = dict(os.environ, FT_TYPE=str(ty), FT_PATCH=str(patch), FT_WAV=out)
        subprocess.run([ROOT + '/build/fasttrack/ft_synth_base', inp], env=env, capture_output=True, text=True)
        if not os.path.exists(out): continue
        y, _ = sf.read(out); h = int(0.005 * SR); e = np.sqrt(np.convolve(y ** 2, np.ones(h) / h, 'same'))
        if NAT:                                                                         # the moment the guitar itself is 50 dB down
            g = np.sqrt(np.convolve(x ** 2, np.ones(h) / h, 'same')); pkg = g.max(); m = int(np.argmax((g < pkg * 10 ** (-50 / 20)) & (np.arange(len(g)) > np.argmax(g)))); ref = e[int(T_ON * SR):m].max()
        else: m = int(T_MUTE * SR); ref = e[m - h:m].mean()
        if ref < 1e-4: continue
        post = e[m:]; i20 = int(np.argmax(post < ref * 0.1)) if (post < ref * 0.1).any() else len(post); i40 = int(np.argmax(post < ref * 0.01)) if (post < ref * 0.01).any() else len(post)
        rows.append((NAMES[ty], patch, i20 / SR * 1000, i40 / SR * 1000))
os.remove(inp)
print(f'{"type":11s} {"patch":>5s} {"-20 dB ms":>10s} {"-40 dB ms":>10s}')
for r in rows: print(f'{r[0]:11s} {r[1]:5d} {r[2]:10.0f} {r[3]:10.0f}')
print()
for nm in NAMES:
    rs = [r for r in rows if r[0] == nm]
    if rs: print(f'{nm:11s} patches {len(rs):2d}: -20 dB median {np.median([r[2] for r in rs]):6.0f} ms, p90 {np.percentile([r[2] for r in rs], 90):6.0f}, max {max(r[2] for r in rs):6.0f};  -40 dB median {np.median([r[3] for r in rs]):6.0f}, p90 {np.percentile([r[3] for r in rs], 90):6.0f}, max {max(r[3] for r in rs):6.0f}')
if '--csv' in sys.argv:
    with open(sys.argv[sys.argv.index('--csv') + 1], 'w') as f:
        f.write('type,patch,ms20,ms40\n'); [f.write('%s,%d,%.1f,%.1f\n' % r) for r in rows]
