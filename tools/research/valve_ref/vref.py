"""Shared loaders / measurement helpers for the 12AX7 validation (see README.md)."""
import json, os, re, subprocess, numpy as np
from scipy.signal import resample_poly, butter, sosfiltfilt
HERE = os.path.dirname(os.path.abspath(__file__)); FS = 44100.0; FS2 = 2 * FS
MAN = json.load(open(f"{HERE}/data/manifest.json"))
def _consts():
    s = open(f"{HERE}/../../../engine/px_triode_table.h").read()
    return {m.group(1): float(m.group(2)) for m in re.finditer(r"#define (PXT_\w+) ([-0-9.e+]+)f", s)}
C = _consts()                                  # the firmware's own constants: PXT_BIAS, PXT_PLATE_IDLE, PXT_PLATE_SCALE, ...
RP = C["PXT_PLATE_KOHM"]; B = C["PXT_SUPPLY"]
def drive_lin(db): return 4.0 * 10 ** (db / 20)             # volts per engine unit, exactly as px_dsp.c line 1715
def dsp(name, db):
    p = f"{HERE}/data/dsp/{name}_{db}"
    d = {k: np.fromfile(f"{p}.{k}.f32", "<f4").astype(np.float64) for k in ("final", "wet", "vin", "vk", "ig", "ia", "ik")}
    d["x"] = np.fromfile(f"{HERE}/data/in/{name}.f32", "<f4").astype(np.float64)[: len(d["final"])]
    return d
def spice(tag, name, db, dec=True):
    """SPICE result. dec: decimate 176.4 kHz -> 88.2 kHz (zero-phase, so sample j == the DSP's oversampled sample j for dspin_ runs)."""
    o = dict(np.load(f"{HERE}/data/spice/{tag}_{name}_{db}.npz"))
    if dec:
        for k in o:
            if k != "t": o[k] = resample_poly(o[k], 1, 2)
        o["t"] = o["t"][::2][: len(o["vk"])]
        # the engine's half-band up-sampler delays its grid voltage; shift the circuit by the measured lag so both are sample-aligned
        dv = np.fromfile(f"{HERE}/data/dsp/{name}_{db}.vin.f32", "<f4").astype(np.float64); n = min(len(dv), len(o["vin"]))
        lag = est_lag(dv[:n], o["vin"][:n]); o["lag_samples"] = lag
        for k in o:
            if k not in ("t", "lag_samples"): o[k] = frac_delay(o[k], lag)
    return o
def est_lag(ref, y):
    """delay of ref relative to y in samples (FFT cross-correlation, parabolic peak)"""
    sos = butter(4, 3000 / (FS2 / 2), output='sos'); ref = sosfiltfilt(sos, ref); y = sosfiltfilt(sos, y)     # the half-band delay is flat at low frequency
    n = len(ref); R = np.fft.rfft(ref, 2 * n); Y = np.fft.rfft(y, 2 * n); c = np.fft.irfft(R * np.conj(Y))
    idx = np.arange(-16, 17); k = int(idx[np.argmax(c[idx % (2 * n)])])      # search only +-16 samples: the up-sampler delay is a few samples
    a, b, cc = c[(k - 1) % (2 * n)], c[k % (2 * n)], c[(k + 1) % (2 * n)]
    return k + 0.5 * (a - cc) / (a - 2 * b + cc + 1e-30)
def frac_delay(y, lag):
    n = len(y); Y = np.fft.rfft(y, 2 * n); f = np.fft.rfftfreq(2 * n)
    return np.fft.irfft(Y * np.exp(-2j * np.pi * f * lag))[:n]
def wet_ref(o, db):
    """Circuit -> engine units, referenced to the TUBE PLATE CURRENT (independent of whatever load the netlist hangs on the plate):
    the DSP's fixed scaling (PXT_PLATE_SCALE / drive) on (Ia - Ia_idle), then the DSP's own 22 ms DC-removal one-pole."""
    from scipy.signal import lfilter
    w = (o["ia"] - o["ia"][:50].mean()) * C["PXT_PLATE_SCALE"] / drive_lin(db)
    k = 1 - np.exp(-1.0 / (0.022 * FS2))
    return w - lfilter([k], [1, -(1 - k)], w)
def wet_ref_v(o, db):
    """Physical output voltage of the netlist (includes the 1 Mohm || coupling-cap load the netlist puts on the plate), same scaling."""
    return -o["vout"] / RP * C["PXT_PLATE_SCALE"] / drive_lin(db)
def post(x, wet88, db, tmp):
    """The engine's output stage (half-band down, add dry, x1.05075) applied to a supplied wet signal."""
    n = len(x); n -= n % 64
    x.astype("<f4")[:n].tofile(tmp + ".x"); wet88.astype("<f4")[: 2 * n].tofile(tmp + ".w")
    subprocess.run([f"{HERE}/build_valve_host", "post", tmp + ".x", tmp + ".w", tmp + ".y", str(db)], check=True)
    return np.fromfile(tmp + ".y", "<f4").astype(np.float64)
def ls_lines(y, fs, freqs, i0, i1):
    """least-squares amplitudes (peak) of sinusoids at freqs over y[i0:i1] (+DC); returns amps, phases, residual"""
    i1 = min(i1, len(y)); t = np.arange(i0, i1) / fs; seg = y[i0:i1]
    A = np.concatenate([np.ones((len(t), 1)), np.cos(2 * np.pi * np.outer(t, freqs)), np.sin(2 * np.pi * np.outer(t, freqs))], axis=1)
    co, *_ = np.linalg.lstsq(A, seg, rcond=None)
    k = len(freqs); a, b = co[1:1 + k], co[1 + k:]
    return np.hypot(a, b), np.arctan2(-b, a), seg - A @ co
def band_rms(y, fs, lo=20.0, hi=20000.0):
    if len(y) < 64: return 0.0
    w = np.hanning(len(y)); Y = np.fft.rfft((y - y.mean()) * w); f = np.fft.rfftfreq(len(y), 1 / fs)
    m = (f >= lo) & (f <= hi); return float(np.sqrt(2 * np.sum(np.abs(Y[m]) ** 2) / (np.sum(w ** 2) * len(y))))
def db(x): return 20 * np.log10(np.maximum(np.abs(x), 1e-12))
