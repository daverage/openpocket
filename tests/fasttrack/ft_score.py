#!/usr/bin/env python3
"""FastTrack scorer (host only).  Scores the event/trace output of ft_trace on a prepared set against its note CSVs.

  python3 tests/fasttrack/ft_score.py VARIANT [VARIANT...] [--set gtechs] [--players P1,P2,P3] [--json out.json]

Definitions (all times are what a real-time synth would see: nothing is back-dated; ms are tracker-output times):
 truth      build/stage5/sets/<set>/<take>.csv  (onset, offset, midi), the hexaphonic MIDI shifted by a per-take lag fitted to the DI.
            Known limit: that lag fit has an uncertainty of several ms, so latency medians carry roughly +-10 ms of reference error.
 group      truth onsets within 40 ms of each other (a chord strum) are one musical event; a group is FOUND if the voice reaches any of its pitches.
 found      first trace sample (64 samples = 1.45 ms) in [on-30 ms, end] with gate on and pitch within 50 cents of a pitch of the group, held 3 samples.
            If the voice was already at that pitch at on-30 ms (a repeated note) a new N event is required.  end = min(next group onset, off, on+600 ms).
 octave     not found at the right pitch, but found at +-12 or +-19 semitones: counted as an octave/harmonic error (still a miss).
 latency    found time - truth onset (can be negative by the lag uncertainty); reported median / p90 / p99 over found groups.
 N-events   the synth's note-ons.  A note-on that is the 'found' event is correct.  Others are classed:
            wrong_init  within 150 ms after a group onset but not at one of its pitches (a wrong start, usually corrected later);
            spurious    elsewhere (not near any truth onset): a false audible note, reported per minute of truth span;
            extra       at a right pitch of a group already found (fragmentation / false retrigger).
 wrong_ms   audible time (gate on) at a pitch that matches no sounding truth note (+-50 cents), per minute of truth span.
"""
import os, glob, json, math, argparse
import numpy as np
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
def hz(m): return 440.0 * 2 ** ((m - 69) / 12)
def cents(f, g): return 1200 * math.log2(f / g) if f > 0 and g > 0 else 1e9

def load_truth(path):
    r = [tuple(map(float, l.split(','))) for l in open(path) if l.strip()]
    return sorted((a * 1000, b * 1000, m) for a, b, m in r)
def load_ev(path):
    ev = []
    for l in open(path):
        p = l.split()
        if p: ev.append((p[0], float(p[1]), [float(x) for x in p[2:] if x.replace('.', '', 1).replace('-', '', 1).isdigit()]))
    return ev
def groups(tr):
    g = []
    for a, b, m in tr:
        if g and a - g[-1]['on'] < 40: g[-1]['notes'].append((a, b, m)); g[-1]['off'] = max(g[-1]['off'], b)
        else: g.append(dict(on=a, off=b, notes=[(a, b, m)]))
    return g

def score_take(trace, ev, tr):
    T = trace[:, 0]; F = trace[:, 1]; G = trace[:, 2] > 0.5
    gs = groups(tr)
    N = [(t, a[0], a[2] if len(a) > 2 else 0) for k, t, a in ev if k == 'N' and a]
    Ls = [(t, a[0]) for k, t, a in ev if k == 'L' and a]
    out = dict(groups=len(gs), found=0, missed=0, octave=0, lat=[], lat_fast=[], found_fast=0, n_fast=0, wrong_init=0, spurious=0, extra=0,
               wrong_ms=0.0, dur_min=0.0, nev=len(N), slur_found=0)
    if not gs: return out
    out['dur_min'] = (tr[-1][1] - tr[0][0]) / 60000.0
    onset_t = np.array([g['on'] for g in gs]); used = set()
    for gi, g in enumerate(gs):
        pitches = [hz(m) for _, _, m in g['notes']]
        nxt = gs[gi + 1]['on'] if gi + 1 < len(gs) else 1e12
        end = min(nxt, g['off'], g['on'] + 600)
        fast = gi > 0 and g['on'] - gs[gi - 1]['on'] < 150
        out['n_fast'] += fast
        lo, hi = np.searchsorted(T, g['on'] - 30), np.searchsorted(T, end)
        pre = np.searchsorted(T, g['on'] - 30)
        def right(i, ps): return G[i] and any(abs(cents(F[i], p)) < 50 for p in ps)
        already = pre < len(T) and right(pre, pitches)
        tfound = None
        if already:
            for (t, f, _) in N:
                if g['on'] - 30 <= t <= end and any(abs(cents(f, p)) < 50 for p in pitches): tfound = t; break
        else:
            run = 0
            for i in range(lo, hi):
                if right(i, pitches):
                    run += 1
                    if run >= 3: tfound = T[i - 2]; break
                else: run = 0
        if tfound is None:
            oc = False
            for i in range(lo, hi):
                if G[i] and any(min(abs(cents(F[i], p * 2 ** (k / 12))) for k in (12, -12, 19, -19, 24, -24)) < 50 for p in pitches): oc = True; break
            out['octave'] += oc; out['missed'] += 1
        else:
            out['found'] += 1; l = tfound - g['on']; out['lat'].append(l)
            if fast: out['lat_fast'].append(l); out['found_fast'] += 1
            # the N event that made it found (nearest at/before tfound within 2 frames)
            cand = [t for t, f, _ in N if -5 <= t - tfound <= 8 and any(abs(cents(f, p)) < 50 for p in pitches)]
            if cand: used.add(cand[0])
            else:
                s = [t for t, f in Ls if abs(t - tfound) < 4]
                if s: out['slur_found'] += 1
    # classify non-found note-ons
    for (t, f, r) in N:
        if t in used: continue
        gi = np.searchsorted(onset_t, t + 1e-9) - 1
        near = None
        if gi >= 0 and t - gs[gi]['on'] <= 150 and t <= gs[gi]['off'] + 150: near = gs[gi]
        if near is not None and any(abs(cents(f, hz(m))) < 50 for _, _, m in near['notes']): out['extra'] += 1
        elif near is not None: out['wrong_init'] += 1
        else:
            # right pitch of a note that is sounding right now (a repick of a ringing note not in the reference): not spurious
            snd = [m for a, b, m in tr if a - 30 <= t <= b + 60]
            if any(abs(cents(f, hz(m))) < 50 for m in snd): out['extra'] += 1
            else: out['spurious'] += 1
    # wrong audible time
    bad = 0
    ends = np.array([b for a, b, m in tr]); starts = np.array([a for a, b, m in tr]); mids = np.array([hz(m) for a, b, m in tr])
    for i in range(len(T)):
        if not G[i]: continue
        t = T[i]
        if t < tr[0][0] - 50 or t > tr[-1][1] + 100: continue
        sel = (starts - 20 <= t) & (ends + 60 >= t)
        if not sel.any() or not any(abs(cents(F[i], p)) < 50 for p in mids[sel]): bad += 1
    out['wrong_ms'] = bad * 1000 * 64 / 44100.0
    return out

