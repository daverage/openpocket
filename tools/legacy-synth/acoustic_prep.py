#!/usr/bin/env python3
"""Host prototype (docs/research/REALISTIC-INSTRUMENT-SYNTHESIS.md, step 1): a small looped sample bank for the
acoustic voice experiment (tools/legacy-synth/acoustic_voice.c).

  python3 tools/legacy-synth/acoustic_prep.py [OUTDIR]          default OUTDIR: build/acoustic

Downloads a few raw WAVs from Versilian Studios' VSCO 2 Community Edition (CC0 1.0) at a pinned commit, and for each:
mono, 22.05 kHz, leading silence trimmed, f0 measured on the sustain (normalised autocorrelation with parabolic
refinement), attack end found from the envelope, and a sustain loop chosen where the waveform matches best across the
splice, then crossfaded. Writes OUTDIR/bank.bin (read by acoustic_voice.c), OUTDIR/bank.json (provenance, tuning, loop
points, levels) and OUTDIR/samples/*.wav (the processed samples, to listen to).

Needs numpy and scipy. Nothing here ships in the firmware; the downloads stay in OUTDIR/raw (build/ is not committed).
"""
import hashlib, json, os, struct, sys, urllib.parse, urllib.request
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

REPO = "sgossner/VSCO-2-CE"
COMMIT = "440300901dfe9275fd84e0b7763af1f8443ae62e"         # 2020-08-05, the repository's master when this was written
LICENSE = "CC0-1.0"
SR = 22050
KEEP_S = {0: 0.75, 1: 1.0}     # at most this much of each recording before the loop end (the section bows in slower)
# loop crossfade: short on the trumpet (periodic, the splice matches), long on the cello section (an ensemble with
# vibrato never repeats exactly: a long crossfade hides the splice)
XFADE_S = {0: 0.030, 1: 0.090}

# instrument id 0 = trumpet (solo, "sus" articulation: a plain tongued sustain, no vibrato),
# instrument id 1 = cello section ("susvib": bowed sustain with vibrato; VSCO 2 CE has no solo cello).
# VSCO names notes an octave low against scientific pitch (its "C1" cello note is the open C string, C2 = 65.4 Hz):
# the root is measured, not taken from the name. v1 / v3 are VSCO's soft / loud dynamics.
SOURCES = [
    (0, "Brass/Trumpet/sus/Sum_SHTrumpet_sus_F2_v1_rr1.wav"),
    (0, "Brass/Trumpet/sus/Sum_SHTrumpet_sus_F2_v3_rr1.wav"),
    (0, "Brass/Trumpet/sus/Sum_SHTrumpet_sus_D#3_v1_rr1.wav"),
    (0, "Brass/Trumpet/sus/Sum_SHTrumpet_sus_D#3_v3_rr1.wav"),
    (0, "Brass/Trumpet/sus/Sum_SHTrumpet_sus_D4_v1_rr1.wav"),
    (0, "Brass/Trumpet/sus/Sum_SHTrumpet_sus_D4_v3_rr1.wav"),
    (1, "Strings/Cello Section/susvib/susvib_C1_v1_1.wav"),
    (1, "Strings/Cello Section/susvib/susvib_C1_v3_1.wav"),
    (1, "Strings/Cello Section/susvib/susvib_B1_v1_1.wav"),
    (1, "Strings/Cello Section/susvib/susvib_B1_v3_1.wav"),
    (1, "Strings/Cello Section/susvib/susvib_A2_v1_1.wav"),
    (1, "Strings/Cello Section/susvib/susvib_A2_v3_1.wav"),
]
INSTR = ["trumpet", "cello"]
F0_RANGE = {0: (140.0, 1100.0), 1: (55.0, 500.0)}


def fetch(path, rawdir):
    local = os.path.join(rawdir, path.replace("/", "__"))
    if not os.path.exists(local):
        url = f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/" + urllib.parse.quote(path)
        print("download", url)
        with urllib.request.urlopen(url, timeout=60) as r, open(local + ".part", "wb") as f:
            f.write(r.read())
        os.replace(local + ".part", local)
    data = open(local, "rb").read()
    return local, hashlib.sha256(data).hexdigest()


