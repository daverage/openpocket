#!/usr/bin/env python3
"""ENHANCE re-amp test: the SAME recorded DI played through the pedal for each setting, so the added signal can be isolated by subtraction (free playing cannot).
  python3 tests/enhance_reamp.py make OUT.wav            writes the test signal (pilot clicks + a DI excerpt of the owner's, 24-bit, identical L/R)
  python3 tests/enhance_reamp.py measure OFF.wav A.wav [B.wav ...] [--labels warm,both,bright]
Procedure: play OUT.wav from the computer into the pedal's input (use a pad or the interface's instrument level so the pedal's recorded OUTPUT of the off pass peaks about -14..-10 dBFS like your guitar),
record the pedal's output once per setting (ENHANCE off, then each setting), same level each time, same start (the pilot clicks align them), LOUDER 0.
measure: aligns each pass to OFF by the pilot clicks, fits one gain (the pedal's own gain/trim), subtracts, and reports the added signal re the off pass (dB), its spectrum by band, and the
second-harmonic change of single notes. A self test (no pedal) is `python3 tests/enhance_reamp.py selftest`."""
import sys, os, subprocess
import numpy as np, soundfile as sf
from scipy import signal
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..')); SR = 44100
def make(out):
    d, _ = sf.read(ROOT + '/My Audio/scale + duo + power chords di.wav', dtype='float64'); d = d[:, 0] if d.ndim > 1 else d; d = d[:SR * 12]
    d = d / np.abs(d).max() * 10 ** (-14 / 20)                           # peak -14 dBFS
    pilot = np.zeros(SR // 2); pilot[1000] = 0.9; pilot[1100] = -0.9; pilot[1300] = 0.9           # three clicks: a sharp, aligned fingerprint
    sig = np.concatenate([np.zeros(SR), pilot, np.zeros(SR), d, np.zeros(SR), pilot, np.zeros(SR)])
    sf.write(out, np.stack([sig, sig], 1), SR, subtype='PCM_24'); print('wrote', out, round(len(sig) / SR, 1), 's; DI peak -14 dBFS, pilot clicks 0.9')
def load(p):
    x, sr = sf.read(p, dtype='float64'); assert sr == SR, f'{p}: {sr} Hz'; return x[:, 0] if x.ndim > 1 else x
def lag_of(ref, x):
    n = min(len(ref), len(x), SR * 6); c = signal.correlate(x[:n], ref[:n], mode='full', method='fft'); return int(np.argmax(np.abs(c))) - (n - 1)
def bands(s):
    from numpy.fft import rfft
    P = np.abs(rfft(s * np.hanning(len(s)))) ** 2; fr = np.fft.rfftfreq(len(s), 1 / SR); tot = P[(fr > 60) & (fr < 16000)].sum()
    return [10 * np.log10(P[(fr >= a) & (fr < b)].sum() / tot + 1e-14) for a, b in ((60, 150), (150, 400), (400, 1200), (1200, 2400), (2400, 4800), (4800, 9600))]
def measure(off_p, ps, labels):
    off = load(off_p); res = []
    start = int(2.6 * SR); L = int(12 * SR)
    for p, lab in zip(ps, labels):
        x = load(p); lag = lag_of(off, x); x = np.roll(x, -lag)[:len(off)]
        a, b = start, start + L; o = off[a:b]; w = x[a:b]
        g = float(np.dot(o, w) / np.dot(o, o))                          # the pedal's own gain between the passes
        add = w - g * o; act = np.abs(o) > 10 ** (-50 / 20)
        e = np.sqrt(np.convolve(o ** 2, np.ones(441) / 441, 'same')); m = e > 10 ** (-50 / 20)
        added = 20 * np.log10(np.sqrt(np.mean(add[m] ** 2)) / np.sqrt(np.mean((g * o)[m] ** 2)) + 1e-12)
        bd = np.array(bands(w)) - np.array(bands(g * o))
        res.append((lab, lag, 20 * np.log10(abs(g) + 1e-12), added, bd))
    print(f'{"setting":10s} {"lag":>6s} {"gain dB":>8s} {"added re off":>13s}   change in band share (dB): 60-150 150-400 400-1.2k 1.2-2.4k 2.4-4.8k 4.8-9.6k')
    for lab, lag, g, ad, bd in res: print(f'{lab:10s} {lag:6d} {g:8.2f} {ad:10.1f} dB      ' + '  '.join(f'{v:+7.2f}' for v in bd))
    return res
def selftest():
    exe = ROOT + '/build/enhance_render'; assert os.path.exists(exe), 'build tests/engine/enhance_render.c first'
    import tempfile; t = tempfile.mkdtemp(); sig = os.path.join(t, 'sig.wav'); make(sig); x = load(sig)
    def run(tone, better): return np.frombuffer(subprocess.run([exe, str(better), '0', str(tone)], input=x.astype('f4').tobytes(), capture_output=True, check=True).stdout, dtype='f4').astype(np.float64)
    rng = np.random.RandomState(0); paths = []
    for name, tone, better in (('off', 0, 0), ('warm', 0, 100), ('both', 50, 100), ('bright', 100, 100)):
        y = run(tone, better) * 0.7 + 3e-5 * rng.randn(len(x)); y = np.concatenate([np.zeros(1200 + int(rng.randint(0, 800))), y])[:len(x)]            # a gain, a start offset and a noise floor
        p = os.path.join(t, name + '.wav'); sf.write(p, np.stack([y, y], 1), SR, subtype='PCM_24'); paths.append(p)
    print('self test: the tool should recover warm about -15.7, both -15.2, bright -23.5 dB (host numbers at BETTER 100, LOUDER 0)'); measure(paths[0], paths[1:], ['warm', 'both', 'bright'])
if __name__ == '__main__':
    c = sys.argv[1]
    if c == 'make': make(sys.argv[2])
    elif c == 'selftest': selftest()
    else:
        a = sys.argv[2:]; labels = None
        if '--labels' in a: i = a.index('--labels'); labels = a[i + 1].split(','); del a[i:i + 2]
        measure(a[0], a[1:], labels or [os.path.basename(p)[:-4] for p in a[1:]])
