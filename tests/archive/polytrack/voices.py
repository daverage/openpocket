"""Audible polyphonic-synth benchmark: tracker frames -> voice events (note-on/off per pitch) -> oscillator render.
Voice-level metrics against exact truth: false voices (no real note within 50 cents), onset error, release lag,
short voices (< 80 ms), voices per true note (fragmentation).   python3 -W ignore voices.py [render]"""
import sys, os, json, warnings; warnings.filterwarnings("ignore")
import numpy as np, wave
import metrics as M, poly_clf as PC, twolevel as TL, switch as SW
HERE = os.path.dirname(os.path.abspath(__file__)); HOP_S = 256 / 44100.0; SR = 44100

def to_voices(frames, rel=3, min_on=0, defer=0):
    """one voice per integer pitch; released after `rel` absent frames. Returns [(midi, t_on, t_off)]"""
    live = {}; out = []
    for t, notes in frames:
        cur = {int(round(m)) for m, a, c in notes}
        for m in cur:
            if m in live: live[m][1] = 0
            else: live[m] = [t, 0]
        for m in list(live):
            if m not in cur:
                live[m][1] += 1
                if live[m][1] >= rel:
                    out.append((m, live[m][0], t - rel * HOP_S)); del live[m]
    for m, (t0, _) in live.items(): out.append((m, t0, frames[-1][0]))
    if defer:   # a voice only sounds once its pitch has been present `defer` frames: onset moves later, shorter blips never sound
        out = [(m, a + defer * HOP_S, b) for m, a, b in out if b - a >= defer * HOP_S]
    return [v for v in out if v[2] - v[1] >= min_on]

def voice_metrics(voices, truth):
    false_s = tot_s = 0.0; short = 0; nfalse = 0
    for m, a, b in voices:
        tot_s += b - a
        ok = [x for x in truth if x["on"] - 0.1 <= b and a <= x["off"] + 0.1 and abs(m - x["midi"]) <= 0.7]
        if not ok: nfalse += 1; false_s += b - a
        if b - a < 0.08: short += 1
    on_err = []; rel_lag = []; per = []
    for x in truth:
        vs = [v for v in voices if abs(v[0] - x["midi"]) <= 0.7 and v[1] <= x["off"] and v[2] >= x["on"]]
        per.append(len(vs))
        if vs:
            v0 = min(vs, key=lambda v: v[1]); on_err.append((v0[1] - x["on"]) * 1000)
            rel_lag.append((max(v[2] for v in vs) - x["off"]) * 1000)
    return dict(n_voices=len(voices), n_false=nfalse, false_s=false_s, tot_s=tot_s, short=short,
                missed=sum(1 for p in per if p == 0), n_true=len(truth), frag=float(np.mean([p for p in per if p > 0])) if any(per) else float("nan"),
                on_med=float(np.median(on_err)) if on_err else float("nan"), on_p90=float(np.percentile(on_err, 90)) if on_err else float("nan"),
                rel_med=float(np.median(rel_lag)) if rel_lag else float("nan"))

def render(voices, dur, kind="saw", atk=0.01, rel=0.08):
    dur = max([dur] + [b for _, _, b in voices]); n = int(dur * SR) + SR; y = np.zeros(n, np.float32)
    for m, a, b in voices:
        f = 440 * 2 ** ((m - 69) / 12); i0 = int(a * SR); i1 = min(int((b + rel) * SR), n - 1)
        if i1 <= i0 or i0 < 0: continue
        t = np.arange(i1 - i0) / SR
        if kind == "organ": w = sum(np.sin(2 * np.pi * f * h * t) / h for h in (1, 2, 3, 4, 6, 8)) / 3
        else: w = 2 * ((f * t) % 1.0) - 1; w = w * 0.6 + 0.4 * (2 * ((f * 1.004 * t) % 1.0) - 1)   # detuned saw pair (string-ish)
        env = np.minimum(t / atk, 1.0) * np.where(t < (b - a), 1.0, np.maximum(0, 1 - (t - (b - a)) / rel))
        y[i0:i1] += (w * env * 0.18).astype(np.float32)
    return np.clip(y, -1, 1)

def wav(path, y):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes((y * 32767).astype(np.int16).tobytes())

def agg(rows):
    s = {k: sum(r[k] for r in rows) for k in ("n_voices", "n_false", "false_s", "tot_s", "short", "missed", "n_true")}
    s["on_med"] = float(np.nanmedian([r["on_med"] for r in rows])); s["on_p90"] = float(np.nanmedian([r["on_p90"] for r in rows]))
    s["rel_med"] = float(np.nanmedian([r["rel_med"] for r in rows])); s["frag"] = float(np.nanmean([r["frag"] for r in rows]))
    return s

if __name__ == "__main__":
    pr = json.load(open(f"{HERE}/poly_protocol.json"))["params"]; kw = json.load(open(f"{HERE}/twolevel_protocol.json"))["kw"]
    dev = PC.load("cache", {"P1"}); m = PC.fit(dev, "all"); tk = PC.load("cache_final", {"P1", "P2", "P3"})
    P = [m.predict_proba(d["feat"][:, PC.cols(PC.FAM["all"])])[:, 1] for d in tk]
    S = TL.systems(tk, P, pr, {"two-level": kw})
    outd = os.path.abspath(f"{HERE}/../../../build/polytrack/voices"); os.makedirs(outd, exist_ok=True)
    groups = {"chord takes": [i for i, d in enumerate(tk) if d["st"] == "gtchords"], "scales": [i for i, d in enumerate(tk) if "scales" in d["name"]],
              "single+tech": [i for i, d in enumerate(tk) if "single" in d["name"] or "tech" in d["name"]]}
    for g, idx in groups.items():
        print(f"--- FINAL {g}: {len(idx)} takes")
        for name, outs in S.items():
            for rel, df in ((3, 0), (3, 4), (3, 8), (8, 8)):
                rows = [voice_metrics(to_voices(outs[i], rel=rel, defer=df), tk[i]["truth"]) for i in idx]; a = agg(rows)
                print(f"  {name:18s} hold {rel} defer {df} fr  voices {a['n_voices']:5d}  false {a['n_false']:4d} ({a['false_s']:.1f}s of {a['tot_s']:.0f}s)  short<80ms {a['short']:4d}  missed {a['missed']}/{a['n_true']}  voices/note {a['frag']:.2f}  onset {a['on_med']:.0f}/{a['on_p90']:.0f} ms  release {a['rel_med']:+.0f} ms")
    if len(sys.argv) > 1:
        sel = [i for i in groups["chord takes"]][:3]
        for i in sel:
            d = tk[i]; dur = d["pt"][-1][0]
            for name, outs in S.items():
                if name in ("classifier switch", "two-level", "mono only"):
                    wav(f"{outd}/{d['name']}_{name.replace(' ', '_')}.wav", render(to_voices(outs[i], rel=3), dur))
            wav(f"{outd}/{d['name']}_TRUTH.wav", render([(x["midi"], x["on"], x["off"]) for x in d["truth"]], dur))
        print("rendered to", outd, sorted(os.listdir(outd))[:12])
