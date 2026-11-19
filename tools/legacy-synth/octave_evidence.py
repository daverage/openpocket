#!/usr/bin/env python3
"""Compare static and causal octave evidence in the existing resonator dump.

Use the pulse_poly_bench binary to measure the input bank. GuitarSet
annotations identify the lower pitch and provide the one- or two-note label.
This measures candidate evidence with a known root, before voice allocation.
"""

import argparse
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

import numpy as np

from owner_di import LOW_E, SCALE
from resonator_track_eval import RATE, read_di
from track_eval import ROOT, jams_notes


# Layout and cadence of PXP_DUMP in pulse_poly_bench.c and px_resonator_test.h.
FIRST_MIDI = 23
BANDS = 66
HOP = 128
PARTIALS = np.array([0, 12, 19, 24, 28, 31])
HISTORY = 16  # 46 ms, including the current report
RECENT = 4


def bank_power(bench, samples):
    with TemporaryDirectory() as directory:
        dump = Path(directory) / "bank.f32"
        subprocess.run(
            [bench, "-"],
            input=np.asarray(samples, dtype="<f4").tobytes(),
            stdout=subprocess.DEVNULL,
            env=dict(os.environ, PXP_DUMP=str(dump)),
            check=True,
        )
        reports = np.fromfile(dump, dtype="<f4").reshape(-1, 2 + 2 * BANDS)
    return np.sum(reports[:, 2:].reshape(-1, BANDS, 2) ** 2, axis=2)


def classify(power, notes):
    if len(power) < HISTORY:
        return
    time = np.arange(1, len(power) + 1) * HOP / RATE
    active = np.zeros((len(time), 128), dtype=bool)
    onset = np.full((len(time), 128), np.inf)
    for start, stop, pitch in notes:
        midi = round(pitch)
        contains = (time >= start) & (time < stop)
        active[contains, midi] = True
        onset[contains, midi] = np.minimum(onset[contains, midi], start)
    note_count = active.sum(axis=1)
    root = active.argmax(axis=1)
    for midi in range(FIRST_MIDI, FIRST_MIDI + BANDS - PARTIALS[-1]):
        root_here = root == midi
        upper = active[:, midi + 12]
        indices = midi - FIRST_MIDI + PARTIALS
        partials = power[:, indices]
        p1, p2 = partials[:, :2].T

        # Work in log power so shared gain changes cancel. Average the
        # differences over several reports to reduce phase beating.
        logged = 10 * np.log10(np.maximum(partials, 1e-12))
        odd = logged[:, ::2].mean(axis=1)
        even = logged[:, 1::2].mean(axis=1)
        contrast = even - odd
        mean_recent = np.convolve(contrast, np.ones(RECENT) / RECENT, mode="full")[:len(time)]
        mean_prior = np.convolve(contrast, np.ones(HISTORY - RECENT) / (HISTORY - RECENT),
                                 mode="full")[:len(time) - RECENT]
        rise = mean_recent[RECENT:] - mean_prior
        rise = np.r_[np.full(RECENT, np.nan), rise]
        # Positive means all four tests in the fixed v67 check pass.
        static = np.min([
            logged[:, 1] - logged[:, 0] - 10 * np.log10(0.55),
            logged[:, 1] - logged[:, 2] + 10 * np.log10(0.10),
            logged[:, 3] - logged[:, 1] - 10 * np.log10(0.04),
            logged[:, 5] - logged[:, 1] - 10 * np.log10(0.025),
        ], axis=0)
        # Require 46 ms of causal history and an audible resonator. Include
        # challenging single-string cases with an appreciable H2 as controls.
        valid = root_here & (np.arange(len(time)) >= HISTORY - 1) & (p2 > 1e-8) & (p2 > 0.15 * p1)
        diff = np.subtract(np.where(upper, onset[:, midi + 12], time), onset[:, midi],
                           out=np.zeros_like(time), where=root_here & (note_count > 0))
        labels = {
            "single": (note_count == 1) & (diff >= 0.03),
            "together": (note_count == 2) & upper & (np.abs(diff) <= 0.02),
            "upper enters": (note_count == 2) & upper & (diff >= 0.03) & (time - onset[:, midi + 12] < 0.07),
            "upper held": (note_count == 2) & upper & (diff >= 0.03) & (time - onset[:, midi + 12] >= 0.07),
            "lower enters": (note_count == 2) & upper & (diff <= -0.03) & (time - onset[:, midi] < 0.07),
        }
        for label, mask in labels.items():
            keep = valid & mask
            if keep.any():
                yield label, np.column_stack((
                    static[keep], contrast[keep], rise[keep], mean_recent[keep],
                    mean_recent[keep] + np.maximum(rise[keep], 0),
                ))


