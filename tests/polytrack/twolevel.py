"""Two-level tracker experiment: mono LEAD always sounds; extra voices only after joint validation.
Compared with the MONO/CHORD classifier switch (poly_protocol.json), mono only and pt only.
Extra-voice validation (all causal): pt note, not a duplicate/octave/12th of the lead, present >= k frames,
classifier polyphony probability p >= th_x.  When the classifier hysteresis says CHORD, pt is authoritative
for pitch identity (the lead is replaced unless pt also holds it).      python3 -W ignore twolevel.py sweep|eval"""
import sys, json, itertools, os, warnings; warnings.filterwarnings("ignore")
import numpy as np
import metrics as M, switch as SW, poly_clf as PC
HERE = os.path.dirname(os.path.abspath(__file__)); HOP_S = 256 / 44100.0
LOWER = (12, 24, 19, 31, 7, 5, 36)   # octave / twelfth harmonic intervals of the lead

def dup(m, lead):
    d = abs(m - lead)
    return d < 0.7 or any(abs(d - h) < 0.6 for h in LOWER[:4])

def two_level(d, p, pr, th_x=0.5, k=6, harm=True, authoritative=True):
    modes = PC.modes_from_p(p, **pr); mn = SW.mono_on_grid(d["mono"], [t for t, _ in d["pt"]])
    age = {}; out = []
    for i, ((t, notes), lead) in enumerate(zip(d["pt"], mn)):
        cur = {int(round(m)): (m, a, c) for m, a, c in notes}
        age = {m: age.get(m, 0) + 1 for m in cur}
        if modes[i] and authoritative:
            out.append((t, notes)); continue
        o = list(lead)
        lm = lead[0][0] if lead else None
        for m, (mm, a, c) in cur.items():
            if age[m] < k or p[i] < th_x: continue
            if lm is not None and (dup(mm, lm) if harm else abs(mm - lm) < 0.7): continue
            o.append((mm, a, c))
        out.append((t, o))
    return out, modes

def byn(frames, truth, settle=0.10):
    """framewise tp/fp/fn split by number of true notes sounding"""
    R = {n: [0, 0, 0] for n in (0, 1, 2, 3, 4)}
    for t, notes in frames:
        gt = [x for x in truth if x["on"] + settle <= t < x["off"]]
        amb = [x for x in truth if (x["on"] <= t < x["on"] + settle) or (x["off"] <= t < x["off"] + 0.06)]
        n = min(len(gt), 4); used = set(); tp = fp = 0
        for m, a, c in notes:
            hit = next((i for i, x in enumerate(gt) if i not in used and abs(m - M.true_midi(x, t)) <= 0.5), None)
            if hit is not None: used.add(hit); tp += 1
            elif not any(abs(m - M.true_midi(x, t)) <= 0.5 for x in amb): fp += 1
        R[n][0] += tp; R[n][1] += fp; R[n][2] += len(gt) - len(used)
    return R

def second_voice(frames, truth, cap=2.0):
    """delay from a note being added (>=1 held note) to the output holding >=2 distinct correct pitches; and wrong-voice time"""
    res = []; prev = 0
    for i, (t, _) in enumerate(frames):
        n = sum(1 for x in truth if x["on"] + 0.02 <= t < x["off"])
        if n >= 2 and prev < 2:
            j = i
            while j < len(frames) and frames[j][0] - t < cap:
                tt = frames[j][0]; gt = [x for x in truth if x["on"] <= tt < x["off"]]
                ok = {id(x) for m, a, c in frames[j][1] for x in gt if abs(m - M.true_midi(x, tt)) <= 0.5}
                if len(ok) >= 2: break
                j += 1
            res.append(min(frames[j][0] - t, cap) * 1000 if j < len(frames) else cap * 1000)
        prev = n
    return res

def fa_stats(frames, truth):
    """frames where the output has MORE voices than are sounding (extra synth voice with no real note), per minute"""
    t0 = frames[0][0]; mins = (frames[-1][0] - t0) / 60; bad = 0; ev = 0; was = False
    for t, notes in frames:
        gt = [x for x in truth if x["on"] <= t < x["off"] + 0.06]
        extra = len(notes) > len(gt) + 0
        wrong = sum(1 for m, a, c in notes if not any(abs(m - M.true_midi(x, t)) <= 0.5 for x in gt)) > 0 and t > t0
        extra = extra or wrong
        bad += extra; ev += (extra and not was); was = extra
    return ev / mins, bad * HOP_S / mins, bad / max(len(frames), 1)

