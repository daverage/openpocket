import sys, time, numpy as np
import synth, detectors as D, metrics as M
kinds = sys.argv[2].split(",") if len(sys.argv) > 2 else ["mono","poly2","poly3","poly4","poly6"]
dets = {"hs": D.HS, "klap": D.Klap, "nnls": D.NNLS, "yin": D.YIN, "pt": D.PT}
for name in sys.argv[1].split(","):
    for k in kinds:
        rs = []; t0 = time.time()
        for g in "AB":
            for seed in range(3):
                tk = synth.scenario(k, g, seed)
                kw = {"lo": 26} if k == "bass3" else {}
                fr = dets[name](**kw).run(tk.audio())
                rs.append(M.evaluate(fr, tk.truth))
        p = M.pool(rs)
        print(f"{name:5s} {k:12s} P={p['P']:.2f} R={p['R']:.2f} F1={p['F1']:.2f} oct={p['octave']} harm={p['harm']} evP={p['evP']:.2f} evR={p['evR']:.2f} lat={p['lat_med']:.0f}/{p['lat_p90']:.0f}ms retrig={p['retrig']} ({time.time()-t0:.0f}s)")
