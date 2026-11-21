#!/usr/bin/env python3
"""Benchmark Lowest Sub on labeled isolated bass recordings and staggered mixes.

Example: bass_di_eval.py build/lowest_octave --pair finger 28 E.wav 33 A.wav
Input samples must represent the labeled pitches at their native tuning.
"""
import argparse
import os
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory

import numpy as np

from lowest_octave_eval import RATE, process, sub_trace, write_wav


def load_note(path):
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"],
        capture_output=True, check=True,
    ).stdout
    samples = np.frombuffer(raw, dtype="<f4").copy()
    block = RATE // 500
    samples = samples[: (len(samples) // block) * block]
    level = np.sqrt(np.mean(samples.reshape(-1, block) ** 2, axis=1))
    if not len(level) or not np.max(level) > 0:
        raise ValueError(f"no audio in {path}")
    start = max(0, (np.flatnonzero(level > 0.05 * np.max(level))[0] - 1) * block)
    note = samples[start:]
    peak = np.max(np.abs(note))
    return note / peak


def place(signal, note, start, end, gain):
    first, stop = round(start * RATE), round(end * RATE)
    size = stop - first
    if len(note) < size:
        raise ValueError(f"source note is shorter than the {end - start:.2f} s fixture segment")
    voice = note[:size].copy()
    tail = round(0.006 * RATE)
    voice[-tail:] *= np.linspace(1, 0, tail)
    signal[first:stop] += gain * voice


def fixture(low, upper, upper_gain, simultaneous=False):
    signal = np.zeros(round(1.8 * RATE), dtype=np.float32)
    place(signal, low, 0.1, 0.75, 0.24)
    place(signal, upper, 0.10 if simultaneous else 0.34, 1.51, upper_gain)
    place(signal, low, 0.9, 1.29, 0.15)
    return signal


def near_pitch(pitch, target):
    return (pitch >= target * 2 ** (-50 / 1200)) & (pitch <= target * 2 ** (50 / 1200))


def near_note(pitch, midi):
    return near_pitch(pitch, 440 * 2 ** ((midi - 69) / 12))


def waveform_metrics(render, time, pitch, low_hz, high_hz):
    # Use the actual wet samples ending at each trace report. The trace labels
    # pitch because a 128-sample window cannot resolve a bass sub's frequency.
    end = np.round(time * RATE).astype(int)
    start = np.r_[0, end[:-1]]
    power = np.r_[0, np.cumsum(render.astype(np.float64) ** 2)]
    rms = np.sqrt((power[end] - power[start]) / (end - start))
    sounding = rms >= 0.002  # -54 dBFS RMS, identical across builds
    low = near_pitch(pitch, low_hz)
    high = near_pitch(pitch, high_hz)
    first = (time >= 0.1) & (time < 0.34)
    held = (time >= 0.42) & (time < 0.75)
    gap = (time >= 0.75) & (time < 0.90)
    repick = (time >= 0.90) & (time < 1.29)
    lower_playing = ((time >= 0.1) & (time < 0.75)) | repick
    upper_only = gap | ((time >= 1.29) & (time < 1.51))
    after = (time >= 1.51) & sounding
    wrong = sounding & ((lower_playing & ~low) | (upper_only & ~high))

    def onset(window, event):
        matches = time[window & sounding & low]
        return (matches[0] - event) * 1000 if len(matches) else float("nan")

    def tail(window, event):
        return (time[window][-1] - event) * 1000 if np.any(window) else 0.0

    duration = np.sum((end - start)[wrong]) * 1000 / RATE
    level = 20 * np.log10(np.sqrt(np.mean(rms[wrong] ** 2))) if np.any(wrong) else float("nan")
    return (onset(first, 0.1), onset(repick, 0.90), duration, level,
            tail(gap & sounding & low, 0.75), tail(after, 1.51),
            100 * np.mean((low & sounding)[held]))


def candidate_metrics(exe, signal, mode, low_midi):
    with TemporaryDirectory() as directory:
        path = Path(directory) / "candidates.csv"
        process(exe, signal, mode, env=dict(os.environ, PX_LOWEST_CANDIDATES=str(path)))
        data = np.loadtxt(path, delimiter=",")
    time, pending, selected = data[:, :3].T
    raw = data[:, 3:]
    stages = np.column_stack((np.any(near_note(raw, low_midi), axis=1),
                              near_note(raw[:, 0], low_midi),
                              near_note(pending, low_midi), near_note(selected, low_midi)))
    windows = ((0.18, 0.34), (0.42, 0.75), (0.81, 0.9), (0.98, 1.29))
    return np.array([100 * np.mean(stages[(time >= start) & (time < stop)], axis=0)
                     for start, stop in windows])


def measure(exe, signal, mode, low_midi, high_midi):
    render, time, pitch, gain = sub_trace(exe, signal, (mode,))
    correct = near_note(pitch, low_midi)
    sounding = gain > 0.002
    good = correct & sounding
    wrong = sounding & ~correct
    octave = wrong & near_note(pitch, low_midi + 12)
    upper = wrong & ~octave & near_note(pitch, high_midi)
    before = (time >= 0.18) & (time < 0.34)
    held = (time >= 0.42) & (time < 0.75)
    repick = (time >= 0.98) & (time < 1.29)
    ghost = (time >= 0.75) & (time < 0.9) & good
    solo_good = (time >= 0.1) & (time < 0.34) & good
    onset = time[solo_good] - 0.1
    continuous = solo_good[:-3] & solo_good[1:-2] & solo_good[2:-1] & solo_good[3:]
    lock = time[:-3][continuous] - 0.1
    resume = time[(time >= 0.9) & (time < 1.29) & good] - 0.9
    pct = lambda values, area: 100 * np.mean(values[area])
    fmt = lambda values: f"{1000 * values[0]:.1f}" if len(values) else "miss"
    tail = 1000 * (time[ghost][-1] - 0.75) if np.any(ghost) else 0.0
    return render, (f"{fmt(onset):>5} {fmt(lock):>5} {pct(good, before):6.1f}% "
                    f"{pct(good, held):6.1f}% {pct(wrong, held):6.1f}% "
                    f"{pct(octave, held):6.1f}% {pct(upper, held):6.1f}% {fmt(resume):>5} "
                    f"{pct(good, repick):6.1f}% {tail:6.1f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("--pair", nargs=5, action="append", metavar=("LABEL", "LOW_MIDI", "LOW_WAV", "HIGH_MIDI", "HIGH_WAV"),
                        required=True)
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--output", type=Path)
    output.add_argument("--candidates", action="store_true", help="measure raw and selected candidate identity")
    parser.add_argument("--mode", action="append", help="evaluate only the specified host mode, repeatable")
    parser.add_argument("--simultaneous", action="store_true", help="start both recorded notes together")
    parser.add_argument("--waveform", action="store_true", help="score the actual wet samples using pitch trace labels")
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
    modes = args.mode or ("sub", "sub-harmonic", "sub-refined", "sub-exclusive")
    early = "early" if args.simultaneous else "solo"
    if args.candidates:
        print(f"fixture                          mode            {early:22} dyad                   after mute             repick")
        print("                                             raw/min/proposed/selected, percent of reports")
    elif args.waveform:
        print("fixture                          mode           onset repick wrong_ms wrong_dBFS low_tail all_tail dyad_pct")
    else:
        print(f"fixture                          mode           onset  lock   {early:>5}    dyad   wrong    +12    upper repick    held   tail")
    for label, low, low_file, high, high_file in args.pair:
        low, high = int(low), int(high)
        if not 0 < low < high < 128:
            parser.error("expected ordered MIDI note numbers")
        if Path(label).name != label or label in (".", ".."):
            parser.error("label must be a file name")
        bass, upper = load_note(low_file), load_note(high_file)
        for level, high_gain in (("equal", 0.24), ("quiet-upper", 0.10)):
            tag = f"{label}-{low}-{high}-{level}"
            if args.simultaneous:
                tag += "-together"
            signal = fixture(bass, upper, high_gain, args.simultaneous)
            if args.output:
                write_wav(args.output / f"{tag}-dry.wav", signal)
            for mode in modes:
                if args.candidates:
                    stats = candidate_metrics(args.executable, signal, mode, low)
                    print(f"{tag:32} {mode:14} " + " ".join("/".join(f"{value:5.1f}" for value in row) for row in stats))
                    continue
                if args.waveform:
                    render, time, pitch, _ = sub_trace(args.executable, signal, (mode,))
                    values = waveform_metrics(render, time, pitch,
                                              440 * 2 ** ((low - 69) / 12),
                                              440 * 2 ** ((high - 69) / 12))
                    stats = " ".join(f"{value:8.1f}" for value in values)
                else:
                    render, stats = measure(args.executable, signal, mode, low, high)
                print(f"{tag:32} {mode:14} {stats}")
                if args.output:
                    write_wav(args.output / f"{tag}-{mode}.wav", render)


if __name__ == "__main__":
    main()
