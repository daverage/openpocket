#!/usr/bin/env python3
"""ENHANCE A/B/C study (docs/ENHANCE.md): numbers and level-matched listening renders on real DI.   python3 tests/enhance_study.py [--renders]
A = the research prototype, B = smooth overload, C = B plus input shaping (60 Hz high-pass, presence, 5.5 kHz). Sensitivity LOW 2x / NORMAL 3.5x / HIGH 5x / EXTRA 7x.
Inputs: the owner's clips (build/fasttrack/labelling mono copies or My Audio), Guitar-TECHS (build/stage5/sets/gtechs), IDMT dataset 4 (hot). -6 dB gain = the pedal's ADC scaling.
Added = RMS of (wet - dry) over active frames, dB re the dry signal.  Renders: build/enhance/<clip>_<variant>.wav, all level matched to one active RMS (tests/fasttrack/level_match.py)."""
import sys, os, glob, subprocess, tempfile
import numpy as np, soundfile as sf
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..')); EXE = ROOT + '/build/enhance_render'
def run(x, better, sens, voice, louder=0, gain_db=0.0):
    r = subprocess.run([EXE, str(better), str(louder), str(sens), str(voice), str(gain_db)], input=x.astype('f4').tobytes(), capture_output=True, check=True)
    return np.frombuffer(r.stdout, dtype='f4'), r.stderr.decode().strip()
def active(x): h = 441; e = np.sqrt(np.convolve(x.astype(np.float64) ** 2, np.ones(h) / h, 'same')); return e > 10 ** (-50 / 20)
def added_db(x, y):
    m = active(x); return 20 * np.log10(np.sqrt(np.mean((y[m] - x[m]) ** 2)) / np.sqrt(np.mean(x[m] ** 2)) + 1e-12)
def load(path, n=44100 * 25):
    x, sr = sf.read(path, dtype='float32'); x = x if x.ndim == 1 else x[:, 0]; return x[:n], sr
SETS = {'owner clips': [ROOT + '/My Audio/bleep3 di.wav', ROOT + '/My Audio/fast play di.wav', ROOT + '/My Audio/scale + duo + power chords di.wav'],
        'Guitar-TECHS scales': sorted(glob.glob(ROOT + '/build/stage5/sets/gtechs/P*_scales_*.wav'))[:6], 'Guitar-TECHS music': sorted(glob.glob(ROOT + '/build/stage5/sets/gtechs/*_music_*.wav'))[:6],
        'IDMT dataset 4 (hot)': sorted(glob.glob(ROOT + '/build/stage5/sets/idmt_d4/Career*fast*.wav'))[:4]}
VARS = [('A ref NORMAL', 1, 0), ('B NORMAL', 1, 1), ('B HIGH', 2, 1), ('B EXTRA', 3, 1), ('C NORMAL', 1, 2), ('C HIGH', 2, 2)]
if __name__ == '__main__':
    print('added signal re dry (dB, RMS over active frames), BETTER 35 / 100, no gain (file level)  |  at the pedal (-6 dB)')
    print(f'{"set":22s} ' + ' '.join(f'{n:>15s}' for n, _, _ in VARS))
    for name, files in SETS.items():
        for gain in (0.0, -6.0):
            row = []
            for _, sens, voice in VARS:
                vals = []
                for b in (35, 100):
                    v = []
                    for f in files:
                        if not os.path.exists(f): continue
                        x, sr = load(f)
                        if sr != 44100 or not active(x).any(): continue
                        y, _ = run(x * 10 ** (gain / 20), b, sens, voice); v.append(added_db(x * 10 ** (gain / 20), y))
                    vals.append(np.median(v) if v else float('nan'))
                row.append(f'{vals[0]:6.1f}/{vals[1]:6.1f}  ')
            print(f'{name + (" (-6 dB)" if gain else ""):22s} ' + ' '.join(row))
    if '--renders' in sys.argv:
        out = ROOT + '/build/enhance'; os.makedirs(out, exist_ok=True); sys.path.insert(0, ROOT + '/tests/fasttrack'); import level_match as LM
        for f in SETS['owner clips']:
            x, sr = load(f, 44100 * 12); tag = os.path.basename(f)[:-4].replace(' ', '_'); paths = []
            sf.write(f'{out}/{tag}_0_dry.wav', x, sr, subtype='PCM_24'); paths.append(f'{out}/{tag}_0_dry.wav')
            for label, sens, voice in (('A', 1, 0), ('B', 1, 1), ('B_high', 2, 1), ('C', 1, 2), ('C_high', 2, 2)):
                y, _ = run(x, 100, sens, voice); p = f'{out}/{tag}_{label}.wav'; sf.write(p, y, sr, subtype='PCM_24'); paths.append(p)
            LM.main(out + '/matched', paths)
        print('level-matched renders in', out + '/matched', '(BETTER 100, LOUDER 0: exaggerated on purpose for listening; each file one gain to the same active RMS)')
