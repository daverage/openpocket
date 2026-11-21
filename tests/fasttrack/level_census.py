#!/usr/bin/env python3
"""Level census of the evaluation sets (docs/FASTTRACK-LEVELS.md).  Active frames = 10 ms RMS above -60 dBFS; medians over files; 'under the gate' = below the tracker's 0.003 (-50.5 dBFS).
Stereo files: left channel (as the harness and the pedal).   python3 tests/fasttrack/level_census.py"""
import glob, os, numpy as np, soundfile as sf
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
def census(files, label):
    pk, med, p10, p90, below = [], [], [], [], []
    for f in files:
        x, sr = sf.read(f, dtype='float32'); x = x if x.ndim == 1 else x[:, 0]
        h = int(0.010 * sr); e = np.sqrt(np.convolve(x ** 2, np.ones(h) / h, 'same')); act = e[e > 10 ** (-60 / 20)]
        if len(act) < 100: continue
        d = 20 * np.log10(act + 1e-9); pk.append(20 * np.log10(np.abs(x).max() + 1e-9)); med.append(np.median(d)); p10.append(np.percentile(d, 10)); p90.append(np.percentile(d, 90)); below.append(np.mean(act < 0.003))
    print(f'{label:30s} files {len(pk):3d} | peak dBFS median {np.median(pk):6.1f} | active RMS dBFS median {np.median(med):6.1f} (p10 {np.median(p10):6.1f} / p90 {np.median(p90):6.1f}) | under the gate {np.mean(below):5.1%}')
S = ROOT + '/build/stage5/sets/'
for lab, pat, step in (('Guitar-TECHS scales', 'gtechs/P*_scales_*.wav', 1), ('Guitar-TECHS music', 'gtechs/*_music_*.wav', 1), ('Guitar-TECHS palm mute', 'gtechs/*techniques_PalmMute*.wav', 1), ('Guitar-TECHS single notes', 'gtechs/*singlenotes*.wav', 1),
                       ('IDMT licks (every 3rd)', 'idmt_licks/*.wav', 3), ('IDMT dataset 4 (every 4th)', 'idmt_d4/*.wav', 4)):
    census(sorted(glob.glob(S + pat))[::step][:60], lab)
census(sorted(glob.glob(ROOT + '/My Audio/*di*.wav')) + [ROOT + '/My Audio/bleep.wav'], 'owner recordings')