def pct(a, q): return float(np.percentile(a, q)) if len(a) else float('nan')
def pool(rs):
    P = {k: sum(r[k] for r in rs) for k in ('groups', 'found', 'missed', 'octave', 'wrong_init', 'spurious', 'extra', 'n_fast', 'found_fast', 'nev', 'wrong_ms', 'dur_min', 'slur_found')}
    lat = np.concatenate([r['lat'] for r in rs]) if rs else np.array([]); lf = np.concatenate([r['lat_fast'] for r in rs]) if rs else np.array([])
    P.update(recall=P['found'] / max(1, P['groups']), lat_med=pct(lat, 50), lat_p90=pct(lat, 90), lat_p99=pct(lat, 99),
             recall_fast=P['found_fast'] / max(1, P['n_fast']), lat_fast_med=pct(lf, 50), lat_fast_p90=pct(lf, 90),
             spur_pm=P['spurious'] / max(1e-9, P['dur_min']), wrong_s_pm=P['wrong_ms'] / 1000 / max(1e-9, P['dur_min']),
             precision=(P['found']) / max(1, P['found'] + P['wrong_init'] + P['spurious'] + P['extra']))
    return P
CATS = [('scales', lambda n: 'scales' in n), ('music', lambda n: 'music' in n), ('single', lambda n: 'singlenotes' in n),
        ('bend+vib', lambda n: 'Bend' in n or 'Vibrato' in n), ('palmmute', lambda n: 'PalmMute' in n), ('harmonics', lambda n: 'Harmonics' in n)]
def run(variant, sset='gtechs', players=None, excerpt=None):
    d = f'{ROOT}/build/fasttrack/out/{sset}/{variant}'; res = {}
    for csv in sorted(glob.glob(f'{ROOT}/build/stage5/sets/{sset}/*.csv')):
        name = os.path.basename(csv)[:-4]
        if players and not any(name.startswith(p + '_') for p in players): continue
        trp, evp = f'{d}/{name}.tr', f'{d}/{name}.ev'
        if not os.path.exists(trp): continue
        tr = load_truth(csv)
        if not tr: continue
        res[name] = score_take(np.fromfile(trp, dtype=np.float32).reshape(-1, 3), load_ev(evp), tr)
    return res
def fmt(P):
    return (f"groups={P['groups']:5d} recall={P['recall']:.3f} miss={P['missed']:4d} oct={P['octave']:3d} lat med/p90/p99={P['lat_med']:5.1f}/{P['lat_p90']:5.1f}/{P['lat_p99']:5.1f}"
            f" | fast n={P['n_fast']:4d} rec={P['recall_fast']:.3f} lat={P['lat_fast_med']:5.1f}/{P['lat_fast_p90']:5.1f}"
            f" | wrong_init={P['wrong_init']:4d} extra={P['extra']:4d} spur/min={P['spur_pm']:5.1f} wrong_s/min={P['wrong_s_pm']:5.2f}")
if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('variants', nargs='+'); ap.add_argument('--set', default='gtechs'); ap.add_argument('--players'); ap.add_argument('--json'); ap.add_argument('--cats', action='store_true')
    a = ap.parse_args(); pl = a.players.split(',') if a.players else None; allr = {}
    for v in a.variants:
        res = run(v, a.set, pl); P = pool(list(res.values())); allr[v] = {k: {kk: vv for kk, vv in r.items() if kk not in ('lat', 'lat_fast')} for k, r in res.items()}
        print(f"{v:22s} {fmt(P)}")
        if a.cats:
            for cn, f in CATS:
                rs = [r for n, r in res.items() if f(n)]
                if rs: print(f"   {cn:10s} {fmt(pool(rs))}")
    if a.json: json.dump(allr, open(a.json, 'w'), indent=1)
