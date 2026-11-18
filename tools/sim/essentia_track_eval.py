#!/usr/bin/env python3
"""Score Essentia MultiPitchKlapuri on past-only GuitarSet frames (host only).

Use a separate Python environment with essentia and numpy installed.
Example: python tools/sim/essentia_track_eval.py --takes 24 --frame 2048 --hop 512
"""
import argparse
import time
from pathlib import Path

import essentia
import essentia.standard as es
import numpy as np

from track_eval import ROOT, dyad_entry_delays, dyad_score, jams_notes, matched_rows, poly_score, report_detector


def frames(wav, size, hop, minimum):
    signal = es.MonoLoader(filename=str(wav), sampleRate=44100)()
    detector = es.MultiPitchKlapuri(frameSize=size, hopSize=size, minFrequency=minimum, referenceFrequency=min(55, minimum))
    # The upstream algorithm centers its first frame on sample zero and skips
    # frames with no salience peaks. Prefix half a frame of silence so only the
    # second frame contains audio. This keeps silent rows in the time series.
    buffer = np.zeros(size + size // 2, dtype=np.float32)
    rows = []
    for end in range(size, len(signal) + 1, hop):
        buffer[size // 2:] = signal[end - size:end]
        detector.reset()
        results = detector(buffer)
        if len(results) > 1:
            raise RuntimeError("Essentia returned more than one nonempty frame")
        candidates = [float(f) for f in results[0] if f > 0] if len(results) else []
        row = {"t": end / 44100, "nn": len(candidates)}
        row.update((f"n{i}", f) for i, f in enumerate(candidates, 1))
        rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--takes", type=int, default=24)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--frame", type=int, default=2048)
    parser.add_argument("--hop", type=int, default=512)
    parser.add_argument("--min-frequency", type=float, default=60)
    parser.add_argument("--compare-suffix", action="append", default=[],
                        help="match against existing track_eval.py CSVs with this suffix")
    args = parser.parse_args()
    if min(args.takes, args.frame, args.hop, args.min_frequency) <= 0 or args.offset < 0:
        parser.error("takes, frame and hop must be positive, and offset nonnegative")
    gs = Path(ROOT) / "build" / "guitarset"
    files = sorted((gs / "mono").glob("*_comp_mix.wav"))[args.offset:args.offset + args.takes]
    if not files:
        parser.error("no GuitarSet comping takes found in the requested range")
    dyads, chords, entries = [], [], []
    compare = {suffix: ([], [], []) for suffix in args.compare_suffix}
    start = time.perf_counter()
    for wav in files:
        notes = jams_notes(gs / "annotation" / (wav.stem.removesuffix("_mix") + ".jams"))
        rows = frames(wav, args.frame, args.hop, args.min_frequency)
        dyads.append(dyad_score(rows, notes))
        chords.append(poly_score(rows, notes))
        entries.extend(dyad_entry_delays(rows, notes))
        for suffix, (ref_dyads, ref_chords, ref_entries) in compare.items():
            path = gs / "csv" / (wav.stem.removesuffix("_mix") + suffix + ".csv")
            matched = matched_rows(path, rows)
            ref_dyads.append(dyad_score(matched, notes))
            ref_chords.append(poly_score(matched, notes))
            ref_entries.extend(dyad_entry_delays(matched, notes))
    print(f"Essentia {essentia.__version__}, {len(files)} takes, frame {args.frame}, hop {args.hop}, "
          f"{time.perf_counter() - start:.1f} s host elapsed")
    report_detector("Essentia", dyads, chords, entries)
    for suffix, (ref_dyads, ref_chords, ref_entries) in compare.items():
        report_detector(suffix, ref_dyads, ref_chords, ref_entries)


if __name__ == "__main__":
    main()
