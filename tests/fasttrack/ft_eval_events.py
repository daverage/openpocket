#!/usr/bin/env python3
"""Score a system's event log against FastTrack v1 annotations (physical actions), separately per kind of action.
  python3 tests/fasttrack/ft_eval_events.py GT_DIR OUT_DIR [--min-conf 0.5]
GT_DIR: recordings.csv + events.csv.  OUT_DIR: <recording>.ev files in the ft_synth / ft_ctrl_trace grammar (A = envelope attack, W N note-on, W L legato change, W V pitch update, W X 1 damp / 0 release, R pick evidence)
and <recording>.tr (t, f0, gate every 64 samples).  Annotations with confidence < min-conf are scored separately and not in the headline.
Windows (ms around the annotated ACOUSTIC onset; the annotation timing is only as good as its confidence): match [-30, +120].
  PICK / REPICK / PALM_MUTE_PICK  expected NOTE_ON/REPICK: found = an attack in the window; pitch ok = within 50 cents; latency = attack - annotated onset (misses are infinite in the percentiles)
  HAMMER_ON / PULL_OFF            expected LEGATO_CHANGE with no new attack: legato = a W L / W V event, new_note = an attack (wrong articulation), none = nothing
  SLIDE / BEND / VIBRATO          expected PITCH_UPDATE: retriggered = an attack inside [t+30, t+duration]; (continuous pitch accuracy needs the pitch trace and the annotated end pitch)
  HARD_MUTE                       expected DAMP: found = W X within [t-10, +100]; latency
  NOISE                           expected NONE: false_note = an attack in [t-30, +120]
  Unlabelled attacks per minute   only on recordings with complete_annotation=1: attacks matching no PICK-like annotation (+-150 ms)
Fast runs are stratified by the ACTUAL annotated inter-onset interval to the previous pick (<40, 40-60, 60-80, 80-125, 125-200, >200 ms)."""
import sys, os, csv, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gt_format as G
def cents(a, b): return 1200 * np.log2(a / b) if a > 0 and b > 0 else 1e9
def hz(m): return 440.0 * 2 ** ((float(m) - 69) / 12)
def parse(path):
    ev = []
    for l in open(path):
        p = l.split()
        if not p: continue
        if p[0] == 'W': ev.append(('W' + p[2], float(p[1]), float(p[3]) if len(p) > 3 else 0.0))
        else: ev.append((p[0], float(p[1]), float(p[2]) if len(p) > 2 else 0.0))
    return ev
def pc(v, q):
    a = np.sort(np.array([np.inf if x is None else x for x in v]));
    return float(a[min(len(a) - 1, int(np.ceil(q / 100 * (len(a) - 1))))]) if len(a) else float('nan')
