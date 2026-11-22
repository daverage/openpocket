"""External reference: Essentia MultiPitchKlapuri (the published-algorithm implementation) on the cached TEST takes.
Same excerpts, same framewise metric.  pip install essentia ; python3 -W ignore essentia_ref.py"""
import glob, pickle, os, json, warnings; warnings.filterwarnings("ignore")
import numpy as np, soundfile as sf
import essentia.standard as es
import metrics as M
HERE = os.path.dirname(os.path.abspath(__file__)); SETS = f"{HERE}/../../../build/stage5/sets"
def run(x, N=4096):
    w = es.Windowing(type="hann"); sp = es.Spectrum()
    mp = es.MultiPitchKlapuri(sampleRate=44100, frameSize=N, hopSize=256, minFrequency=65, maxFrequency=1400)
    # Klapuri's algorithm wants spectral peaks/whitened spectrum; Essentia's own chain:
    out = []
    for i, fr in enumerate(es.FrameGenerator(x, frameSize=N, hopSize=256, startFromZero=True)):
        s = sp(w(fr))
        f = mp(s)
        out.append(((i * 256 + N) / 44100.0, [(69 + 12 * np.log2(v / 440), 1.0, 1.0) for v in f if v > 40]))
    return out
if __name__ == "__main__":
    import sys
    from multiprocessing import Pool
    def job(f):
        d = pickle.load(open(f, "rb"))
        st, name = d["st"], d["name"]
        x, sr = sf.read(f"{SETS}/{st}/{name}.wav", dtype="float32"); x = x[int(20 * sr):int(50 * sr)]
        return d["st"], M.evaluate(run(x), d["truth"])
    fs = [f for f in sorted(glob.glob(f"{HERE}/cache/*.pkl")) if os.path.basename(f).split("__")[1].split("_")[0] in ("P2", "P3")]
    with Pool() as p: res = p.map(job, fs)
    for st in ("gtchords", "gtechs"):
        o = M.pool([r for s, r in res if s == st])
        print(f"essentia MultiPitchKlapuri {st}: F1={o['F1']:.2f} P={o['P']:.2f} R={o['R']:.2f} lat={o['lat_med']:.0f}/{o['lat_p90']:.0f}ms")
