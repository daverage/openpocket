#!/usr/bin/env python3
"""Zoomed review strips for an annotation set: waveform + 2 ms level + labels, and a spectrogram below.  python3 review_plots.py AUDIO.wav RECORDING EVENTS.csv OUTPREFIX [strip_s=1.5] [strips_per_image=3]"""
import sys, csv
import numpy as np, soundfile as sf, librosa
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
audio, rec, evp, pre = sys.argv[1:5]; strip = float(sys.argv[5]) if len(sys.argv) > 5 else 1.5; per = int(sys.argv[6]) if len(sys.argv) > 6 else 3
x, sr = sf.read(audio, dtype='float32'); x = x if x.ndim == 1 else x[:, 0]; ev = [r for r in csv.DictReader(open(evp)) if r['recording'] == rec]
h = int(0.002 * sr); e = np.sqrt(np.convolve(x ** 2, np.ones(h) / h, 'same'))
S = librosa.amplitude_to_db(np.abs(librosa.stft(x, n_fft=2048, hop_length=256)), ref=np.max); fr = librosa.fft_frequencies(sr=sr, n_fft=2048); tt = librosa.frames_to_time(np.arange(S.shape[1]), sr=sr, hop_length=256)
nstrips = int(np.ceil(len(x) / sr / strip)); img = 0; col = {'PICK': 'g', 'REPICK': 'lime', 'PALM_MUTE_PICK': 'olive', 'HAMMER_ON': 'm', 'PULL_OFF': 'purple', 'HARD_MUTE': 'r', 'NOISE': 'orange', 'SLIDE': 'c', 'BEND': 'c'}
for k0 in range(0, nstrips, per):
    n = min(per, nstrips - k0); fig, ax = plt.subplots(2 * n, 1, figsize=(19, 3.0 * n), gridspec_kw={'height_ratios': [1.3, 1] * n})
    for j in range(n):
        t0 = (k0 + j) * strip; t1 = min(len(x) / sr, t0 + strip); a, b = ax[2 * j], ax[2 * j + 1]; i0, i1 = int(t0 * sr), int(t1 * sr); t = np.arange(i0, i1) / sr
        a.plot(t, x[i0:i1], lw=.4, color='#555'); a.plot(t, e[i0:i1], lw=.9, color='#d33'); a.set_xlim(t0, t1); a.tick_params(labelsize=7)
        for r in ev:
            tm = float(r['t_ms']) / 1000
            if t0 <= tm <= t1:
                a.axvline(tm, color=col.get(r['action'], 'b'), lw=1.2); a.text(tm, a.get_ylim()[1] * 0.85, f"{r['action'][:4]} {r['pitch_midi']}\n{tm*1000:.0f}", fontsize=7, color=col.get(r['action'], 'b'), va='top')
        m = (tt >= t0) & (tt <= t1); b.imshow(S[(fr < 3500)][:, m][::-1], aspect='auto', extent=[t0, t1, 0, 3500], cmap='magma', vmin=-70); b.set_xlim(t0, t1); b.tick_params(labelsize=7)
    plt.tight_layout(); plt.savefig(f'{pre}_{img}.png', dpi=60); plt.close(); img += 1
print(img, 'images')
