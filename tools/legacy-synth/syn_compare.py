#!/usr/bin/env python3
"""Compare old and HQ SYN implementations for catalogue consolidation (v63). Host only.

  cc -O2 -Iengine -Ifirmware tools/legacy-synth/ensemble_bench.c firmware/px_math.c -lm -o build/ensemble_bench
  PYTHONPATH=tools/research python3 tools/legacy-synth/syn_compare.py [--takes N]

For every annotated single note of at least 150 ms in N GuitarSet solo takes (build/guitarset), the share of the
output's energy (60 Hz .. 4 kHz, middle half of the note) that lies within 30 cents of the harmonics of the note's
expected base (the guitar's pitch times the type's lowest voice: 1/2 for the organs, strings and choir, 1/4 for BASS
SYNTH, 1 for PWM). --comp: the comping takes instead, 93 ms windows where the same two or more notes sound
throughout, the harmonics of any of them. Higher is cleaner: less energy at pitches the player did not play. Also the host render time.
Knobs at noon, all wet (DRY 0; PWM's MIX 100).
`--match` compares paired wet and mixed renders on sustained synthetic singles.
Add `--match-real` to use isolated notes from evenly spaced GuitarSet solo takes
and retain the played attacks and releases.
"""
import argparse, glob, os, subprocess, sys, time
from pathlib import Path
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

def render(name, x, blk='SYN', given=None, octave=None, trim_from=None):
    knobs = ['50'] * 5
    knobs[4] = '100' if name.startswith('PWM') else '0'      # all wet: PWM's MIX at 100, the others' DRY at 0
    if given: knobs = given.split()
    env = dict(os.environ, BLK=blk)
    if octave:
        env['OCTAVE_VOICE'] = octave
    if trim_from:
        env['TRIM_FROM'] = trim_from
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
    """Added level wobble (dB), as tools/research/octave_banks.c: the wet level's 10 ms deviations from its own 150 ms
    trend, relative to the dry level's, where the input is within 30 dB of its peak."""
    lx, ly = levels(x), levels(y); top = lx.max(); acc = []
    for j in range(9, len(lx) - 9):
        if lx[j] < top - 30 or ly[j] < -90: continue
        avg = np.mean(ly[j - 7:j + 8] - lx[j - 8:j + 7])
        acc.append((ly[j] - lx[j - 1] - avg) ** 2)
    return float(np.sqrt(np.mean(acc))) if acc else 0.0


def real_match_notes(takes):
    dataset = Path(ROOT) / 'build' / 'guitarset'
    files = sorted((dataset / 'mono').glob('*_solo_mix.wav'))
    if not files:
        raise ValueError('no GuitarSet solo WAVs in build/guitarset/mono')
    before, after = 2646, 7938   # 60 ms of pre-roll and 180 ms of natural tail
    for index in np.linspace(0, len(files) - 1, min(takes, len(files)), dtype=int):
        path = files[index]
        notes = jams_notes(dataset / 'annotation' / (path.stem.removesuffix('_mix') + '.jams'))
        audio = read_di(path)
        for i, (start, stop, midi) in enumerate(notes):
            first, last = round(start * 44100), round(stop * 44100)
            if stop - start < .38 or first < before or last + after > len(audio):
                continue
            if any(a < stop + .18 and b > start - .06
                   for j, (a, b, _) in enumerate(notes) if i != j):
                continue
            yield audio[first - before:last + after], .06, .06 + (last - first) / 44100, midi, path.stem, start


def rms_trace(y):
    """Return timestamps and powers for a causal 10 ms RMS window."""
    rate = 44100
    win = 441
    energy = np.r_[0, np.cumsum(y.astype(float) ** 2)]
    end = np.arange(win, len(y) + 1, 44)
    return end / rate, (energy[end] - energy[end - win]) / win


def dry_attack(x, start):
    t, power = rms_trace(x)
    near = (t >= start - .05) & (t < start + .065)
    levels = power[near]
    if not levels.size or levels.max() < 1e-8:
        return np.nan
    hits = t[near][levels >= .025 * levels.max()]
    return hits[0] if hits[0] > t[near][0] else np.nan


def attack_release(y, start, stop, dry_on):
    # Measure entry relative to the note's own settled level and release
    # relative to its pre-off level.
    t, power = rms_trace(y)
    settled = (t >= start + .25 * (stop - start)) & (t < start + .60 * (stop - start))
    steady = np.median(power[settled])
    start_search = dry_on - .01
    on = t[(t >= start_search) & (t < stop) & (power >= .25 * steady)]
    before_off = np.median(power[(t >= stop - .05) & (t < stop)])
    # Ignore release levels already at the noise floor. A tail still above
    # threshold at the end of the clip is right-censored.
    after_off = t[(t >= stop) & (power >= .01 * before_off)]
    attack = 1000 * (on[0] - dry_on) if on.size and steady > 1e-8 and on[0] > start_search + .001 else np.nan
    release = np.nan
    if before_off > 1e-8 and (not after_off.size or after_off[-1] < t[-1] - .01):
        release = 1000 * (after_off[-1] - stop) if after_off.size else 0.0
    return attack, release


