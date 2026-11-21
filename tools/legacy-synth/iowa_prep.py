#!/usr/bin/env python3
"""v49 host experiment: a trumpet bank from the University of Iowa Musical Instrument Samples (B-flat trumpet,
non-vibrato, mf and ff; "may be downloaded and used for any projects, without restrictions",
https://theremin.music.uiowa.edu/MIS.html). Not firmware.

  python3 -I tools/legacy-synth/iowa_prep.py [RAWDIR] [OUTDIR] [MIDI ...]
      RAWDIR  the downloaded scale files (default build/iowa/raw: Trumpet.novib.{mf,ff}.{E3B3,C4B4,C5B5}.aiff)
      OUTDIR  default build/iowa: notes/*.wav (every note, as cut), samples/*.wav + bank.json (the chosen ones, in
              tools/legacy-synth/acoustic_prep.py's format, for tools/mk_acoustic_bank.py)
      MIDI    the sounding notes to keep (default 52 60 72: E3, C4, C5)

Each scale file is cut at its silences into single notes; each note's sounding pitch is measured (the file names give
a range, not whether it is written or sounding), then prepared like the VSCO samples (trimmed, attack end, a
crossfaded sustain loop, -18 dBFS loop RMS) by acoustic_prep.py's own functions.
"""
import aifc, glob, hashlib, importlib.util, json, os, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("acoustic_prep", os.path.join(HERE, "acoustic_prep.py"))
ap = importlib.util.module_from_spec(spec); spec.loader.exec_module(ap)

raw = sys.argv[1] if len(sys.argv) > 1 else "build/iowa/raw"
out = sys.argv[2] if len(sys.argv) > 2 else "build/iowa"
want = [int(v) for v in sys.argv[3:]] or [52, 60, 72]
os.makedirs(os.path.join(out, "notes"), exist_ok=True); os.makedirs(os.path.join(out, "samples"), exist_ok=True)


def read_aiff(fn):
    with aifc.open(fn, "rb") as a:
        ch, sw, rate, n = a.getnchannels(), a.getsampwidth(), a.getframerate(), a.getnframes()
        b = a.readframes(n)
    assert sw == 2, fn
    x = np.frombuffer(b, dtype=">i2").astype(np.float64) / 32768.0
    if ch > 1: x = x.reshape(-1, ch).mean(axis=1)
    return x, rate


def cut(x, rate):
    """notes between silences: a 20 ms envelope above -45 dB of the file's peak, gaps of at least 150 ms"""
    w = int(0.02 * rate)
    env = np.sqrt(np.convolve(x * x, np.ones(w) / w, mode="same"))
    on = env > env.max() * 10 ** (-45 / 20)
    segs, i, n = [], 0, len(x)
    while i < n:
        if not on[i]: i += 1; continue
        j = i
        while j < n:
            if not on[j]:
                k = j
                while k < n and not on[k] and k - j < int(0.15 * rate): k += 1
                if k - j >= int(0.15 * rate) or k >= n: break
                j = k
            j += 1
        if j - i > int(0.3 * rate): segs.append((max(0, i - int(0.005 * rate)), min(n, j)))
        i = j + 1
    return segs


chosen = {}
log = []
for fn in sorted(glob.glob(os.path.join(raw, "Trumpet.novib.*.aiff"))):
    dyn = os.path.basename(fn).split(".")[2]
    sha = hashlib.sha256(open(fn, "rb").read()).hexdigest()
    x, rate = read_aiff(fn)
    for a, b in cut(x, rate):
        seg = x[a:b]
        mid = seg[int(0.3 * len(seg)):int(0.6 * len(seg))]
        f0, conf = ap.measure_f0(resample_poly(mid, 1, 2), 140.0, 1200.0)
        f0 *= 1.0                                         # (measure_f0 assumes ap.SR = 22050: the half-rate copy matches it)
        midi = 69 + 12 * np.log2(f0 / 440.0)
        m = int(round(midi))
        name = f"{dyn}_{m}"
        wavfile.write(os.path.join(out, "notes", name + ".wav"), rate, (seg * 32767).astype(np.int16))
        log.append(f"{os.path.basename(fn)} {a / rate:6.2f}..{b / rate:6.2f} s: {f0:7.2f} Hz = MIDI {midi:6.2f} (conf {conf:.2f})")
        if m in want and (dyn, m) not in chosen:
            chosen[(dyn, m)] = (seg, rate, os.path.basename(fn), sha, a / rate, b / rate)
print("\n".join(log))

entries = []
for (dyn, m), (seg, rate, src, sha, t0, t1) in sorted(chosen.items(), key=lambda k: (k[0][1], k[0][0])):
    g = np.gcd(rate, ap.SR)
    xs = resample_poly(seg, ap.SR // g, rate // g)
    y, meta = ap.process(0, xs)
    gain = 10 ** (-18 / 20) / meta["loop_rms"]
    y = y * gain
    if np.abs(y).max() > 0.999: y *= 0.999 / np.abs(y).max()
    pcm = np.round(np.clip(y, -1, 1) * 32767).astype("<i2")
    midi = 69 + 12 * np.log2(meta["f0"] / 440.0)
    soft = dyn == "mf"
    name = f"trumpet_{'soft' if soft else 'loud'}_{int(round(midi))}"
    wavfile.write(os.path.join(out, "samples", name + ".wav"), ap.SR, pcm)
    entries.append(dict(name=name, instrument="trumpet", instrument_id=0, dynamic="soft" if soft else "loud",
                        source=f"https://theremin.music.uiowa.edu/sound%20files/MIS/Brass/Bbtrumpet/{src} ({t0:.2f}..{t1:.2f} s)",
                        license="University of Iowa MIS: free for any project, without restrictions", source_sha256=sha,
                        source_rate=rate, rate=ap.SR, frames=len(pcm), f0_hz=round(float(meta["f0"]), 3), midi=round(float(midi), 3),
                        cents_off=round(float((midi - round(midi)) * 100), 1), f0_confidence=round(float(meta["f0_conf"]), 3),
                        trim_start_frames=int(meta["trim_start"]), attack_end=int(meta["attack_end"]), loop_start=int(meta["loop_start"]),
                        loop_end=int(meta["loop_end"]), loop_crossfade=int(ap.XFADE_S[0] * ap.SR), loop_score=round(float(meta["loop_score"]), 3),
                        level_db=round(float(20 * np.log10(meta["loop_rms"] + 1e-12)), 2), gain_applied_db=round(float(20 * np.log10(gain)), 2)))
    print(f"chosen {name}: {meta['f0']:.2f} Hz, attack {1000 * meta['attack_end'] / ap.SR:.0f} ms, loop "
          f"{1000 * (meta['loop_end'] - meta['loop_start']) / ap.SR:.0f} ms (score {meta['loop_score']:.2f}), {len(pcm)} frames")
json.dump(dict(repository="https://theremin.music.uiowa.edu/MISBbtrumpet.html", commit="(2003 recordings)",
               license="University of Iowa MIS", rate=ap.SR, samples=entries), open(os.path.join(out, "bank.json"), "w"), indent=1)
missing = [f"{d} {m}" for d in ("mf", "ff") for m in want if (d, m) not in chosen]
print("missing:", missing or "none")
