#!/usr/bin/env python3
"""Reclassify the 18579 flux picks of onset_study.py without calling every unmatched pick false.  Output: build/fasttrack/flux_picks.csv (one row per pick, proposed class + evidence) and a table.
Classes (ordered rules, first match; every rule uses annotation context AND audio features; the label is a PROPOSAL with a confidence, to be confirmed by listening/plot review):
  genuine_aligned        an annotated group onset within [-15, +45] ms
  precursor_before_pick  a flux 15..120 ms BEFORE an annotated onset whose primary (nearest) pick is another flux: an acoustic event that precedes the pluck (fretting-hand placement, string contact, pick approach)
  late_extra_after_pick  a flux 0..120 ms AFTER the primary pick of an annotated onset (flux re-fire inside one attack, string ring-up)
  genuine_misaligned     an annotated onset within +-120 ms but no other pick nearer to it (the annotation timing is the offset, not the audio)
  note_end_transient     within 120 ms after an annotated note's end (release / damping / decay of that note)
  in_note_fluctuation    an annotated note sounds, the fast level is not rising (level rise < 1.5): decay, vibrato, bend or harmonic fluctuation
  in_note_rise           an annotated note sounds and the fast level rises >= 1.5: an UNANNOTATED energy increase (a repick, a second note, string noise) -> uncertain
  silence_noise          no annotated note near and the level before the pick was low
  unannotated_rise       no annotated note near, level rising >= 1.5 -> uncertain (real unlabelled pick? hexaphonic MIDI misses some playing)"""
import sys, os, glob, collections, csv
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ft_score as S
ROOT = S.ROOT; rows = []
for tp in sorted(glob.glob(ROOT + '/build/fasttrack/early/*.txt')):
    name = os.path.basename(tp)[:-4]; tr = S.load_truth(f'{ROOT}/build/stage5/sets/gtechs/{name}.csv')
    if not tr: continue
    on = np.array([a for a, b, m in tr]); off = np.array([b for a, b, m in tr]); gon = np.array([g['on'] for g in S.groups(tr)])
    picks = []
    for l in open(tp):
        p = l.split()
        if p[0] == 'P':
            t = float(p[1])
            if tr[0][0] - 200 <= t <= tr[-1][1] + 200: picks.append(dict(t=t, flux=float(p[2]), weak=int(p[3]), lr=float(p[5]), conf=float(p[6]), pk=float(p[9])))
    for r in picks:
        t = r['t']; d = gon - t; i = np.argmin(np.abs(d)); dn = float(d[i]) if len(gon) else 1e9
        r['dnear'] = dn; r['name'] = name; r['tech'] = name.split('_')[1] + ('/' + name.split('_')[2] if 'techniques' in name else '')
        sounding = bool(np.any((on <= t) & (off >= t))); nend = bool(np.any((off <= t) & (off >= t - 120)))
        r['sounding'] = sounding
        if -45 <= dn <= 15: r['cls'] = 'genuine_aligned'      # annotated onset 15 ms after .. 45 ms before the flux frame
        else: r['cls'] = None
    prim = {}                                          # annotated group index -> nearest pick index (the primary pick of that note)
    ts = np.array([q['t'] for q in picks]) if picks else np.array([])
    for gi, g in enumerate(gon):
        if len(ts):
            j = int(np.argmin(np.abs(ts - g)))
            if abs(ts[j] - g) <= 120: prim[gi] = j
    for k, r in enumerate(picks):
        if r['cls']: continue
        t = r['t']; gi = int(np.argmin(np.abs(gon - t))) if len(gon) else -1
        if gi >= 0 and abs(gon[gi] - t) <= 120:
            pj = prim.get(gi)
            if pj is not None and pj != k:
                r['cls'] = 'precursor_before_pick' if t < picks[pj]['t'] else 'late_extra_after_pick'
            else: r['cls'] = 'genuine_misaligned'
        elif bool(np.any((off <= t) & (off >= t - 120))): r['cls'] = 'note_end_transient'
        elif r['sounding']: r['cls'] = 'in_note_rise' if r['lr'] >= 1.5 else 'in_note_fluctuation'
        else: r['cls'] = 'unannotated_rise' if r['lr'] >= 1.5 else 'silence_noise'
    if picks:                                           # absolute level around each pick (RMS 20 ms after / 20 ms before): the layer's own gate is 0.003
        import soundfile as sf
        x, sr = sf.read(f'{ROOT}/build/stage5/sets/gtechs/{name}.wav', dtype='float32'); x = x if x.ndim == 1 else x[:, 0]; c2 = np.concatenate([[0], np.cumsum(x.astype(np.float64) ** 2)])
        def rms(a, b): a, b = max(0, int(a)), min(len(x), int(b)); return float(np.sqrt((c2[b] - c2[a]) / max(1, b - a)))
        for r in picks:
            i = r['t'] * sr / 1000.0; r['after'] = rms(i, i + 0.020 * sr); r['before'] = rms(i - 0.020 * sr, i)
            r['trend'] = r['after'] / (r['before'] + 1e-6)
            if r['cls'] in ('unannotated_rise', 'silence_noise', 'in_note_rise', 'in_note_fluctuation', 'note_end_transient') and r['after'] < 0.003: r['cls'] = 'below_gate_noise'
    rows += picks
os.makedirs(ROOT + '/build/fasttrack', exist_ok=True)
with open(ROOT + '/build/fasttrack/flux_picks.csv', 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=['name', 'tech', 't', 'cls', 'weak', 'flux', 'lr', 'conf', 'pk', 'dnear', 'sounding', 'after', 'before', 'trend']); w.writeheader()
    for r in rows: w.writerow({k: r[k] for k in w.fieldnames})
n = len(rows); c = collections.Counter(r['cls'] for r in rows)
print(f'{n} flux picks')
for k, v in c.most_common(): print(f'  {k:22s} {v:6d} {v/n:6.1%}')
un = [r for r in rows if r['cls'] != 'genuine_aligned']
print(f'\nunmatched by the +-(-15,+45) rule: {len(un)} = {len(un)/n:.0%}; of those, reclassified as:')
cu = collections.Counter(r['cls'] for r in un)
for k, v in cu.most_common(): print(f'  {k:22s} {v:6d} {v/len(un):6.1%}')
print('\nnearest annotated onset to the unmatched picks (ms, signed: + = annotation after the flux): percentiles 5/25/50/75/95 =', np.round(np.percentile([r['dnear'] for r in un if abs(r['dnear']) < 1000], [5, 25, 50, 75, 95]), 0))
print('\nby technique take:')
for tch in sorted({r['tech'] for r in rows}):
    rs = [r for r in rows if r['tech'] == tch]; cc = collections.Counter(r['cls'] for r in rs)
    print(f"  {tch:24s} n={len(rs):5d} " + ' '.join(f"{k[:14]}={v/len(rs):4.0%}" for k, v in cc.most_common(5)))

print('\nper class: median level rise (fast level / its 8-frame min), median level 20 ms after (linear), median after/before RMS:')
for k in sorted({r['cls'] for r in rows}):
    rs = [r for r in rows if r['cls'] == k]; print(f"  {k:22s} lr {np.median([r['lr'] for r in rs]):6.2f}  after {np.median([r['after'] for r in rs]):.4f}  trend {np.median([r['trend'] for r in rs]):6.2f}  weak {np.mean([r['weak'] for r in rs]):.0%}")
