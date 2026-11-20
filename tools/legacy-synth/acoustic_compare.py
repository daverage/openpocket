#!/usr/bin/env python3
"""Host prototype (docs/research/REALISTIC-INSTRUMENT-PROTOTYPE.md): how far is each synthetic voice's steady spectrum from
the real instrument's at the same pitch and dynamic? A numeric proxy only: it says nothing about attacks, movement or
noise, and it does not replace listening.

  python3 tools/legacy-synth/acoustic_compare.py [DIR]        default DIR: build/acoustic (needs bank.json and out/raw/steady_*)

For each recorded root and dynamic: the long-term average spectrum in 1/3-octave bands (100 Hz .. 8 kHz) of the
recording's sustain loop, of the waveguide voice and of the engine's current BRASS / STRINGS (out/raw/steady_*,
rendered by acoustic_voice.c), each normalised to its own total level. Reports the mean absolute band difference (dB)
against the recording and the spectral centroid. Appends a table to out/report.md.
"""
import json, os, sys
import numpy as np
from scipy.io import wavfile

d = sys.argv[1] if len(sys.argv) > 1 else "build/acoustic"
bank = json.load(open(os.path.join(d, "bank.json")))
centres = 1000 * 2 ** (np.arange(-10, 10) / 3)            # 1/3-octave centres, about 100 Hz .. 8 kHz


def load(fn):
    r, x = wavfile.read(fn)
    x = x.astype(np.float64) / (32768.0 if x.dtype == np.int16 else 1.0)
    return r, x if x.ndim == 1 else x.mean(axis=1)


def bands(x, r):
    n = 8192
    if len(x) < n: x = np.pad(x, (0, n - len(x)))
    frames = [x[i:i + n] * np.hanning(n) for i in range(0, len(x) - n + 1, n // 2)]
    p = np.mean([np.abs(np.fft.rfft(f)) ** 2 for f in frames], axis=0)
    fr = np.fft.rfftfreq(n, 1 / r)
    out = []
    for c in centres:
        m = (fr >= c / 2 ** (1 / 6)) & (fr < c * 2 ** (1 / 6))
        out.append(p[m].sum() + 1e-20)
    out = np.array(out)
    cen = float((fr * p).sum() / (p.sum() + 1e-20))
    return 10 * np.log10(out / out.sum()), cen


rows = []
for s in bank["samples"]:
    r, x = load(os.path.join(d, "samples", s["name"] + ".wav"))
    ref, ref_c = bands(x[s["loop_start"]:s["loop_end"]], r)
    midi = int(round(s["midi"]))
    row = [s["name"], f"{ref_c:.0f}"]
    for kind in ("waveguide", "engine"):
        fn = os.path.join(d, "out", "raw", f"steady_{s['instrument']}_{kind}_{s['dynamic']}_{midi}.wav")
        rr, y = load(fn)
        b, c = bands(y[int(0.4 * rr):int(1.4 * rr)], rr)
        mask = ref > -60                                         # ignore bands where the recording has next to nothing
        row += [f"{np.mean(np.abs(b[mask] - ref[mask])):.1f}", f"{c:.0f}"]
    rows.append(row)

lines = ["", "## Steady spectrum against the recording (a proxy, not a listening test)", "",
         "Mean absolute difference of the 1/3-octave long-term spectrum (100 Hz .. 8 kHz, each normalised to its own level) "
         "against the VSCO recording's sustain loop at the same pitch and dynamic, and the spectral centroid. Lower is closer. "
         "The engine column is today's BRASS (trumpet rows) or STRINGS (cello rows) on a plucked DI note at the sounding pitch. "
         "The sample voice is the recording itself at its roots, so it has no row; between roots it is transposed by up to "
         "six semitones.", "",
         "| recording | centroid (Hz) | waveguide diff (dB) | waveguide centroid | engine diff (dB) | engine centroid |",
         "| --- | --- | --- | --- | --- | --- |"]
lines += ["| " + " | ".join(r) + " |" for r in rows]
txt = "\n".join(lines) + "\n"
print(txt)
with open(os.path.join(d, "out", "report.md"), "a") as f:
    f.write(txt)
