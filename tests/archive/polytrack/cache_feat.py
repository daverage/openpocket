"""Adds poly_feat features to the cached takes.  python3 -W ignore cache_feat.py [cache|cache_final]"""
import sys, glob, pickle, os, warnings; warnings.filterwarnings("ignore")
import numpy as np, soundfile as sf
from multiprocessing import Pool
import poly_feat
HERE = os.path.dirname(os.path.abspath(__file__)); SETS = f"{HERE}/../../../build/stage5/sets"
CDIR = sys.argv[1] if len(sys.argv) > 1 else "cache"
START = 80.0 if CDIR == "cache_final" else 20.0
def job(f):
    d = pickle.load(open(f, "rb"))
    if "feat" in d: return f
    x, sr = sf.read(f"{SETS}/{d['st']}/{d['name']}.wav", dtype="float32"); x = x[int(START * sr):int((START + 30) * sr)]
    P = poly_feat.Poly(); times = [t for t, _ in d["pt"]]
    import time; t0 = time.time()
    d["feat"] = P.run(x, d["mono"], times); d["feat_sec_per_audio_sec"] = (time.time() - t0) / 30
    pickle.dump(d, open(f, "wb")); return f
if __name__ == "__main__":
    fs = sorted(glob.glob(f"{HERE}/{CDIR}/*.pkl"))
    with Pool() as p: p.map(job, fs, chunksize=1)
    print("done", len(fs))
