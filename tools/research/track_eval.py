#!/usr/bin/env python3
"""Scores tools/legacy-synth/track_bench.c CSVs against note annotations (v49, docs/archive/NOTE-TRACKING.md). Host only.

  python3 tools/research/track_eval.py lead                 the bundled lead loop against its transcription (build/acoustic)
  python3 tools/research/track_eval.py guitarset [N] [offset] GuitarSet's mono pickup mix against its per-string notes
                                                       (build/guitarset: annotation/*.jams, mono/*_mix.wav), N files each
Single notes (a mono voice follows the newest note): for every played note of at least 60 ms, over the time it is
the newest note: right when the tracker is within 50 cents for most of its middle half; octave when it is an octave
off instead; the time from the onset to the first right frame. Chords (GuitarSet comping): frame by frame, the notes
found against the notes sounding, matched within 50 cents: precision, recall; and the lowest note (the bass).
"""
import csv, glob, json, math, os, subprocess, sys
from bisect import bisect_right
from statistics import median

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BENCH = os.environ.get("BENCH", os.path.join(ROOT, "build", "track_bench"))   # BENCH=build/q_bench: another tracker's CSV
SUFFIX = os.environ.get("SUFFIX", "")                                          # (its CSVs beside, with this suffix)
NOT_KEYS = {"t", "conf", "onset", "level", "nn", "n1", "n2", "n3", "n4", "n5", "n6", "old_layer", "v1", "v2", "pick1", "pick2"}


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


def dyad_score(rows, notes, intervals=None):
    """Count settled dyad frames and false extra candidates on settled single notes."""
    starts = sorted((a + 0.03, b, m) for a, b, m in notes if b > a + 0.03)
    active = []
    cursor = 0
    dyads = both = lowest = singles = extra = 0
    for row in rows:
        t = row["t"]
        while cursor < len(starts) and starts[cursor][0] <= t:
            _, stop, midi = starts[cursor]
            active.append((stop, midi))
            cursor += 1
        active = [(stop, midi) for stop, midi in active if t < stop]
        pitches = sorted({midi for _, midi in active})
        candidates = [row[f"n{i}"] for i in range(1, int(row["nn"]) + 1)]
        if len(pitches) == 1:
            singles += 1
            extra += len(candidates) > 1
        elif len(pitches) == 2:
            dyads += 1
            hits = [
                {i for i, f in enumerate(candidates) if abs(cents(f, 440 * 2 ** ((m - 69) / 12))) < 50}
                for m in pitches
            ]
            pair_hit = any(i != j for i in hits[0] for j in hits[1])
            both += pair_hit
            if intervals is not None:
                distance = round(pitches[1] - pitches[0])
                totals = intervals.setdefault(distance, [0, 0])
                totals[0] += pair_hit
                totals[1] += 1
            lowest += bool(candidates) and candidates.index(min(candidates)) in hits[0]
    return dyads, both, lowest, singles, extra


def dyad_entry_delays(rows, notes, *, reports=1, new_rows=None):
    """First simultaneous hit after a second pitch enters a held note.

    Require a held note already sounding for 30 ms and both notes to last at
    least 100 ms after entry. With reports > 1, time the first run of that
    many consecutive paired reports completed inside the window. With
    new_rows (aligned with rows), the entering pitch must also be found
    there. Return None if no run qualifies.
    """
    from bisect import bisect_left

    times = [row["t"] for row in rows]
    delays = []
    for start, end, midi in notes:
        if end < start + 0.1:
            continue
        held = {m for a, b, m in notes if a < start - 0.03 and b >= start + 0.1}
        other = {m for a, b, m in notes if a <= start and b > start}
        if len(held) != 1 or other != held | {midi} or midi in held:
            continue
        expected = [440 * 2 ** ((m - 69) / 12) for m in (midi, next(iter(held)))]
        delay = None
        consecutive = 0
        for i in range(bisect_left(times, start), bisect_left(times, start + 0.1)):
            row = rows[i]
            found = [row[f"n{i}"] for i in range(1, int(row["nn"]) + 1)]
            fresh = found if new_rows is None else \
                [new_rows[i][f"n{k}"] for k in range(1, int(new_rows[i]["nn"]) + 1)]
            if all(any(abs(cents(f, wanted)) < 50 for f in found) for wanted in expected) and \
                    any(abs(cents(f, expected[0])) < 50 for f in fresh):
                consecutive += 1
                if consecutive == reports:
                    delay = 1000 * (times[i - reports + 1] - start)
                    break
            else:
                consecutive = 0
        delays.append(delay)
    return delays


