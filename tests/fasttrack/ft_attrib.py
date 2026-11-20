#!/usr/bin/env python3
"""Where is each physical pick lost?  Needs ft_synth output (R/H/S/U/W/A lines) for a set.   python3 tests/fasttrack/ft_attrib.py VARIANT [--set gtechs]
Each truth group (strum = onsets within 40 ms) is followed down the legacy chain and put in exactly one bin:
  attack_ok      a synth ENVELOPE ATTACK at a pitch of the group within [on-30 ms, end]  (what the player hears as a note)
  glide_only     the voice reaches the pitch, but with no attack (slur / FOLLOW / continuous pitch follow)
  lost_F         tracker emitted the note-on at a right pitch, adapter FOLLOW consumed it (no attack)
  lost_PQ        tracker emitted it, adapter pending-flag collapse (P: overwritten, Q: released in the same 32-sample block)
  lost_<code>    wrapper veto in the window: S amplitude slur, C 35 ms confirm hold, D dead pick, M mute-thump undo, H hold undo
  lost_layer     a pick began (R) but the note layer never admitted a note (acquisition: stability/wait/fundamental check/expiry)
  lost_noR       no pick evidence at all in the window (the layer saw no onset)
  wrong_pitch    an attack sounded, but not at a pitch of the group
 end = min(next group onset, group off, on+600 ms).  Pitch within 50 cents.  Truth timing caveats as in docs/FASTTRACK-BENCHMARKS.md."""
import sys, os, glob, argparse, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ft_score as S

def parse(path):
    ev = []
    for l in open(path):
        p = l.split()
        if not p: continue
        if p[0] == 'W': ev.append(('W' + p[2], float(p[1]), float(p[3])))
        else: ev.append((p[0], float(p[1]), float(p[2]) if len(p) > 2 else 0.0))
    return ev

def attribute(ev, tr, trace=None):
    gs = S.groups(tr); res = []
    t_ev = np.array([t for k, t, f in ev]); 
    for gi, g in enumerate(gs):
        ps = [S.hz(m) for _, _, m in g['notes']]
        nxt = gs[gi + 1]['on'] if gi + 1 < len(gs) else 1e12
        lo, hi = g['on'] - 30, min(nxt, g['off'], g['on'] + 600)
        win = [(k, t, f) for k, t, f in ev if lo <= t <= hi]
        ok = lambda f: any(abs(S.cents(f, p)) < 50 for p in ps)
        att = [(t, f) for k, t, f in win if k == 'A']
        good = [t for t, f in att if ok(f)]
        fast = gi > 0 and g['on'] - gs[gi - 1]['on'] < 150
        if good: res.append(('attack_ok', good[0] - g['on'], fast)); continue
        if att: res.append(('wrong_pitch', None, fast)); continue
        Nok = [t for k, t, f in win if k == 'WN' and ok(f)]
        kinds = collections.Counter(k for k, t, f in win)
        if Nok:
            for c in ('WF', 'WP', 'WQ'):
                if kinds[c]: res.append(('lost_' + c[1], None, fast)); break
            else: res.append(('lost_adapter_other', None, fast))
            continue
        glide = False
        if trace is not None:
            T = trace[:, 0]; F = trace[:, 1]; G = trace[:, 2] > 0.5; a, b = np.searchsorted(T, lo), np.searchsorted(T, hi)
            right = lambda i: G[i] and any(abs(S.cents(F[i], p)) < 50 for p in ps)
            if not right(a) and any(all(right(j) for j in range(i, min(i + 3, len(T)))) for i in range(a, max(a, b - 2))): glide = True
        if glide and not any(kinds[c] for c in ('WS', 'WC', 'WD', 'WM', 'WH')): res.append(('glide_only', None, fast)); continue
        for c in ('WS', 'WC', 'WD', 'WM', 'WH'):
            if kinds[c]: res.append(('lost_' + c[1], None, fast)); break
        else:
            # glide only: a slur event or the voice following at the right pitch without an attack
            if any(k == 'WL' and ok(f) for k, t, f in win): res.append(('glide_only', None, fast))
            elif kinds['R'] or kinds['H']: res.append(('lost_layer', None, fast))
            else: res.append(('lost_noR', None, fast))
    return res, len(gs)

