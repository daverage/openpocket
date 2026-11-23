#!/usr/bin/env python3
"""Overlay graphs (figs/) and blind-listening audio (audio/) from data/figdata.pkl + the raw runs."""
import json, os, pickle, random, numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from scipy.io import wavfile
from vref import *
fig = pickle.load(open(f"{HERE}/data/figdata.pkl", "rb")); os.makedirs(f"{HERE}/figs", exist_ok=True)
COL = {"DSP": "k", "dempwolf": "tab:blue", "koren": "tab:red", "koren_cpar": "tab:orange"}; LS = {"DSP": "-", "dempwolf": "--", "koren": "-.", "koren_cpar": ":"}
def pl(ax, lab, *a, **k): ax.plot(*a, color=COL.get(lab, "g"), ls=LS.get(lab, "-"), label=lab, lw=1.2, **k)

# compression
for d in (0, 6, 12, 16, 24):
    for nm in ("ampsweep_220", "ampsweep_1000"):
        if (f"{nm}_{d}", d) not in fig: continue
        ain, cur = fig[(f"{nm}_{d}", d)]; f, ax = plt.subplots(figsize=(6, 3.6))
        for lab, g in cur.items(): pl(ax, lab, ain * drive_lin(d), g)
        ax.set_xscale("log"); ax.set_xlabel("grid input amplitude (V peak)"); ax.set_ylabel("fundamental gain re small signal (dB)")
        ax.set_title(f"Gain compression, {nm.split('_')[1]} Hz, +{d} dB drive"); ax.grid(alpha=.3); ax.legend(); f.tight_layout(); f.savefig(f"{HERE}/figs/compression_{nm.split('_')[1]}_{d}dB.png", dpi=110); plt.close(f)
# bias recovery
for d in (0, 12, 24):
    if ("bias_recovery", d) not in fig: continue
    t, k = fig[("bias_recovery", d)]; f, ax = plt.subplots(figsize=(7, 3.6))
    per = int(FS2 / 220)
    for lab, v in k.items(): pl(ax, lab, t[:len(v)], np.convolve(v, np.ones(per) / per, "same"))
    ax.set_xlabel("time (s)"); ax.set_ylabel("cathode voltage, cycle-averaged (V)"); ax.set_title(f"Cathode bias displacement and recovery, +{d} dB"); ax.grid(alpha=.3); ax.legend(); f.tight_layout(); f.savefig(f"{HERE}/figs/bias_recovery_{d}dB.png", dpi=110); plt.close(f)
# frequency response
for d in (0, 24):
    f, ax = plt.subplots(figsize=(7, 3.8))
    for lab in ("DSP", "dempwolf", "koren", "koren_cpar"):
        if ("fr", d, lab) in fig: ff, H, _ = fig[("fr", d, lab)]; pl(ax, lab, ff, db(H))
    ax.set_xscale("log"); ax.set_xlabel("Hz"); ax.set_ylabel("plate-voltage gain (dB, V/V)"); ax.set_title(f"Small-signal frequency response (grid to plate), +{d} dB drive"); ax.grid(alpha=.3, which="both"); ax.legend(); f.tight_layout(); f.savefig(f"{HERE}/figs/freqresp_{d}dB.png", dpi=110); plt.close(f)
# waveforms: sine cycle overlays + spectra
def sine_overlay(name, d):
    if not os.path.exists(f"{HERE}/data/dsp/{name}_{d}.final.f32"): return
    D = dsp(name, d); m = MAN[name]; S = {"DSP": D["wet"]}
    for lab, tag in (("dempwolf", "dempwolf"), ("koren", "koren")):
        p = f"{HERE}/data/spice/{tag}_{name}_{d}.npz"
        if os.path.exists(p): S[lab] = wet_ref(spice(tag, name, d), d)
    n = min(map(len, S.values())); i0 = int(0.3 * FS2); L = int(2 * FS2 / m["f"])
    f, (a, b) = plt.subplots(1, 2, figsize=(11, 3.8))
    for lab, w in S.items():
        pl(a, lab, np.arange(L) / FS2 * 1e3, w[i0:i0 + L][:L]); seg = w[i0:i0 + 16384] * np.hanning(16384); sp = db(np.fft.rfft(seg) / 4096); fr = np.fft.rfftfreq(16384, 1 / FS2)
        pl(b, lab, fr, sp)
    a.set_xlabel("ms"); a.set_ylabel("stage output (engine units)"); a.set_title(f"{m['f']} Hz, amp {m['amp']}, +{d} dB"); a.grid(alpha=.3); a.legend()
    b.set_xscale("log"); b.set_xlim(50, 44100); b.set_ylim(-140, 0); b.set_xlabel("Hz"); b.set_ylabel("dB"); b.set_title("spectrum (aliasing visible above the harmonics)"); b.grid(alpha=.3, which="both")
    f.tight_layout(); f.savefig(f"{HERE}/figs/sine_{m['f']}_{m['amp']}_{d}dB.png", dpi=110); plt.close(f)
for nm, d in (("sine_220_0.05", 12), ("sine_220_0.2", 24), ("sine_1000_0.05", 16), ("sine_82_0.2", 24), ("sine_440_0.01", 6), ("sine_1000_0.2", 24)): sine_overlay(nm, d)
# guitar / transient overlays
for key_, S in fig.items():
    if len(key_) != 2: continue
    name, d = key_
    if not (isinstance(name, str) and (name.startswith("guitar_") or name.startswith("transient") or name.startswith("impulse"))): continue
    if d not in (12, 24): continue
    f, ax = plt.subplots(figsize=(10, 3.4)); n0, n1 = (int(0.9 * FS2), int(1.1 * FS2)) if name.startswith("guitar") else (int(0.04 * FS2), int(0.09 * FS2))
    for lab, w in S.items(): pl(ax, lab, np.arange(n0, n1) / FS2, w[n0:n1])
    ax.set_title(f"{name}, +{d} dB (stage output)"); ax.grid(alpha=.3); ax.legend(); f.tight_layout(); f.savefig(f"{HERE}/figs/wave_{name}_{d}dB.png", dpi=110); plt.close(f)

# blind-listening audio: processed outputs, raw level (no normalisation).  named/ has labels; blind/ has A/B/C with a key.
os.makedirs(f"{HERE}/audio/named", exist_ok=True); os.makedirs(f"{HERE}/audio/blind", exist_ok=True); key = {}; rng = random.Random(12345)
for name in [n for n in MAN if n.startswith("guitar_")]:
    for d in MAN[name]["drives"]:
        D = dsp(name, d); outs = {"DSP": D["final"], "dry": D["x"]}
        for lab, tag in (("dempwolf_SPICE", "dempwolf"), ("koren_SPICE", "koren")):
            if os.path.exists(f"{HERE}/data/spice/{tag}_{name}_{d}.npz"): outs[lab] = post(D["x"], wet_ref(spice(tag, name, d), d), d, f"{HERE}/data/tmp_a")
        for lab, y in outs.items(): wavfile.write(f"{HERE}/audio/named/{name}_{d}dB_{lab}.wav", 44100, y.astype(np.float32))
        labs = [l for l in outs if l != "dry"]; rng.shuffle(labs); key[f"{name}_{d}dB"] = {chr(65 + i): l for i, l in enumerate(labs)}
        for i, l in enumerate(labs): wavfile.write(f"{HERE}/audio/blind/{name}_{d}dB_{chr(65 + i)}.wav", 44100, outs[l].astype(np.float32))
json.dump(key, open(f"{HERE}/audio/blind_key.json", "w"), indent=1); print("figs + audio done")
