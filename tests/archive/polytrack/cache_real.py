"""Runs mono + pt once over Guitar-TECHS excerpts and caches frames + truth (cache/*.pkl) so the mode switch can be
developed on DEV (player P1) and scored once on TEST (players P2, P3).  python3 -W ignore cache_real.py"""
import os, glob, json, pickle, warnings; warnings.filterwarnings("ignore")
import numpy as np, soundfile as sf
from multiprocessing import Pool
import detectors as D
HERE = os.path.dirname(os.path.abspath(__file__)); SETS = f"{HERE}/../../../build/stage5/sets"
PT = json.load(open(f"{HERE}/protocol.json"))["params"]["pt"]
import sys
FINAL = len(sys.argv) > 1 and sys.argv[1] == "final"   # final: later excerpt (80 s) and chord takes not used before
START, LEN = (80.0, 30.0) if FINAL else (20.0, 30.0)
CDIR = "cache_final" if FINAL else "cache"
def job(a):
    st, name = a
    out = f"{HERE}/{CDIR}/{st}__{name}.pkl"
    if os.path.exists(out): return out
    x, sr = sf.read(f"{SETS}/{st}/{name}.wav", dtype="float32")
    x = x[int(START * sr):int((START + LEN) * sr)]
    truth = []
    for ln in open(f"{SETS}/{st}/{name}.csv"):
        on, off, m = ln.strip().split(","); on, off = float(on) - START, float(off) - START
        if off > 0 and on < LEN: truth.append(dict(on=on, off=off, midi=int(float(m)), string=0, bend=None, hammer=False))
    if not truth or len(x) < 20 * 44100: return None
    pickle.dump(dict(st=st, name=name, truth=truth, mono=D.MONO().run(x), pt=D.PT(**PT).run(x)), open(out, "wb"))
    return out
if __name__ == "__main__":
    os.makedirs(f"{HERE}/{CDIR}", exist_ok=True)
    tasks = []
    for st in ("gtchords", "gtechs"):
        names = sorted(os.path.basename(f)[:-4] for f in glob.glob(f"{SETS}/{st}/*.wav"))
        if st == "gtchords": names = names[1::3] if FINAL else names[::3]
        if FINAL and st == "gtechs": names = [n for n in names if not n.startswith("P1")]
        tasks += [(st, n) for n in names]
    with Pool() as p: r = p.map(job, tasks)
    print(sum(1 for x in r if x), "cached of", len(tasks))