def match_sound(outdir, real=False, takes=20, share_trim=False):

    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    rate = 44100
    notes = list(real_match_notes(takes)) if real else []
    if real and not notes:
        raise ValueError('no long isolated notes in the selected GuitarSet takes')
    if not real:
        t = np.arange(rate * 2) / rate
        for m in (40, 45, 50, 55, 60, 65, 69, 72):
            f0 = 440 * 2 ** ((m - 69) / 12)
            env = np.clip(np.minimum((t - 0.1) / 0.01, (1.45 - t) / 0.02), 0, 1)
            x = 0.16 * env * sum(amplitude * np.sin(2 * np.pi * f0 * h * t)
                                 for h, amplitude in enumerate((1, 0.36, 0.18, 0.09, 0.05), 1))
            notes.append((x, .1, 1.45, m, 'synthetic', .1))
    write_wav(out / 'input.wav', np.concatenate([note[0] for note in notes]))
    spans = [(start + .25 * (stop - start), stop - .25 * (stop - start))
             if real else (.45, 1.25) for _, start, stop, *_ in notes]
    dry_onsets = [dry_attack(x, start) if real else start for x, start, *_ in notes]
    labels = ['onset_s\tend_s\tmidi\tsource\tsource_onset_s']
    position = 0
    for x, start, stop, midi, source, original_time in notes:
        labels.append(f'{position / rate + start:.5f}\t{position / rate + stop:.5f}\t'
                      f'{midi:.2f}\t{source}\t{original_time:.5f}')
        position += len(x)
    (out / 'notes.tsv').write_text('\n'.join(labels) + '\n')

    def features(y, span):
        seg = y[round(span[0] * rate):round(span[1] * rate)]
        energy = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
        f = np.fft.rfftfreq(len(seg), 1 / rate)
        total = energy.sum()
        return np.array([
            10 * np.log10(np.mean(seg ** 2) + 1e-20),
            np.sum(f * energy) / max(total, 1e-20),
            10 * np.log10(max(energy[f < 200].sum() / max(total, 1e-20), 1e-6)),
            10 * np.log10(max(energy[f > 3000].sum() / max(total, 1e-20), 1e-6)),
        ])

    scores = ['original\thq\tratio\tmode\tnote\tharmonic_filter\twithin_limits\t'
              'level_db\tcentroid_pct\tbelow_200_db\tabove_3000_db']
    for old, new, ratio, *settings in PAIRS:
        block, knobs = settings if settings else ('SYN', None)
        original = [render(old, x, block, knobs)[0] for x, *_ in notes]
        trim_from = old if share_trim else None
        hq = [render(new, x, block, knobs, trim_from=trim_from)[0] for x, *_ in notes]
        # Report the pitch filter separately. Compare tone on steady notes with
        # at least half their measured energy on the expected harmonics.
        pitch_ok = np.array([
            all(np.max(on_pitch(y, [(a, b, m)], ratio), initial=0) >= 0.5 for y in (left, right))
            for (_, a, b, m, *_), left, right in zip(notes, original, hq)
        ])
        mixed = knobs.split() if knobs else ['50'] * 5
        mixed[{'PITCH': 0, 'SFX': 3, 'SYN': 4}[block]] = '50'
        mixed_knobs = ' '.join(mixed)
        mixed_original = [render(old, x, block, mixed_knobs)[0] for x, *_ in notes]
        mixed_hq = [render(new, x, block, mixed_knobs, trim_from=trim_from)[0] for x, *_ in notes]
        for mode, left, right in [('wet', original, hq), ('mixed', mixed_original, mixed_hq)]:
            audio = [np.concatenate(left), np.concatenate(right)]
            peak = max(np.max(abs(y)) for y in audio)
            scale = min(1., .999 / max(peak, 1e-12))
            for name, y in zip((old, new), audio):
                stem = f'{name.lower().replace(" ", "-")}-{ratio:g}'
                suffix = '-mixed' if mode == 'mixed' else ''
                write_wav(out / f'{stem}{suffix}.wav', y * scale)
            expected = np.stack([features(y, span) for y, span in zip(left, spans)])
            delta = np.stack([features(y, span) for y, span in zip(right, spans)]) - expected
            delta[:, 1] *= 100 / np.maximum(expected[:, 1], 1)
            matches = (np.abs(delta) <= [.5, 5, 1, 1]).all(axis=1) & pitch_ok
            for index, (changes, eligible, match) in enumerate(zip(delta, pitch_ok, matches), 1):
                values = '\t'.join(f'{change:.4f}' for change in changes)
                scores.append(f'{old}\t{new}\t{ratio:g}\t{mode}\t{index}\t{int(eligible)}\t{int(match)}\t{values}')
            median = ' '.join(f'{x:+.2f}' for x in np.median(delta[pitch_ok], axis=0)) if pitch_ok.any() else '—'
            print(f'{old:18s} -> {new:18s} {ratio:g}× {mode:5s}  {matches.sum()}/{pitch_ok.sum()} within limits; '
                  f'{len(notes) - pitch_ok.sum()} excluded by pitch filter; '
                  f'median differences (dB, %, dB, dB) {median}; WAV gain {scale:.3f}')
        metrics = np.array([[attack_release(y, a, b, onset) for y, (_, a, b, *_), onset
                             in zip(audio, notes, dry_onsets)]
                            for audio in (original, hq)])
        for i, label in enumerate(('attack', 'release')):
            keep = pitch_ok & np.isfinite(metrics[:, :, i]).all(axis=0)
            medians = np.median(metrics[:, keep, i], axis=1) if keep.any() else [np.nan, np.nan]
            print(f'  wet {label}: {medians[0]:.1f} / {medians[1]:.1f} ms original / HQ ({keep.sum()}/{pitch_ok.sum()} paired)')
    (out / 'scores.tsv').write_text('\n'.join(scores) + '\n')