def matched_rows(csv_path, target_rows):
    with csv_path.open() as source:
        candidates = [{key: float(val) for key, val in row.items()} for row in csv.DictReader(source)]
    times = [row["t"] for row in candidates]
    aligned = []
    for row in target_rows:
        i = bisect_right(times, row["t"] + 0.000005) - 1  # CSV timestamps are rounded to 5 decimals
        if i >= 0:
            aligned.append({**candidates[i], "t": row["t"]})
    return aligned


def report_detector(name, dyads, chords, entries):
    n, both, low, single, extra = map(sum, zip(*dyads))
    print(f"{name}: {both}/{n} dyads both right ({100 * both / max(1, n):.1f}%), "
          f"lowest {100 * low / max(1, n):.1f}%; "
          f"extra candidates on singles {100 * extra / max(1, single):.1f}% of {single} frames")
    detected = sorted(value for value in entries if value is not None)
    if detected:
        print(f"second-note entries: {len(detected)}/{len(entries)} found within 100 ms, "
              f"median {median(detected):.1f} ms among hits, p90 {detected[int(0.9 * len(detected))]:.1f} ms")
    else:
        print(f"second-note entries: 0/{len(entries)} found within 100 ms")
    print("Comping " + ", ".join(f"{k} {100 * sum(c[k] for c in chords) / len(chords):.1f}%"
                                 for k in ("precision", "recall", "f", "bass")))


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
    offset = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    if n <= 0 or offset < 0:
        raise ValueError("takes must be positive and offset nonnegative")
    gs = os.path.join(ROOT, "build", "guitarset")
    out = os.path.join(gs, "csv"); os.makedirs(out, exist_ok=True)
    for kind in ("solo", "comp"):
        files = sorted(glob.glob(os.path.join(gs, "mono", f"*_{kind}_mix.wav")))[offset:offset + n]
        tot = None; poly = []; cm = []; dyad = []
        for w in files:
            base = os.path.basename(w).replace("_mix.wav", "")
            notes = jams_notes(os.path.join(gs, "annotation", base + ".jams"))
            rows = run(w, os.path.join(out, base + ".csv"))
            if kind == "solo":
                r = mono_score(rows, notes, keys); tot = r if tot is None else merge(tot, r)
            else:
                poly.append(poly_score(rows, notes)); cm.append(chord_mono(rows, notes))
                dyad.append(dyad_score(rows, notes))
        if kind == "solo" and tot: report(f"GuitarSet solo, {len(files)} files (single notes)", tot)
        if poly:
            m = {k: sum(p[k] for p in poly) / len(poly) for k in poly[0]}
            print(f"GuitarSet comping, {len(poly)} files (chords, new tracker): precision {100 * m['precision']:.1f} %, recall "
                  f"{100 * m['recall']:.1f} %, F {100 * m['f']:.1f} %, lowest note right {100 * m['bass']:.1f} % of frames")
            nd, hit, low, ns, extra = map(sum, zip(*dyad))
            print(f"  settled dyads: {hit}/{nd} both ({100 * hit / max(1, nd):.1f} %), "
                  f"lowest {100 * low / max(1, nd):.1f} %; "
                  f"extra candidates on single pitches {100 * extra / max(1, ns):.1f} % of {ns} frames")
            print(f"  one-note voice (layer) under chords: on a chord tone {100 * sum(c[0] for c in cm) / len(cm):.1f} % of frames, "
                  f"{sum(c[1] for c in cm) / len(cm):.1f} jumps a second")


if __name__ == "__main__":
    main()
