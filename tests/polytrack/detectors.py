"""Host reference detectors. Every detector maps audio -> per-hop frames.
frames[i] = (t_end_seconds, [(midi_float, amp, conf), ...])  (causal: window ends at t_end).
Candidates:  hs    harmonic summation + peak picking          (baseline 4)
             klap  iterative estimate-and-cancel (Klapuri-style) (baseline 5)
             nnls  joint sparse non-negative fit, multi-res + temporal tracker (candidate)
             yin   monophonic YIN                              (mono reference)
"""
import numpy as np
from scipy.optimize import nnls
SR = 44100
HOP = 256
MIDI_LO, MIDI_HI = 38, 88        # D2..E6 candidate range (bass handled by lo override)
fmidi = lambda m: 440 * 2 ** ((np.asarray(m, float) - 69) / 12)


def spec(x, end, N, nfft):
    seg = x[max(0, end - N):end]
    if len(seg) < N:
        seg = np.pad(seg, (N - len(seg), 0))
    w = np.hanning(N)
    return np.abs(np.fft.rfft(seg * w, nfft)) / N


class Grid:
    def __init__(self, N, nfft, fmax=5000, fmin=60):
        self.N, self.nfft = N, nfft
        self.df = SR / nfft
        self.k0, self.k1 = int(fmin / self.df), int(fmax / self.df)
        self.f = np.arange(self.k0, self.k1) * self.df


def kernel(N, nfft):
    w = np.hanning(N)
    W = np.abs(np.fft.rfft(w, nfft)); W /= W.max()
    c = int(np.argmax(W))
    half = int(3.2 * nfft / N)  # main lobe ~ +-2 bins of N
    return W[:half + 1]


def make_dict(g, N, nfft, mlo, mhi, gamma=1.0, K=14, B=1.5e-4, kern=None):
    """Harmonic templates (columns) on grid g, one per semitone."""
    kern = kernel(N, nfft) if kern is None else kern
    half = len(kern) - 1
    mids = np.arange(mlo, mhi + 1)
    D = np.zeros((len(g.f), len(mids)))
    for j, m in enumerate(mids):
        f0 = fmidi(m)
        for k in range(1, K + 1):
            fk = f0 * k * np.sqrt(1 + B * k * k)
            if fk > g.f[-1]:
                break
            c = fk / g.df - g.k0
            lo = int(np.floor(c)) - half; hi = int(np.ceil(c)) + half
            idx = np.arange(max(lo, 0), min(hi + 1, len(g.f)))
            d = np.abs(idx - c) / 1.0
            d_int = np.minimum(np.round(d).astype(int), half)
            # linear interp of kernel
            kv = np.interp(d, np.arange(half + 1), kern, right=0)
            D[idx, j] = np.maximum(D[idx, j], kv * k ** -gamma)
    return D, mids


class Det:
    name = "base"
    lo = MIDI_LO
    def run(self, x):
        raise NotImplementedError


