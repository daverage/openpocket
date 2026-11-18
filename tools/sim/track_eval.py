#!/usr/bin/env python3
"""Scores tools/sim/track_bench CSVs against note annotations (v49, docs/NOTE-TRACKING.md). Host only.

  python3 tools/sim/track_eval.py lead                 the bundled lead loop against its transcription (build/acoustic)
  python3 tools/sim/track_eval.py guitarset [N]        GuitarSet's mono pickup mix against its per-string notes
                                                       (build/guitarset: annotation/*.jams, mono/*_mix.wav), N files each
Single notes (a mono voice follows the newest note): for every played note of at least 60 ms, over the time it is
the newest note: right when the tracker is within 50 cents for most of its middle half; octave when it is an octave
off instead; the time from the onset to the first right frame. Chords (GuitarSet comping): frame by frame, the notes
found against the notes sounding, matched within 50 cents: precision, recall; and the lowest note (the bass).
"""
import csv, glob, json, math, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BENCH = os.environ.get("BENCH", os.path.join(ROOT, "build", "track_bench"))   # BENCH=build/q_bench: another tracker's CSV
SUFFIX = os.environ.get("SUFFIX", "")                                          # (its CSVs beside, with this suffix)
NOT_KEYS = {"t", "conf", "onset", "level", "nn", "n1", "n2", "n3", "n4", "n5", "n6", "old_layer"}


def cents(a, b): return 1200 * math.log2(a / b)


def run(wav, out):
    if SUFFIX: out = out.replace(".csv", SUFFIX + ".csv")
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(BENCH):
        subprocess.run([BENCH, wav, out], check=True)
    rows = list(csv.DictReader(open(out)))
    for r in rows:
        for k in r: r[k] = float(r[k])
    return rows


def mono_score(rows, notes, keys):
    """notes: [(start, end, midi)] sorted"""
    keys = [k for k in rows[0] if k not in NOT_KEYS] if rows else keys
    T = [r["t"] for r in rows]
    import bisect
    res = {k: dict(n=0, ok=0, octave=0, lat=[]) for k in keys}
    for i, (a, b, m) in enumerate(notes):
        end = b
        for j in range(i + 1, len(notes)):
            if notes[j][0] > a + 0.001: end = min(end, notes[j][0]); break
        if b - a < 0.06 or end - a < 0.03: continue
        f = 440 * 2 ** ((m - 69) / 12)
        i0, i1 = bisect.bisect_left(T, a), bisect.bisect_left(T, end)
        if i1 <= i0: continue
        q0, q1 = i0 + (i1 - i0) // 4, i1 - (i1 - i0) // 4
        for k in keys:
            s = res[k]; s["n"] += 1
            good = octv = 0
            for r in rows[q0:q1]:
                v = r[k]
                if v > 0:
                    c = abs(cents(v, f))
                    if c < 50: good += 1
                    elif abs(c - 1200) < 50: octv += 1
            tot = max(1, q1 - q0)
            if good * 2 > tot: s["ok"] += 1
            elif octv * 2 > tot: s["octave"] += 1
            for r in rows[i0:i1]:
                if r[k] > 0 and abs(cents(r[k], f)) < 50: s["lat"].append(1000 * (r["t"] - a)); break
    return res


def poly_score(rows, notes):
    import bisect
    starts = sorted(notes)
    tp = fp = fn = 0; bass_ok = bass_n = 0
    for r in rows:
        t = r["t"]
        act = [m for (a, b, m) in starts if a + 0.03 <= t < b]     # (30 ms after the onset: the settled part)
        if not act: continue
        est = [r[f"n{k}"] for k in range(1, int(r["nn"]) + 1)]
        tf = [440 * 2 ** ((m - 69) / 12) for m in act]
        used = set()
        for e in est:
            hit = next((j for j, f in enumerate(tf) if j not in used and abs(cents(e, f)) < 50), None)
            if hit is None: fp += 1
            else: tp += 1; used.add(hit)
        fn += len(tf) - len(used)
        bass_n += 1
        if est and abs(cents(min(est), min(tf))) < 50: bass_ok += 1
    p = tp / max(1, tp + fp); rc = tp / max(1, tp + fn)
    return dict(precision=p, recall=rc, f=2 * p * rc / max(1e-9, p + rc), bass=bass_ok / max(1, bass_n))