def summarize(takes, outs, label):
    R = {n: [0, 0, 0] for n in range(5)}; delays = []; ev = fas = fsec = 0; mins = 0; fr = bad = 0; res = []
    for d, o in zip(takes, outs):
        res.append(M.evaluate(o, d["truth"]))
        for n, v in byn(o, d["truth"]).items():
            for q in range(3): R[n][q] += v[q]
        delays += second_voice(o, d["truth"])
        e, s, f = fa_stats(o, d["truth"]); m = (o[-1][0] - o[0][0]) / 60; ev += e * m; fsec += s * m; mins += m; fr += f * len(o); bad += len(o)
    P = M.pool(res); rep = dict(label=label, F1=P["F1"], P=P["P"], R=P["R"], retrig=P["retrig"], lat=P["lat_med"],
        evP=P.get("evP", float("nan")), evR=P.get("evR", float("nan")), sec_med=float(np.median(delays)) if delays else float("nan"),
        sec_p90=float(np.percentile(delays, 90)) if delays else float("nan"), n_sec=len(delays),
        bad_per_min=ev / mins, bad_sec_per_min=fsec / mins, bad_frac=fr / bad)
    for n in (1, 2, 3, 4):
        tp, fp, fn = R[n]; rep[f"P{n}"] = tp / max(tp + fp, 1); rep[f"R{n}"] = tp / max(tp + fn, 1)
    return rep

def fmt(r):
    return (f"{r['label']:34s} F1 {r['F1']:.2f} P {r['P']:.2f} R {r['R']:.2f} | R by n: " + " ".join(f"{n}:{r[f'R{n}']:.2f}" for n in (1, 2, 3, 4)) +
            " | P by n: " + " ".join(f"{n}:{r[f'P{n}']:.2f}" for n in (1, 2, 3, 4)) +
            f" | 2nd-voice delay {r['sec_med']:.0f}/{r['sec_p90']:.0f} ms | bad-voice {r['bad_per_min']:.1f}/min {r['bad_sec_per_min']:.2f}s/min lead-lat {r['lat']:.0f}ms retrig {r['retrig']}")

def systems(takes, P, pr, extra=None):
    mn = [SW.mono_on_grid(d["mono"], [t for t, _ in d["pt"]]) for d in takes]
    S = {"mono only": [list(zip([t for t, _ in d["pt"]], m)) for d, m in zip(takes, mn)],
         "pt only": [d["pt"] for d in takes],
         "classifier switch": [[(t, n if mo else mm) for (t, n), mo, mm in zip(d["pt"], PC.modes_from_p(p, **pr), m)] for d, p, m in zip(takes, P, mn)]}
    for name, kw in (extra or {}).items(): S[name] = [two_level(d, p, pr, **kw)[0] for d, p in zip(takes, P)]
    return S

if __name__ == "__main__":
    mode = sys.argv[1]; pr = json.load(open(f"{HERE}/poly_protocol.json"))["params"]; dev = PC.load("cache", {"P1"})
    if mode == "sweep":
        P = PC.oof_prob(dev, "all"); ex = {}
        for thx, k, auth in itertools.product([0.3, 0.5, 0.7, 0.9], [2, 4, 8], [True, False]):
            ex[f"2L x>={thx} k={k} auth={int(auth)}"] = dict(th_x=thx, k=k, authoritative=auth)
        S = systems(dev, P, pr, ex)
        rows = {k: summarize(dev, v, k) for k, v in S.items()}
        for r in rows.values(): print(fmt(r), flush=True)
        base = rows["classifier switch"]
        ok = [r for k, r in rows.items() if k.startswith("2L") and r["bad_per_min"] <= base["bad_per_min"] * 1.0]
        pick = max(ok or [r for k, r in rows.items() if k.startswith("2L")], key=lambda r: r["F1"] - 0.002 * r["sec_med"] / 10)
        print("PICK", pick["label"]); json.dump(dict(rule="max F1 - 0.0002*2nd-voice delay s.t. DEV bad voices/min <= classifier switch", pick=pick["label"], kw=ex[pick["label"]], dev=pick), open(f"{HERE}/twolevel_protocol.json", "w"), indent=1, default=float)
    else:
        kw = json.load(open(f"{HERE}/twolevel_protocol.json"))["kw"]; m = PC.fit(dev, "all")
        for name, tk in {"FINAL": PC.load("cache_final", {"P1", "P2", "P3"})}.items():
            groups = {"all": tk, "chord takes": [d for d in tk if d["st"] == "gtchords"], "scales": [d for d in tk if "scales" in d["name"]],
                      "single+tech": [d for d in tk if "single" in d["name"] or "tech" in d["name"]]}
            for g, ts in groups.items():
                if not ts: continue
                P = [m.predict_proba(d["feat"][:, PC.cols(PC.FAM["all"])])[:, 1] for d in ts]
                S = systems(ts, P, pr, {"two-level (frozen)": kw})
                print(f"--- {name} {g}: {len(ts)} takes")
                for k, v in S.items(): print(fmt(summarize(ts, v, k)))