def auc(positive, negative):
    ordered = np.sort(negative)
    lower = np.searchsorted(ordered, positive, side="left")
    upper = np.searchsorted(ordered, positive, side="right")
    return np.mean((lower + upper) / (2 * len(ordered)))


def comping(bench, paths, annotations):
    groups = {}
    for path in paths:
        notes = jams_notes(annotations / (path.stem.removesuffix("_mix") + ".jams"))
        for label, evidence in classify(bank_power(bench, read_di(path)), notes):
            groups.setdefault(label, []).append(evidence)
    return {label: np.concatenate(values) for label, values in groups.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bench")
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--takes", type=int, default=24)
    parser.add_argument("--owner-di", type=Path, help="include both owner DI recordings as controls")
    args = parser.parse_args()
    data = Path(ROOT) / "build" / "guitarset"
    all_paths = sorted((data / "mono").glob("*_comp_mix.wav"))
    paths = all_paths[args.offset:args.offset + args.takes]
    if args.offset < 0 or args.takes <= 0 or not paths:
        parser.error("expected a populated GuitarSet range")
    groups = comping(args.bench, paths, data / "annotation")
    if "single" not in groups:
        parser.error("no usable single-note reference frames")
    singles = groups.pop("single")
    calibration = singles
    if args.offset != 0 or args.takes != 24:
        calibration = comping(args.bench, all_paths[:24], data / "annotation")["single"]
    static_control_rate = np.mean(calibration[:, 0] > 0)
    thresholds = np.r_[0, np.quantile(calibration[:, 1:], 1 - static_control_rate, axis=0)]

    def rates(evidence):
        values = 100 * np.mean(evidence > thresholds, axis=0)
        return " / ".join(f"{rate:.2f}%" for rate in values)

    print(f"GuitarSet comping {args.offset}–{args.offset + len(paths) - 1}: {len(singles)} difficult single-note frames")
    print("Columns: static / even-odd / rise / recent even-odd / recent + positive rise")
    print("Match development control rates first")
    print("Thresholds (dB): " + " / ".join(f"{threshold:.5f}" for threshold in thresholds))
    print(f"Controls admitted: {rates(singles)}")
    print(f"{'class':18s} {'frames':>7s}     {'admitted':29s}     AUC")
    for label, evidence in groups.items():
        areas = [auc(evidence[:, i], singles[:, i]) for i in range(evidence.shape[1])]
        print(f"{label:18s} {len(evidence):7d}     {rates(evidence):29s}"
              f"     {' / '.join(f'{area:.3f}' for area in areas)}")
    if args.owner_di:
        import soundfile as sf

        for name, notes in [("scale + duo + power chords di.wav", SCALE), ("low e open di.wav", LOW_E)]:
            samples, rate = sf.read(args.owner_di / name, dtype="float32", always_2d=True)
            if rate != RATE:
                parser.error(f"{name}: expected {RATE} Hz")
            controls = [evidence for label, evidence in classify(bank_power(args.bench, samples[:, 0]), notes)
                        if label == "single"]
            if controls:
                evidence = np.concatenate(controls)
                print(f"{name}: {len(evidence)} difficult single-note frames, admitted {rates(evidence)}")


if __name__ == "__main__":
    main()
