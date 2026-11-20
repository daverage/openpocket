"""Frozen-protocol benchmark.  Usage:
  python3 bench.py tune     # sweeps small grids on the TUNING split only, writes protocol.json
  python3 bench.py test     # runs the frozen protocol on the held-out TEST split, writes results.json
Tuning split: guitars A,B  seeds 0..2.   Test split: guitars C,D  seeds 100..102  (never seen while tuning).
"""
import sys, json, itertools, time, os
import numpy as np
from multiprocessing import Pool
import synth, detectors as D, metrics as M

SCENARIOS = ["mono", "poly2", "power", "poly3", "poly4", "poly5", "poly6", "arpeggio", "add_note",
             "release_note", "bend_ring", "hammer_pull", "palm_mute", "quiet", "bass3"]
TUNE_SC = ["mono", "poly2", "poly3", "poly4", "power", "arpeggio", "add_note", "quiet"]
TUNE = dict(guitars="AB", seeds=range(0, 3))
TEST = dict(guitars="CD", seeds=range(100, 103))
CLS = {"mono": D.MONO, "hyb": D.HYB, "hs": D.HS, "klap": D.Klap, "nnls": D.NNLS, "pt": D.PT, "yin": D.YIN}
HERE = os.path.dirname(os.path.abspath(__file__))


def job(a):
    name, params, sc, g, seed = a
    tk = synth.scenario(sc, g, seed)
    p = dict(params)
    if sc == "bass3":
        p["lo"] = 26
    t0 = time.time()
    fr = CLS[name](**p).run(tk.audio())
    dt = time.time() - t0
    r = M.evaluate(fr, tk.truth)
    r["sec"] = dt / (len(tk.x) / synth.SR)   # host seconds per audio second (NOT pedal time)
    r["sc"] = sc
    return r


def run(name, params, scs, split, pool):
    tasks = [(name, params, sc, g, s) for sc in scs for g in split["guitars"] for s in split["seeds"]]
    res = pool.map(job, tasks)
    by = {}
    for r in res:
        by.setdefault(r["sc"], []).append(r)
    return {sc: M.pool(v) for sc, v in by.items()}, float(np.mean([r["sec"] for r in res]))


def score(per):  # tuning objective: mean framewise F1 over the tuning scenarios, mild event penalty
    return float(np.mean([per[s]["F1"] for s in per]))


GRIDS = {
    "hs": dict(rel=[0.3, 0.5, 0.7]),
    "klap": dict(amp_stop=[0.15, 0.25, 0.4, 0.6]),
    "nnls": dict(rel=[0.08, 0.15, 0.3], resid_gate=[True]),
    "pt": dict(resid_gate=[0.03, 0.05, 0.1], birth_rel=[0.15, 0.3], fast=[False, True]),
}


def tune():
    best = {}
    with Pool() as pool:
        for name, grid in GRIDS.items():
            keys = list(grid)
            for vals in itertools.product(*[grid[k] for k in keys]):
                params = dict(zip(keys, vals))
                per, sec = run(name, params, TUNE_SC, TUNE, pool)
                sc = score(per)
                print(f"tune {name} {params} F1={sc:.3f}  ({sec:.2f}x realtime on host)", flush=True)
                if name not in best or sc > best[name][0]:
                    best[name] = (sc, params)
    proto = dict(
        frozen=time.strftime("%Y-%m-%d %H:%M"),
        params={k: v[1] for k, v in best.items()},
        tuning_F1={k: v[0] for k, v in best.items()},
        acceptance=dict(  # fixed BEFORE the test split is run
            description="PT passes only if, on the held-out split, for poly3 AND poly4 it beats the best baseline "
                        "by >=0.03 framewise F1 and has fewer octave+harmonic false positives, and mono F1 >= 0.95 "
                        "with chord onset latency median <30 ms and p90 <50 ms.",
            min_F1_gain=0.03, mono_F1=0.95, chord_lat_med_ms=30, chord_lat_p90_ms=50),
    )
    json.dump(proto, open(os.path.join(HERE, "protocol.json"), "w"), indent=1)
    print(json.dumps(proto, indent=1))


def clean(o):
    return {k: (v if not isinstance(v, (list, np.ndarray)) else None) for k, v in o.items() if k not in ("cents", "lat")}


def test():
    proto = json.load(open(os.path.join(HERE, "protocol.json")))
    out = {"protocol": proto, "results": {}}
    with Pool() as pool:
        for name in ["yin", "hs", "klap", "nnls", "pt"]:
            params = proto["params"].get(name, {})
            per, sec = run(name, params, SCENARIOS, TEST, pool)
            out["results"][name] = dict(params=params, host_x_realtime=sec, per={k: clean(v) for k, v in per.items()})
            print(name, {k: round(v["F1"], 2) for k, v in per.items()}, f"{sec:.2f}x", flush=True)
        # ablations of PT (same frozen params unless stated)
        base = dict(proto["params"]["pt"])
        abl = {"pt_no_onset_gate": dict(base, differential=False),
               "pt_no_LOO": dict(base, resid_gate=-1.0),
               "pt_single_profile": dict(base, gammas=(1.0,)),
               "pt_slow_only": dict(base, fast=False),
               "pt_multires": dict(base, fast=True)}
        for k, p in abl.items():
            per, sec = run("pt", p, ["mono", "poly2", "poly3", "poly4", "add_note", "arpeggio"], TEST, pool)
            out["results"][k] = dict(params={a: b for a, b in p.items() if a != "gammas"}, host_x_realtime=sec,
                                     per={s: clean(v) for s, v in per.items()})
            print(k, {s: round(v["F1"], 2) for s, v in per.items()}, flush=True)
    json.dump(out, open(os.path.join(HERE, "results.json"), "w"), indent=1, default=float)


if __name__ == "__main__":
    {"tune": tune, "test": test}[sys.argv[1]]()