def chord_mono(rows, notes, key="layer"):
    """a one-note voice under chords: the share of chord frames (2+ notes sounding) where its pitch is one of the
    sounding notes, and its jumps (> 50 cents) per second of chord"""
    on = hit = jumps = 0; prev = 0.0
    for r in rows:
        t = r["t"]; act = [m for (a, b, m) in notes if a + 0.03 <= t < b]
        if len(act) < 2: prev = 0.0; continue
        on += 1; v = r[key]
        if v > 0 and any(abs(cents(v, 440 * 2 ** ((m - 69) / 12))) < 50 for m in act): hit += 1
        if v > 0 and prev > 0 and abs(cents(v, prev)) > 50: jumps += 1
        prev = v
    dt = rows[1]["t"] - rows[0]["t"] if len(rows) > 1 else 0.00145
    return hit / max(1, on), jumps / max(1e-9, on * dt)


def report(title, res):
    print(title)
    for k, s in res.items():
        lat = sorted(s["lat"]); med = lat[len(lat) // 2] if lat else float("nan"); p90 = lat[int(len(lat) * 0.9)] if lat else float("nan")
        print(f"  {k:10s} {s['ok']:4d}/{s['n']:<4d} right ({100 * s['ok'] / max(1, s['n']):5.1f} %), octave {s['octave']:3d}, "
              f"time to pitch median {med:5.1f} ms, 90 % {p90:5.1f} ms")


def merge(a, b):
    for k in a:
        for f in ("n", "ok", "octave"): a[k][f] += b[k][f]
        a[k]["lat"] += b[k]["lat"]
    return a


def jams_notes(fn):
    d = json.load(open(fn))
    out = []
    for an in d["annotations"]:
        if an["namespace"] != "note_midi": continue
        data = an["data"]
        if isinstance(data, dict):              # (JAMS dense form)
            data = [dict(time=t, duration=du, value=v) for t, du, v in zip(data["time"], data["duration"], data["value"])]
        for o in data: out.append((o["time"], o["time"] + o["duration"], o["value"]))
    return sorted(out)


def main():
    keys = ["layer", "mono", "old_hq"]
    what = sys.argv[1] if len(sys.argv) > 1 else "lead"
    if what == "file":                                   # file IN.wav TRUTH.csv: any DI against any note list
        wav, tru = sys.argv[2], sys.argv[3]
        notes = []
        for line in open(tru):
            if line.startswith("#"): continue
            a, b, m, v = map(float, line.split(","))
            notes.append((a, b, m))
        out = os.path.join(os.path.dirname(os.path.abspath(tru)), "track.csv")
        report(f"{os.path.basename(wav)} (single notes, {len(notes)} in the reference)", mono_score(run(wav, out), notes, keys))
        return
    if what == "lead":
        notes = []
        for line in open(os.path.join(ROOT, "build", "acoustic", "lead.truth.csv")):
            if line.startswith("#"): continue
            a, b, m, v = map(float, line.split(","))
            if v >= 30: notes.append((a, b, m))
        rows = run(os.path.join(ROOT, "assets", "loops", "lead.wav"), os.path.join(ROOT, "build", "lead_track.csv"))
        report("lead loop (single notes)", mono_score(rows, notes, keys))
        return
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    gs = os.path.join(ROOT, "build", "guitarset")
    out = os.path.join(gs, "csv"); os.makedirs(out, exist_ok=True)
    for kind in ("solo", "comp"):
        files = sorted(glob.glob(os.path.join(gs, "mono", f"*_{kind}_mix.wav")))[:n]
        tot = None; poly = []; cm = []
        for w in files:
            base = os.path.basename(w).replace("_mix.wav", "")
            notes = jams_notes(os.path.join(gs, "annotation", base + ".jams"))
            rows = run(w, os.path.join(out, base + ".csv"))
            if kind == "solo":
                r = mono_score(rows, notes, keys); tot = r if tot is None else merge(tot, r)
            else:
                poly.append(poly_score(rows, notes)); cm.append(chord_mono(rows, notes))
        if kind == "solo" and tot: report(f"GuitarSet solo, {len(files)} files (single notes)", tot)
        if poly:
            m = {k: sum(p[k] for p in poly) / len(poly) for k in poly[0]}
            print(f"GuitarSet comping, {len(poly)} files (chords, new tracker): precision {100 * m['precision']:.1f} %, recall "
                  f"{100 * m['recall']:.1f} %, F {100 * m['f']:.1f} %, lowest note right {100 * m['bass']:.1f} % of frames")
            print(f"  one-note voice (layer) under chords: on a chord tone {100 * sum(c[0] for c in cm) / len(cm):.1f} % of frames, "
                  f"{sum(c[1] for c in cm) / len(cm):.1f} jumps a second")


if __name__ == "__main__":
    main()
