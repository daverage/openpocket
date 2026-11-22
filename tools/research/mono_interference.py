#!/usr/bin/env python3
"""Measure the MONO SYNTH lead through interference and after a lead mute.

Run against one or more tools/archive/legacy-synth/mono_bench.c builds. The bench reports the
sounding oscillator pitch every 64 samples. Use --pair LEAD_MIDI LEAD.wav
SECOND_MIDI SECOND.wav to add a pair of isolated real guitar plucks.
"""
import argparse
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

import numpy as np

from bass_di_eval import load_note, near_note, place
from lowest_octave_eval import RATE, write_wav


LEAD_ON = .10
SECOND_ON = .34
LEAD_OFF = .76
SECOND_OFF = 1.14


def synthetic_note(midi, length, partials=(1., .32, .16)):
    t = np.arange(round(length * RATE)) / RATE
    f0 = 440 * 2 ** ((midi - 69) / 12)
    envelope = np.minimum(t / .003, 1) * np.exp(-1.5 * t)
    return envelope * sum(a * np.sin(2 * np.pi * f0 * h * t) for h, a in enumerate(partials, 1))


def fixtures(real_pairs):
    for label, lead, second, pluck, interference in [
        ('upper octave', 40, 52, synthetic_note(40, 1.2), synthetic_note(52, 1.2)),
        ('lower octave', 52, 40, synthetic_note(52, 1.2), synthetic_note(40, 1.2)),
        ('fourth above', 45, 50, synthetic_note(45, 1.2), synthetic_note(50, 1.2)),
        ('third below', 55, 51, synthetic_note(55, 1.2), synthetic_note(51, 1.2)),
        *real_pairs,
    ]:
        for ratio in (.55, 1.20):
            x = np.zeros(round(1.4 * RATE))
            place(x, pluck, LEAD_ON, LEAD_OFF, .20)
            place(x, interference, SECOND_ON, SECOND_OFF, .20 * ratio)
            yield f'{label}, second level {ratio:.2f}', x, lead, second

    for harmonic in (2, 3):
        x = np.zeros(round(1.4 * RATE))
        lead = 40
        note = synthetic_note(lead, SECOND_OFF - LEAD_ON)
        t = np.arange(len(note)) / RATE
        # Continuous partial of the same string, with a rise and decay.
        change = np.clip((t - (SECOND_ON - LEAD_ON)) / .008, 0, 1)
        change *= np.clip(((LEAD_OFF - LEAD_ON) - t) / .04, 0, 1)
        f0 = 440 * 2 ** ((lead - 69) / 12)
        note += 1.30 * change * np.sin(2 * np.pi * f0 * harmonic * t)
        place(x, note, LEAD_ON, SECOND_OFF, .20)
        yield f'partial H{harmonic} rises', x, lead, None


def score(rows, lead, second):
    time, pitch = rows[:, :2].T
    acquired = near_note(pitch, lead)
    before = (time >= SECOND_ON - .05) & (time < SECOND_ON)
    overlap = (time >= SECOND_ON) & (time < LEAD_OFF)
    # Qualify the lead independently. A missed acquisition can never count
    # as a successful hold. Silence is part of the overlap denominator.
    locked = acquired[before].mean() >= .8
    retain = 100 * acquired[overlap].mean()
    wrong = 100 * ((pitch[overlap] > 0) & ~acquired[overlap]).mean()
    escapes = np.sum(acquired[:-1] & ~acquired[1:] & (pitch[1:] > 0) & overlap[1:])
    handoff = None
    already_switched = False
    if second is not None:
        on_second = near_note(pitch, second)
        before_mute = (time >= LEAD_OFF - .01) & (time < LEAD_OFF)
        already_switched = bool(on_second[before_mute].all())
        hits = on_second & (time >= LEAD_OFF) & (time < SECOND_OFF)
        stable = np.convolve(hits.astype(int), np.ones(5, dtype=int), mode='valid') == 5
        if stable.any():
            handoff = (time[np.flatnonzero(stable)[0]] - LEAD_OFF) * 1000
    return locked, retain, wrong, escapes, handoff, already_switched


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('benches', nargs='+')
    parser.add_argument('--pair', action='append', nargs=4, default=[],
                        metavar=('LEAD_MIDI', 'LEAD_WAV', 'SECOND_MIDI', 'SECOND_WAV'))
    args = parser.parse_args()
    real = [(f'recorded {lead}/{second}', float(lead), float(second), load_note(a), load_note(b))
            for lead, a, second, b in args.pair]
    cases = list(fixtures(real))
    with TemporaryDirectory() as tmp:
        directory = Path(tmp)
        for i, (_, samples, _, _) in enumerate(cases):
            write_wav(directory / f'{i}.wav', samples)
        for bench in args.benches:
            print(bench)
            results = []
            for i, (label, _, lead, second) in enumerate(cases):
                trace = directory / 'trace.csv'
                subprocess.run([bench, directory / f'{i}.wav', trace], check=True)
                rows = np.loadtxt(trace, delimiter=',', skiprows=1)
                locked, retain, wrong, escapes, handoff, already_switched = score(rows, lead, second)
                results.append((locked, retain))
                transfer = '—'
                if second is not None:
                    transfer = 'miss' if handoff is None else f'{handoff:.1f} ms'
                    if already_switched:
                        transfer += ' (already on second)'
                print(f'  {label:33s} lead {"ok" if locked else "MISS":4s}  '
                      f'retained {retain:5.1f}%  wrong {wrong:5.1f}%  departures {escapes}  handoff {transfer}')
            qualified = [retain for locked, retain in results if locked]
            if qualified:
                print(f'  Qualified leads {len(qualified)}/{len(cases)}, mean retention {np.mean(qualified):.1f}%')


if __name__ == '__main__':
    main()
