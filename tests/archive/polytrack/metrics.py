import numpy as np
HOP = 256; SR = 44100
TOL_FR = 0.5   # semitones (50 cents) for framewise match
ONSET_TOL = 0.080

def truth_active(truth, t):
    return [x for x in truth if x["on"] <= t < x["off"]]

def true_midi(x, t):
    if x["bend"]:
        t0, t1, st = x["bend"]
        return x["midi"] + st * min(max((t - x["on"] - t0 + x["on"]) / max(t1 - t0, 1e-3), 0), 1) if False else \
               x["midi"] + st * min(max((t - x["on"] - t0) / max(t1 - t0, 1e-3), 0), 1)
    return x["midi"]

def evaluate(frames, truth, settle=0.10, quant=False):
    """Framewise + event metrics for one take. Frames at/after (onset+settle) and before (offset+release_grace)
    are scored; transition regions are covered by latency/event metrics instead."""
    tp = fp = fn = 0
    octave = harm = 0
    cents = []
    ev_det = []
    grace = 0.06
    for t, notes in frames:
        gt = [x for x in truth if x["on"] + settle <= t < x["off"]]
        amb = [x for x in truth if (x["on"] <= t < x["on"] + settle) or (x["off"] <= t < x["off"] + grace)]
        used = set()
        for m, a, c in notes:
            hit = None
            for i, x in enumerate(gt):
                if i in used: continue
                if abs(m - true_midi(x, t)) <= TOL_FR:
                    hit = i; break
            if hit is not None:
                used.add(hit); tp += 1; cents.append((m - true_midi(gt[hit], t)) * 100)
            else:
                # forgiven if it matches a note in its attack/release transition
                if any(abs(m - true_midi(x, t)) <= TOL_FR for x in amb):
                    continue
                fp += 1
                for x in gt:
                    d = m - true_midi(x, t)
                    if abs(abs(d) - 12) < 0.6 or abs(abs(d) - 24) < 0.6:
                        octave += 1; break
                else:
                    for x in gt:
                        d = m - true_midi(x, t)
                        if any(abs(d - h) < 0.6 for h in (12, 19.02, 24, 27.86, 31.02)):
                            harm += 1; break
        fn += len(gt) - len(used)
    P = tp / max(tp + fp, 1); R = tp / max(tp + fn, 1)
    F = 2 * P * R / max(P + R, 1e-9)
    # events: onset when a midi track becomes present after >=3 absent frames
    evs = {}
    last = {}
    cnt = {}
    for t, notes in frames:
        pres = {int(round(m)) for m, a, c in notes}
        for m in pres:
            if cnt.get(m, 0) == 0:
                evs.setdefault(m, []).append(t)
            cnt[m] = 3  # hysteresis counter: a track is "off" only after 3 absent frames
        for m in list(cnt):
            if m not in pres:
                cnt[m] -= 1
    # match
    lat = []; matched = set(); ev_tp = 0
    for x in truth:
        if x["hammer"]: continue
        cand = [(tt, m) for m, ts in evs.items() for tt in ts if abs(m - x["midi"]) <= 0 and x["on"] - 0.01 <= tt <= x["on"] + ONSET_TOL + 0.15]
        cand = [c for c in cand if (c[1], c[0]) not in matched]
        if cand:
            tt, m = min(cand)
            matched.add((m, tt)); ev_tp += 1; lat.append((tt - x["on"]) * 1000)
    n_truth = sum(1 for x in truth if not x["hammer"])
    n_est = sum(len(v) for v in evs.values())
    # extra onsets per truth note on same midi beyond the first = false retriggers
    retrig = 0
    for x in truth:
        ts = [tt for tt in evs.get(x["midi"], []) if x["on"] + 0.2 < tt < x["off"] - 0.05]
        retrig += len(ts)
    return dict(tp=tp, fp=fp, fn=fn, octave=octave, harm=harm, cents=cents, ev_tp=ev_tp, ev_truth=n_truth,
                ev_est=n_est, lat=lat, retrig=retrig)

def pool(rs):
    keys = ["tp", "fp", "fn", "octave", "harm", "ev_tp", "ev_truth", "ev_est", "retrig"]
    o = {k: sum(r[k] for r in rs) for k in keys}
    o["cents"] = sum((r["cents"] for r in rs), []); o["lat"] = sum((r["lat"] for r in rs), [])
    P = o["tp"] / max(o["tp"] + o["fp"], 1); R = o["tp"] / max(o["tp"] + o["fn"], 1)
    o.update(P=P, R=R, F1=2 * P * R / max(P + R, 1e-9),
             evP=o["ev_tp"] / max(o["ev_est"], 1), evR=o["ev_tp"] / max(o["ev_truth"], 1),
             lat_med=float(np.median(o["lat"])) if o["lat"] else float("nan"),
             lat_p90=float(np.percentile(o["lat"], 90)) if o["lat"] else float("nan"),
             cents_mad=float(np.median(np.abs(o["cents"]))) if o["cents"] else float("nan"))
    return o
