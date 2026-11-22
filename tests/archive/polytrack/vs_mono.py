"""PT vs the production mono tracker on the held-out split (reuses protocol.json; no retuning)."""
import json, sys, warnings; warnings.filterwarnings("ignore")
from multiprocessing import Pool
import bench, metrics as M
SC = ["mono", "hammer_pull", "bend_ring", "palm_mute", "quiet", "bass3", "poly2", "poly3", "poly4", "add_note", "arpeggio"]
if __name__ == "__main__":
    proto = json.load(open("protocol.json"))
    res = {}
    with Pool() as pool:
        for n in ["mono", "pt", "hyb"]:
            per, sec = bench.run(n, proto["params"].get(n, {}), SC, bench.TEST, pool)
            res[n] = {k: bench.clean(v) for k, v in per.items()}
    json.dump(res, open("vs_mono.json", "w"), indent=1, default=float)
    print(f"{'scenario':12s} " + "  ".join(f"{n:>26s}" for n in res))
    print(f"{'':12s} " + "  ".join(f"{'F1   P    R   oct lat/p90':>26s}" for n in res))
    for s in SC:
        print(f"{s:12s} " + "  ".join(
            f"{res[n][s]['F1']:.2f} {res[n][s]['P']:.2f} {res[n][s]['R']:.2f} {res[n][s]['octave']:4d} {res[n][s]['lat_med']:3.0f}/{res[n][s]['lat_p90']:3.0f}".rjust(26) for n in res))
