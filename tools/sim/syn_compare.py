#!/usr/bin/env python3
"""Compare old and HQ SYN implementations for catalogue consolidation (v63). Host only.

  cc -O2 -Iengine -Ifirmware tools/sim/ensemble_bench.c firmware/px_math.c -lm -o build/ensemble_bench
  PYTHONPATH=tools/sim python3 tools/sim/syn_compare.py [--takes N]

For every annotated single note of at least 150 ms in N GuitarSet solo takes (build/guitarset), the share of the
output's energy (60 Hz .. 4 kHz, middle half of the note) that lies within 30 cents of the harmonics of the note's
expected base (the guitar's pitch times the type's lowest voice: 1/2 for the organs, strings and choir, 1/4 for BASS
SYNTH, 1 for PWM). --comp: the comping takes instead, 93 ms windows where the same two or more notes sound
throughout, the harmonics of any of them. Higher is cleaner: less energy at pitches the player did not play. Also the host render time.
Knobs at noon, all wet (DRY 0; PWM's MIX 100).
`--match` compares paired wet renders on sustained synthetic singles and saves
the dry input and both outputs for listening.
"""
import argparse, glob, os, subprocess, sys, time
import numpy as np
from track_eval import ROOT, jams_notes
from resonator_track_eval import read_di
from lowest_octave_eval import write_wav

# (old, new, base ratio[, block, knobs]): knobs given here override the all-wet default
PAIRS = [("ORGAN", "ORGAN HQ", 0.5), ("CHURCH ORGAN", "CHURCH ORGAN HQ", 0.5), ("STRINGS", "STRINGS HQ", 0.5),
         ("CHOIR", "CHOIR HQ", 0.5), ("PWM SYNTH", "PWM SYNTH HQ", 1.0), ("BASS SYNTH", "BASS SYNTH HQ", 0.25),
         ("SQUARE SYNTH", "SQUARE SYNTH HQ", 1.0, "SFX", "50 50 0 100 50"),         # SWEEP RESO SUB MIX: no sub, all wet
         ("POLY OCTAVE", "POLY OCTAVE HQ", 0.5, "PITCH", "0 100 0 0 0"),            # DRY 0, OCT -1 only (HQ RANGE 0: all)
         ("POLY OCTAVE", "POLY OCTAVE HQ", 2.0, "PITCH", "0 0 100 0 0")]            # OCT +1 only
BENCH = os.path.join(ROOT, 'build', 'ensemble_bench')

def render(name, x, blk='SYN', given=None, octave=None):
    knobs = ['50'] * 5
    knobs[4] = '100' if name.startswith('PWM') else '0'      # all wet: PWM's MIX at 100, the others' DRY at 0
    if given: knobs = given.split()
    env = dict(os.environ, BLK=blk)
    if octave:
        env['OCTAVE_VOICE'] = octave
    r = subprocess.run([BENCH, name, *knobs], input=x.astype('<f4').tobytes(), capture_output=True, check=True, env=env)
    return np.frombuffer(r.stdout, dtype='<f4'), r

def on_pitch(y, notes, base):
    shares = []
    for a, b, m in notes:
        if b - a < 0.15: continue
        i0, i1 = int((a + (b - a) / 4) * 44100), int((b - (b - a) / 4) * 44100)
        seg = y[i0:i1]
        if len(seg) < 2048: continue
        w = np.hanning(len(seg)); X = np.abs(np.fft.rfft(seg * w)) ** 2
        f = np.fft.rfftfreq(len(seg), 1 / 44100)
        band = (f >= 60) & (f <= 4000)
        tot = X[band].sum()
        if tot <= 1e-12: continue
        f0 = 440 * 2 ** ((m - 69) / 12) * base
        k = np.maximum(1, np.round(f / f0))
        near = np.abs(1200 * np.log2(np.maximum(f, 1) / (k * f0))) < 30
        shares.append(X[band & near].sum() / tot)
    return np.array(shares)

