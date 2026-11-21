"""Synthetic electric-guitar DI generator with exact ground truth.

NOT a substitute for real playing (see docs/POLYTRACK-X-BENCHMARKS.md): partials are
additive with string inharmonicity, pluck-position comb, per-partial decay, pick noise
and a pickup low-pass. Four "guitars" (parameter sets) exist so tuning and test can be
split by instrument as well as by random seed.
"""
import numpy as np
SR = 44100
STRINGS = [40, 45, 50, 55, 59, 64]  # EADGBE MIDI
BASS = [28, 33, 38, 43]

GUITARS = {  # B inharmonicity, pluck pos (fraction), tau0 s, decay slope, pickup lp Hz, pick noise
    "A": dict(B=1.2e-4, pp=0.17, tau=2.2, cs=0.35, lp=3800, nz=0.05),
    "B": dict(B=2.5e-4, pp=0.12, tau=1.6, cs=0.55, lp=2800, nz=0.08),
    "C": dict(B=0.8e-4, pp=0.22, tau=2.8, cs=0.25, lp=5200, nz=0.04),   # test only
    "D": dict(B=3.5e-4, pp=0.10, tau=1.2, cs=0.70, lp=2200, nz=0.10),   # test only
}


def note(rng, midi, dur, g, amp=0.3, bend=None, mute=False, soft_attack=False, sr=SR):
    """One plucked string. bend: (t0, t1, semitones) linear glide."""
    n = int(dur * sr)
    t = np.arange(n) / sr
    f0 = 440 * 2 ** ((midi - 69) / 12)
    gp = GUITARS[g]
    fi = f0 * np.ones(n)
    if bend:
        t0, t1, st = bend
        fi = f0 * 2 ** (st / 12 * np.clip((t - t0) / max(t1 - t0, 1e-3), 0, 1) / 1)
    ph0 = 2 * np.pi * np.cumsum(fi) / sr
    y = np.zeros(n)
    tau0 = gp["tau"] * (0.25 if mute else 1.0) * (110 / f0) ** 0.15
    for k in range(1, 40):
        fk = f0 * k * np.sqrt(1 + gp["B"] * k * k)
        if fk > 9000:
            break
        a = abs(np.sin(np.pi * k * gp["pp"])) / k ** 1.05
        # partial phase follows the (possibly bent) fundamental
        ph = ph0 * k * np.sqrt(1 + gp["B"] * k * k) + rng.uniform(0, 2 * np.pi)
        tau = tau0 / (1 + gp["cs"] * (k - 1) ** 0.8)
        env = np.exp(-t / tau)
        y += a * env * np.sin(ph)
    atk = 0.012 if soft_attack else 0.0015
    y *= np.minimum(t / atk, 1)
    if not soft_attack:  # pick click
        nz = rng.standard_normal(n) * np.exp(-t / 0.004) * gp["nz"]
        y += nz
    # pickup low-pass (one pole x2)
    a = np.exp(-2 * np.pi * gp["lp"] / sr)
    from scipy.signal import lfilter
    y = lfilter([1 - a], [1, -a], lfilter([1 - a], [1, -a], y))
    y *= amp / (np.max(np.abs(y)) + 1e-9)
    return y


class Take:
    def __init__(self, g, seed, dur=3.0, noise_db=-70, tuning=None):
        self.rng = np.random.default_rng(seed)
        self.g = g
        self.n = int(dur * SR)
        self.x = np.zeros(self.n)
        self.truth = []  # dict(on, off, midi, string)
        self.noise_db = noise_db
        self.tuning = tuning or STRINGS

    def add(self, t, midi, dur, string=0, amp=0.3, off=None, **kw):
        """off = time the note is muted (fade 15 ms); default rings for dur."""
        y = note(self.rng, midi, dur, self.g, amp, **kw)
        off = t + dur if off is None else off
        i0 = int(t * SR)
        ln = min(len(y), self.n - i0)
        if off < t + dur:
            m = np.ones(len(y))
            io = int((off - t) * SR)
            fade = int(0.015 * SR)
            m[io:io + fade] = np.linspace(1, 0, len(m[io:io + fade]))
            m[io + fade:] = 0
            y = y * m
        self.x[i0:i0 + ln] += y[:ln]
        self.truth.append(dict(on=t, off=off, midi=midi, string=string, bend=kw.get("bend"),
                               hammer=kw.get("soft_attack", False)))

    def audio(self):
        nz = self.rng.standard_normal(self.n) * 10 ** (self.noise_db / 20)
        return (self.x + nz).astype(np.float32)