def main(gt, out, minconf=0.5):
    recs = {r['recording']: r for r in csv.DictReader(open(f'{gt}/recordings.csv'))}; evs = collections.defaultdict(list)
    for e in csv.DictReader(open(f'{gt}/events.csv')): evs[e['recording']].append(e)
    R = collections.defaultdict(list); low = collections.Counter(); unl = 0; mins = 0.0
    for name, es in evs.items():
        p = f'{out}/{name}.ev'
        if not os.path.exists(p): continue
        sy = parse(p); att = [(t, f) for k, t, f in sy if k == 'A']; leg = [t for k, t, f in sy if k in ('WL', 'WV')]; dmp = [(t, f) for k, t, f in sy if k == 'WX']
        picks = [float(e['t_ms']) for e in es if e['action'] in G.PICK_LIKE]; prev = None
        used = set(); strum = {}                       # strum groups: pick-like events within 40 ms of the group's first event are ONE musical event for a one-voice system
        grp_t = None; grp = []
        for e in es:
            if e['action'] in G.PICK_LIKE:
                t = float(e['t_ms'])
                if grp_t is not None and t - grp_t < 40: grp.append(e)
                else: grp = [e]; grp_t = t
                strum[id(e)] = grp
        for e in es:
            t = float(e['t_ms']); a = e['action']; c = float(e['confidence'])
            if c < minconf: low[a] += 1; continue
            if a in G.PICK_LIKE:
                g = strum[id(e)]
                if g[0] is not e: R['strum_member'].append(dict(found=any(ta for ta, f in att if t - 30 <= ta <= t + 120))); continue   # later members of a strum: reported, not in the headline
                ioi = None if prev is None else t - prev; prev = t
                m = [(ta, f) for ta, f in att if t - 30 <= ta <= t + 120]
                pits = [hz(x['pitch_midi']) for x in g if x['pitch_midi'] not in ('', 'None')]
                ok = [ta for ta, f in m if (not pits or any(abs(cents(f, p)) < 50 for p in pits))]
                R[a].append(dict(found=bool(ok), lat=(ok[0] - t) if ok else None, wrong=bool(m and not ok), bin=G.ioi_bin(ioi) if ioi is not None else '>200 ms', chord=len(g) > 1))
                if ok: used.add(ok[0])
            elif a in ('HAMMER_ON', 'PULL_OFF'):
                R[a].append(dict(legato=any(t - 30 <= x <= t + 120 for x in leg), new_note=any(t - 30 <= ta <= t + 120 for ta, f in att)))
            elif a in ('SLIDE', 'BEND', 'VIBRATO'):
                d = float(e['duration_ms']) if e['duration_ms'] not in ('', 'None') else 300.0
                R[a].append(dict(retrig=any(t + 30 <= ta <= t + max(60.0, d) for ta, f in att)))
            elif a == 'HARD_MUTE':
                m = [td for td, f in dmp if t - 10 <= td <= t + 100]; R[a].append(dict(found=bool(m), lat=(m[0] - t) if m else None))
            elif a == 'NOISE':
                R[a].append(dict(false_note=any(t - 30 <= ta <= t + 120 for ta, f in att)))
        if recs[name]['complete_annotation'] == '1' and picks:
            pk = np.array(picks); unl += sum(1 for ta, f in att if not np.any(np.abs(pk - ta) <= 150)); mins += (max(picks) - min(picks)) / 60000.0
    lines = []
    for a in G.PICK_LIKE:
        r = R.get(a)
        if not r: continue
        lat = [x['lat'] for x in r]; lines.append(f"{a:15s} n={len(r):5d} found {np.mean([x['found'] for x in r]):6.1%}  wrong-pitch attack {np.mean([x['wrong'] for x in r]):5.1%}  latency med {np.median([x for x in lat if x is not None]) if any(x is not None for x in lat) else float('nan'):5.1f}  p90* {pc(lat,90):6.1f} ms")
        for b in G.IOI_LABELS:
            rb = [x for x in r if x['bin'] == b]
            if len(rb) >= 5: lines.append(f"    previous pick {b:22s} n={len(rb):5d} found {np.mean([x['found'] for x in rb]):6.1%}")
    if R.get('HAMMER_ON') or R.get('PULL_OFF'):
        for a in ('HAMMER_ON', 'PULL_OFF'):
            r = R.get(a)
            if r: lines.append(f"{a:15s} n={len(r):5d} legato event {np.mean([x['legato'] for x in r]):6.1%}  treated as a new note {np.mean([x['new_note'] for x in r]):6.1%}")
    for a in ('SLIDE', 'BEND', 'VIBRATO'):
        r = R.get(a)
        if r: lines.append(f"{a:15s} n={len(r):5d} retriggered inside the gesture {np.mean([x['retrig'] for x in r]):6.1%}")
    r = R.get('strum_member')
    if r: lines.append(f"(strum members after the first note of a chord, not scored as separate picks: {len(r)})")
    r = R.get('HARD_MUTE')
    if r: lines.append(f"HARD_MUTE       n={len(r):5d} damp event {np.mean([x['found'] for x in r]):6.1%}")
    r = R.get('NOISE')
    if r: lines.append(f"NOISE           n={len(r):5d} false note {np.mean([x['false_note'] for x in r]):6.1%}")
    if mins: lines.append(f"unlabelled attacks per minute (complete annotations, {mins:.0f} min): {unl/mins:.1f}")
    if low: lines.append(f"excluded for confidence < {minconf}: {dict(low)}")
    return lines
if __name__ == '__main__':
    gt, out = sys.argv[1], sys.argv[2]; mc = float(sys.argv[sys.argv.index('--min-conf') + 1]) if '--min-conf' in sys.argv else 0.5
    print('\n'.join(main(gt, out, mc)))