def on_chords(y, notes, base, win=4096):
    """Comping: 93 ms windows where the same two or more notes sound throughout; harmonics of any of them."""
    shares = []
    for i0 in range(0, len(y) - win, win):
        t0, t1 = i0 / 44100, (i0 + win) / 44100
        held = {m for a, b, m in notes if a <= t0 - 0.03 and b >= t1}
        if len(held) < 2 or any(t0 - 0.03 < a < t1 for a, b, m in notes): continue
        seg = y[i0:i0 + win]; X = np.abs(np.fft.rfft(seg * np.hanning(win))) ** 2
        f = np.fft.rfftfreq(win, 1 / 44100); band = (f >= 60) & (f <= 4000); tot = X[band].sum()
        if tot <= 1e-12: continue
        near = np.zeros_like(f, dtype=bool)
        for m in held:
            f0 = 440 * 2 ** ((m - 69) / 12) * base; k = np.maximum(1, np.round(f / f0))
            near |= np.abs(1200 * np.log2(np.maximum(f, 1) / (k * f0))) < 30
        shares.append(X[band & near].sum() / tot)
    return np.array(shares)

def levels(y, w=441):
    m = len(y) // w
    return 10 * np.log10((y[:m * w].reshape(m, w).astype(float) ** 2).mean(1) + 1e-20)

def wobble(x, y):
    """Added level wobble (dB), as tools/sim/octave_banks.c: the wet level's 10 ms deviations from its own 150 ms
    trend, relative to the dry level's, where the input is within 30 dB of its peak."""
    lx, ly = levels(x), levels(y); top = lx.max(); acc = []
    for j in range(9, len(lx) - 9):
        if lx[j] < top - 30 or ly[j] < -90: continue
        avg = np.mean(ly[j - 7:j + 8] - lx[j - 8:j + 7])
        acc.append((ly[j] - lx[j - 1] - avg) ** 2)
    return float(np.sqrt(np.mean(acc))) if acc else 0.0


def match_sound(outdir):
    from pathlib import Path

    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    rate = 44100
    t = np.arange(rate * 2) / rate
    window = (t >= 0.45) & (t < 1.25)
    midi = [40, 45, 50, 55, 60, 65, 69, 72]
    notes = []
    for m in midi:
        f0 = 440 * 2 ** ((m - 69) / 12)
        env = np.clip(np.minimum((t - 0.1) / 0.01, (1.45 - t) / 0.02), 0, 1)
        x = 0.16 * env * sum(amplitude * np.sin(2 * np.pi * f0 * h * t)
                             for h, amplitude in enumerate((1, 0.36, 0.18, 0.09, 0.05), 1))
        notes.append(x)
    write_wav(out / 'input.wav', np.concatenate(notes))

    def features(y):
        seg = y[window]
        energy = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
        f = np.fft.rfftfreq(len(seg), 1 / rate)
        total = energy.sum()
        return np.array([
            10 * np.log10(np.mean(seg ** 2) + 1e-20),
            np.sum(f * energy) / max(total, 1e-20),
            10 * np.log10(max(energy[f < 200].sum() / max(total, 1e-20), 1e-6)),
            10 * np.log10(max(energy[f > 3000].sum() / max(total, 1e-20), 1e-6)),
        ])

    for old, new, ratio, *settings in PAIRS:
        block, knobs = settings if settings else ('SYN', None)
        original = [render(old, x, block, knobs)[0] for x in notes]
        hq = [render(new, x, block, knobs)[0] for x in notes]
        for name, audio in [(old, original), (new, hq)]:
            stem = f'{name.lower().replace(" ", "-")}-{ratio:g}'
            write_wav(out / f'{stem}.wav', np.concatenate(audio))
        expected = np.stack([features(y) for y in original])
        delta = np.stack([features(y) for y in hq]) - expected
        delta[:, 1] *= 100 / np.maximum(expected[:, 1], 1)
        # Report the pitch filter separately. Compare tone on steady notes with
        # at least half their measured energy on the expected harmonics.
        pitch_ok = np.array([
            all(np.max(on_pitch(y, [(0.1, 1.45, m)], ratio), initial=0) >= 0.5 for y in (left, right))
            for m, left, right in zip(midi, original, hq)
        ])
        rms_ok = np.abs(delta[:, 0]) <= 0.5
        shape_ok = (np.abs(delta[:, 1:]) <= [5, 1, 1]).all(axis=1)
        median = ' '.join(f'{x:+.2f}' for x in np.median(delta[pitch_ok], axis=0)) if pitch_ok.any() else '—'
        print(f'{old:18s} -> {new:18s} {ratio:g}×  {sum(rms_ok & shape_ok & pitch_ok)}/{pitch_ok.sum()} within limits; '
              f'{len(midi) - pitch_ok.sum()} excluded by pitch filter; '
              f'median differences (dB, %, dB, dB) {median}')

