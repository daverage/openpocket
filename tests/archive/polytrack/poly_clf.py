"""Independent-polyphony classifier -> MONO/CHORD mode, compared with the persistence switch.
  python3 -W ignore poly_clf.py ablate     # DEV only: out-of-fold AUC per evidence family
  python3 -W ignore poly_clf.py sweep      # DEV only: temporal rule Pareto, freezes poly_protocol.json
  python3 -W ignore poly_clf.py eval       # frozen: INSPECTED (P2/P3 excerpt used before) and FINAL (new excerpts/takes)
Never tunes on inspected or final."""
import sys, glob, pickle, os, json, itertools, warnings; warnings.filterwarnings("ignore")
import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.preprocessing import StandardScaler
from sklearn.pipeline import make_pipeline
from sklearn.model_selection import GroupKFold
from sklearn.metrics import roc_auc_score
import metrics as M, switch as SW, poly_feat
HERE = os.path.dirname(os.path.abspath(__file__)); HOP = 256 / 44100.0
FAM = {"C": ["gC", "rC", "bic", "snr"], "B": ["nsc", "nse", "oct", "lvl"], "A": ["gA", "rA", "mon"]}
FAM["all"] = FAM["C"] + FAM["B"] + FAM["A"]
def load(cdir, players):
    out = []
    for f in sorted(glob.glob(f"{HERE}/{cdir}/*.pkl")):
        d = pickle.load(open(f, "rb"))
        if d["name"].split("_")[0] in players and "feat" in d: out.append(d)
    return out
def npoly(d, t): return sum(1 for x in d["truth"] if x["on"] + 0.02 <= t < x["off"])
def labels(d): return np.array([npoly(d, t) >= 2 for t, _ in d["pt"]])
def cols(names): return [poly_feat.FEATS.index(n) for n in names]
def fit(takes, fam):
    X = np.vstack([d["feat"][:, cols(FAM[fam])] for d in takes]); y = np.concatenate([labels(d) for d in takes])
    m = make_pipeline(StandardScaler(), LogisticRegression(C=1.0, max_iter=500)); m.fit(X, y); return m
def oof_prob(takes, fam, k=5):
    """out-of-fold probabilities (grouped by take) so DEV tuning sees no model trained on its own frames"""
    P = [None] * len(takes); idx = np.arange(len(takes))
    for tr, te in GroupKFold(k).split(idx, groups=idx):
        m = fit([takes[i] for i in tr], fam)
        for i in te: P[i] = m.predict_proba(takes[i]["feat"][:, cols(FAM[fam])])[:, 1]
    return P
def modes_from_p(p, th_in, E, th_out, L):
    mode = False; ce = cl = 0; out = []
    for v in p:
        if not mode:
            ce = ce + 1 if v > th_in else 0
            if ce >= E: mode, cl = True, 0
        else:
            cl = cl + 1 if v < th_out else 0
            if cl >= L: mode, ce = False, 0
        out.append(mode)
    return np.array(out)
def stats(d, modes):
    t = np.array([t for t, _ in d["pt"]]); poly = np.array([npoly(d, x) >= 2 for x in t]); mins = (t[-1] - t[0]) / 60.0
    seg = []; i = 0
    while i < len(modes):
        if modes[i]:
            j = i
            while j < len(modes) and modes[j]: j += 1
            seg.append((i, j)); i = j
        else: i += 1
    grace = int(0.3 / HOP); pg = poly.copy()
    for k in range(len(poly)):
        if poly[k]: pg[k:k + grace] = True
    fa = [(a, b) for a, b in seg if pg[a:b].mean() < 0.25]
    entry, exitl = [], []
    for k in range(1, len(poly) - 1):
        if poly[k] and not poly[k - 1] and poly[k:k + int(0.2 / HOP)].all():
            j = k
            while j < len(modes) and not modes[j] and (j - k) * HOP < 2.0: j += 1
            entry.append((j - k) * HOP * 1000 if j < len(modes) and modes[j] else 2000)
        if (not poly[k]) and poly[k - 1] and (~poly[k:k + int(0.2 / HOP)]).all() and modes[k - 1]:
            j = k
            while j < len(modes) and modes[j] and (j - k) * HOP < 2.0: j += 1
            exitl.append((j - k) * HOP * 1000)
    return dict(mins=mins, fa=len(fa), fa_dur=sum(b - a for a, b in fa) * HOP, entry=entry, exit=exitl,
                chord_frames=int(poly.sum()), hit=int((modes & poly).sum()), nonchord=int((~poly).sum()), false=int((modes & ~poly).sum()))
def system(d, modes):
    mn = SW.mono_on_grid(d["mono"], [t for t, _ in d["pt"]])
    out = [(t, n if m else mm) for (t, n), m, mm in zip(d["pt"], modes, mn)]
    return M.evaluate(out, d["truth"])
