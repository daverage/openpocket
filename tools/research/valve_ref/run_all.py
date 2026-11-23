#!/usr/bin/env python3
"""Run every test of data/manifest.json through (a) the host DSP, (b) SPICE with the Dempwolf equations, (c) SPICE with
Koren's 12AX7 (and Koren + interelectrode capacitances where the manifest asks).  Cached: re-running only fills gaps.
    python3 run_all.py [-j 16] [name-substring ...]"""
import json, os, subprocess, sys, time, numpy as np
from concurrent.futures import ProcessPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
SPICE = os.path.join(HERE, "data", os.environ.get("VREF_SPICE", "spice"))
import gen_spice

def job(a):
    kind, name, db, model, cpar = a
    inp = f"{HERE}/data/in/{name}.f32"
    if kind == "dsp":
        out = f"{HERE}/data/dsp/{name}_{db}"
        if os.path.exists(out + ".final.f32"): return a, 0
        r = subprocess.run([f"{HERE}/build_valve_host", "run", inp, out, str(db)], capture_output=True, text=True)
        if r.returncode: raise RuntimeError(f"{a}: {r.stderr}")
        return a, 1
    dspin = model.startswith("dspin_"); mdl = model.replace("dspin_", "")
    tag = model + ("_cpar" if cpar else "")
    out = f"{SPICE}/{tag}_{name}_{db}.npz"
    if os.path.exists(out): return a, 0
    x = np.fromfile(f"{HERE}/data/dsp/{name}_{db}.vin.f32" if dspin else inp, "<f4")
    o, _ = gen_spice.run(mdl, x, db, cpar=cpar, dspin=dspin)
    np.savez_compressed(out, **o)
    return a, 1

if __name__ == "__main__":
    args = sys.argv[1:]; jobs_n = 16; MODE = "ideal"
    if "--dspin" in args: args.remove("--dspin"); MODE = "dspin"
    if "-j" in args: i = args.index("-j"); jobs_n = int(args[i + 1]); del args[i:i + 2]
    man = json.load(open(f"{HERE}/data/manifest.json"))
    os.makedirs(f"{HERE}/data/dsp", exist_ok=True); os.makedirs(f"{HERE}/data/spice", exist_ok=True)
    todo = []
    for name, m in man.items():
        if args and not any(s in name for s in args): continue
        for db in m["drives"]:
            todo.append(("dsp", name, db, None, False))
            todo += [("spice", name, db, "dempwolf", False), ("spice", name, db, "koren", False)]
            if m["cpar"]: todo.append(("spice", name, db, "koren", True))
    if MODE == "dspin":
        todo = []
        for name, m in man.items():
            if args and not any(s in name for s in args): continue
            for db in m["drives"]:
                todo += [("spice", name, db, "dspin_dempwolf", False), ("spice", name, db, "dspin_koren", False)]
                if m["cpar"]: todo.append(("spice", name, db, "dspin_koren", True))
    todo.sort(key=lambda a: (a[0] != "dsp", -man[a[1]]["n"]))      # DSP first (fast), then longest SPICE runs first
    t0 = time.time(); done = 0
    with ProcessPoolExecutor(jobs_n) as ex:
        for a, ran in ex.map(job, todo):
            done += 1
            if done % 10 == 0 or done == len(todo): print(f"{done}/{len(todo)}  {time.time()-t0:.0f}s", flush=True)