p = argparse.ArgumentParser(); p.add_argument('--takes', type=int, default=20); p.add_argument('--comp', action='store_true')
p.add_argument('--wobble', action='store_true', help='added level wobble on the bundled loops instead')
p.add_argument('--bench', help='another ensemble_bench build (e.g. with -DPX_SUB2_TEST)')
p.add_argument('--pair', help='OLD|NEW|BASE|BLK|KNOBS instead of the built-in pairs')
p.add_argument('--match', metavar='OUTDIR', help='compare wet sound on paired synthetic notes and save audition WAVs')
p.add_argument('--octave-voices', choices=['up', 'down'], help='score only the specified organ, strings or choir octave')
a = p.parse_args()
if a.bench: BENCH = a.bench
if a.pair:
    o, n, b, k, kn = a.pair.split('|'); PAIRS = [(o, n, float(b), k, kn)]
if a.match:
    match_sound(a.match)
    sys.exit(0)
if a.octave_voices:
    names = {'ORGAN', 'CHURCH ORGAN', 'CHOIR'}
    if a.octave_voices == 'up':
        names.add('STRINGS')
    ratio = 2.0 if a.octave_voices == 'up' else 0.5
    PAIRS = [(pair[0], pair[1], ratio) for pair in PAIRS if pair[0] in names]
    if not PAIRS:
        p.error('selected pair has no comparable isolated octave voice')
if a.wobble:
    loops = [read_di(os.path.join(ROOT, 'assets', 'loops', n + '.wav')) for n in ('chug', 'riff', 'chords', 'arpeggio', 'lead')]
    print(f'{"type":18s} added wobble dB: chug riff chords arpeggio lead | mean')
    for pair in PAIRS:
        blk, given = (pair[3], pair[4]) if len(pair) > 3 else ('SYN', None)
        for name in pair[:2]:
            w = [wobble(x, render(name, x, blk, given, a.octave_voices)[0]) for x in loops]
            print(f'{name:18s} ' + ' '.join(f'{v:5.2f}' for v in w) + f' | {np.mean(w):5.2f}')
        print()
    sys.exit(0)
if a.comp: on_pitch = on_chords
files = sorted(glob.glob(os.path.join(ROOT, 'build', 'guitarset', 'mono', '*_comp_mix.wav' if a.comp else '*_solo_mix.wav')))[:a.takes]
items = [(read_di(w), jams_notes(os.path.join(ROOT, 'build', 'guitarset', 'annotation', os.path.basename(w).replace('_mix.wav', '.jams')))) for w in files]
print(f'{"type":18s} {"on-pitch":>9s} {"p25":>6s} {"host s":>7s}')
for pair in PAIRS:
    old, new, base = pair[:3]
    blk, given = (pair[3], pair[4]) if len(pair) > 3 else ('SYN', None)
    for name in (old, new):
        shares = []; secs = 0.0
        for x, notes in items:
            t0 = time.perf_counter(); y, _ = render(name, x, blk, given, a.octave_voices); secs += time.perf_counter() - t0
            shares.append(on_pitch(y, notes, base))
        s = np.concatenate(shares)
        print(f'{name + (" x" + str(base) if blk == "PITCH" else ""):18s} {100*s.mean():8.1f}% {100*np.percentile(s,25):5.1f}% {secs:7.2f}')
    print()
