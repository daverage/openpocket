#!/usr/bin/env python3
"""Render the synth tracking prototype and generate its listening page."""
import argparse
import html
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
FASTTRACK = ROOT / "tests/archive/fasttrack"
OUT = ROOT / "build/synth-tracking-prototype"
SR = 44100
MODES = ("Current", "Direct envelope", "Gate and pitch", "No harmonic hold")
VOICES = ("Analog Classic", "Choir AH", "Ensemble Strings", "Ensemble Organ")
CASES = (
    ("Low E", .40, .220, 82.41),
    ("E3", 1.00, .220, 164.81),
    ("High E", 1.60, .220, 329.63),
    ("Short low E", 2.20, .018, 82.41),
    ("Short E3", 2.80, .018, 164.81),
    ("Short high E", 3.40, .018, 329.63),
    ("A3 after A2", 4.18, .220, 220.00),
    ("Quiet E3 repick", 5.16, .220, 164.81),
)


def compile_renderers(scratch):
    engine = scratch / "engine"
    shutil.copytree(ROOT / "engine", engine)
    (scratch / "third_party").symlink_to(ROOT / "third_party", target_is_directory=True)
    adapter = engine / "px_synth_adapter.h"
    source = adapter.read_text()
    call = "px_mono_run(e, &s->mono, in[k], &o);"
    assert source.count(call) == 1
    prototype = source.replace(call, "proto_track(e, s, in[k], &o);")
    accent = "(1.0f + s->pick_accent)"
    assert prototype.count(accent) == 1
    prototype = prototype.replace(accent, "(1.0f + ((proto_mode == ENVELOPE || proto_mode == GATE)"
                                  " ? 0.0f : s->pick_accent))")
    hook = "static void px_s2_voice("
    assert prototype.count(hook) == 1
    prototype = prototype.replace(hook, '#include "tracking.h"\n' + hook)
    adapter.write_text(prototype)
    binaries = {}
    for variant, flag in (("main", []), ("no-hold", ["-DPX_MONO_CONFIRM_MS=0"])):
        binary = scratch / variant
        subprocess.run([os.environ.get("CC", "cc"), "-O2", "-w", "-fno-builtin",
                        "-ffp-contract=off", f"-I{engine}", f"-I{HERE}", f"-I{FASTTRACK}",
                        *flag, HERE / "render.c", ROOT / "firmware/px_math.c", "-lm", "-o", binary],
                       check=True)
        binaries[variant] = binary
    return binaries


def pluck(signal, start, length, hz, level=.18):
    t = np.arange(round(length * SR)) / SR
    env = (1 - np.exp(-t / .003)) * np.exp(-t / .15)
    if length < .025:
        env *= np.clip((length - t) / .001, 0, 1)
    offset = round(start * SR)
    signal[offset:offset + len(t)] += level * env * sum(
        np.sin(h * 2 * np.pi * hz * t) / h for h in range(1, 7))


def fixture(path):
    x = np.zeros(6 * SR)
    for name, start, length, pitch in CASES:
        pluck(x, start, length, pitch, .07 if name.startswith("Quiet") else .18)
    pluck(x, 4.0, .18, 110)
    pluck(x, 5.00, .32, 164.81)
    sf.write(path, x, SR, subtype="PCM_16")


def render(binaries, mode, voice, source, raw):
    binary = binaries["no-hold" if mode == 3 else "main"]
    trace = subprocess.check_output([binary, str(mode), str(voice), source, raw], text=True)
    time, pitch, gate, events = np.fromstring(trace, sep=" ").reshape(-1, 4).T
    audio, _ = sf.read(raw)
    return (time, pitch, gate > 0, events), audio


def rolling_rms(audio, ms):
    width = round(ms * SR / 1000)
    return np.sqrt(np.convolve(audio ** 2, np.ones(width) / width, "full")[:len(audio)])


def pluck_metrics(trace, audio):
    time, pitch, gate, events = trace
    rms = rolling_rms(audio, 5)
    rows = []
    for index, (name, start, length, hz) in enumerate(CASES):
        # Observe the tail of the deliberately short plucks as well.
        stop = start + max(length, .10)
        tr = (time >= start) & (time < stop) & gate
        correct = tr & (pitch >= hz * 2 ** (-50/1200)) & (pitch <= hz * 2 ** (50/1200))
        onset = 1000 * (time[correct][0] - start) if correct.any() else np.nan
        envelope = rms[round(start * SR):round(stop * SR)]
        peak = float(np.max(envelope))
        half = 1000 * np.flatnonzero(envelope >= peak / 2)[0] / SR if peak > 1e-4 else np.nan
        new_events = np.diff(events, prepend=0)[(time >= start) & (time < stop)].sum()
        tail = ""
        if index == 0 and peak > 1e-4:
            decay = rms[round(stop * SR):round(CASES[1][1] * SR)]
            audible = np.flatnonzero(decay > peak * 10 ** (-30 / 20))
            tail = f"{(audible[-1] + 1) * 1000 / SR:.1f}" if len(audible) else "0.0"
        rows.append((name, peak, half, onset, int(new_events), tail))
    return rows


def match_level(audio, path):
    rms = rolling_rms(audio, 10)
    active = rms[rms > 10 ** (-50 / 20)]
    if len(active):
        gain = min(10 ** (-24 / 20) / np.sqrt(np.mean(active ** 2)),
                   10 ** (-1 / 20) / np.max(np.abs(audio)))
        audio = audio * gain
    sf.write(path, audio, SR, subtype="PCM_24")