def octave_attacks(direction):
    rate = 44100
    start = rate // 10
    t = (np.arange(rate * 2) - start) / rate
    print(f'{"type":18s} arrival D2–E3 mean / worst ms  pick high-band excess dB')
    for old, new, ratio, *settings in PAIRS:
        block, knobs = settings if settings else ('SYN', None)
        for name in (old, new):
            arrivals, clicks = [], []
            for m in range(38, 53):
                hz = 440 * 2 ** ((m - 69) / 12)
                x = np.where(t >= 0, 0.3 * np.sin(2 * np.pi * hz * t), 0)
                y, _ = render(name, x, block, knobs, direction)
                # Match octave_banks.c: half the steady wet level, with a
                # one-cycle RMS window at the shifted frequency.
                window = int(rate / (hz * ratio)) + 1
                energy = np.r_[0, np.cumsum(y.astype(float) ** 2)]
                steady = (energy[rate + rate // 10] - energy[rate]) / (rate // 10)
                i = np.arange(start, len(y) - window, 16)
                hit = i[energy[i + window] - energy[i] > 0.25 * steady * window]
                if hit.size:
                    arrivals.append((hit[0] + window // 2 - start) * 1000 / rate)
                # Same finite-difference high-band proxy as octave_banks.c.
                high = np.convolve(y, [1, -1.68, 0.71], 'full')[:len(y)]
                pick = np.mean(high[start:start + rate // 50] ** 2)
                clicks.append(10 * np.log10((pick + 1e-20) / (np.mean(high ** 2) + 1e-20)))
            response = f'{np.mean(arrivals):6.1f} / {max(arrivals):6.1f}' if arrivals else '      no response'
            print(f'{name:18s} {response}  {len(arrivals)}/15 responded  {np.mean(clicks):+6.2f}')

p = argparse.ArgumentParser(); p.add_argument('--takes', type=int, default=20); p.add_argument('--comp', action='store_true')
p.add_argument('--wobble', action='store_true', help='added level wobble on the bundled loops instead')
p.add_argument('--bench', help='another ensemble_bench build (e.g. with -DPX_SUB2_TEST)')
p.add_argument('--pair', help='OLD|NEW|BASE|BLK|KNOBS instead of the built-in pairs')
p.add_argument('--match', metavar='OUTDIR', help='compare wet and mixed sound and save paired audition WAVs')
p.add_argument('--match-real', action='store_true', help='use isolated GuitarSet solo notes for --match')
p.add_argument('--share-trim', action='store_true', help='apply the original block trim to its HQ partner in --match')
p.add_argument('--octave-voices', choices=['up', 'down'], help='score only the specified organ, strings or choir octave')
p.add_argument('--attacks', action='store_true', help='measure isolated low-note arrival and pick transients')
a = p.parse_args()
if a.bench: BENCH = a.bench
if a.pair:
    o, n, b, k, kn = a.pair.split('|'); PAIRS = [(o, n, float(b), k, kn)]
if a.match_real and (not a.match or a.takes < 1):
    p.error('--match-real requires --match and positive --takes')
if a.share_trim and not a.match:
    p.error('--share-trim requires --match')
if a.match:
    try:
        match_sound(a.match, a.match_real, a.takes, a.share_trim)
    except ValueError as error:
        p.error(str(error))
    sys.exit(0)
if a.octave_voices:
    names = {'ORGAN', 'CHURCH ORGAN', 'CHOIR'}
    if a.octave_voices == 'up':
        names.add('STRINGS')
    ratio = 2.0 if a.octave_voices == 'up' else 0.5
    PAIRS = [(pair[0], pair[1], ratio, *pair[3:]) for pair in PAIRS if pair[0] in names]
    if not PAIRS:
        p.error('selected pair has no comparable isolated octave voice')
if a.attacks:
    if not a.octave_voices:
        p.error('--attacks requires --octave-voices')
    octave_attacks(a.octave_voices)
    sys.exit(0)
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
