"""Can a dyad-weighted polyphony gate recover dyads without more false voices?  Tune on DEV (P1, out-of-fold), freeze, test FINAL.
sample weight w2 on frames with exactly 2 true notes; thresholds swept on the weighted gate.     python3 -W ignore dyad_gate.py"""
import json, os, itertools, warnings; warnings.filterwarnings("ignore")
import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.preprocessing import StandardScaler
from sklearn.pipeline import make_pipeline
from sklearn.model_selection import GroupKFold
import poly_clf as PC, twolevel as TL, metrics as M
HERE = os.path.dirname(os.path.abspath(__file__)); C = PC.cols(PC.FAM["all"])
kw = json.load(open(f"{HERE}/twolevel_protocol.json"))["kw"]
def n_of(d, t): return PC.npoly(d, t)
def fitw(takes, w2):
    X = np.vstack([d["feat"][:, C] for d in takes]); y = np.concatenate([PC.labels(d) for d in takes])
    n = np.concatenate([[n_of(d, t) for t, _ in d["pt"]] for d in takes]); w = np.where(n == 2, w2, 1.0)
    m = make_pipeline(StandardScaler(), LogisticRegression(C=1.0, max_iter=500)); m.fit(X, y, logisticregression__sample_weight=w); return m
def oof(takes, w2):
    P = [None] * len(takes); idx = np.arange(len(takes))
    for tr, te in GroupKFold(5).split(idx, groups=idx):
        m = fitw([takes[i] for i in tr], w2)
        for i in te: P[i] = m.predict_proba(takes[i]["feat"][:, C])[:, 1]
    return P
def dyad_both(takes, outs):
    a = b = 0
    for d, fr in zip(takes, outs):
        for t, notes in fr:
            gt = [x for x in d["truth"] if x["on"] + 0.1 <= t < x["off"]]
            if len(gt) == 2: b += 1; a += all(any(abs(m - x["midi"]) <= 0.5 for m, _, _ in notes) for x in gt)
    return a / max(b, 1)
def run(takes, P, pr):
    outs = [TL.two_level(d, p, pr, **kw)[0] for d, p in zip(takes, P)]
    r = TL.summarize(takes, outs, "x"); return r, dyad_both(takes, outs)
if __name__ == "__main__":
    dev = PC.load("cache", {"P1"}); fin = PC.load("cache_final", {"P1", "P2", "P3"}); res = []
    for w2 in (1, 3, 6):
        P = oof(dev, w2)
        for th_in, E, th_out, L in itertools.product([0.5, 0.8, 0.95], [2, 4], [0.2, 0.4], [6]):
            pr = dict(th_in=th_in, E=E, th_out=th_out, L=L); r, db = run(dev, P, pr)
            res.append((w2, pr, r, db)); print(f"w2={w2} {pr} dyad-both {db:.2f} F1 {r['F1']:.2f} bad {r['bad_per_min']:.0f}/min {r['bad_sec_per_min']:.1f}s/min 2nd-voice {r['sec_med']:.0f}/{r['sec_p90']:.0f}", flush=True)
    ref = next(r for r in res if r[0] == 1 and r[1] == json.load(open(f"{HERE}/poly_protocol.json"))["params"])
    ok = [r for r in res if r[2]["bad_per_min"] <= ref[2]["bad_per_min"] * 1.1 and r[2]["F1"] >= ref[2]["F1"] - 0.01]
    pick = max(ok, key=lambda r: r[3]); print("PICK", pick[0], pick[1], "dyad-both", round(pick[3], 2), "(reference", round(ref[3], 2), ")")
    json.dump(dict(w2=pick[0], params=pick[1], rule="max DEV dyad-both subject to bad voices/min <= 1.1x reference and F1 >= reference-0.01"), open(f"{HERE}/dyad_protocol.json", "w"), indent=1)
    m = fitw(dev, pick[0]); P = [m.predict_proba(d["feat"][:, C])[:, 1] for d in fin]
    m0 = fitw(dev, 1); P0 = [m0.predict_proba(d["feat"][:, C])[:, 1] for d in fin]
    ref_pr = json.load(open(f"{HERE}/poly_protocol.json"))["params"]
    for lab, pp, pr in (("FINAL reference gate", P0, ref_pr), ("FINAL dyad-weighted gate", P, pick[1])):
        r, db = run(fin, pp, pr); print(f"{lab}: dyad-both {db:.2f} F1 {r['F1']:.2f} R by n 2:{r['R2']:.2f} 3:{r['R3']:.2f} 4:{r['R4']:.2f} P by n 1:{r['P1']:.2f} 2:{r['P2']:.2f} | bad {r['bad_per_min']:.0f}/min {r['bad_sec_per_min']:.1f}s/min | 2nd-voice {r['sec_med']:.0f}/{r['sec_p90']:.0f} ms")
