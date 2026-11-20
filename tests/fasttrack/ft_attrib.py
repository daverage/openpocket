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
