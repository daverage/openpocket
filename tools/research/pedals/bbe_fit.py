"""Fit the BBE structure to the measured magnitudes: x -> Linkwitz-Riley 4th-order split at f1 and f2 -> low band delayed tL, mid band delayed tM -> band gains gL, gM, gH -> sum.
The crossovers and delays are shared by all settings; the gains are per setting. Analytic complex responses (no audio): LR4 = two cascaded 2nd-order Butterworth sections.
   python3 bbe_fit.py bbe_mag.json"""
import sys, os, json, numpy as np
from scipy import optimize, signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
d = json.load(open(sys.argv[1])); f = np.array(d['freq']); S = d['settings']; keys = sorted(S, key=lambda k: S[k]['proc']); w = 2 * np.pi * f
def lr4(fc, high):
    b, a = signal.butter(2, 2 * np.pi * fc, 'high' if high else 'low', analog=True); h = signal.freqs(b, a, w)[1]; return h * h
def bands(f1, f2):
    lp1, hp1, lp2, hp2 = lr4(f1, 0), lr4(f1, 1), lr4(f2, 0), lr4(f2, 1); return lp1, hp1 * lp2, hp1 * hp2        # low, mid, high (they sum to an all-pass)
def model(p, g):
    f1, f2, tL, tM = p; L, M, Hh = bands(f1, f2); return g[0] * L * np.exp(-1j * w * tL) + g[1] * M * np.exp(-1j * w * tM) + g[2] * Hh
nS = len(keys); target = np.array([S[k]['mag'] for k in keys])
def unpack(x): return x[:4], x[4:].reshape(nS, 3)
def res(x):
    p, G = unpack(x); out = []
    for i in range(nS): out.append(20 * np.log10(np.abs(model(p, G[i])) + 1e-9) - target[i])
    return np.concatenate(out)
best = None
for f1 in (150, 300, 500):
    for f2 in (1500, 2500, 4000):
        x0 = np.concatenate([[f1, f2, 0.0012, 0.0004], np.tile([1.0, 1.0, 1.0], nS)])
        lo = np.concatenate([[60, 800, 0.0, 0.0], np.full(nS * 3, 0.1)]); hi = np.concatenate([[700, 6000, 0.004, 0.003], np.full(nS * 3, 3.0)])
        r = optimize.least_squares(res, x0, bounds=(lo, hi), max_nfev=400)
        if best is None or r.cost < best.cost: best = r
p, G = unpack(best.x); rms = np.sqrt(np.mean(best.fun ** 2))
print('shared: f1 %.0f Hz, f2 %.0f Hz, low-band delay %.3f ms, mid-band delay %.3f ms; rms error %.2f dB over %d settings' % (p[0], p[1], p[2] * 1e3, p[3] * 1e3, rms, nS))
for i, k in enumerate(keys): print('  LO/PROC %-6s gains L %.3f M %.3f H %.3f   rms %.2f dB' % (k, *G[i], np.sqrt(np.mean(res(best.x)[i * len(f):(i + 1) * len(f)] ** 2))))
json.dump(dict(f1=p[0], f2=p[1], tL=p[2], tM=p[3], settings={k: dict(lo=S[k]['lo'], proc=S[k]['proc'], g=G[i].tolist()) for i, k in enumerate(keys)}), open(sys.argv[1].replace('.json', '_fit.json'), 'w'))
idx = [np.argmin(abs(f - x)) for x in (40, 100, 200, 400, 700, 1000, 1500, 2500, 4000, 7000, 12000, 17000)]
print('freq         ' + ' '.join(f'{f[i]:6.0f}' for i in idx))
for i, k in enumerate(keys):
    fit = 20 * np.log10(np.abs(model(p, G[i])) + 1e-9); print('%-6s meas  ' % k + ' '.join(f'{target[i][j]:6.1f}' for j in idx)); print('%-6s model ' % k + ' '.join(f'{fit[j]:6.1f}' for j in idx))
