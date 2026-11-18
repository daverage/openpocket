#!/usr/bin/env python3
"""An offline note reference for a DI recording without annotations (v49, docs/NOTE-TRACKING.md). Host only.

  python3 tools/sim/track_ref.py IN.wav OUT.csv

librosa's pYIN (2048-sample frames, centred: it looks ahead, as no pedal can) and its onset detector (backtracked to
the energy minimum before each pick). A note runs from an onset to the next onset or to the end of its voiced run;
its pitch is the median of its voiced frames after the first 30 ms, rounded to the nearest semitone when within 30
cents. Notes shorter than 60 ms or with under half their frames voiced are left out. Writes start s, end s, MIDI note
(the format of build/acoustic/lead.truth.csv, velocity 100). An automatic reference, not a transcription.
"""
import sys
import numpy as np
import librosa

x, sr = librosa.load(sys.argv[1], sr=44100, mono=True)
hop = 256
f0, voiced, prob = librosa.pyin(x, fmin=65, fmax=1300, sr=sr, frame_length=2048, hop_length=hop)
on = librosa.onset.onset_detect(y=x, sr=sr, hop_length=hop, backtrack=True, units="frames")
t = librosa.frames_to_time(np.arange(len(f0)), sr=sr, hop_length=hop)
rows = []
for k, a in enumerate(on):
    b = on[k + 1] if k + 1 < len(on) else len(f0)
    seg = np.arange(a, b)
    v = seg[voiced[seg]]
    if len(v) == 0: continue
    end = v[-1] + 1                                      # the end of the voiced run
    s0 = a + int(0.03 * sr / hop)
    vv = v[v >= s0]
    if len(vv) < max(2, 0.5 * (end - s0)): continue
    m = 69 + 12 * np.log2(np.median(f0[vv]) / 440)
    if abs(m - round(m)) < 0.3: m = round(m)
    start, stop = t[a], t[min(end, len(t) - 1)]
    if stop - start < 0.06: continue
    rows.append((start, stop, m))
with open(sys.argv[2], "w") as f:
    f.write(f"# automatic reference: librosa pYIN + onsets of {sys.argv[1]}\n")
    for a, b, m in rows: f.write(f"{a:.4f},{b:.4f},{m:.2f},100\n")
print(f"{len(rows)} notes -> {sys.argv[2]}")
