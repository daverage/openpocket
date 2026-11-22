"""MONO/CHORD state machine: ONE tracker is authoritative per frame (no note-list union).
  MONO : production mono tracker's note.        CHORD: pt's confirmed notes.
  enter CHORD when >= 2 pt notes have each been present for `age` frames and that has held `enter` frames
  leave CHORD when < 2 stable pt notes for `leave` frames
Frames are on pt's 256-sample grid.  Uses cache/ from cache_real.py.   python3 -W ignore switch.py dev|test|sweep"""
import sys, glob, pickle, itertools, json, os, warnings; warnings.filterwarnings("ignore")
import numpy as np
import metrics as M
HERE = os.path.dirname(os.path.abspath(__file__))
HOP_S = 256 / 44100.0

def load(players):
    out = []
    for f in sorted(glob.glob(f"{HERE}/cache/*.pkl")):
        d = pickle.load(open(f, "rb"))
        if d["name"].split("_")[0] in players: out.append(d)
    return out

def mono_on_grid(mono, grid_t):
    tm = np.array([t for t, _ in mono])
    return [mono[min(int(np.searchsorted(tm, t)), len(mono) - 1)][1] for t in grid_t]

def run_switch(d, age=14, enter=4, leave=12, busy=99):
    pt = d["pt"]; gt = [t for t, _ in pt]; mono = mono_on_grid(d["mono"], gt)
    run = {}; mode = "MONO"; ce = cl = 0; out = []; modes = []
    chg = []; prev = None; W = 26   # mono pitch changes in the last ~150 ms (a fast run changes pitch every few frames)
    for (t, notes), mn in zip(pt, mono):
        cur = {int(round(m)) for m, a, c in notes}
        mm = int(round(mn[0][0])) if mn else None
        chg.append(1 if (mm is not None and prev is not None and mm != prev) else 0); chg = chg[-W:]; prev = mm if mm is not None else prev
        busy_now = sum(chg) > busy
        run = {m: run.get(m, 0) + 1 for m in cur}
        stable = [m for m, n in run.items() if n >= age]
        if mode == "MONO":
            ce = ce + 1 if (len(stable) >= 2 and not busy_now) else 0
            if ce >= enter: mode, cl = "CHORD", 0
        else:
            cl = cl + 1 if len(stable) < 2 else 0
            if cl >= leave: mode, ce = "MONO", 0
        out.append((t, notes if mode == "CHORD" else mn)); modes.append(mode == "CHORD")
    return out, modes, mono

def score(takes, **kw):
    delays = []; R = {"sw": [], "mono": [], "pt": []}; fa = tot_mono_truth = chord_hit = chord_truth = 0
    for d in takes:
        out, modes, mono = run_switch(d, **kw)
        R["sw"].append(M.evaluate(out, d["truth"]))
        R["mono"].append(M.evaluate(list(zip([t for t, _ in out], mono)), d["truth"]))
        R["pt"].append(M.evaluate(d["pt"], d["truth"]))
        prevn = 0
        for i, ((t, _), ch) in enumerate(zip(out, modes)):
            nn = sum(1 for x in d["truth"] if x["on"] + 0.02 <= t < x["off"])
            if nn >= 2 and prevn < 2:
                j = i
                while j < len(modes) and not modes[j] and (j - i) * HOP_S < 2.0: j += 1
                delays.append((j - i) * HOP_S * 1000 if j < len(modes) else 2000)
            prevn = nn
            n = sum(1 for x in d["truth"] if x["on"] + 0.1 <= t < x["off"])
            if n <= 1:
                tot_mono_truth += 1; fa += ch
            else:
                chord_truth += 1; chord_hit += ch
    P = {k: M.pool(v) for k, v in R.items()}
    return P, dict(false_chord=fa / max(tot_mono_truth, 1), chord_recall=chord_hit / max(chord_truth, 1), entry_med=float(np.median(delays)) if delays else float('nan'), entry_p90=float(np.percentile(delays, 90)) if delays else float('nan'))

def show(title, takes, **kw):
    P, m = score(takes, **kw)
    print(f"{title}  takes={len(takes)} params={kw}  false-CHORD frames {m['false_chord']*100:.1f}%  CHORD recall {m['chord_recall']*100:.1f}%  chord entry delay {m['entry_med']:.0f}/{m['entry_p90']:.0f} ms (median/p90, from 2nd note onset)")
    for k in P: print(f"   {k:5s} F1={P[k]['F1']:.2f} P={P[k]['P']:.2f} R={P[k]['R']:.2f} retrig={P[k]['retrig']} lat={P[k]['lat_med']:.0f}/{P[k]['lat_p90']:.0f}ms")
    return P, m

if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "sweep":
        dev = load({"P1"}); best = None
        for age, busy, leave in itertools.product([40, 60, 90], [99, 2, 1], [8]):
            enter = 6
            P, m = score(dev, age=age, enter=enter, leave=leave, busy=busy)
            obj = P["sw"]["F1"] - 2 * m["false_chord"]          # a false chord on a single-note phrase is penalised
            print(f"age={age} busy={busy} enter={enter} leave={leave} F1={P['sw']['F1']:.3f} falseCHORD={m['false_chord']*100:.1f}% chordRecall={m['chord_recall']*100:.0f}% obj={obj:.3f}", flush=True)
            if best is None or obj > best[0]: best = (obj, dict(age=age, enter=enter, leave=leave, busy=busy))
        json.dump(best[1], open(f"{HERE}/switch_protocol.json", "w")); print("FROZEN", best)
    else:
        p = json.load(open(f"{HERE}/switch_protocol.json"))
        takes = load({"P1"} if mode == "dev" else {"P2", "P3"})
        alts = [dict(p)] + ([dict(p, age=a) for a in (40, 60)] if mode == 'test' else [])
        for pp in alts:
         print('==== switch params', pp)
         for grp, f in (("chords", lambda d: d["st"] == "gtchords"), ("scales/music/singles/techniques", lambda d: d["st"] == "gtechs")):
             show(f"[{mode}] {grp}", [d for d in takes if f(d)], **pp)
