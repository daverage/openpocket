#!/usr/bin/env python3
"""Run the external resonator adapter on past-only GuitarSet and bass fixtures."""
import argparse
import subprocess
import time
import wave
from pathlib import Path

import numpy as np

from track_eval import (
    ROOT, cents, dyad_entry_delays, dyad_score, jams_notes, matched_rows, poly_score, report_detector,
)

RATE = 44100


def read_di(wav):
    with wave.open(str(wav), "rb") as stream:
        if (stream.getframerate(), stream.getsampwidth(), stream.getnchannels()) != (RATE, 2, 1):
            raise ValueError(f"{wav}: expected 44.1 kHz mono PCM16")
        return np.frombuffer(stream.readframes(stream.getnframes()), dtype="<i2") / 32768.0


def detect(samples, args, bass=False):
    factor = args.decimate
    if factor > 1:
        # Causal anti-alias filter. Retain its 32-sample delay in the timestamps.
        n = np.arange(65) - 32
        cutoff = 0.43 / factor
        filt = 2 * cutoff * np.sinc(2 * cutoff * n) * np.hanning(len(n))
        samples = np.convolve(samples, filt / sum(filt))[:len(samples)][factor - 1::factor]
    rate = RATE // factor
    hop = args.hop // factor
    alpha = np.exp(-1000 / (rate * args.decay_ms))
    result = subprocess.run(
        [args.detector, str(rate), str(hop), "23" if bass else "36", str(alpha), str(args.threshold)],
        input=np.asarray(samples, dtype="<f4").tobytes(), capture_output=True, check=True,
    )
    rows = []
    consecutive = {}
    for line in result.stdout.decode().splitlines():
        t, *pitches = map(float, line.split(","))
        if args.confirm > 1:
            # Upstream emits tempered frequencies in descending energy order.
            consecutive = {p: consecutive.get(p, 0) + 1 for p in pitches}
            pitches = [p for i, p in enumerate(pitches) if i == 0 or consecutive[p] >= args.confirm]
        row = {"t": t, "nn": len(pitches)}
        row.update((f"n{i}", f) for i, f in enumerate(pitches, 1))
        rows.append(row)
    return rows


def bass_fixture(low, high):
    # Staggered attacks, a soft repick with the upper note held, then independent mutes.
    notes = [(0.15, 0.78, low, 1.0), (0.43, 1.55, high, 0.65), (0.84, 1.28, low, 0.55)]
    samples = np.zeros(RATE * 2)
    for start, end, midi, amp in notes:
        t = np.arange(round((end - start) * RATE)) / RATE
        pitch = 440 * 2 ** ((midi - 69) / 12)
        envelope = np.minimum(np.minimum(t / 0.004, (end - start - t) / 0.006), 1) * np.exp(-2.1 * t)
        partials = sum(np.sin(2 * np.pi * h * pitch * t + 0.3 * h) / h for h in range(1, 9))
        first = round(start * RATE)
        samples[first:first + len(t)] += 0.12 * amp * envelope * partials
    return samples, [(a, b, midi) for a, b, midi, _ in notes]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("detector", help="path to the compiled Rust host adapter")
    parser.add_argument("--takes", type=int, default=24)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--decimate", choices=(1, 4), type=int, default=1)
    parser.add_argument("--hop", choices=(128, 256, 512), type=int, default=512,
                        help="input samples per candidate report at 44.1 kHz")
    parser.add_argument("--confirm", type=int, default=1,
                        help="consecutive reports before a secondary note is emitted")
    parser.add_argument("--decay-ms", type=float, default=22.6644)
    parser.add_argument("--threshold", type=float, default=0.15)
    parser.add_argument("--compare-suffix", default="_review15")
    args = parser.parse_args()
    if args.takes < 0 or args.offset < 0 or args.decay_ms <= 0 or args.confirm < 1 or not 0 <= args.threshold <= 1:
        parser.error("invalid takes, offset, decay or threshold")
    gs = Path(ROOT) / "build" / "guitarset"
    files = sorted((gs / "mono").glob("*_comp_mix.wav"))[args.offset:args.offset + args.takes]
    if args.takes and not files:
        parser.error("no GuitarSet comping takes found")
    dyads, chords, entries, ref_dyads, ref_chords, ref_entries = [], [], [], [], [], []
    intervals, ref_intervals = {}, {}
    start = time.perf_counter()
    for wav in files:
        stem = wav.stem.removesuffix("_mix")
        notes = jams_notes(gs / "annotation" / f"{stem}.jams")
        rows = detect(read_di(wav), args)
        dyads.append(dyad_score(rows, notes, intervals))
        chords.append(poly_score(rows, notes))
        entries.extend(dyad_entry_delays(rows, notes))
        if args.compare_suffix:
            matched = matched_rows(gs / "csv" / f"{stem}{args.compare_suffix}.csv", rows)
            ref_dyads.append(dyad_score(matched, notes, ref_intervals))
            ref_chords.append(poly_score(matched, notes))
            ref_entries.extend(dyad_entry_delays(matched, notes))
    print(f"{len(files)} DI takes, decimation {args.decimate}, decay {args.decay_ms:g} ms, "
          f"threshold {args.threshold:g}, hop {args.hop}, confirmation {args.confirm}, {time.perf_counter() - start:.2f} s host elapsed")
    if files:
        report_detector("Resonator reference", dyads, chords, entries)
    if ref_dyads:
        report_detector("OpenPocket at matched timestamps", ref_dyads, ref_chords, ref_entries)
    if files:
        for distance in (1, 2, 3, 7, 12):
            hit, count = intervals.get(distance, (0, 0))
            ref_hit, ref_count = ref_intervals.get(distance, (0, 0))
            if count:
                print(f"Interval {distance} semitones: {hit}/{count} reference, {ref_hit}/{ref_count} OpenPocket")
    for low, high in [(23, 38), (28, 43)]:
        samples, notes = bass_fixture(low, high)
        rows = detect(samples, args, bass=True)
        n, both, _, _, _ = dyad_score(rows, notes)
        pitch = 440 * 2 ** ((low - 69) / 12)
        alone = [row for row in rows if 0.18 <= row["t"] < 0.43]
        found = [row for row in alone if any(abs(cents(row[f"n{i}"], pitch)) < 50 for i in range(1, row["nn"] + 1))]
        delays = dyad_entry_delays(rows, notes)
        lingering = [row["t"] for row in rows if 1.28 <= row["t"] < 1.55
                     and any(abs(cents(row[f"n{i}"], pitch)) < 50 for i in range(1, row["nn"] + 1))]
        tail = (max(lingering) - 1.28) * 1000 if lingering else 0.0
        print(f"Synthetic {pitch:.2f} Hz: {len(found)}/{len(alone)} isolated frames, {both}/{n} dyad frames, "
              f"entry times within 100 ms: {delays}, last low candidate {tail:.1f} ms after mute")


if __name__ == "__main__":
    main()
