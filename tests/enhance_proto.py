#!/usr/bin/env python3
"""ENHANCE concept prototypes, OFFLINE (numpy/scipy), for audition and measurement. Nothing here is firmware.
  PRESENCE  Aphex-style exciter path (US 4,150,253): 2-pole high-pass -> one-sided soft clip with a threshold RELATIVE to the running level (so only the louder peaks/transients clip) -> low-pass -> mixed in.
  WARMTH    even-harmonic generator on the low-mid band: band-pass 80-400 Hz -> full-wave rectifier (pure even harmonics) -> high-pass (DC) -> low-pass -> mixed in.
Measures: added signal re dry, the change in the 4.8-9.6 kHz and 2.4-4.8 kHz band energy, the second harmonic of single notes; writes level-matched renders.
   python3 tests/enhance_proto.py [--renders]"""
import sys, os, numpy as np, soundfile as sf, librosa
from scipy import signal
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..')); SR = 44100
def sos(kind, fc, order=2): return signal.butter(order, fc, kind, fs=SR, output='sos')
def presence(x, hp=2500.0, thr=1.3, lp=10000.0, drive=1.0):
    h = signal.sosfilt(sos('highpass', hp), x) * drive
    env = np.sqrt(signal.lfilter([1 - np.exp(-1 / (0.02 * SR))], [1, -np.exp(-1 / (0.02 * SR))], h ** 2))      # 20 ms RMS of the high band
    T = thr * env + 1e-9
    # one-sided soft clip above the threshold: positive peaks are rounded, the negative side is untouched (odd and even harmonics, low order)
    over = np.maximum(h - T, 0.0); y = h - over + T * np.tanh(over / T) * 0.0 - 0.5 * over      # knee: the part above T is halved (a hard-ish soft clip)
    y = h - 0.6 * over
    return signal.sosfilt(sos('lowpass', lp), y)
def warmth(x, lo=80.0, hi=400.0, out_lp=1200.0):
    b = signal.sosfilt(signal.butter(2, [lo, hi], 'bandpass', fs=SR, output='sos'), x)
    r = np.abs(b); r = r - signal.sosfilt(sos('lowpass', 40.0, 1), r)                                            # remove the DC the rectifier makes
    return signal.sosfilt(sos('lowpass', out_lp), r)
def process(x, p_mix=0.0, w_mix=0.0, **kw):
    y = x.copy()
    if p_mix: y = y + p_mix * presence(x, **kw)
    if w_mix: y = y + w_mix * warmth(x)
    return y
def band_db(s, bands=((2400, 4800), (4800, 9600))):
    S = np.abs(librosa.stft(s, n_fft=8192, hop_length=2048)) ** 2; P = S.mean(1); fr = librosa.fft_frequencies(sr=SR, n_fft=8192); tot = P[(fr > 60) & (fr < 16000)].sum()
    return np.array([10 * np.log10(P[(fr >= a) & (fr < b)].sum() / tot + 1e-12) for a, b in bands])
def h2_median(sig):
    f0, vf, vp = librosa.pyin(sig, fmin=100, fmax=400, sr=SR, frame_length=4096, hop_length=1024); S = np.abs(librosa.stft(sig, n_fft=16384, hop_length=1024)); fr = librosa.fft_frequencies(sr=SR, n_fft=16384); r = []
    pk = lambda i, f: S[max(0, np.argmin(np.abs(fr - f)) - 3):np.argmin(np.abs(fr - f)) + 4, i].max()
    for i in range(min(S.shape[1], len(f0))):
        if vf[i] and vp[i] >= 0.85 and not np.isnan(f0[i]): r.append(20 * np.log10(pk(i, 2 * f0[i]) / pk(i, f0[i])))
    return float(np.median(r))
def added_db(x, y):
    h = 441; e = np.sqrt(np.convolve(x ** 2, np.ones(h) / h, 'same')); m = e > 10 ** (-50 / 20); return 20 * np.log10(np.sqrt(np.mean((y[m] - x[m]) ** 2)) / np.sqrt(np.mean(x[m] ** 2)))
if __name__ == '__main__':
    g, _ = sf.read(ROOT + '/My Audio/enhances2.wav', dtype='float64'); note = g[:, 0][int(40.6 * SR):int(46.8 * SR)]
    CFG = [('dry', 0, 0), ('presence 0.1', 0.1, 0), ('presence 0.25', 0.25, 0), ('presence 0.5', 0.5, 0), ('warmth 0.2', 0, 0.2), ('warmth 0.5', 0, 0.5), ('both 0.15 + 0.3', 0.15, 0.3)]
    print('on the owner\'s playing (the "off" take of enhances2.wav):')
    print(f'{"setting":18s} {"added re dry":>12s} {"2.4-4.8k":>9s} {"4.8-9.6k":>9s} {"H2 of notes":>12s}')
    b0 = band_db(note); h0 = h2_median(note)
    for name, pm, wm in CFG:
        y = process(note, pm, wm); b = band_db(y) - b0
        print(f'{name:18s} {("-" if pm == wm == 0 else f"{added_db(note, y):7.1f} dB"):>12s} {b[0]:+8.1f} {b[1]:+8.1f}  {h2_median(y):7.1f} dB')
    print(f'(dry H2 {h0:.1f} dB re fundamental; the flashed build 926 moved it by +1.9 dB)')
    if '--renders' in sys.argv:
        sys.path.insert(0, ROOT + '/tests/fasttrack'); import level_match as LM
        out = ROOT + '/build/enhance4'; os.makedirs(out, exist_ok=True)
        for f in ('bleep3 di', 'fast play di', 'scale + duo + power chords di'):
            d, _ = sf.read(f'{ROOT}/My Audio/{f}.wav', dtype='float64'); d = d[:, 0][:SR * 14]; tag = f.replace(' ', '_').replace('+', 'and'); paths = []
            for k, (name, pm, wm) in enumerate(CFG):
                p = f'{out}/{tag}_{k}_{name.replace(" ", "_").replace("+", "and")}.wav'; sf.write(p, process(d, pm, wm), SR, subtype='PCM_24'); paths.append(p)
            LM.main(out + '/matched', paths)
        print('level-matched renders in', out + '/matched')
