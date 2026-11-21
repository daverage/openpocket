#!/usr/bin/env python3
"""Measure fixed Range versus tracked Lowest on known signals and render matching DI WAVs."""
import argparse
import os
import subprocess
import wave
from tempfile import TemporaryDirectory
from pathlib import Path

import numpy as np

from resonator_track_eval import RATE, read_di
from track_eval import ROOT, dyad_score, jams_notes


def process(exe, signal, *mode, env=None):
    data = subprocess.run([exe, *map(str, mode)], input=signal.astype("<f4").tobytes(),
                          capture_output=True, check=True, env=env).stdout
    output = np.frombuffer(data, dtype="<f4")
    if len(output) != len(signal) or not np.isfinite(output).all():
        raise ValueError(f"invalid result in {mode}")
    return output


def sub_trace(exe, signal, mode):
    with TemporaryDirectory() as directory:
        trace = Path(directory) / "sub.csv"
        render = process(exe, signal, *mode, env=dict(os.environ, PX_LOWEST_TRACE=str(trace)))
        t, pitch, gain = np.loadtxt(trace, delimiter=",", unpack=True)
    return render, t, pitch, gain


def sub_measure(exe, signal, mode, wanted):
    render, t, pitch, gain = sub_trace(exe, signal, mode)
    correct = np.abs(pitch - wanted) < 0.028 * wanted
    steady = (t >= 0.31) & (t < 0.42) & correct
    level = np.median(gain[steady]) if np.any(steady) else 0.0
    audible = gain >= max(0.001, level * 0.5)
    onsets = t[(t >= 0.1) & (t < 0.42) & audible & correct]
    chord = (t >= 0.69) & (t < 1.08)
    correct_pc = 100 * np.mean((correct & audible)[chord])
    wrong_pc = 100 * np.mean((~correct & audible)[chord])
    lingering = t[(t >= 1.15) & (t < 1.55) & correct & audible]
    tail = 1000 * (lingering[-1] - 1.15) if len(lingering) else 0
    arrival = f"{1000 * (onsets[0] - 0.1):.1f}" if len(onsets) else "miss"
    metrics = f"correct onset {arrival} ms, chord correct {correct_pc:.0f}%, wrong {wrong_pc:.0f}%, last low {tail:.1f} ms"
    return render, metrics


def fixture(low, upper, harmonic=False):
    t = np.arange(RATE * 2) / RATE
    signal = np.zeros(len(t))
    for f, start, stop, gain in [(low, 0.1, 1.15, 0.14), (upper, 0.48, 1.8, 0.11)]:
        ramp = np.clip(np.minimum((t - start) / 0.004, (stop - t) / 0.006), 0, 1)
        phase = 2 * np.pi * f * (t - start)
        tone = np.sin(phase)
        if harmonic:
            tone = 0.35 * tone + 0.55 * np.sin(2 * phase + 0.3) + 0.28 * np.sin(3 * phase + 0.6)
        signal += gain * ramp * tone
    return signal


def bend_test(exe):
    t = np.arange(2 * RATE) / RATE
    # A 150-cent bend up and back, with a held upper note appearing part-way through.
    cents = np.interp(t, [0, 0.4, 0.9, 1.2, 1.65, 2], [0, 0, 150, 150, 0, 0])
    pitch = 82.4069 * 2 ** (cents / 1200)
    phase = 2 * np.pi * np.cumsum(pitch) / RATE
    lower = np.clip(np.minimum((t - 0.1) / 0.005, (1.85 - t) / 0.006), 0, 1)
    higher = np.clip(np.minimum((t - 0.75) / 0.005, (1.79 - t) / 0.006), 0, 1)
    signal = 0.13 * lower * (np.sin(phase) + 0.55 * np.sin(2 * phase)) + 0.08 * higher * np.sin(2 * np.pi * 123.47 * t)
    print("Synthetic 150-cent lower-note bend and held B2:")
    for mode in ("sub", "sub-harmonic", "sub-refined", "sub-exclusive"):
        _, times, hz, gain = sub_trace(exe, signal, (mode,))
        bending = (times >= 0.4) & (times <= 1.65)
        active = bending & (gain >= 0.012) & (hz > 0)
        expected = np.interp(times[active], t, pitch)
        errors = np.abs(1200 * np.log2(hz[active] / expected))
        if not len(errors):
            print(f"{mode}: no audible frames")
            continue
        print(f"{mode}: audible {100 * active.sum() / bending.sum():.1f}%, "
              f"median/p95 error {np.median(errors):.1f}/{np.percentile(errors, 95):.1f} cents, "
              f"within 50 cents {100 * np.count_nonzero(errors < 50) / bending.sum():.1f}% of bend frames")


def amplitude(x, frequencies, a, b):
    indices = np.arange(round(a * RATE), round(b * RATE))
    phases = 2 * np.pi * indices[:, None] * np.array(frequencies) / RATE
    mat = np.concatenate((np.sin(phases), np.cos(phases)), axis=1)
    fit = np.linalg.lstsq(mat, x[indices], rcond=None)[0].reshape(2, -1)
    return np.linalg.norm(fit, axis=0)


def db(ratio):
    return 20 * np.log10(max(ratio, 1e-9))