def run(variant, sset='gtechs', players=None, pick=None):
    d = f'{S.ROOT}/build/fasttrack/out/{sset}/{variant}'; tot = collections.Counter(); fast = collections.Counter(); lat = []; latf = []; n = 0
    for csv in sorted(glob.glob(f'{S.ROOT}/build/stage5/sets/{sset}/*.csv')):
        name = os.path.basename(csv)[:-4]; evp = f'{d}/{name}.ev'
        if players and not any(name.startswith(p + '_') for p in players): continue
        if pick and not pick(name): continue
        if not os.path.exists(evp): continue
        tr = S.load_truth(csv)
        if not tr: continue
        trp = f'{d}/{name}.tr'; r, ng = attribute(parse(evp), tr, np.fromfile(trp, dtype=np.float32).reshape(-1, 3) if os.path.exists(trp) else None); n += ng
        for b, l, f in r:
            tot[b] += 1; fast[b] += f
            if b == 'attack_ok': lat.append(l); latf.extend([l] if f else [])
    return n, tot, fast, np.array(lat), np.array(latf)

def report(variant, sset, players=None, pick=None):
    n, tot, fast, lat, latf = run(variant, sset, players, pick); nf = sum(fast.values())
    print(f'{variant} on {sset}: {n} picks (groups), {nf} fast (<150 ms after the previous)')
    order = ['attack_ok', 'glide_only', 'wrong_pitch', 'lost_F', 'lost_P', 'lost_Q', 'lost_S', 'lost_C', 'lost_D', 'lost_M', 'lost_H', 'lost_layer', 'lost_noR', 'lost_adapter_other']
    for b in order:
        if tot[b]: print(f'  {b:20s} {tot[b]:6d} {100*tot[b]/n:5.1f} %   fast {fast[b]:4d} ({100*fast[b]/max(1,nf):5.1f} %)')
    if len(lat): print(f'  attack latency (annotated onset -> envelope attack) med/p90/p99 = {np.median(lat):.1f}/{np.percentile(lat,90):.1f}/{np.percentile(lat,99):.1f} ms' + (f'; fast med/p90 = {np.median(latf):.1f}/{np.percentile(latf,90):.1f}' if len(latf) else ''))
    return n, tot

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('variant'); ap.add_argument('--set', default='gtechs'); ap.add_argument('--players')
    ap.add_argument('--cats', action='store_true')
    a = ap.parse_args(); report(a.variant, a.set, a.players.split(',') if a.players else None)
    if a.cats:
        for cn, f in S.CATS:
            print('--', cn); report(a.variant, a.set, a.players.split(',') if a.players else None, f)

# ---------------------------------------------------------------------------------------------------------------------------------------------
# Three separate questions per pick (brief of the refactor): was the right PITCH delivered, was the note EVENT recognised, was an ENVELOPE ATTACK made.
def layers(ev, tr, trace):
    gs = S.groups(tr); out = []
    T = trace[:, 0]; F = trace[:, 1]; G = trace[:, 2] > 0.5
    for gi, g in enumerate(gs):
        ps = [S.hz(m) for _, _, m in g['notes']]
        nxt = gs[gi + 1]['on'] if gi + 1 < len(gs) else 1e12
        lo, hi = g['on'] - 30, min(nxt, g['off'], g['on'] + 600)
        ok = lambda f: any(abs(S.cents(f, p)) < 50 for p in ps)
        fast = gi > 0 and g['on'] - gs[gi - 1]['on'] < 150
        win = [(k, t, f) for k, t, f in ev if lo <= t <= hi]
        a, b = np.searchsorted(T, lo), np.searchsorted(T, hi)
        right = lambda i: G[i] and any(abs(S.cents(F[i], p)) < 50 for p in ps)
        already = a < len(T) and right(a)
        tp = None
        for i in range(a, max(a, b - 2)):
            if all(right(j) for j in range(i, min(i + 3, len(T)))): tp = T[i]; break
        te = next((t for k, t, f in win if k in ('WN', 'WL') and ok(f)), None)
        ta = next((t for k, t, f in win if k == 'A' and ok(f)), None)
        fa = [t for k, t, f in win if k == 'A' and not ok(f)]
        out.append(dict(on=g['on'], fast=fast, pitch=None if tp is None else tp - g['on'], event=None if te is None else te - g['on'], attack=None if ta is None else ta - g['on'],
                        wrong_attack=bool(fa and ta is None), repeated=already))
    return out

