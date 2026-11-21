"""Fit a Wiener-Hammerstein boost model to a NAM capture (offline): shelf -> asymmetric soft clip -> one-pole low-pass.
   y = LP( f(shelf(x) + b) - f(b) ),  shelf = g_lo*x + (g_hi-g_lo)*HP1(x, fc);  f(u) = kp*C(u/kp) for u>0, kn*C(u/kn) for u<0, C(z)=1.5z-0.5z^3 (|z|<=1), +-1 beyond.
   python3 fit_boost.py CAPTURE.nam [label]"""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scipy import signal, optimize
from common import *
def C(z): return np.where(np.abs(z) <= 1, 1.5 * z - 0.5 * z ** 3, np.sign(z))
def f(u, kp, kn): return np.where(u > 0, kp * C(u / kp), kn * C(u / kn))
def model(p, x):
    g_lo, g_hi, fc, b, kp, kn, fcp, gout = p
    bh, ah = signal.bilinear([1, 0], [1, 2 * np.pi * fc], fs=SR); hp = signal.lfilter(bh, ah, x)
    u = g_lo * x + (g_hi - g_lo) * hp
    v = f(u + b, kp, kn) - f(b, kp, kn)
    bl, al = signal.bilinear([2 * np.pi * fcp], [1, 2 * np.pi * fcp], fs=SR)
    return gout * signal.lfilter(bl, al, v)
def fit(net, label=''):
    x1 = owner_di(); sets = [(s, x1 * s) for s in (0.5, 1.0, 2.0)]
    refs = [run_nam(net, x) for _, x in sets]
    # the reference gain scale: NAM output units are the model's own; the engine output is compared in the same units
    tgt = [r for r in refs]
    freqs = (100, 300, 1000, 3000, 8000); g_small = 10 ** (tf_db(net, freqs, -50) / 20)         # small-signal gain shape in NAM units: input NAM = 2*engine -> engine-in gain = same ratio
    def res(lp):
        p = [np.exp(lp[0]), np.exp(lp[1]), np.exp(lp[2]), lp[3], np.exp(lp[4]), np.exp(lp[5]), np.exp(lp[6]), np.exp(lp[7])]
        out = []
        for (s, x), r in zip(sets, tgt):
            y = model(p, x * ENGINE_TO_NAM); out.append((y - r) / (np.sqrt(np.sum(r ** 2)) + 1e-12))
        return np.concatenate(out)
    best = None
    for start in ([0.3, 6, 2500, 0.0, 0.4, 0.3, 8000, 1.0], [1, 8, 1500, 0.1, 0.6, 0.4, 12000, 1.0], [0.1, 4, 4000, -0.05, 0.3, 0.5, 6000, 1.0]):
        x0 = np.array([np.log(start[0]), np.log(start[1]), np.log(start[2]), start[3], np.log(start[4]), np.log(start[5]), np.log(start[6]), np.log(start[7])])
        try: r = optimize.least_squares(res, x0, method='trf', max_nfev=60)
        except Exception as e: print('start failed', e); continue
        if best is None or r.cost < best.cost: best = r
    lp = best.x; p = [np.exp(lp[0]), np.exp(lp[1]), np.exp(lp[2]), lp[3], np.exp(lp[4]), np.exp(lp[5]), np.exp(lp[6]), np.exp(lp[7])]
    errs = [esr(r, model(p, x * ENGINE_TO_NAM)) for (s, x), r in zip(sets, refs)]
    print(label, 'params g_lo %.3f g_hi %.3f fc %.0f b %.3f kp %.3f kn %.3f fcp %.0f gout %.3f' % tuple(p), ' ESR at x0.5/x1/x2: %.3f %.3f %.3f' % tuple(errs))
    return p, errs
if __name__ == '__main__':
    fit(capture(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else sys.argv[1])
