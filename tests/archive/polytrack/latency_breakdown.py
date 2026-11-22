"""Where does pt's note-confirmation latency come from?  Synthetic DEV split only (guitars A,B, seeds 0-2).
Each row changes ONE stage of the frozen pt configuration.   python3 -W ignore latency_breakdown.py"""
import json, warnings; warnings.filterwarnings("ignore")
from multiprocessing import Pool
import bench
base = json.load(open("protocol.json"))["params"]["pt"]
V = {"frozen pt": {}, "confirm 2 -> 1 frame (-5.8 ms)": dict(confirm=1), "difference lag 1536 -> 512 samples": dict(lag=512),
     "window 4096 -> 2048 (loses low notes)": dict(N=2048), "no birth gate (whole spectrum)": dict(differential=False),
     "confirm 1 + lag 512": dict(confirm=1, lag=512), "confirm 1 + lag 512 + window 2048": dict(confirm=1, lag=512, N=2048)}
if __name__ == "__main__":
    with Pool() as p:
        for k, v in V.items():
            per, sec = bench.run("pt", dict(base, **v), ["poly3", "poly4", "add_note"], bench.TUNE, p)
            print(f"{k:42s} " + "  ".join(f"{s}: F1={per[s]['F1']:.2f} lat={per[s]['lat_med']:.0f}/{per[s]['lat_p90']:.0f}ms oct+harm={per[s]['octave']+per[s]['harm']}" for s in per), flush=True)
