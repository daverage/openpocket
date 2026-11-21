#!/usr/bin/env python3
"""Audacity/Reaper-style label text -> FastTrack events.csv rows, and an assist that proposes onset candidates.
  python3 tests/fasttrack/labels_import.py import RECORDING LABELS.txt EVENTS.csv [--basis waveform] [--append]
        LABELS.txt: one label per line, tab separated: start_seconds <TAB> end_seconds <TAB> text.  text = ACTION[ pitch_midi[ confidence]]  e.g. 'PICK 57 0.9', 'HAMMER_ON 59', 'HARD_MUTE', 'NOISE 0.4'
  python3 tests/fasttrack/labels_import.py draft AUDIO.wav OUT_LABELS.txt
        writes onset CANDIDATES (a plain energy-rise detector, independent of the FastTrack tracker) as 'CAND' labels for the annotator to correct and classify.  Candidates are NOT annotations;
        an annotator who only deletes false candidates inherits this detector's blind spots, so scroll the whole waveform and add missed onsets by hand.
Actual acoustic timing: place the label at the first waveform rise, zoomed in, not on the metronome grid."""
import sys, os, csv
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gt_format as G
def imp(rec, labels, out, basis='waveform', append=False):
    rows = []
    for ln in open(labels):
        p = ln.rstrip('\n').split('\t')
        if len(p) < 3 or not p[0].strip(): continue
        t = float(p[0]) * 1000; tok = p[2].split(); act = tok[0].upper()
        if act not in G.ACTIONS: print('skip unknown action', tok[0]); continue
        pitch = tok[1] if len(tok) > 1 and not tok[1].replace('.', '', 1).startswith('0.') and float(tok[1]) > 1.0 else ''
        conf = tok[-1] if tok and tok[-1].replace('.', '', 1).isdigit() and float(tok[-1]) <= 1.0 else '0.8'
        end = float(p[1]) * 1000 - t
        rows.append(dict(recording=rec, t_ms=round(t, 1), action=act, pitch_midi=pitch, string='', fret='', expected_event=G.DEFAULT_EXPECTED[act], confidence=conf, timing_basis=basis, end_pitch_midi='', duration_ms=round(end, 1) if end > 1 else '', notes=''))
    new = not (append and os.path.exists(out))
    with open(out, 'a' if append else 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=G.EV_COLS)
        if new: w.writeheader()
        w.writerows(rows)
    print(len(rows), 'events ->', out)
def draft(audio, out):
    import soundfile as sf, numpy as np
    x, sr = sf.read(audio, dtype='float32'); x = x if x.ndim == 1 else x[:, 0]; h = int(0.010 * sr)
    e = np.sqrt(np.convolve(x ** 2, np.ones(h) / h, 'same')); db = 20 * np.log10(e + 1e-7); step = int(0.001 * sr)
    cands = []; last = -1e9
    for i in range(int(0.04 * sr), len(db) - int(0.02 * sr), step):
        if db[i] - np.min(db[i - int(0.04 * sr):i]) > 8 and db[i] > -50 and (i - last) > 0.040 * sr and db[i] >= db[i - step]:
            cands.append(i / sr); last = i
    with open(out, 'w') as f:
        for t in cands: f.write(f'{t:.4f}\t{t:.4f}\tCAND\n')
    print(len(cands), 'candidates ->', out)
if __name__ == '__main__':
    if sys.argv[1] == 'import': imp(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[sys.argv.index('--basis') + 1] if '--basis' in sys.argv else 'waveform', '--append' in sys.argv)
    elif sys.argv[1] == 'draft': draft(sys.argv[2], sys.argv[3])
