"""Run the detectors on a real DI recording (no ground truth) -> piano-roll PNG + event-driven synth WAV.
  python3 realdi.py "../../My Audio/scale + duo + power chords di.wav" outdir
Visual/auditory sanity only: nothing here is a measurement."""
import sys, os, warnings
import numpy as np
import scipy.io.wavfile as wf
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import detectors as D
warnings.filterwarnings("ignore")

def load(p):
    sr, d = wf.read(p)
    d = d.astype(np.float64)
    if d.ndim > 1: d = d[:, 0]
    d /= max(np.abs(d).max(), 1)
    if sr != 44100:
        from scipy.signal import resample_poly
        d = resample_poly(d, 44100, sr)
    return (d * 0.3 / max(np.abs(d).max(), 1e-9) * 3).astype(np.float32).clip(-1, 1)

def render(frames, n, sr=44100):
    """Reference synth: one band-limited saw per tracked note, fed only by note events (no audio from the guitar)."""
    y = np.zeros(n); ph = {}; amp = {}
    for (t, notes), (t2, _) in zip(frames, frames[1:] + [(frames[-1][0] + 256 / sr, [])]):
        i0, i1 = int(t * sr) - 128, int(t * sr) + 128
        act = {int(round(m)) for m, a, c in notes}
        for m in act | set(amp):
            tgt = 1.0 if m in act else 0.0
            a0 = amp.get(m, 0.0); a1 = a0 + (tgt - a0) * 0.35
            amp[m] = a1
            if a1 < 1e-3 and tgt == 0: amp.pop(m, None); continue
            f = D.fmidi(m); idx = np.arange(max(i0, 0), min(i1, n))
            p = ph.get(m, 0.0) + 2 * np.pi * f * np.arange(1, len(idx) + 1) / sr
            ph[m] = p[-1] if len(p) else ph.get(m, 0.0)
            saw = sum(np.sin(k * p) / k for k in range(1, 6))
            y[idx] += 0.08 * np.linspace(a0, a1, len(idx)) * saw
    return y / max(np.abs(y).max(), 1e-9) * 0.5

if __name__ == "__main__":
    path, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    x = load(path); base = os.path.splitext(os.path.basename(path))[0].replace(" ", "_")
    dets = {"klap": D.Klap(), "pt": D.PT(resid_gate=0.03, birth_rel=0.3)}
    fig, ax = plt.subplots(len(dets) + 1, 1, figsize=(14, 3 * (len(dets) + 1)), sharex=True)
    ax[0].specgram(x, NFFT=4096, Fs=44100, noverlap=3584, cmap="magma"); ax[0].set_ylim(0, 2500); ax[0].set_title("DI spectrogram")
    for a, (k, d) in zip(ax[1:], dets.items()):
        fr = d.run(x)
        for t, n in fr:
            for m, amp, c in n: a.plot(t, m, "k.", ms=2)
        a.set_ylabel(k); a.set_ylim(36, 90); a.grid(alpha=.3)
        if k == "pt":
            wf.write(os.path.join(out, base + "_pt_synth.wav"), 44100, (render(fr, len(x)) * 32767).astype(np.int16))
    plt.tight_layout(); plt.savefig(os.path.join(out, base + "_roll.png"), dpi=80)
    print("wrote", out)