def load_mono(fn):
    rate, x = wavfile.read(fn)
    if x.dtype == np.int16: x = x / 32768.0
    elif x.dtype == np.int32: x = x / 2147483648.0
    elif x.dtype == np.uint8: x = (x - 128) / 128.0
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 2: x = x.mean(axis=1)
    g = np.gcd(int(rate), SR)
    return resample_poly(x, SR // g, int(rate) // g), int(rate)


def envelope(x, win):
    return np.sqrt(np.convolve(x * x, np.ones(win) / win, mode="same"))


def measure_f0(seg, lo, hi):
    """Normalised autocorrelation (the YIN-like cumulative difference would do as well on these steady tones)."""
    seg = seg - seg.mean()
    n = len(seg)
    spec = np.fft.rfft(seg, 2 * n)
    ac = np.fft.irfft(spec * np.conj(spec))[:n]
    energy = np.cumsum(seg[::-1] ** 2)[::-1]                # energy of the overlapping part, for normalisation
    norm = ac / np.sqrt(energy[0] * np.maximum(energy, 1e-12))
    lmin, lmax = int(SR / hi), int(SR / lo)
    lag = lmin + int(np.argmax(norm[lmin:lmax]))
    # prefer the shortest lag within 90 % of the best: avoids picking two periods
    best = norm[lag]
    for L in range(lmin, lag):
        if norm[L] > 0.9 * best and norm[L] >= norm[L - 1] and norm[L] >= norm[L + 1]:
            lag = L; break
    a, b, c = norm[lag - 1], norm[lag], norm[lag + 1]
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return SR / (lag + d), float(norm[lag])


def find_loop(x, f0, start_min, end_max, instr):
    """Search loop start / length where the windows across the splice correlate best. The cello section has a
    ~5-6 Hz vibrato, so its loop is longer to hold at least one vibrato cycle."""
    period = SR / f0
    w = int(max(2 * period, 0.012 * SR))
    w = max(w, int(XFADE_S[instr] * SR))
    lens = np.arange(int((0.30 if instr else 0.20) * SR), int((0.45 if instr else 0.35) * SR), max(1, int(period / 4)))
    best = (-2.0, None, None)
    for s in range(start_min, end_max - lens[0] - w, max(1, int(period / 2))):
        a = x[s - w:s]
        na = np.linalg.norm(a)
        for L in lens:
            e = s + L
            if e + w > end_max: break
            b = x[e - w:e]
            c = float(np.dot(a, b) / (na * np.linalg.norm(b) + 1e-12))
            # also the level must match (a loop across a swell pumps)
            r = np.linalg.norm(b) / (na + 1e-12)
            score = c - 0.5 * abs(np.log(r))
            if score > best[0]: best = (score, s, e)
    return best


def process(instr, x):
    env = envelope(x, int(0.010 * SR))
    peak = env.max()
    on = int(np.argmax(env > peak * 10 ** (-45 / 20)))
    on = max(0, on - int(0.002 * SR))
    x = x[on:]
    env = env[on:]
    # attack end: the envelope first reaches 80 % of its median over 0.25..0.6 s
    sus_level = np.median(env[int(0.25 * SR):int(0.6 * SR)])
    attack_end = int(np.argmax(env > 0.8 * sus_level))
    lo, hi = F0_RANGE[instr]
    f0, conf = measure_f0(x[int(0.25 * SR):int(0.55 * SR)], lo, hi)
    start_min = max(attack_end + int(0.04 * SR), int(0.18 * SR))
    end_max = min(len(x), int((KEEP_S[instr] + 0.1) * SR))
    score, ls, le = find_loop(x, f0, start_min, end_max, instr)
    xf = int(XFADE_S[instr] * SR)
    y = x[:le].copy()
    # crossfade the end of the loop into the material just before the loop start (equal power)
    t = np.linspace(0, 1, xf)
    fade_out, fade_in = np.cos(t * np.pi / 2), np.sin(t * np.pi / 2)
    y[le - xf:le] = x[le - xf:le] * fade_out + x[ls - xf:ls] * fade_in
    loop_rms = float(np.sqrt(np.mean(y[ls:le] ** 2)))
    return y, dict(f0=f0, f0_conf=conf, attack_end=attack_end, loop_start=ls, loop_end=le, loop_score=score,
                   loop_rms=loop_rms, trim_start=on)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/acoustic"
    rawdir = os.path.join(out, "raw"); os.makedirs(rawdir, exist_ok=True)
    os.makedirs(os.path.join(out, "samples"), exist_ok=True)
    entries, blobs = [], []
    for instr, path in SOURCES:
        local, sha = fetch(path, rawdir)
        x, orig_rate = load_mono(local)
        y, m = process(instr, x)
        dyn = 0 if "_v1" in path else 1
        # every sample normalised so that its loop sits at -18 dBFS RMS; the recorded level stays in level_db
        g = 10 ** (-18 / 20) / m["loop_rms"]
        y = y * g
        if np.abs(y).max() > 0.999: y = y * (0.999 / np.abs(y).max())     # (a hard attack may peak above)
        pcm = np.round(np.clip(y, -1, 1) * 32767).astype("<i2")
        midi = 69 + 12 * np.log2(m["f0"] / 440.0)
        name = f"{INSTR[instr]}_{['soft', 'loud'][dyn]}_{midi:.0f}"
        wavfile.write(os.path.join(out, "samples", name + ".wav"), SR, pcm)
        e = dict(name=name, instrument=INSTR[instr], instrument_id=instr, dynamic=["soft", "loud"][dyn],
                 source=f"https://github.com/{REPO}/blob/{COMMIT}/{path}", license=LICENSE, source_sha256=sha,
                 source_rate=orig_rate, rate=SR, frames=len(pcm), f0_hz=round(float(m["f0"]), 3), midi=round(float(midi), 3),
                 cents_off=round(float((midi - round(midi)) * 100), 1), f0_confidence=round(float(m["f0_conf"]), 3),
                 trim_start_frames=int(m["trim_start"]), attack_end=int(m["attack_end"]), loop_start=int(m["loop_start"]),
                 loop_end=int(m["loop_end"]), loop_crossfade=int(XFADE_S[instr] * SR), loop_score=round(float(m["loop_score"]), 3),
                 level_db=round(float(20 * np.log10(m["loop_rms"] + 1e-12)), 2), gain_applied_db=round(float(20 * np.log10(g)), 2))
        entries.append(e); blobs.append(pcm)
        print(f"{name:20s} f0 {m['f0']:8.2f} Hz ({e['cents_off']:+5.1f} c, conf {m['f0_conf']:.2f})  attack "
              f"{1000 * m['attack_end'] / SR:5.0f} ms  loop {m['loop_start']}..{m['loop_end']} "
              f"({1000 * (m['loop_end'] - m['loop_start']) / SR:.0f} ms, score {m['loop_score']:.2f})  "
              f"rec level {e['level_db']:.1f} dB")
    with open(os.path.join(out, "bank.bin"), "wb") as f:
        f.write(b"PXAB" + struct.pack("<I", len(entries)))
        for e, pcm in zip(entries, blobs):
            f.write(struct.pack("<iifiiif", e["instrument_id"], ["soft", "loud"].index(e["dynamic"]), e["f0_hz"],
                                e["frames"], e["loop_start"], e["loop_end"], e["level_db"]))
            f.write(pcm.tobytes())
    total = sum(len(b) for b in blobs)
    json.dump(dict(repository=f"https://github.com/{REPO}", commit=COMMIT, license=LICENSE, rate=SR,
                   pcm16_bytes=2 * total, samples=entries), open(os.path.join(out, "bank.json"), "w"), indent=1)
    print(f"bank: {len(entries)} samples, {total} frames, {2 * total} bytes PCM16 at {SR} Hz -> {out}/bank.bin")


if __name__ == "__main__":
    main()
