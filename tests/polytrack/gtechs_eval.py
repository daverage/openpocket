"""Guitar-TECHS real electric DI (build/stage5/sets, made by tools/research/prep_sets.py): mono tracker vs pt vs klap.
Uses 30 s excerpts (pt runs ~0.6x real time on a laptop). Truth = hexaphonic MIDI, lag-corrected by prep_sets.
No tuning is done on these takes; `pt` uses the frozen protocol.json parameters.
  python3 -W ignore gtechs_eval.py"""
import sys, json, glob, os, warnings; warnings.filterwarnings("ignore")
import numpy as np, soundfile as sf
from multiprocessing import Pool
import detectors as D, metrics as M
SETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "stage5", "sets")
START, LEN = 20.0, 30.0
PROTO = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "protocol.json")))["params"]
CLS = {"mono": lambda: D.MONO(), "pt": lambda: D.PT(**PROTO["pt"]), "klap": lambda: D.Klap(**PROTO["klap"])}

def job(a):
    st, name, det = a
    x, sr = sf.read(f"{SETS}/{st}/{name}.wav", dtype="float32")
    x = x[int(START * sr):int((START + LEN) * sr)]
    truth = []
    for ln in open(f"{SETS}/{st}/{name}.csv"):
        on, off, m = ln.strip().split(","); on, off = float(on) - START, float(off) - START
        if off > 0 and on < LEN:
            truth.append(dict(on=on, off=off, midi=int(float(m)), string=0, bend=None, hammer=False))
    if not truth: return None
    r = M.evaluate(CLS[det]().run(x), truth); r["sc"] = st; r["det"] = det; r["take"] = name
    r["poly"] = float(np.mean([sum(1 for u in truth if u["on"] <= t < u["off"]) for t in np.arange(0, LEN, 0.5)]))
    return r

if __name__ == "__main__":
    takes = {"gtchords": sorted(os.path.basename(f)[:-4] for f in glob.glob(f"{SETS}/gtchords/*.wav"))[::14][:8],
             "gtechs": sorted(os.path.basename(f)[:-4] for f in glob.glob(f"{SETS}/gtechs/*.wav"))[::12][:8]}
    tasks = [(st, n, d) for st, ns in takes.items() for n in ns for d in CLS]
    with Pool() as p: res = [r for r in p.map(job, tasks) if r]
    out = {}
    for st in takes:
        for d in CLS:
            rs = [r for r in res if r["sc"] == st and r["det"] == d]
            if rs:
                o = M.pool(rs); out[f"{st}/{d}"] = {k: v for k, v in o.items() if not isinstance(v, list)}
                print(f"{st:9s} {d:5s} takes={len(rs)} meanpoly={np.mean([r['poly'] for r in rs]):.1f} F1={o['F1']:.2f} P={o['P']:.2f} R={o['R']:.2f} oct={o['octave']} harm={o['harm']} "
                      f"evR={o['evR']:.2f} lat={o['lat_med']:.0f}/{o['lat_p90']:.0f}ms retrig={o['retrig']}")
    json.dump(out, open("gtechs_results.json", "w"), indent=1, default=float)
    print([ (r['take']) for r in res if r['det']=='pt'])
