#!/usr/bin/env python3
"""MACHINE-ASSISTED DRAFT annotation of an unlabelled DI clip (NOT ground truth, NOT listened to).  Independent of the FastTrack tracker: plain energy rise + spectral features + librosa pYIN.
Events: onset candidates (energy rise), refined to the first waveform rise; level collapses (damp candidates).  Per event: features and a proposed class by transparent rules.
  pluck burst  = energy in 2-8 kHz in the 6 ms after the onset vs the 6 ms before (dB): a pick/finger pluck makes a broadband click, a hammer-on/pull-off much less
  ring_before  = level just before the onset relative to the clip's recent peak: a ringing note exists
  pitch_before / pitch_after = pYIN median in [-80,-10] ms and [+50,+150] ms
  damp         = fast level falls by >= 20 dB within 40 ms
Rules (first match): damp -> HARD_MUTE; low level (<0.004 rms after) or unvoiced after -> NOISE; ringing note + pitch changes >= 0.7 semitone + weak burst -> HAMMER_ON (up) / PULL_OFF (down);
 otherwise pluck: level collapses to < 20 % within 120 ms -> PALM_MUTE_PICK; same pitch as before while ringing -> REPICK; else PICK.  Confidence = 0.4 .. 0.7 by how far each feature is from its boundary; never above 0.7: a person must confirm.
Usage: python3 tests/archive/fasttrack/draft_annotate.py AUDIO.wav RECORDING_NAME OUT_DIR"""
import sys, os, csv
import numpy as np, soundfile as sf, librosa
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gt_format as G
def main(audio, rec, outdir):
    x, sr = sf.read(audio, dtype='float32'); x = x if x.ndim == 1 else x[:, 0]
    h = int(0.010 * sr); e = np.sqrt(np.convolve(x ** 2, np.ones(h) / h, 'same')); db = 20 * np.log10(e + 1e-7); step = int(0.001 * sr)
    cand = []; last = -1e9
    for i in range(int(0.04 * sr), len(db) - int(0.02 * sr), step):
        if db[i] - np.min(db[i - int(0.04 * sr):i]) > 8 and db[i] > -50 and (i - last) > 0.040 * sr and db[i] >= db[i - step]: cand.append(i); last = i
    # refine to the first waveform rise: scan back from the candidate to where |x| first exceeds 4x the pre-event noise
    def refine(i):
        a = max(0, i - int(0.040 * sr)); pre = np.percentile(np.abs(x[max(0, i - int(0.060 * sr)):max(1, i - int(0.040 * sr))]), 90) + 1e-5
        seg = np.abs(x[a:i + int(0.010 * sr)]); thr = max(4 * pre, 0.004); k = np.argmax(seg > thr) if (seg > thr).any() else i - a
        return a + int(k)
    f0, vf, vp = librosa.pyin(x, fmin=60, fmax=900, sr=sr, frame_length=2048, hop_length=128); tf = librosa.times_like(f0, sr=sr, hop_length=128)
    def pitch(t0, t1):
        m = (tf >= t0) & (tf <= t1) & vf; return (float(np.nanmedian(f0[m])), float(np.mean(vp[(tf >= t0) & (tf <= t1)]))) if m.sum() >= 4 else (None, 0.0)
    S = np.abs(librosa.stft(x, n_fft=512, hop_length=64)); fr = librosa.fft_frequencies(sr=sr, n_fft=512); hf = (fr >= 2000) & (fr <= 8000); hfe = 10 * np.log10(S[hf].sum(0) ** 2 + 1e-12); ts = librosa.frames_to_time(np.arange(S.shape[1]), sr=sr, hop_length=64)
    events = []; seen = []
    for c in cand:
        i = refine(c); t = i / sr
        if any(abs(t - u) < 0.020 for u in seen): continue          # two candidates refined to the same waveform rise
        seen.append(t)
        after = hfe[(ts >= t) & (ts < t + 0.006)].max() if ((ts >= t) & (ts < t + 0.006)).any() else -120; before = np.median(hfe[(ts >= t - 0.012) & (ts < t - 0.004)]) if ((ts >= t - 0.012) & (ts < t - 0.004)).any() else -120
        burst = after - before; lv_after = float(np.sqrt(np.mean(x[i:i + int(0.030 * sr)] ** 2))); lv_before = float(np.sqrt(np.mean(x[max(0, i - int(0.030 * sr)):i] ** 2)) + 1e-9)
        peak150 = float(np.sqrt(np.mean(x[max(0, i - int(0.150 * sr)):max(1, i - int(0.020 * sr))] ** 2)) + 1e-9); ring = lv_before / (peak150 + 1e-9)
        pb, _ = pitch(t - 0.080, t - 0.010); pa, vq = pitch(t + 0.050, t + 0.150); dsemi = 12 * np.log2(pa / pb) if pa and pb else None
        tail = float(np.sqrt(np.mean(x[i + int(0.100 * sr):i + int(0.140 * sr)] ** 2))) if i + int(0.140 * sr) < len(x) else lv_after; decay = tail / (lv_after + 1e-9)
        events.append(dict(t=t, burst=burst, lv_after=lv_after, lv_before=lv_before, ring=ring, pb=pb, pa=pa, vq=vq, dsemi=dsemi, decay=decay))
    # damp candidates: fast level falls >= 20 dB within 40 ms (starting from a level above the gate)
    fe = np.sqrt(np.convolve(x ** 2, np.ones(int(0.004 * sr)) / int(0.004 * sr), 'same')); damps = []; k = int(0.04 * sr); lastd = -1e9
    for i in range(int(0.05 * sr), len(fe) - k, int(0.002 * sr)):
        if fe[i] > 0.01 and fe[i + k] < 0.1 * fe[i] and i - lastd > 0.15 * sr: damps.append(i); lastd = i
    rows = []
    for ev in events:
        t = ev['t']; conf = 0.5; act = 'PICK'; pm = ''; note = f"burst {ev['burst']:.0f} dB, ring {ev['ring']:.2f}, pitch {ev['pb'] and round(ev['pb'],1)}->{ev['pa'] and round(ev['pa'],1)}, decay {ev['decay']:.2f}"
        if ev['lv_after'] < 0.004: act, conf = 'NOISE', 0.4                                   # below the gate: not a note
        elif (ev['pa'] is None or ev['vq'] < 0.3) and ev['burst'] < 12: act, conf = 'NOISE', 0.3   # no pluck burst and no stable pitch: uncertain, kept at low confidence
        elif ev['ring'] > 0.15 and ev['dsemi'] is not None and abs(ev['dsemi']) >= 0.7 and ev['burst'] < 12: act, conf = ('HAMMER_ON' if ev['dsemi'] > 0 else 'PULL_OFF'), 0.5
        else:
            if ev['pa'] is None or ev['vq'] < 0.3: conf = 0.4      # a pluck burst with an unreliable pitch estimate (the transient): pitch left to the human
            if ev['decay'] < 0.2: act, conf = 'PALM_MUTE_PICK', 0.55
            elif ev['ring'] > 0.15 and ev['dsemi'] is not None and abs(ev['dsemi']) < 0.5: act, conf = 'REPICK', 0.55
            else: act, conf = 'PICK', 0.6
            if ev['pa'] is None or ev['vq'] < 0.3: conf = min(conf, 0.4)
            elif ev['burst'] > 20: conf = min(0.7, conf + 0.1)
        if act in ('PICK', 'REPICK', 'PALM_MUTE_PICK', 'HAMMER_ON', 'PULL_OFF') and ev['pa']: pm = round(69 + 12 * np.log2(ev['pa'] / 440.0))
        rows.append(dict(recording=rec, t_ms=round(t * 1000, 1), action=act, pitch_midi=pm, string='', fret='', expected_event=G.DEFAULT_EXPECTED[act], confidence=round(conf, 2), timing_basis='spectrogram', end_pitch_midi='', duration_ms='', notes='DRAFT (machine-assisted, not listened to): ' + note))
    for i in damps:
        rows.append(dict(recording=rec, t_ms=round(i / sr * 1000, 1), action='HARD_MUTE', pitch_midi='', string='', fret='', expected_event='DAMP', confidence=0.5, timing_basis='waveform', end_pitch_midi='', duration_ms='', notes='DRAFT: level fell >= 20 dB within 40 ms'))
    rows.sort(key=lambda r: r['t_ms']); os.makedirs(outdir, exist_ok=True)
    p = os.path.join(outdir, 'events.csv'); new = not os.path.exists(p)
    with open(p, 'a', newline='') as f:
        w = csv.DictWriter(f, fieldnames=G.EV_COLS)
        if new: w.writeheader()
        w.writerows(rows)
    import collections; print(rec, len(rows), 'draft events', dict(collections.Counter(r['action'] for r in rows)))
    return rows
if __name__ == '__main__': main(*sys.argv[1:4])
