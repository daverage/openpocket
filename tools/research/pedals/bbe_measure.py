"""Magnitude response of the BBE 882i NAM captures (offline measurement of the real unit) on a dense log grid: a 5 s log chirp at -30 dBFS through the A2-Full network.
   python3 bbe_measure.py OUT.json CAPTURE.nam [...]   (the setting is read from the file name: 'LO CONT a PROC b')"""
import sys, os, re, json, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *
def chirp_response(net, db=-30.0, T=5.0):
    n = int(T * SR); t = np.arange(n) / SR; f0, f1 = 20.0, 22000.0; k = np.log(f1 / f0); x = 10 ** (db / 20) * np.sin(2 * np.pi * f0 * T / k * (np.exp(t / T * k) - 1))
    y = nam_a2.run(net, np.concatenate([x, np.zeros(SR // 2)]))[:n + SR // 2]; xx = np.concatenate([x, np.zeros(SR // 2)]); N = 1 << 19
    X = np.fft.rfft(xx, N); Y = np.fft.rfft(y, N); f = np.fft.rfftfreq(N, 1 / SR); H = Y / (X + 1e-12); return f, H
if __name__ == '__main__':
    out = sys.argv[1]; res = {}
    grid = np.geomspace(30, 18000, 90)
    for p in sys.argv[2:]:
        m = re.search(r'LO CONT (\d+) PROC (\d+)', os.path.basename(p)); net = capture(p, 'full'); f, H = chirp_response(net)
        mag = np.interp(grid, f, 20 * np.log10(np.abs(H) + 1e-9)); ph = np.interp(grid, f, np.unwrap(np.angle(H)))
        res[f'{m.group(1)}/{m.group(2)}'] = dict(lo=int(m.group(1)), proc=int(m.group(2)), mag=mag.tolist()); print(p, 'ok')
    json.dump(dict(freq=grid.tolist(), settings=res), open(out, 'w'))
