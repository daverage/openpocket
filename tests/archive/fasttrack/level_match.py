#!/usr/bin/env python3
"""Level-match WAVs for A/B listening.  python3 tests/archive/fasttrack/level_match.py OUT_DIR a.wav b.wav [...] [--target -24]
Each file is scaled by ONE gain so that the RMS over its active parts (10 ms frames above -50 dBFS, gated so silence does not count) equals the target (default -24 dBFS RMS);
the peak is checked and the gain reduced if it would clip above -1 dBFS.  Nothing else is changed (no limiter, no EQ).  Writes OUT_DIR/<name>.lm.wav (24-bit mono) and prints the gains.
Why: the controller's neutral oscillator, the legacy Felucca voice and the dry guitar differ by 10-20 dB; unmatched, the louder one is judged better."""
import sys, os
import numpy as np, soundfile as sf
def active_rms_db(x, sr):
    h = int(0.010 * sr); e = np.sqrt(np.convolve(x.astype(np.float64) ** 2, np.ones(h) / h, 'same')); a = e[e > 10 ** (-50 / 20)]
    return 20 * np.log10(np.sqrt(np.mean(a ** 2)) + 1e-12) if len(a) else -120.0
def main(out, files, target=-24.0):
    os.makedirs(out, exist_ok=True)
    for f in files:
        x, sr = sf.read(f, dtype='float64'); x = x if x.ndim == 1 else x[:, 0]
        g = target - active_rms_db(x, sr); pk = 20 * np.log10(np.abs(x).max() + 1e-12) + g
        if pk > -1.0: g -= pk + 1.0
        y = x * 10 ** (g / 20); p = os.path.join(out, os.path.basename(f)[:-4] + '.lm.wav'); sf.write(p, y, sr, subtype='PCM_24')
        print(f'{os.path.basename(f):44s} active RMS {active_rms_db(x, sr):6.1f} dBFS -> gain {g:+5.1f} dB -> {active_rms_db(y, sr):6.1f} dBFS, peak {20*np.log10(np.abs(y).max()+1e-12):6.1f} dBFS')
if __name__ == '__main__':
    a = sys.argv[1:]; t = -24.0
    if '--target' in a: i = a.index('--target'); t = float(a[i + 1]); del a[i:i + 2]
    main(a[0], a[1:], t)