def voicing(rng, nnotes, tuning=STRINGS, lo=0, hi=12):
    """Random playable voicing on `nnotes` adjacent-ish strings (like real chords)."""
    nstr = len(tuning)
    top = rng.integers(nnotes - 1, nstr)
    strs = list(range(top - nnotes + 1, top + 1))
    if rng.random() < 0.3 and nnotes < nstr:  # skip a string sometimes
        pass
    frets = rng.integers(lo, hi, size=nnotes)
    if rng.random() < 0.4:
        frets[rng.random(nnotes) < 0.5] = 0   # open strings
    return [(s, tuning[s] + int(f)) for s, f in zip(strs, frets)]


def chord(take, t, vc, dur, strum=0.012, amp=0.25, **kw):
    for i, (s, m) in enumerate(vc):
        j = take.rng.uniform(-0.002, 0.002)
        take.add(t + i * strum + max(j, 0), m, dur, s, amp * take.rng.uniform(0.8, 1.2), **kw)


def scenario(kind, g, seed):
    """Returns Take. kinds named in bench.SCENARIOS."""
    rng = np.random.default_rng(seed + 7919)
    tk = Take(g, seed, dur=3.0)
    if kind.startswith("poly") and kind[4:].isdigit():
        n = int(kind[4:])
        vc = voicing(rng, n)
        chord(tk, 0.15, vc, 2.5, strum=rng.choice([0.0, 0.006, 0.015]))
    elif kind == "power":
        s = int(rng.integers(0, 3)); f = int(rng.integers(0, 10))
        m0 = STRINGS[s] + f
        chord(tk, 0.15, [(s, m0), (s + 1, m0 + 7), (s + 2, m0 + 12)], 2.5, strum=0.008)
    elif kind == "arpeggio":
        vc = voicing(rng, 4)
        for i, (s, m) in enumerate(vc):
            tk.add(0.15 + i * 0.35, m, 2.5 - i * 0.35, s)
    elif kind == "add_note":
        vc = voicing(rng, 3)
        chord(tk, 0.15, vc[:2], 2.6, strum=0.01)
        s, m = vc[2]
        tk.add(1.4, m, 1.4, s)
    elif kind == "release_note":
        vc = voicing(rng, 4)
        for i, (s, m) in enumerate(vc):
            tk.add(0.15 + i * 0.01, m, 2.6, s, off=(1.5 if i == 1 else None))
    elif kind == "bend_ring":
        vc = voicing(rng, 3)
        (s0, m0), (s1, m1), (s2, m2) = vc
        tk.add(0.15, m0, 2.6, s0); tk.add(0.17, m1, 2.6, s1)
        tk.add(0.19, m2, 2.6, s2, bend=(0.7, 1.1, 2.0), amp=0.35)
        tk.truth[-1]["bend"] = (0.7, 1.1, 2.0)
    elif kind == "hammer_pull":
        s = int(rng.integers(1, 5)); m = STRINGS[s] + int(rng.integers(0, 5))
        tk.add(0.15, m, 0.5, s)
        tk.add(0.65, m + 2, 0.5, s, soft_attack=True, amp=0.2)
        tk.add(1.15, m, 0.8, s, soft_attack=True, amp=0.15)
    elif kind == "palm_mute":
        vc = voicing(rng, 3, lo=0, hi=7)
        for i in range(4):
            chord(tk, 0.15 + i * 0.5, vc, 0.45, strum=0.006, mute=True)
    elif kind == "quiet":
        vc = voicing(rng, 3)
        chord(tk, 0.15, vc, 2.5, amp=0.015, strum=0.01)
    elif kind == "bass3":
        tk = Take(g, seed, dur=3.0, tuning=BASS)
        s = int(rng.integers(0, 3)); f = int(rng.integers(0, 7))
        chord(tk, 0.15, [(s, BASS[s] + f), (s + 1, BASS[s + 1] + int(rng.integers(0, 7)))], 2.5, strum=0.012)
    elif kind == "mono":
        s = int(rng.integers(0, 6)); m = STRINGS[s] + int(rng.integers(0, 15))
        tk.add(0.15, m, 2.5, s)
    else:
        raise ValueError(kind)
    return tk