def summarize(takes, modes_list, label):
    S = [stats(d, m) for d, m in zip(takes, modes_list)]
    mins = sum(s["mins"] for s in S); ent = sum((s["entry"] for s in S), []); ex = sum((s["exit"] for s in S), [])
    Rs = M.pool([system(d, m) for d, m in zip(takes, modes_list)])
    nz = sum(s["nonchord"] for s in S); ch = sum(s["chord_frames"] for s in S)
    return dict(label=label, fa_per_min=sum(s["fa"] for s in S) / mins, fa_sec_per_min=sum(s["fa_dur"] for s in S) / mins,
                false_frames=sum(s["false"] for s in S) / max(nz, 1), recall=sum(s["hit"] for s in S) / max(ch, 1),
                entry_med=float(np.median(ent)) if ent else np.nan, entry_p90=float(np.percentile(ent, 90)) if ent else np.nan, n_entry=len(ent),
                exit_med=float(np.median(ex)) if ex else np.nan, F1=Rs["F1"], P=Rs["P"], R=Rs["R"], evP=Rs["evP"], evR=Rs["evR"], retrig=Rs["retrig"])
def fmt(r):
    return (f"{r['label']:34s} FA/min={r['fa_per_min']:5.2f} ({r['fa_sec_per_min']:.2f}s/min) falseframes={r['false_frames']*100:4.1f}% chordrecall={r['recall']*100:4.1f}% "
            f"entry={r['entry_med']:5.0f}/{r['entry_p90']:5.0f}ms exit={r['exit_med']:5.0f}ms F1={r['F1']:.2f} evP={r['evP']:.2f} evR={r['evR']:.2f} retrig={r['retrig']}")
def persistence_modes(d, **kw): return np.array(SW.run_switch(d, **kw)[1])
SLOW = dict(age=90, enter=6, leave=8, busy=99); FAST = dict(age=40, enter=6, leave=8, busy=99)
def main():
    mode = sys.argv[1]; dev = load("cache", {"P1"})
    if mode == "ablate":
        y = np.concatenate([labels(d) for d in dev])
        for fam in ("C", "B", "A", "all"):
            p = np.concatenate(oof_prob(dev, fam)); print(f"evidence {fam:3s} out-of-fold AUC (frame, DEV P1, {len(dev)} takes) = {roc_auc_score(y, p):.3f}")
        return
    if mode == "sweep":
        P = oof_prob(dev, "all"); res = []
        base = summarize(dev, [persistence_modes(d, **SLOW) for d in dev], "persistence slow (DEV)"); print(fmt(base))
        print(fmt(summarize(dev, [persistence_modes(d, **FAST) for d in dev], "persistence fast (DEV)")))
        for th_in, E, th_out, L in itertools.product([0.5, 0.65, 0.8, 0.9, 0.95], [1, 2, 4], [0.2, 0.4], [6, 12]):
            r = summarize(dev, [modes_from_p(p, th_in, E, th_out, L) for p in P], f"clf in={th_in} E={E} out={th_out} L={L}")
            r.update(params=dict(th_in=th_in, E=E, th_out=th_out, L=L)); res.append(r); print(fmt(r), flush=True)
        ok = [r for r in res if r["fa_per_min"] <= base["fa_per_min"] and r["recall"] > 0.5]
        pick = min(ok, key=lambda r: r["entry_med"]) if ok else max(res, key=lambda r: r["recall"] - 2 * r["fa_per_min"])
        json.dump(dict(params=pick["params"], rule="min median entry latency s.t. DEV FA/min <= slow persistence switch and recall > 50%",
                       dev=dict((k, v) for k, v in pick.items() if k != "params")), open(f"{HERE}/poly_protocol.json", "w"), indent=1, default=float)
        print("FROZEN", pick["params"]); return
    pr = json.load(open(f"{HERE}/poly_protocol.json"))["params"]; m = fit(dev, "all")
    sets = {"INSPECTED (P2/P3 excerpt 20-50 s, seen earlier)": load("cache", {"P2", "P3"}),
            "FINAL (new excerpts 80-110 s of P2/P3, new chord takes)": load("cache_final", {"P1", "P2", "P3"})}
    for name, tk in sets.items():
        groups = {"all": tk, "chord takes": [d for d in tk if d["st"] == "gtchords"],
                  "scales": [d for d in tk if "scales" in d["name"]], "music": [d for d in tk if "music" in d["name"]],
                  "single notes + techniques": [d for d in tk if "single" in d["name"] or "tech" in d["name"]]}
        print("\n=====", name)
        for g, ts in groups.items():
            if not ts: continue
            print(f"--- {g}: {len(ts)} takes")
            pm = [modes_from_p(m.predict_proba(d["feat"][:, cols(FAM["all"])])[:, 1], **pr) for d in ts]
            print(fmt(summarize(ts, pm, "new classifier"))); print(fmt(summarize(ts, [persistence_modes(d, **SLOW) for d in ts], "persistence slow")))
            print(fmt(summarize(ts, [persistence_modes(d, **FAST) for d in ts], "persistence fast")))
            print(fmt(summarize(ts, [np.zeros(len(d["pt"]), bool) for d in ts], "mono only"))); print(fmt(summarize(ts, [np.ones(len(d["pt"]), bool) for d in ts], "pt only")))
        # recall by simultaneous-note count (frame level)
        for k in (2, 3, 4):
            hit = tot = 0
            for d in tk:
                md = modes_from_p(m.predict_proba(d["feat"][:, cols(FAM["all"])])[:, 1], **pr)
                for i, (t, _) in enumerate(d["pt"]):
                    n = npoly(d, t)
                    if (n == k) if k < 4 else (n >= 4): tot += 1; hit += md[i]
            print(f"  classifier CHORD recall with {k}{'+' if k == 4 else ''} true notes: {hit / max(tot, 1) * 100:.1f}% ({tot} frames)")
if __name__ == "__main__": main()
