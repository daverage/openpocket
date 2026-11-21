"""Independent-polyphony evidence, per frame, from the audio and the production mono tracker's note only.
Does NOT use pt's note list, ages or counts.  Window 4096 (as pt), hop 256, causal.
Features (all scale-free):
  C  gC   (r1-r2)/|S|^2   best 2-note fit vs best 1-note fit (multi-profile harmonic atoms, NNLS)
     rC   (r1-r2)/r1
     bic  Neff*log(r1/r2) - 3*log(Neff)   complexity-penalised log-likelihood gain (Gaussian residual)
     snr  r1 / (Neff*noise)  how far above the spectral noise floor the 1-note residual still is
  B  nsc  number of the 2nd note's harmonics (k<=8) that are NOT near a harmonic of the 1st and stand >=4x above the floor
     nse  energy at those non-shared harmonics / |S|^2
     oct  1 if the pair is an octave (12/24) apart
  A  gA   residual gain of adding the best extra note to the MONO tracker's note (fitted, not subtracted)
     rA   |S - mono fit|^2 / |S|^2
     mon  1 if the mono tracker had a note
  lvl log10 rms
"""
import numpy as np, itertools
import detectors as D
FEATS = ["gC", "rC", "bic", "snr", "nsc", "nse", "oct", "gA", "rA", "mon", "lvl"]
class Poly:
    def __init__(self):
        self.pt = D.PT()          # only for its dictionary / NNLS helper
        self.D, self.grp, self.g, self.N = self.pt.D, self.pt.grp, self.pt.g, 4096
        self.cols = {j: np.where(self.grp == j)[0] for j in range(len(self.pt.mids))}
        self.mids = self.pt.mids
        self.Neff = len(self.g.f) / 3.0
    def res(self, S, js):
        c = np.concatenate([self.cols[j] for j in js]); a = self.pt.fit(S, c)
        return float(np.sum((S - self.D[:, c] @ a) ** 2))
    def nonshared(self, S, fl):
        f1 = D.fmidi(self.mids[fl[0]]); f2 = D.fmidi(self.mids[fl[1]])
        h1 = f1 * np.arange(1, 30)
        floor = np.percentile(S, 20) + 1e-12
        cnt = 0; en = 0.0
        for k in range(1, 9):
            fk = f2 * k
            if fk > 5000: break
            if np.min(np.abs(h1 - fk)) < 1.5 * self.g.df * 1.0 * 2:     # within ~3 bins (hann main lobe) of a harmonic of note 1
                continue
            i = int(round(fk / self.g.df)) - self.g.k0
            if 0 <= i < len(S):
                v = S[max(i - 1, 0):i + 2].max()
                if v > 4 * floor: cnt += 1
                en += v ** 2
        return cnt, en
    def frame(self, S, mono_midi):
        tot = float(np.sum(S ** 2)) + 1e-18
        lvl = np.log10(np.sqrt(np.mean(S ** 2)) + 1e-9)
        if S.max() < 2e-4: return None
        allc = np.arange(self.D.shape[1])
        na = self.pt.note_amps(self.pt.fit(S, allc), allc)
        if not na: return None
        top = [j for j, v in sorted(na.items(), key=lambda z: -z[1])[:5] if v > 0.05 * max(na.values())]
        r1s = {j: self.res(S, [j]) for j in top}
        j1 = min(r1s, key=r1s.get); r1 = r1s[j1]
        r2, pair = r1, (j1, None)
        for a, b in itertools.combinations(top, 2):
            r = self.res(S, [a, b])
            if r < r2: r2, pair = r, (a, b)
        gC = (r1 - r2) / tot; rC = (r1 - r2) / (r1 + 1e-18)
        bic = self.Neff * np.log((r1 + 1e-18) / (r2 + 1e-18)) - 3 * np.log(self.Neff)
        noise = (np.percentile(S, 20) ** 2) + 1e-18
        snr = r1 / (self.Neff * noise)
        nsc = nse = oct_ = 0
        if pair[1] is not None:
            lo, hi = sorted(pair, key=lambda j: na[j], reverse=True)   # 1st = stronger
            nsc, en = self.nonshared(S, (lo, hi)); nse = en / tot
            d = abs(self.mids[pair[0]] - self.mids[pair[1]]); oct_ = 1 if d % 12 == 0 else 0
        # A: mono-explained
        mon = 0; gA = 0.0; rA = 1.0
        if mono_midi is not None:
            jm = int(np.clip(round(mono_midi) - self.mids[0], 0, len(self.mids) - 1)); mon = 1
            rA_abs = self.res(S, [jm]); rA = rA_abs / tot
            best = rA_abs
            for j in top:
                if j != jm: best = min(best, self.res(S, [jm, j]))
            gA = (rA_abs - best) / tot
        return [gC, rC, bic, snr, nsc, nse, oct_, gA, rA, mon, lvl]
    def run(self, x, mono_frames, times):
        tm = np.array([t for t, _ in mono_frames]); out = []
        for t in times:
            end = int(round(t * 44100))
            S = D.spec(x, end, self.N, 2 * self.N)[self.g.k0:self.g.k1]
            mn = mono_frames[min(int(np.searchsorted(tm, t)), len(tm) - 1)][1]
            f = self.frame(S, mn[0][0] if mn else None)
            out.append(f if f is not None else [0, 0, 0, 0, 0, 0, 0, 0, 1, 0, -9])
        return np.array(out, float)
