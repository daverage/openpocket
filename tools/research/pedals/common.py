"""Shared pieces of the pedal-emulation fits (tools/research/pedals): the NAM captures are used OFFLINE ONLY, as measurements of the real hardware.
Level: the engine scales the ADC by 0.5/2^23 (firmware/px_audio.h), so engine 0.5 is converter full scale: a NAM input is 2 x the engine sample."""
import os, sys, numpy as np, soundfile as sf
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nam_a2
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
SR = 48000
ENGINE_TO_NAM = 2.0
def owner_di(seconds=10.0, peak_engine=0.14):
    """the owner's DI (My Audio, never committed), resampled to 48 kHz, scaled so its peak is `peak_engine` engine units; returns engine-unit samples"""
    d, sr = sf.read(os.path.join(ROOT, 'My Audio', 'scale + duo + power chords di.wav'), dtype='float64'); d = d[:, 0] if d.ndim > 1 else d
    d = signal.resample_poly(d[:int(sr * seconds)], SR, sr); return d / np.abs(d).max() * peak_engine
def capture(path, size='full'): return nam_a2.load(path, size)
_align = {}
def _raw(net, x_engine):
    pad = np.zeros(4000); return nam_a2.run(net, np.concatenate([pad, x_engine * ENGINE_TO_NAM]))[4000:]
def alignment(net):
    """(lag in samples, sign) of a capture against its input: captures carry the reamp round-trip latency and, for an inverting stage, the polarity; found once per capture
    by cross-correlating a mid-level broadband probe (the owner's DI, high-passed so the boosters' treble dominates)"""
    k = id(net)
    if k not in _align:
        x = owner_di(6, 0.035); y = _raw(net, x); b, a = signal.butter(2, 2500 / (SR / 2), 'high'); xh = signal.lfilter(b, a, x); yh = signal.lfilter(b, a, y)
        c = signal.correlate(yh, xh, mode='full', method='fft'); i = int(np.argmax(np.abs(c))); _align[k] = (i - (len(xh) - 1), 1.0 if c[i] > 0 else -1.0)
    return _align[k]
def run_nam(net, x_engine):
    """the capture's output in NAM units, shifted back by its latency and with polarity restored to the input's (the pedals invert; the engine need not)"""
    lag, sg = alignment(net); y = sg * _raw(net, x_engine)
    return np.concatenate([y[lag:], np.zeros(lag)]) if lag >= 0 else np.concatenate([np.zeros(-lag), y[:lag]])
def esr(ref, y): return float(np.sum((ref - y) ** 2) / (np.sum(ref ** 2) + 1e-18))
def tf_db(net, freqs, db=-40.0):
    """small-signal gain (dB, input dBFS `db`) at each frequency, steady-state sines"""
    out = []
    for f in freqs:
        n = SR * 2; t = np.arange(n) / SR; x = 10 ** (db / 20) * np.sin(2 * np.pi * f * t)
        y = nam_a2.run(net, np.concatenate([np.zeros(4000), x]))[4000 + SR // 2:]; w = np.hanning(len(y)); Y = np.abs(np.fft.rfft(y * w)) / (w.sum() / 2); k = int(round(f * len(y) / SR))
        out.append(20 * np.log10(Y[max(0, k - 2):k + 3].max() / 10 ** (db / 20)))
    return np.array(out)
