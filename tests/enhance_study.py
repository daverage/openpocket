#!/usr/bin/env python3
"""ENHANCE v3 study (docs/ENHANCE.md): numbers and level-matched listening renders on real DI, through engine/px_enhance.h (tests/engine/enhance_render.c).
  python3 tests/enhance_study.py [--renders]
Added = RMS of (wet - dry) over active frames, dB re the dry signal. TONE 0 = warmth only, 50 = both, 100 = presence only. Renders: build/enhance5/matched/ (BETTER 100 and 35)."""
import sys, os, subprocess
import numpy as np, soundfile as sf
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..')); EXE = ROOT + '/build/enhance_render'
def run(x, better, tone, louder=0, gain_db=0.0):
    return np.frombuffer(subprocess.run([EXE, str(better), str(louder), str(tone), str(gain_db)], input=x.astype('f4').tobytes(), capture_output=True, check=True).stdout, dtype='f4')
def active(x): h = 441; e = np.sqrt(np.convolve(x.astype(np.float64) ** 2, np.ones(h) / h, 'same')); return e > 10 ** (-50 / 20)
def added_db(x, y): m = active(x); return 20 * np.log10(np.sqrt(np.mean((y[m] - x[m]) ** 2)) / np.sqrt(np.mean(x[m] ** 2)) + 1e-12)
CLIPS = ['My Audio/bleep3 di.wav', 'My Audio/fast play di.wav', 'My Audio/scale + duo + power chords di.wav']
CFG = [('warm', 0), ('both', 50), ('bright', 100)]
if __name__ == '__main__':
    X = [sf.read(f'{ROOT}/{f}', dtype='float32')[0][:, 0][:44100 * 14] for f in CLIPS]
    print('added signal re dry on the owner\'s DI clips (median of 3), LOUDER 0')
    print(f'{"BETTER":>8s} ' + ' '.join(f'{n:>10s}' for n, _ in CFG))
    for b in (35, 100): print(f'{b:8d} ' + ' '.join(f'{np.median([added_db(x, run(x, b, t)) for x in X]):9.1f} ' for _, t in CFG))
    if '--renders' in sys.argv:
        sys.path.insert(0, ROOT + '/tests/fasttrack'); import level_match as LM
        out = ROOT + '/build/enhance5'; os.makedirs(out, exist_ok=True)
        for f, x in zip(CLIPS, X):
            tag = os.path.basename(f)[:-4].replace(' ', '_').replace('+', 'and'); paths = []
            sf.write(f'{out}/{tag}_0_dry.wav', x, 44100, subtype='PCM_24'); paths.append(f'{out}/{tag}_0_dry.wav')
            for k, (b, t, nm) in enumerate(((35, 50, 'both_35'), (100, 0, 'warm_100'), (100, 100, 'bright_100'), (100, 50, 'both_100'), (35, 0, 'warm_35'), (35, 100, 'bright_35')), 1):
                p = f'{out}/{tag}_{k}_{nm}.wav'; sf.write(p, run(x, b, t), 44100, subtype='PCM_24'); paths.append(p)
            LM.main(out + '/matched', paths)
        print('level-matched renders in', out + '/matched')
