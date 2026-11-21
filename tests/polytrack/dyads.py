"""Dyad analysis: for frames with exactly 2 true notes, recall of BOTH notes by interval class, for pt alone, the two-level system and an
oracle gate (mode = true polyphony).  Shows whether the gate or the estimator limits dyads.   python3 -W ignore dyads.py"""
import json, os, warnings; warnings.filterwarnings("ignore")
import numpy as np, collections
import metrics as M, poly_clf as PC, twolevel as TL, switch as SW
HERE = os.path.dirname(os.path.abspath(__file__))
pr = json.load(open(f"{HERE}/poly_protocol.json"))["params"]; kw = json.load(open(f"{HERE}/twolevel_protocol.json"))["kw"]
dev = PC.load("cache", {"P1"}); m = PC.fit(dev, "all"); tk = PC.load("cache_final", {"P1", "P2", "P3"}) + PC.load("cache", {"P2", "P3"})
cls = lambda s: {0: "unison", 12: "octave", 7: "fifth", 5: "fourth", 4: "maj3", 3: "min3", 9: "maj6", 8: "min6"}.get(s, "2nd/7th/tritone" if s < 12 else "other>oct")
acc = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0]))
for d in tk:
    P = m.predict_proba(d["feat"][:, PC.cols(PC.FAM["all"])])[:, 1]
    sysu = {"pt alone": d["pt"], "two-level": TL.two_level(d, P, pr, **kw)[0], "oracle gate": None}
    mn = SW.mono_on_grid(d["mono"], [t for t, _ in d["pt"]])
    orc = [(t, n if sum(1 for x in d["truth"] if x["on"] + .02 <= t < x["off"]) >= 2 else mm) for (t, n), mm in zip(d["pt"], mn)]
    sysu["oracle gate"] = orc
    for name, fr in sysu.items():
        for t, notes in fr:
            gt = [x for x in d["truth"] if x["on"] + 0.1 <= t < x["off"]]
            if len(gt) != 2: continue
            iv = int(round(abs(gt[0]["midi"] - gt[1]["midi"]))); c = cls(iv if iv <= 12 else iv)
            got = sum(any(abs(mm - x["midi"]) <= 0.5 for mm, a, cc in notes) for x in gt)
            acc[name][c][0] += got == 2; acc[name][c][1] += 1; acc[name]["ALL"][0] += got == 2; acc[name]["ALL"][1] += 1
cs = ["ALL"] + sorted({c for a in acc.values() for c in a if c != "ALL"})
print("fraction of dyad frames with BOTH notes output (n frames in brackets)")
print(f"{'':12s}" + "".join(f"{c:>18s}" for c in cs))
for n, a in acc.items(): print(f"{n:12s}" + "".join(f"{a[c][0] / max(a[c][1], 1):>11.2f} ({a[c][1]:4d})" for c in cs))
