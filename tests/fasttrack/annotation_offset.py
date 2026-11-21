#!/usr/bin/env python3
"""How far is the annotated onset from the audio flux pick?  (docs/FASTTRACK-GROUND-TRUTH.md section 2/3)  Guitar-TECHS single-note groups, nearest flux pick within +-150 ms; and the flux picks per annotated note in the scales.
Needs build/fasttrack/early/*.txt.   python3 tests/fasttrack/annotation_offset.py"""
import sys, os, glob, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ft_score as S
ROOT = S.ROOT; off = collections.defaultdict(list); bytake = collections.defaultdict(list); allv = []; per = collections.Counter(); extra = []
for tp in sorted(glob.glob(ROOT + '/build/fasttrack/early/*.txt')):
    name = os.path.basename(tp)[:-4]; tr = S.load_truth(f'{ROOT}/build/stage5/sets/gtechs/{name}.csv')
    if not tr: continue
    gs = S.groups(tr); picks = np.array([float(l.split()[1]) for l in open(tp) if l.startswith('P ')])
    if not len(picks): continue
    for g in gs:
        m = np.where(np.abs(picks - g['on']) <= 150)[0]
        if 'scales' in name:
            per[len(m)] += 1
            if len(m) >= 2:
                j = m[np.argmin(np.abs(picks[m] - g['on']))]; extra += [picks[k] - picks[j] for k in m if k != j]
        if len(g['notes']) != 1 or not len(m): continue
        j = m[np.argmin(np.abs(picks[m] - g['on']))]; v = g['on'] - picks[j]; off[int(g['notes'][0][2] // 12)].append(v); bytake[name].append(v); allv.append(v)
print('annotation minus nearest flux pick, single-note groups: n=%d median %.0f  p10 %.0f p90 %.0f ms' % (len(allv), np.median(allv), np.percentile(allv, 10), np.percentile(allv, 90)))
print('by octave of the annotated note (MIDI//12): n, median ms:', {k: (len(v), round(float(np.median(v)))) for k, v in sorted(off.items())})
tk = [(np.median(v), np.percentile(v, 90) - np.percentile(v, 10)) for v in bytake.values() if len(v) > 20]
print('per-take median offset: min %.0f max %.0f ms; within-take p10-p90 spread, median %.0f ms' % (min(t[0] for t in tk), max(t[0] for t in tk), np.median([t[1] for t in tk])))
tot = sum(per.values()); print('scales: flux picks within +-150 ms of an annotated note:', {k: f'{v/tot:.0%}' for k, v in sorted(per.items())})
eo = np.array(extra); print('extra picks relative to the primary pick: n=%d, earlier %.0f%%, median %.0f ms' % (len(eo), 100 * np.mean(eo < 0), np.median(eo)))