def rise(out, pitch, start, ref, limit):
    # Measure wet response with a trailing half-cycle RMS window.
    # Timestamp the middle of the window to limit resolution bias.
    window = round(RATE / pitch / 2)
    for end in range(round(start * RATE) + window, round(limit * RATE), 64):
        if np.sqrt(np.mean(out[end - window:end] ** 2)) >= ref / (2 * np.sqrt(2)):
            return f"{1000 * ((end - window / 2) / RATE - start):.1f}"
    return "—"


def write_wav(path, signal):
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", ""))
        output.writeframes(np.round(np.clip(signal, -1, 1) * 32767).astype("<i2").tobytes())


def score_selection(exe, takes, offset):
    guitarset = Path(ROOT) / "build" / "guitarset"
    files = sorted((guitarset / "mono").glob("*_comp_mix.wav"))[offset:offset + takes]
    if len(files) != takes:
        raise ValueError("requested GuitarSet takes are missing")
    modes = ("lowest", "sub", "sub-harmonic", "sub-refined", "sub-exclusive",
             "sub-low-smooth", "sub-low-hold", "sub-fit", "sub-fit-hybrid")
    scores = {mode: [0, 0] for mode in modes}
    with TemporaryDirectory() as directory:
        trace = Path(directory) / "lowest.csv"
        env = dict(os.environ, PX_LOWEST_TRACE=str(trace))
        for wav in files:
            signal = read_di(wav).astype("<f4").tobytes()
            notes = jams_notes(guitarset / "annotation" / (wav.stem.removesuffix("_mix") + ".jams"))
            for mode in modes:
                subprocess.run([exe, mode], input=signal, env=env, stdout=subprocess.DEVNULL, check=True)
                rows = []
                for line in trace.read_text().splitlines():
                    t, pitch, *gain = map(float, line.split(","))
                    active = pitch > 0 and (not gain or gain[0] > 0.001)
                    rows.append({"t": t, "nn": int(active), "n1": pitch})
                dyads, _, hits, _, _ = dyad_score(rows, notes)
                scores[mode][0] += hits
                scores[mode][1] += dyads
    print(f"Lowest pitch on {takes} GuitarSet comping takes at offset {offset}:")
    for mode, (right, total) in scores.items():
        print(f"{mode}: {right}/{total} settled dyad frames ({100 * right / max(1, total):.1f}%)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--harmonic", action="store_true", help="use weak-fundamental tone pairs")
    parser.add_argument("--takes", type=int, default=0)
    parser.add_argument("--offset", type=int, default=24)
    args = parser.parse_args()
    if args.takes < 0 or args.offset < 0:
        parser.error("takes and offset must be nonnegative")
    print("input Hz         mode     wanted vs full   upper vs wanted   wet arrival ms   old low at mute+20–100 ms")
    for low, upper in [(82.41, 87.31), (82.41, 110), (82.41, 123.47),
                       (82.41, 164.82), (73.42, 110.00), (65.41, 98.00), (61.74, 92.50),
                       (55.00, 82.41), (41.20, 55.00), (30.87, 73.42)]:
        signal = fixture(low, upper, args.harmonic)
        modes = [("full",), ("range", 130.81), ("lowest",), ("oracle", low, upper), ("focused",), ("focused-oracle", low), ("sub",), ("sub-fast",), ("sub-harmonic",), ("sub-refined",), ("sub-exclusive",), ("sub-oracle", low)]
        full = process(args.executable, signal, "full")
        target = (low / 2, upper / 2)
        base = amplitude(full, target, 0.69, 1.08)[0]
        for mode in modes:
            metrics = ""
            if mode[0].startswith("sub"):
                out, metrics = sub_measure(args.executable, signal, mode, low)
            else:
                out = full if mode[0] == "full" else process(args.executable, signal, *mode)
            a, b = amplitude(out, target, 0.69, 1.08)
            before = amplitude(out, [low / 2], 0.26, 0.42)[0]
            tail = amplitude(out, target, 1.17, 1.25)[0]
            print(f"{low:6.2f}/{upper:6.2f} {mode[0]:>14} {db(a/max(base,1e-9)):15.1f} dB "
                  f"{db(b/max(a,1e-9)):14.1f} dB {rise(out, low/2, 0.1, before, 0.42) if before > 0.001 else '—':>15} "
                  f"{db(tail/max(a,1e-9)):18.1f} dB   {metrics}")
    bend_test(args.executable)
    if args.takes:
        score_selection(args.executable, args.takes, args.offset)
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        for name in ("chug", "riff", "chords", "arpeggio", "lead"):
            di = read_di(Path(ROOT) / "assets" / "loops" / f"{name}.wav") * 0.25
            write_wav(args.output / f"{name}-dry.wav", di)
            modes = [("full",), ("range", 130.81), ("lowest",), ("focused",), ("sub",), ("sub-fast",), ("sub-harmonic",), ("sub-refined",), ("sub-exclusive",)]
            wet = {mode[0]: process(args.executable, di, *mode) for mode in modes}
            for mode, render in wet.items():
                write_wav(args.output / f"{name}-{mode}.wav", render)
            # Match wet RMS for a listening comparison while keeping the raw renders above.
            level = np.sqrt(np.mean(wet["range"] ** 2))
            for mode in ("range", "sub", "sub-fast", "sub-harmonic", "sub-refined", "sub-exclusive"):
                render = wet[mode]
                gain = level / max(np.sqrt(np.mean(render ** 2)), 1e-9)
                gain = min(gain, 0.95 / max(np.max(np.abs(render)), 1e-9))
                write_wav(args.output / f"{name}-{mode}-matched.wav", render * gain)


if __name__ == "__main__":
    main()