def pct_cens(v, q):
    """percentile of latency with misses (None) counted as +inf: reports the real tail, not only the notes that worked"""
    a = np.sort(np.array([np.inf if x is None else x for x in v])); return float(a[min(len(a) - 1, int(np.ceil(q / 100 * (len(a) - 1))))]) if len(a) else float('nan')

def false_attacks(ev, tr):
    """envelope attacks that are not the first right-pitch attack of a pick: retrig = right pitch again inside a pick's window, wrong_init = wrong pitch within 150 ms of a pick, spurious = elsewhere"""
    gs = S.groups(tr); on = np.array([g['on'] for g in gs]); used = set(); c = collections.Counter()
    for gi, g in enumerate(gs):
        ps = [S.hz(m) for _, _, m in g['notes']]; nxt = gs[gi + 1]['on'] if gi + 1 < len(gs) else 1e12
        for k, t, f in ev:
            if k == 'A' and g['on'] - 30 <= t <= min(nxt, g['off'], g['on'] + 600) and any(abs(S.cents(f, p)) < 50 for p in ps): used.add(t); break
    for k, t, f in ev:
        if k != 'A' or t in used: continue
        gi = np.searchsorted(on, t + 1e-9) - 1
        if gi >= 0 and t - gs[gi]['on'] <= 150 and t <= gs[gi]['off'] + 150:
            c['retrig' if any(abs(S.cents(f, S.hz(m))) < 50 for _, _, m in gs[gi]['notes']) else 'wrong_init'] += 1
        else: c['spurious'] += 1
    return c, (tr[-1][1] - tr[0][0]) / 60000.0

def fa_total(variant, sset='gtechs', pick=None):
    d = f'{S.ROOT}/build/fasttrack/out/{sset}/{variant}'; c = collections.Counter(); mins = 0.0
    for csv in sorted(glob.glob(f'{S.ROOT}/build/stage5/sets/{sset}/*.csv')):
        name = os.path.basename(csv)[:-4]
        if pick and not pick(name): continue
        if not os.path.exists(f'{d}/{name}.ev'): continue
        tr = S.load_truth(csv)
        if not tr: continue
        cc, m = false_attacks(parse(f'{d}/{name}.ev'), tr); c += cc; mins += m
    return c, mins

def report3(variant, sset='gtechs', players=None, pick=None):
    rows = []
    d = f'{S.ROOT}/build/fasttrack/out/{sset}/{variant}'
    for csv in sorted(glob.glob(f'{S.ROOT}/build/stage5/sets/{sset}/*.csv')):
        name = os.path.basename(csv)[:-4]
        if players and not any(name.startswith(p + '_') for p in players): continue
        if pick and not pick(name): continue
        evp, trp = f'{d}/{name}.ev', f'{d}/{name}.tr'
        if not (os.path.exists(evp) and os.path.exists(trp)): continue
        tr = S.load_truth(csv)
        if tr: rows += layers(parse(evp), tr, np.fromfile(trp, dtype=np.float32).reshape(-1, 3))
    return rows

def fmt3(rows, label):
    n = len(rows); f = [r for r in rows if r['fast']]
    def one(rs, k): 
        v = [r[k] for r in rs]; got = [x for x in v if x is not None]
        return f"{len(got)/max(1,len(rs)):5.1%} med {np.median(got) if got else float('nan'):5.1f} p90* {pct_cens(v,90):6.1f} p99* {pct_cens(v,99):6.1f}"
    s = f'{label}: {n} picks ({len(f)} fast)\n'
    for k, nm in (('pitch', 'right pitch delivered   '), ('event', 'note event recognised  '), ('attack', 'envelope attack         ')):
        s += f'  {nm} all  {one(rows,k)}   | fast {one(f,k)}\n'
    return s + '  (p90*/p99* count a missed pick as infinite latency; "inf" means more than that share of picks never arrive)'