def truth_metrics(trace, annotations):
    time, pitch, gate, _ = trace
    right = heard = total = 0
    arrivals = []
    first_wrong = 0
    for i, (on, off, midi) in enumerate(annotations):
        end = min(off, annotations[i + 1, 0]) if i + 1 < len(annotations) else off
        window = (time >= on) & (time < end)
        sounding = window & gate
        hz = 440 * 2 ** ((midi - 69) / 12)
        match = sounding & (pitch >= hz * 2 ** (-50/1200)) & (pitch <= hz * 2 ** (50/1200))
        total += window.sum()
        right += match.sum()
        heard += sounding.sum()
        if i and midi == annotations[i - 1, 2]:
            continue
        if sounding.any() and not match[np.flatnonzero(sounding)[0]]:
            first_wrong += 1
        if match.any():
            arrivals.append((time[match][0] - on) * 1000)
    return total, heard, right, first_wrong, arrivals


def compare_truth(binaries, directory, raw):
    lines = []
    for category, pattern in (("Scales", "*_scales_*.csv"), ("Palm mutes", "*PalmMute*.csv")):
        paths = sorted(directory.glob(pattern))
        if not paths:
            continue
        lines.append(f"{category} ({len(paths)} takes)")
        for mode, name in enumerate(MODES):
            totals = np.zeros(4, dtype=int)
            times = []
            for labels in paths:
                trace, _ = render(binaries, mode, 0, labels.with_suffix(".wav"), raw)
                notes = np.atleast_2d(np.loadtxt(labels, delimiter=","))
                *counts, arrivals = truth_metrics(trace, notes)
                totals += counts
                times.extend(arrivals)
            total, gated, right, wrong = totals
            median, p90 = np.percentile(times, [50, 90]) if times else (np.nan, np.nan)
            lines.append(f"{name:17s} correct {right/total:6.1%}, "
                         f"wrong while gated {(gated-right)/total:5.1%}, "
                         f"first gated frame off pitch {wrong:3d}, "
                         f"arrival {median:.1f}/{p90:.1f} ms (median/p90)")
    if not lines:
        raise ValueError(f"No scale or palm-mute CSVs in {directory}")
    report = "\n".join(lines) + "\n"
    (OUT / "pitch.txt").write_text(report)
    print(report, end="")


def listening_page(clips):
    sections = []
    for clip in clips:
        label = html.escape(clip.stem)
        table = ["<table><thead><tr><th>Patch</th>" +
                 "".join(f"<th>{mode}</th>" for mode in MODES) + "</tr></thead><tbody>"]
        for i, voice in enumerate(VOICES):
            cells = "".join(f'<td><audio controls preload="none" src="{label}-{i}-{j}.wav">'
                            f'</audio></td>' for j in range(len(MODES)))
            table.append(f"<tr><th>{voice}</th>{cells}</tr>")
        sections.append(f"<h2>{label}</h2><audio controls preload='none' src='{label}-di.wav'>"
                        "</audio>" + "".join(table) + "</tbody></table>")
    (OUT / "index.html").write_text("""<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Synth tracking comparison</title>
<style>
body{font:16px system-ui,sans-serif;margin:2rem;line-height:1.5}
td,th{padding:.4rem;text-align:left}audio{width:180px}body{overflow-x:auto}
</style>
<h1>Synth tracking comparison</h1>
<p>Listen for notes that arrive late, wrong pitches at the start of a note,
and whether each pick still sounds distinct. Names under each clip are synth patches.
The first player in each section is the dry guitar. Wet renders are level-matched.</p>
""" + "\n".join(sections) + """
<script>
document.addEventListener('play', ({target}) => {
  if (target.tagName === 'AUDIO') {
    document.querySelectorAll('audio').forEach(audio => {
      if (audio !== target) audio.pause();
    });
  }
}, true);
</script></html>
""")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("di", nargs="*", type=Path, help="optional 44.1 kHz DI WAVs")
    parser.add_argument("--truth-dir", type=Path, help="prepared Guitar-TECHS directory")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        scratch = Path(tmp)
        binaries = compile_renderers(scratch)
        test = scratch / "Plucks.wav"
        raw = scratch / "render.wav"
        fixture(test)
        cases = [test, *args.di]
        if len({source.stem for source in cases}) != len(cases):
            parser.error("input WAVs must have distinct names")
        report = ["source,patch,mode,case,peak_rms,half_level_ms,first_right_pitch_ms,note_ons,tail_to_minus30_ms"]
        for source in cases:
            dry, sr = sf.read(source, always_2d=True)
            if sr != SR:
                parser.error(f"{source} must be recorded at 44100 Hz")
            sf.write(OUT / f"{source.stem}-di.wav", dry[:, 0], SR, subtype="PCM_24")
            for voice, patch in enumerate(VOICES):
                for mode, name in enumerate(MODES):
                    trace, audio = render(binaries, mode, voice, source, raw)
                    match_level(audio, OUT / f"{source.stem}-{voice}-{mode}.wav")
                    if source == test:
                        for label, peak, half, first, count, tail in pluck_metrics(trace, audio):
                            report.append(f"{source.stem},{patch},{name},{label},{peak:.6f},"
                                          f"{half:.1f},{first:.1f},{count},{tail}")
        (OUT / "plucks.csv").write_text("\n".join(report) + "\n")
        if args.truth_dir:
            compare_truth(binaries, args.truth_dir, raw)
    listening_page(cases)
    print(OUT / "index.html")


if __name__ == "__main__":
    main()
