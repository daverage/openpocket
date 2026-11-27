"""Render guitar DIs through the filter-bank organ prototype. Usage: python3 run.py [DI.wav ...]"""
import subprocess, sys, pathlib, html
import numpy as np, soundfile as sf
ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = ROOT / "build/organ-bank"; OUT.mkdir(parents=True, exist_ok=True)
BIN = ROOT / "build/organ_proto"
subprocess.run(["cc", "-O2", "-w", "-fno-builtin", "-ffp-contract=off", f"-I{ROOT/'engine'}",
                str(pathlib.Path(__file__).with_name("organ.c")), str(ROOT / "firmware/px_math.c"), "-lm", "-o", str(BIN)], check=True)
VOICES = ["jazz (16+8+4, perc, slow rotary)", "full (16+8+4+2 2/3+2, perc, fast rotary)", "church (swell, no perc)", "poly octave reference"]
inputs = sys.argv[1:] or [str(ROOT / "My Audio" / n) for n in ("fast play di.wav", "bleep3 di.wav", "scale + duo + power chords di.wav", "low e open di.wav")]
rows = []
for path in inputs:
    x, sr = sf.read(path, dtype="float32", always_2d=True); x = x[:, 0]
    assert sr == 44100, f"{path}: {sr} Hz"
    stem = pathlib.Path(path).stem.replace(" ", "_")
    files = [(f"{stem}_dry.wav", x)]
    for v, name in enumerate(VOICES):
        y = np.frombuffer(subprocess.run([str(BIN), str(v)], input=x.tobytes(), capture_output=True, check=True).stdout, dtype=np.float32)
        rms = np.sqrt(np.mean(y ** 2)) + 1e-9
        y = y * min(0.08 / rms, 0.9 / (np.max(np.abs(y)) + 1e-9)); y = np.clip(y, -1, 1)
        files.append((f"{stem}_{v}.wav", y))
        print(f"{stem} {name}: peak {np.max(np.abs(y)):.2f}")
    for f, y in files: sf.write(OUT / f, y, sr, subtype="PCM_16")
    rows.append((pathlib.Path(path).name, files))
page = ["<!doctype html><meta charset=utf-8><title>Organ bank prototype</title><body style='font:15px system-ui;max-width:900px;margin:2em auto'><h1>Filter-bank organ (no tracking)</h1>"]
for name, files in rows:
    page.append(f"<h2>{html.escape(name)}</h2>")
    for (f, _), label in zip(files, ["dry DI"] + VOICES):
        page.append(f"<p>{html.escape(label)}<br><audio controls preload=none src='{f}'></audio></p>")
(OUT / "index.html").write_text("\n".join(page))
print(OUT / "index.html")
