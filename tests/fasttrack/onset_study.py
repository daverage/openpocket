#!/usr/bin/env python3
"""What distinguishes a genuine pick from a false spectral-flux pick?  (host only; descriptive, NOT a rule)  Needs build/fasttrack/early/*.txt (P lines).
A flux pick is GENUINE if an annotated group onset lies within [-15, +45] ms of it (the flux frame can lead or lag the annotation).  Otherwise it is classified by what the annotation says is
sounding: note-end (within 120 ms after an annotated note's end), in-note (an annotated note is sounding), no-note.  Technique takes are labelled by file name.
Features per pick: flux strength, flux over the running average, the weak flag, level rise (fast level / its 8-frame minimum), level relative to the 150 ms peak, tracker confidence, waveform repeat at the tracker's pitch.
AUC = probability a random genuine pick scores higher than a random false pick on that feature (0.5 = no information)."""
import sys, os, glob, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ft_score as S
ROOT = S.ROOT
def auc(pos, neg):
    if not len(pos) or not len(neg): return float('nan')
    a = np.concatenate([pos, neg]); r = a.argsort().argsort() + 1.0
    return float((r[:len(pos)].sum() - len(pos) * (len(pos) + 1) / 2) / (len(pos) * len(neg)))
rows = []
for tp in sorted(glob.glob(ROOT + '/build/fasttrack/early/*.txt')):
    name = os.path.basename(tp)[:-4]; tr = S.load_truth(f'{ROOT}/build/stage5/sets/gtechs/{name}.csv')
    if not tr: continue
    on = np.array([a for a, b, m in tr]); off = np.array([b for a, b, m in tr]); gon = np.array([g['on'] for g in S.groups(tr)])
    for l in open(tp):
        p = l.split()
        if p[0] != 'P': continue
        t, flux, weak, favg, lr, conf, mono, dm, pk = float(p[1]), float(p[2]), int(p[3]), float(p[4]), float(p[5]), float(p[6]), float(p[7]), float(p[8]), float(p[9])
        if t < tr[0][0] - 200 or t > tr[-1][1] + 200: continue
        genuine = np.any((gon >= t - 45) & (gon <= t + 15))
        sounding = np.any((on <= t) & (off >= t)); nend = np.any((off <= t) & (off >= t - 120))
        if genuine:
            cat = 'genuine'; quiet = np.any((on < t - 45) & (off > t))        # a note from before still rings: a repick of a ringing string
        else: cat = 'note-end' if nend else 'in-note' if sounding else 'no-note'; quiet = False
        rows.append(dict(name=name, cat=cat, ring=quiet, weak=weak, flux=flux, fr=flux / (favg + 1e-6), lr=lr, conf=conf, dm=dm, pk=pk))
print(f'{len(rows)} flux picks')
cnt = collections.Counter((r['cat'], r['weak']) for r in rows)
print('counts (category, weak flag):', {f'{k[0]}/{"weak" if k[1] else "strong"}': v for k, v in sorted(cnt.items())})
tech = collections.Counter(r['name'].split('_')[1] + '/' + r['name'].split('_')[2] for r in rows if r['cat'] != 'genuine' and 'techniques' in r['name'])
print('false picks in technique takes:', dict(tech))
feats = [('flux', 'flux strength'), ('fr', 'flux / running average'), ('lr', 'level rise (fast level / 8-frame min)'), ('pk', 'level / 150 ms peak'), ('conf', 'tracker confidence'), ('dm', 'waveform non-repeat at mono pitch (low = periodic)')]
for label, sel in (('ALL picks', lambda r: True), ('weak-flag picks only', lambda r: r['weak']), ('strong-flag picks only', lambda r: not r['weak']), ('picks of ringing strings (genuine repicks vs false in-note/note-end)', lambda r: r['cat'] in ('in-note', 'note-end') or (r['cat'] == 'genuine' and r['ring']))):
    pos = [r for r in rows if sel(r) and r['cat'] == 'genuine']; neg = [r for r in rows if sel(r) and r['cat'] != 'genuine']
    print(f'\n{label}: genuine {len(pos)}, false {len(neg)} ({len(neg)/max(1,len(pos)+len(neg)):.0%} of picks)   AUC per feature (>0.5: higher in genuine):')
    for k, nm in feats:
        a = auc(np.array([r[k] for r in pos]), np.array([r[k] for r in neg])); print(f'   {nm:52s} {a:.3f}   medians genuine {np.median([r[k] for r in pos]):.3g} false {np.median([r[k] for r in neg]):.3g}')

# ---- how far do the features get together?  (diagnostic ceiling for onset evidence, NOT a proposed rule): logistic regression, cross-validated by take
try:
    from sklearn.linear_model import LogisticRegression
    from sklearn.model_selection import GroupKFold
    from sklearn.metrics import roc_auc_score
    def vec(r): return [np.log1p(r['flux']), np.log(r['fr'] + 1e-3), np.log(r['lr'] + 1e-3), np.log(r['pk'] + 1e-3), r['conf'], r['dm'], r['weak']]
    X = np.array([vec(r) for r in rows]); y = np.array([r['cat'] == 'genuine' for r in rows]); g = np.array([r['name'] for r in rows]); pr = np.zeros(len(y))
    for tr_i, te_i in GroupKFold(5).split(X, y, g):
        m = LogisticRegression(max_iter=500).fit(X[tr_i], y[tr_i]); pr[te_i] = m.predict_proba(X[te_i])[:, 1]
    print(f'\nall features together (logistic, 5-fold by take): AUC {roc_auc_score(y, pr):.3f}')
    for fpr_t in (0.05, 0.10, 0.20):
        thr = np.percentile(pr[~y], 100 * (1 - fpr_t)); print(f'   at {fpr_t:.0%} of false picks admitted: {np.mean(pr[y] > thr):.0%} of genuine picks kept')
    ring = np.array([(r['cat'] in ('in-note', 'note-end')) or (r['cat'] == 'genuine' and r['ring']) for r in rows])
    print(f'   ringing-string picks only: AUC {roc_auc_score(y[ring], pr[ring]):.3f}')
except ImportError:
    print('\n(sklearn not installed: combination ceiling not computed)')