# ---------------------------------------------------------------- baseline: harmonic summation
class HS(Det):
    name = "hs"
    def __init__(self, N=4096, nmax=6, rel=0.30, floor=2e-4, lo=MIDI_LO):
        self.N, self.nmax, self.rel, self.floor, self.lo = N, nmax, rel, floor, lo
        self.g = Grid(N, 2 * N)
        self.D, self.mids = make_dict(self.g, N, 2 * N, lo, MIDI_HI)
    def run(self, x):
        out = []
        for end in range(self.N // 2, len(x), HOP):
            S = spec(x, end, self.N, 2 * self.N)[self.g.k0:self.g.k1]
            sal = self.D.T @ S / self.D.sum(0)
            notes = []
            if S.max() > self.floor:
                pk = [i for i in range(len(sal)) if sal[i] >= sal[max(i - 1, 0)] and sal[i] >= sal[min(i + 1, len(sal) - 1)]]
                pk.sort(key=lambda i: -sal[i])
                for i in pk[:self.nmax]:
                    if sal[i] >= self.rel * sal[pk[0]]:
                        notes.append((float(self.mids[i]), float(sal[i]), float(sal[i] / sal[pk[0]])))
            out.append((end / SR, notes))
        return out


# ---------------------------------------------------------------- baseline: iterative cancellation
class Klap(Det):
    name = "klap"
    def __init__(self, N=4096, nmax=6, stop=0.12, floor=2e-4, lo=MIDI_LO, amp_stop=0.25):
        self.N, self.nmax, self.stop, self.floor, self.lo, self.amp_stop = N, nmax, stop, floor, lo, amp_stop
        self.g = Grid(N, 2 * N)
        self.D, self.mids = make_dict(self.g, N, 2 * N, lo, MIDI_HI)
        self.Dn = self.D / self.D.sum(0)
    def run(self, x):
        out = []
        for end in range(self.N // 2, len(x), HOP):
            R = spec(x, end, self.N, 2 * self.N)[self.g.k0:self.g.k1].copy()
            notes = []
            s0 = None; a0 = None
            if R.max() > self.floor:
                for _ in range(self.nmax):
                    sal = self.Dn.T @ R
                    j = int(np.argmax(sal))
                    if s0 is None:
                        s0 = sal[j]
                    if sal[j] < self.stop * s0:
                        break
                    # estimate per-note amplitude as the lowest ratio over its harmonic support
                    sup = self.D[:, j] > 0.2
                    a = float(np.percentile(R[sup] / self.D[sup, j], 25))
                    a = max(a, 0)
                    if a0 is None:
                        a0 = a
                    if a < self.amp_stop * a0:
                        break
                    R = np.maximum(R - 0.9 * a * self.D[:, j], 0)
                    notes.append((float(self.mids[j]), a, float(sal[j] / s0)))
            out.append((end / SR, notes))
        return out


# ---------------------------------------------------------------- candidate: joint sparse NNLS + tracker
class NNLS(Det):
    name = "nnls"
    def __init__(self, Ns=(4096,), lam=0.002, rel=0.08, floor=2e-4, lo=MIDI_LO, gamma=1.0, mode="mag",
                 resid_gate=True, tracker=True, confirm=2, release=3, nmax=6, absfloor=0.015):
        self.Ns, self.lam, self.rel, self.floor, self.lo = Ns, lam, rel, floor, lo
        self.mode, self.resid_gate, self.use_tracker = mode, resid_gate, tracker
        self.confirm, self.release, self.nmax, self.absfloor = confirm, release, nmax, absfloor
        self.grids, self.Ds = [], []
        for N in Ns:
            g = Grid(N, 2 * N if N >= 2048 else 4 * N, fmax=5000 if N > 1024 else 4000, fmin=60 if N > 1024 else 150)
            D, self.mids = make_dict(g, N, g.nfft, lo, MIDI_HI, gamma=gamma)
            self.grids.append(g); self.Ds.append(D)
        # stack rows; the fast (short) window only sees notes above its resolution limit
        self.cols_ok = [np.array([fmidi(m) > (3.0 * SR / N) for m in self.mids]) for N in Ns]

    def frame(self, x, end):
        rows, Dr = [], []
        for g, D, N, ok in zip(self.grids, self.Ds, self.Ns, self.cols_ok):
            S = spec(x, end, N, g.nfft)[g.k0:g.k1]
            if self.mode == "pow":
                S = S ** 2
            rows.append(S); Dr.append(D * ok[None, :])
        S = np.concatenate(rows)
        D = np.vstack(Dr)
        if self.mode == "pow":
            D = D ** 2
        lvl = np.sqrt(np.mean(S ** 2)) + 1e-9
        if np.max(S) < (self.floor if self.mode == "mag" else self.floor ** 2):
            return []
        # L1 via augmented NNLS: min |S-Da|^2 + lam*sum(a)  (a>=0)  -> add one row sqrt(lam)*... handled by shift
        Dn = D / (D.sum(0) + 1e-9)          # amplitude in "spectral mass" units
        # L1 as ridge-row trick:  [D; sqrt(lam)*1^T] a ~ [S; 0] is L2; for L1 use data shift
        A = np.vstack([Dn, np.sqrt(self.lam) * lvl * np.ones((1, Dn.shape[1]))])
        b = np.concatenate([S, [0.0]])
        try:
            a, _ = nnls(A, b, maxiter=400)
        except Exception:
            return []
        if not np.all(np.isfinite(a)) or a.max() <= 0:
            return []
        recon = Dn @ a
        notes = []
        amax = a.max()
        for j in np.argsort(-a)[:self.nmax + 2]:
            if a[j] < self.rel * amax or a[j] < self.absfloor * lvl:
                continue
            if self.resid_gate:
                # fundamental + 2nd harmonic must be explained by observed evidence, not by others' harmonics
                sup = D[:, j] > 0.3
                own = a[j] * Dn[sup, j]
                if np.mean(np.minimum(S[sup], recon[sup]) / (recon[sup] + 1e-12) * own / (own + 1e-12)) < 0.35 or \
                   np.sum(np.minimum(S[sup], own)) < 0.6 * np.sum(own):
                    continue
            notes.append((float(self.mids[j]), float(a[j]), float(a[j] / amax)))
            if len(notes) >= self.nmax:
                break
        return notes

    def run(self, x):
        Nmax = max(self.Ns)
        raw = []
        for end in range(Nmax // 2, len(x), HOP):
            raw.append((end / SR, self.frame(x, end)))
        return Tracker(self).apply(raw) if self.use_tracker else raw


class Tracker:
    """Candidate -> Confirmed -> Sustained -> Releasing -> Inactive per semitone track."""
    def __init__(self, d):
        self.c, self.r = d.confirm, d.release
    def apply(self, raw):
        state = {}   # midi -> dict(st, cnt, miss, amp)
        out = []
        for t, notes in raw:
            seen = {int(round(m)): (m, a, cf) for m, a, cf in notes}
            for m in list(set(seen) | set(state)):
                s = state.setdefault(m, dict(st="cand", cnt=0, miss=0, amp=0.0, on=False))
                if m in seen:
                    s["cnt"] += 1; s["miss"] = 0; s["amp"] = seen[m][1]
                    if s["st"] in ("cand",) and s["cnt"] >= self.c:
                        s["st"] = "conf"; s["on"] = True
                    elif s["st"] == "rel":
                        s["st"] = "conf"
                else:
                    if s["st"] == "cand":
                        s["cnt"] = 0
                        if s["miss"] >= 1:
                            del state[m]; continue
                    s["miss"] += 1
                    if s["on"] and s["miss"] >= self.r:
                        del state[m]; continue
                    if s["on"]:
                        s["st"] = "rel"
                    elif s["miss"] > 2:
                        del state[m]
            # bend continuity: a track that vanished next to a fresh candidate (+-2 st) hands over its identity
            act = [(float(m), s["amp"], 1.0) for m, s in state.items() if s["on"] and s["st"] != "rel"]
            act += [(float(m), s["amp"], 0.5) for m, s in state.items() if s["on"] and s["st"] == "rel"]
            out.append((t, act))
        return out


# ---------------------------------------------------------------- mono reference: YIN
class YIN(Det):
    name = "yin"
    def __init__(self, N=2048, thr=0.12, lo=38):
        self.N, self.thr, self.lo = N, thr, lo
    def run(self, x):
        out = []
        tmax = int(SR / fmidi(self.lo)) + 1
        for end in range(self.N + tmax, len(x), HOP):
            seg = x[end - self.N - tmax:end].astype(np.float64)
            w = seg[tmax:]
            n = len(w)
            # difference function via FFT autocorrelation
            f = np.fft.rfft(seg, 2 * len(seg))
            ac = np.fft.irfft(f * np.conj(f))[:tmax]
            cs = np.concatenate([[0], np.cumsum(seg ** 2)])
            d = np.zeros(tmax)
            for tau in range(1, tmax):
                e1 = cs[tmax + n] - cs[tmax]
                e2 = cs[tmax + n - tau] - cs[tmax - tau]
                d[tau] = e1 + e2 - 2 * ac[tau] * 1.0
            cm = np.ones(tmax); cm[1:] = d[1:] * np.arange(1, tmax) / np.maximum(np.cumsum(d[1:]), 1e-12)
            notes = []
            if np.sqrt(np.mean(w ** 2)) > 1e-3:
                tau = None
                for i in range(int(SR / 1400), tmax - 1):
                    if cm[i] < self.thr and cm[i] <= cm[i + 1]:
                        tau = i; break
                if tau:
                    notes = [(69 + 12 * np.log2(SR / tau / 440), 1.0, 1 - cm[tau])]
            out.append((end / SR, notes))
        return out


# ---------------------------------------------------------------- candidate: PolyTrack-X  (onset-gated, tracker-driven joint fit)
GAMMAS = (0.4, 1.0, 1.8)


class PT(Det):
    """Layer A  slow (4096) + optional fast (1024) spectra; onset evidence = positive spectral difference.
    Layer C  joint non-negative fit of the spectrum by multi-profile harmonic atoms (profile adaptation).
    Layer E  notes are *born* only where new energy appeared (difference-spectrum fit) and then *tracked*
             by a restricted joint fit over live tracks only, so a sustained note's own harmonics
             can never be reborn as another note (the Duo failure)."""
    name = "pt"
    def __init__(self, N=4096, fast=False, lo=MIDI_LO, birth_rel=0.25, keep_abs=0.10,
                 dS_gate=0.25, confirm=2, release=4, lag=1536, nmax=6, gammas=GAMMAS, birth_window=12,
                 differential=True, resid_gate=0.02, lam=0.01, B=1.5e-4):
        self.N, self.lo, self.fast = N, lo, fast
        self.br, self.ka, self.gate = birth_rel, keep_abs, dS_gate
        self.confirm, self.release, self.lag, self.nmax = confirm, release, lag, nmax
        self.bw, self.diff, self.rg, self.lam = birth_window, differential, resid_gate, lam
        self.g = Grid(N, 2 * N)
        self.mids = np.arange(lo, MIDI_HI + 1)
        cols, grp = [], []
        for j, m in enumerate(self.mids):
            for gm in gammas:
                Dm, _ = make_dict(self.g, N, 2 * N, m, m, gamma=gm, B=B)
                cols.append(Dm[:, 0]); grp.append(j)
        self.D = np.array(cols).T
        self.grp = np.array(grp)
        if fast:
            self.g2 = Grid(1024, 4096, fmax=4000, fmin=180)
            cols = []
            for j, m in enumerate(self.mids):
                for gm in gammas:
                    if fmidi(m) < 4 * SR / 1024:
                        cols.append(np.zeros(len(self.g2.f)))
                    else:
                        Dm, _ = make_dict(self.g2, 1024, 4096, m, m, gamma=gm, B=B)
                        cols.append(Dm[:, 0])
            self.D = np.vstack([self.D, np.array(cols).T])

    def spectra(self, x, end):
        S = spec(x, end, self.N, 2 * self.N)[self.g.k0:self.g.k1]
        if self.fast:
            S = np.concatenate([S, spec(x, end, 1024, 4096)[self.g2.k0:self.g2.k1]])
        return S

    def fit(self, S, cols):
        if len(cols) == 0:
            return np.zeros(0)
        A = self.D[:, cols]
        lvl = np.sqrt(np.mean(S ** 2)) + 1e-12
        A2 = np.vstack([A, np.sqrt(self.lam) * lvl * np.ones((1, len(cols)))])
        b = np.concatenate([S, [0.0]])
        try:
            a, _ = nnls(A2, b, maxiter=600)
        except Exception:
            return np.zeros(len(cols))
        return a if np.all(np.isfinite(a)) else np.zeros(len(cols))

    def note_amps(self, a, cols):
        out = {}
        for c, v in zip(cols, a):
            out[self.grp[c]] = out.get(self.grp[c], 0) + v
        return out

    def run(self, x):
        out = []
        tracks = {}
        last_onset = -999
        lag_h = max(1, self.lag // HOP)
        hist = []
        allc = np.arange(self.D.shape[1])
        for fi, end in enumerate(range(self.N // 2, len(x), HOP)):
            S = self.spectra(x, end)
            hist.append(S)
            Sp = hist[-1 - lag_h] if len(hist) > lag_h else np.zeros_like(S)
            silent = S.max() < 2e-4
            dS = np.maximum(S - Sp * 0.9, 0)
            e_new = dS.sum() / (S.sum() + 1e-12)
            if not silent and (e_new > self.gate or not self.diff):
                last_onset = fi
            if not silent and fi - last_onset <= self.bw:
                tgt = dS if self.diff else S
                na = self.note_amps(self.fit(tgt, allc), allc)
                if na:
                    amax = max(na.values())
                    births = sorted([(j, v / amax) for j, v in na.items() if v >= self.br * amax and j not in tracks],
                                    key=lambda z: -z[1])[:self.nmax]
                    for j, r in births:
                        if len(tracks) < self.nmax + 2:
                            tracks[j] = dict(amp=0.0, cnt=0, miss=0, on=False)
            live = sorted(tracks)
            ls = set(live)
            cols = np.array([c for c in range(self.D.shape[1]) if self.grp[c] in ls], dtype=int)
            if silent or len(cols) == 0:
                for j in list(tracks):
                    tracks[j]["miss"] += 1
                    if tracks[j]["miss"] >= self.release or not tracks[j]["on"]:
                        del tracks[j]
                out.append((end / SR, []))
                continue
            a = self.fit(S, cols)
            na = self.note_amps(a, cols)
            amax = max(list(na.values()) + [1e-12])
            res_full = float(np.sum((S - self.D[:, cols] @ a) ** 2))
            tot = float(np.sum(S ** 2)) + 1e-18
            for j in live:
                v = na.get(j, 0.0)
                t = tracks[j]
                # leave-one-out: how much worse is the explanation without this note?
                cj = np.array([c for c in cols if self.grp[c] != j], dtype=int)
                if len(cj):
                    a2 = self.fit(S, cj)
                    res_wo = float(np.sum((S - self.D[:, cj] @ a2) ** 2))
                else:
                    res_wo = tot
                gain = (res_wo - res_full) / tot
                if v >= self.ka * amax and gain >= self.rg:
                    t["cnt"] += 1; t["miss"] = 0; t["amp"] = v
                    if t["cnt"] >= self.confirm:
                        t["on"] = True
                else:
                    t["miss"] += 1
                    if not t["on"] or t["miss"] >= self.release:
                        del tracks[j]
            out.append((end / SR, [(float(self.mids[j]), t["amp"], 1.0) for j, t in tracks.items() if t["on"] and t["miss"] == 0]))
        return out


# ---------------------------------------------------------------- the REAL production mono tracker (engine/px_mono.h)
import subprocess, os
class MONO(Det):
    """Wraps build/polytrack_mono (tests/polytrack/mono_cli.c). Build: see README."""
    name = "mono"
    def __init__(self, lo=38, **kw):
        self.fmin = 38.0 if lo < 30 else 0.0
    def run(self, x):
        exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "polytrack_mono")
        p = subprocess.run([exe, str(self.fmin)], input=np.asarray(x, np.float32).tobytes(), capture_output=True)
        out = []
        for ln in p.stdout.decode().split("\n"):
            if not ln: continue
            t, f, g = ln.split()
            out.append((float(t), [(69 + 12 * np.log2(float(f) / 440), 1.0, 1.0)] if g == "1" and float(f) > 20 else []))
        return out


class HYB(Det):
    """Production mono tracker as the main voice; PT notes are added only if they are not the mono note.
    The mono frame always wins; PT only contributes extra voices."""
    name = "hyb"
    def __init__(self, lo=38, **kw):
        self.m, self.p = MONO(lo=lo), PT(resid_gate=0.03, birth_rel=0.3, lo=max(lo, 26) if lo < 30 else 38)
    def run(self, x):
        fm, fp = self.m.run(x), self.p.run(x)
        tp = np.array([t for t, _ in fp])
        out = []
        for t, n in fm:
            i = int(np.argmin(np.abs(tp - t)))
            extra = [z for z in fp[i][1] if not any(abs(z[0] - q[0]) < 0.75 for q in n)]
            out.append((t, list(n) + extra))
        return out
