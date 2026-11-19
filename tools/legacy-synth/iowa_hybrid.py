#!/usr/bin/env python3
"""v49: the TRUMPET bank as recorded attack + synthesised sustain, from the Iowa samples tools/sim/iowa_prep.py made.

  python3 -I tools/sim/iowa_hybrid.py [build/iowa] [build/iowa_hybrid] [NH]

Each sample keeps the recording up to its attack end + 20 ms (the splice). The sustain is synthesised: NH (default 64)
harmonics below 10.5 kHz, their levels and phases fitted (least squares) to the recording in the 20 ms before the
splice, so the 20 ms crossfade meets the same waveform. The sustain is one loop of a whole number of cycles (pitch
within 1 cent of the recording's), so the existing sample player (engine/px_acoustic.h) plays it as a loop: no new
engine code, the same cost. Writes OUTDIR/samples/*.wav and OUTDIR/bank.json (acoustic_prep.py's format).
"""
import json, os, sys
import numpy as np
from scipy.io import wavfile

src = sys.argv[1] if len(sys.argv) > 1 else "build/iowa"
dst = sys.argv[2] if len(sys.argv) > 2 else "build/iowa_hybrid"
NH = int(sys.argv[3]) if len(sys.argv) > 3 else 64
TOP = 10500.0
os.makedirs(os.path.join(dst, "samples"), exist_ok=True)
bank = json.load(open(os.path.join(src, "bank.json")))


def best_loop(f0, sr, lo=0.025, hi=0.06):
    """a loop of L samples holding c whole cycles, its pitch nearest f0 (cents)"""
    best = None
    for L in range(int(lo * sr), int(hi * sr) + 1):
        c = round(L * f0 / sr)
        if c < 1: continue
        err = 1200 * np.log2(c * sr / L / f0)
        if best is None or abs(err) < abs(best[2]): best = (L, c, err)
    return best


out = []
for s in bank["samples"]:
    sr, x = wavfile.read(os.path.join(src, "samples", s["name"] + ".wav")); x = x.astype(np.float64) / 32768.0
    f0 = s["f0_hz"]; xf = int(0.02 * sr)
    splice = s["attack_end"] + xf
    L, c, err = best_loop(f0, sr)
    fl = c * sr / L                                         # the loop's pitch (within 1 cent of f0)
    nh = min(NH, int(TOP / f0))
    # fit the harmonics to the recording over the 20 ms before the splice (time 0 = the splice), at its local pitch
    seg = x[splice - xf:splice]; t = (np.arange(len(seg)) - xf) / sr
    H = np.arange(1, nh + 1)
    def fitf(fr):
        M = np.concatenate([np.cos(2 * np.pi * fr * np.outer(t, H)), np.sin(2 * np.pi * fr * np.outer(t, H))], axis=1)
        coef = np.linalg.lstsq(M, seg, rcond=None)[0]
        return np.sum((seg - M @ coef) ** 2), coef
    fr = min((fitf(f0 * 2 ** (c_ / 1200))[0], f0 * 2 ** (c_ / 1200)) for c_ in np.arange(-40, 41, 2))[1]
    r2, coef = fitf(fr)
    a, b = coef[:nh], coef[nh:]
    resid = 10 * np.log10(r2 / np.sum(seg ** 2))
    # levels: the steady loop's (MODE=loop, default) or the splice's; phases: the splice's
    if os.environ.get("MODE", "loop") == "loop":
        lp = x[s["loop_start"]:s["loop_end"]]                      # (spectral peaks: robust to a few cents of pitch)
        nf = 4 * (1 << int(np.ceil(np.log2(len(lp)))))
        S = np.abs(np.fft.rfft(lp * np.hanning(len(lp)), nf)); fq = np.fft.rfftfreq(nf, 1 / sr)
        amp_l = np.array([S[(fq > h * f0 * 0.97) & (fq < h * f0 * 1.03)].max() for h in H]); amp_s = np.hypot(a, b) + 1e-12
        g = np.sqrt(np.sum(amp_s ** 2) / np.sum(amp_l ** 2))      # the splice's level, the loop's spectrum
        a, b = a / amp_s * amp_l * g, b / amp_s * amp_l * g
    # the sustain from the crossfade's start: periodic in L samples
    n_syn = xf + L
    ts = (np.arange(n_syn) - xf) / sr
    syn = np.cos(2 * np.pi * fl * np.outer(ts, H)) @ a + np.sin(2 * np.pi * fl * np.outer(ts, H)) @ b
    w = np.sin(0.5 * np.pi * np.arange(xf) / xf) ** 2
    y = np.concatenate([x[:splice - xf], x[splice - xf:splice] * (1 - w) + syn[:xf] * w, syn[xf:]])
    loop = syn[xf:]
    assert np.abs(loop[0] - (np.cos(2 * np.pi * fl * H * L / sr) @ a + np.sin(2 * np.pi * fl * H * L / sr) @ b)) < 1e-6
    if np.abs(y).max() > 0.999: y *= 0.999 / np.abs(y).max()
    pcm = np.round(np.clip(y, -1, 1) * 32767).astype("<i2")
    wavfile.write(os.path.join(dst, "samples", s["name"] + ".wav"), sr, pcm)
    e = dict(s); e.update(frames=len(pcm), loop_start=splice, loop_end=splice + L, loop_crossfade=0, loop_score=1.0,
                          synth=dict(harmonics=int(nh), loop_cycles=int(c), loop_cents=round(float(err), 2), fit_residual_db=round(float(resid), 1)))
    out.append(e)
    print(f"{s['name']:18s} splice {1000 * splice / sr:4.0f} ms, loop {L} samples = {c} cycles ({err:+.2f} cents), "
          f"{nh} harmonics, fit residual {resid:.1f} dB at {1200*np.log2(fr/f0):+.0f} cents, {len(pcm)} frames (was {s['frames']})")
b2 = dict(bank); b2["samples"] = out
b2["note"] = "recorded attack + synthesised sustain loop (tools/sim/iowa_hybrid.py)"
json.dump(b2, open(os.path.join(dst, "bank.json"), "w"), indent=1)
print("frames:", sum(e["frames"] for e in out), "(looped:", sum(e["frames"] for e in bank["samples"]), ")")
