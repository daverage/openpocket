#!/usr/bin/env python3
"""Generate every test input (float32, 44.1 kHz, engine units: 4 V per unit at 0 dB drive) and the test manifest.
Same samples feed the host DSP and (band-limited upsampled) the SPICE circuits.  Nothing is normalised."""
import json, os, wave, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); FS = 44100
DRIVES = [0, 6, 12, 16, 24]
os.makedirs(f"{HERE}/data/in", exist_ok=True)
tests = {}

def add(name, x, drives=DRIVES, kind="", cpar=False, **meta):
    x = np.asarray(x, np.float64); x = x[: len(x) - len(x) % 64]
    x.astype("<f4").tofile(f"{HERE}/data/in/{name}.f32")
    tests[name] = dict(kind=kind, drives=drives, n=len(x), cpar=cpar, **meta)

t = lambda s: np.arange(int(s * FS)) / FS
# 1. steady sines: 4 frequencies x 3 levels (0.2 s of silence-free run-in is the circuit's own settling; analysed after 0.3 s)
for f in (82, 220, 440, 1000):
    for a in (0.01, 0.05, 0.2):
        add(f"sine_{f}_{a}", a * np.sin(2 * np.pi * f * t(0.5)), kind="sine", f=f, amp=a, cpar=(f in (440, 1000)))
# 2. two-tone IMD
for name, f1, f2 in (("guitar_440_660", 440, 660), ("smpte_60_7k", 60, 7000)):
    for a in (0.02, 0.1):
        tt = t(0.6)
        x = (a * 0.8 if "smpte" in name else a / 2) * np.sin(2 * np.pi * f1 * tt) + (a * 0.2 if "smpte" in name else a / 2) * np.sin(2 * np.pi * f2 * tt)
        add(f"imd_{name}_{a}", x, kind="imd", f1=f1, f2=f2, amp=a)
# 3. logarithmic amplitude sweep, 60 dB in 1.2 s, per drive: tops out at min(1.0 unit, 30 V at the grid) so SPICE stays in a physical range
for f in (220, 1000):
    for d in DRIVES:
        top = min(1.0, 30.0 / (4 * 10 ** (d / 20))); tt = t(1.2); amp = top * 1e-3 * 10 ** (3 * tt / 1.2)
        add(f"ampsweep_{f}_{d}", amp * np.sin(2 * np.pi * f * tt), drives=[d], kind="ampsweep", f=f, a0=top * 1e-3, a1=top, dur=1.2)
# 4. log frequency sweep, small signal (peak 4 mV at the grid at every drive), for the frequency response; 20 Hz..20 kHz in 3 s
tt = t(2.0); k = np.log(20000 / 20) / 2.0
add("chirp_small", 1e-3 * np.sin(2 * np.pi * 20 / k * (np.exp(k * tt) - 1)), kind="chirp", drives=[0, 24], cpar=True, dur=2.0)
# 5. impulses / transients
im = np.zeros(int(0.4 * FS)); im[int(0.05 * FS)] = 0.5
add("impulse_0.5", im, kind="impulse", amp=0.5)
im = np.zeros(int(0.4 * FS)); im[int(0.05 * FS)] = 0.05
add("impulse_0.05", im, kind="impulse", amp=0.05)
tt = t(0.5); x = np.zeros_like(tt); on = (tt >= 0.05) & (tt < 0.06)
x[on] = 0.4 * np.sin(2 * np.pi * 1500 * tt[on]) * np.hanning(on.sum()) ** 0.1      # a hard 10 ms pick burst, near-instant onset
add("transient_pick", x, kind="transient")
# 6. cathode bias displacement and recovery: loud burst, then a small probe tone.  0.2 s silence, 0.15 s of 220 Hz at 0.3, then 0.5 s of 220 Hz at 0.01
tt = t(0.2 + 0.15 + 0.6); x = np.zeros_like(tt)
b = (tt >= 0.2) & (tt < 0.35); p = tt >= 0.35
x[b] = 0.3 * np.sin(2 * np.pi * 220 * tt[b]); x[p] = 0.01 * np.sin(2 * np.pi * 220 * tt[p])
add("bias_recovery", x, kind="bias", t_on=0.2, t_off=0.35, loud=0.3, probe=0.01)
# 7. guitar recordings (the repo's own DI files; left+right averaged, untouched level)
def rd24(fn):
    w = wave.open(fn); ch = w.getnchannels(); a = np.frombuffer(w.readframes(w.getnframes()), np.uint8).reshape(-1, 3).astype(np.int32)
    v = a[:, 0] | a[:, 1] << 8 | a[:, 2] << 16; v = np.where(v & 0x800000, v - (1 << 24), v) / 8388608.0
    return v.reshape(-1, ch).mean(1)
clips = {"single_note": ("low e open di", 1.0, 2.0), "chords": ("e chord string duo", 1.0, 2.0),
         "hard_picking": ("fast play di", 0.9, 2.0), "power_chords": ("scale + duo + power chords di", 7.0, 2.0)}
for nm, (fn, s0, d) in clips.items():
    v = rd24(f"{HERE}/../../../My Audio/{fn}.wav"); i0 = int(s0 * FS)
    add(f"guitar_{nm}", v[i0:i0 + int(d * FS)], kind="guitar", src=fn, start=s0, cpar=False)
json.dump(tests, open(f"{HERE}/data/manifest.json", "w"), indent=1)
print(len(tests), "tests,", sum(len(v["drives"]) for v in tests.values()), "drive settings")
