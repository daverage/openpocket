#!/usr/bin/env python3
"""Same audio, same scoring, side by side: pitch delivered / note event recognised / envelope attack (fast and all) with missed picks counted in the percentiles, and false attacks per minute.
  python3 tests/archive/fasttrack/ft_compare.py VARIANT[=label] ... [--set gtechs] [--cat scales|music|...]"""
import sys, argparse
sys.path.insert(0, __file__.rsplit('/', 1)[0]); import ft_attrib as A, ft_score as S
ap = argparse.ArgumentParser(); ap.add_argument('variants', nargs='+'); ap.add_argument('--set', default='gtechs'); ap.add_argument('--cat'); a = ap.parse_args()
pick = dict(S.CATS)[a.cat] if a.cat else None
hdr = f"{'variant':26s} | {'pitch right':>11s} {'fast':>6s} | {'event':>6s} {'fast':>6s} | {'attack':>6s} {'fast':>6s} {'med':>5s} {'p90*':>6s} {'fast med':>8s} {'p90*':>6s} | false attacks/min: wrong-init spurious"
print(hdr)
for v in a.variants:
    name, _, lab = v.partition('='); rows = A.report3(name, a.set, None, pick); f = [r for r in rows if r['fast']]
    def share(rs, k): return sum(r[k] is not None for r in rs) / max(1, len(rs))
    def med(rs, k): 
        g = sorted(r[k] for r in rs if r[k] is not None); return g[len(g) // 2] if g else float('nan')
    c, m = A.fa_total(name, a.set, pick)
    print(f"{(lab or name):26s} | {share(rows,'pitch'):11.1%} {share(f,'pitch'):6.1%} | {share(rows,'event'):6.1%} {share(f,'event'):6.1%} | {share(rows,'attack'):6.1%} {share(f,'attack'):6.1%} {med(rows,'attack'):5.1f} {A.pct_cens([r['attack'] for r in rows],90):6.1f} {med(f,'attack'):8.1f} {A.pct_cens([r['attack'] for r in f],90):6.1f} | {c['wrong_init']/m:15.1f} {c['spurious']/m:8.1f}")
