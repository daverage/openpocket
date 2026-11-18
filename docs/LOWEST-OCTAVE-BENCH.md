# Lowest Note octave-down host experiment

The experiment combines the v51 resonator detector with the existing HQ phase-halving shifter.
A pitch must be stable across two consecutive reports to control the shifter. The shared-bank
version keeps a few low harmonics, downweights bands supported by an upper note, and fades the
band weights. The focused version instead retunes a narrow complex band to the selected
fundamental. It hard-resets filter state on a pitch change, so it is only a separation experiment.
The oracle modes supply known pitches and isolate the limitations of the filters from the tracker.
Smart Gate is off throughout.

## Findings

On 24 held-out GuitarSet mono pickup mixes, the selector held the correct lowest pitch in
31,221 of 44,883 settled dyad frames (**69.6%**). This measures the selection decision on recorded
guitar. The recordings lack separate aligned wet-reference stems, so leakage and arrival below
are measured with controlled staggered tone pairs. Identical recorded DI renders of all five
bundled loops are available through the benchmark.

| Controlled test | Fixed Range (C3) | Shared-bank Lowest | Focused Lowest |
| --- | ---: | ---: | ---: |
| E2–B2, unwanted octave relative to wanted | −14.8 dB | −19.3 dB | −99.0 dB |
| E2–B2, wanted octave relative to full HQ | 0.0 dB | −9.9 dB | +2.8 dB |
| E2–E3, unwanted octave relative to wanted | −17.5 dB | −10.0 dB | −97.7 dB |
| Isolated E2 wet response at 50% RMS | 10.4 ms | 14.8 ms | 72.8 ms |

The shared-bank mask was nearly as weak when given the correct pitches. On the E2–B2 fixture
its oracle also lost 9.9 dB of the intended octave. Close notes and bass pairs produced more
upper-note energy than wanted energy, even with correct note selection. A post-analysis mask
cannot separate two input tones that have already produced interference inside the same band.

The focused oracle could isolate the low note but still needed roughly 48 ms for E2 and 76–84 ms
for bass E1 and B0. Adding tracking raised E2 response to roughly 73 ms and bass responses to
138–154 ms on the tested pairs. It selected the upper tone on the close guitar pair and on the
bass E1 fixture during part of the sustain. The fitted old-low component after a mute was
particularly unreliable on close pairs because short analysis windows could not separate nearby
frequencies and modulation products. These are host signal-response measurements, without
converter latency or a pedal listening test.

## Decision

Keep fixed Range as the playable option. The shared-band mask is quick but loses level and
cannot give reliable isolation. The narrow filter can separate wider intervals but settles too
slowly for the target feel. Neither is enabled on the pedal.

For another strict Lowest prototype, test a fast low-note detector plus a controlled synthesized
sub, or restrict the spectral effect to widely spaced candidates and fade out when separation is
unreliable. Compare either choice against fixed Range at matched wet level. Real bass DI, playing
latency and AC791N peak block load remain unverified.

## Reproduce

```sh
cc -O2 -Iengine -Ifirmware tools/sim/lowest_octave.c firmware/px_math.c -lm -o build/lowest_octave
python3 tools/sim/lowest_octave_eval.py build/lowest_octave --takes 24 --output /tmp/openpocket-lowest-review
```

The optional GuitarSet score reads the existing pickup and annotation directories. The WAV renders
retain the same input level and original output gains so the lost octave output is audible in an A/B.

## Synthesized sub follow-up

The host-only `sub` mode drives a sine at half the selected pitch. The amplitude comes from the
selected resonator, and a pitch change releases the old tone before switching. `sub-fast` briefly
raises early level using the input envelope and limits it by resonator evidence. Neither mode changes
the pitch candidates. The ordinary sub avoided the narrow-filter prototype's long settling on simple
tones, but weak fundamentals still exposed octave errors.

| Fixture | Fixed Range wet onset | Sub first correct output | Sub correct during held dyad | Sub wrong during held dyad |
| --- | ---: | ---: | ---: | ---: |
| E2–B2, sine pair | 10.4 ms | 36.3 ms | 100% | 0% |
| E2–B2, weak fundamental and strong harmonics | 13.3 ms | 65.4 ms | 70% | 6% |
| E1–A1, sine pair | 12.1 ms | 68.3 ms | 7% | 58% |
| E1–A1, weak fundamental and strong harmonics | 12.1 ms | Miss | 9% | 91% |

The sub figures require oscillator pitch within about 48 cents of the wanted note and level above
half of the correct settled output (or 0.001 when there is no settled correct output). The fixed
Range column measures the first half-level wet RMS window. These are different measurements.
An early wet signal can contain the wrong pitch. On the simple E2–B2 pair, the sub's unwanted
octave was 75.7 dB below the wanted octave and the last audible low tone was 13.8 ms after its
mute. On the weak-fundamental E2–B2, the unwanted octave was only 8.4 dB down. Giving the same
sub the correct E2 identity restored 100% correct output during that dyad and put the unwanted
octave 57.1 dB down. The faster gain envelope did not improve correct onset on either E2 fixture.

The synthesized sub remains a host prototype. For a strict Lowest effect, the next experiment is
confirming a weak fundamental using harmonic evidence and rejecting unsupported octave changes.
Do not select a new oscillator solely from a broad onset or a stronger higher resonator.
A separate real bass DI test still needs low E, low B, fingerstyle, close notes and independent
mutes. Frequencies below about 30 Hz are also unlikely to translate well through many rigs,
even when the detector is correct.

Generate the additional weak-fundamental table with:

```sh
python3 tools/sim/lowest_octave_eval.py build/lowest_octave --harmonic
```

With `--output`, the same five recorded DI clips also render in raw and wet-RMS-matched
Range, sub and sub-fast WAVs. The tracking and musical comparisons leave Smart Gate off.

## Harmonic verification and continuous refinement

`sub-harmonic` keeps the lowest coarse candidate and can recover an octave below a detected note
when the fundamental, second and third harmonics independently rotate near the expected frequencies.
An upward octave jump requires five reports rather than two. `sub-refined` adds continuous pitch:
it subtracts each resonator's expected carrier phase advance before estimating frequency and combines
higher-harmonic estimates only when they agree with the fundamental. Nearby note changes preserve
the oscillator phase. These are host experiments using the v51 quarter-rate analysis
(11.025 kHz) and the 128-input-sample report interval (2.90 ms).

| Fixture or dataset | Original sub | Harmonic verification | Verification + refinement |
| --- | ---: | ---: | ---: |
| Weak E2–B2, first correct output | 65.4 ms | 30.5 ms | 30.5 ms |
| Weak E2–B2, correct through held dyad | 70% | 100% | 96% |
| Weak E1–A1, correct through held dyad | 9% | 0% | 0% |
| Weak B0–D2, correct through held dyad | 15% | 33% | 34% |
| 150-cent bend under held B2, median error | 31.6 cents | 31.9 cents | 22.7 cents |
| Same bend, 95th-percentile error | 58.2 cents | 58.1 cents | 68.3 cents |
| Same bend, within 50 cents of truth | 70.3% | 72.2% | 81.4% |
| Recorded holdout, correct emitted lowest pitch | 70.3% | 70.4% | 69.6% |

The weak-fundamental fifth's last audible low output remained about 13.8 ms after the low note
was muted for all three sub modes. The bass low E fixture sometimes gave a correct isolated
onset after verification (48 ms), then lost the lowest pitch when A entered. The continuous
refinement improved typical bend accuracy and eliminated dropouts on that fixture, while
increasing high-percentile pitch error. The held-note bend is synthetic. The recorded result is
from 24 GuitarSet mono pickup mixes (44,883 settled dyad frames) and scores the emitted pitch
rather than the unplayed candidate. Gain at or below 0.001 counts as no note.

Do not ship either variant. The octave check helps a controlled case but barely shifts recorded
lowest-note accuracy, and the phase refinement is sensitive to partial overlap. Next isolate
phase estimates on the active note's less-shared harmonics and test candidate identity and
pitch error independently. A bass-specific confirmation pass needs real DI. No firmware,
Smart Gate behavior, or pedal DSP measurement changed.

```sh
cc -O2 -Iengine -Ifirmware tools/sim/lowest_octave.c firmware/px_math.c -lm -o build/lowest_octave
python3 tools/sim/lowest_octave_eval.py build/lowest_octave --harmonic --takes 24 --output /tmp/lowest-verified
```

## Less-shared harmonics and lower tunings

`sub-exclusive` excludes phase evidence that also falls near a harmonic of another reported note.
If its fundamental is unavailable, it requires two agreeing higher partials before it updates pitch.
This variant shares the same note-identity check as `sub-refined`. It tests whether cleaner phase
evidence alone improves the result. The overlap test uses a minimum 7 Hz tolerance and grows to
2.4% of the partial frequency. It cannot identify overlap from an upper voice the detector missed.

| Weak-fundamental synthetic pair | Low note | Refined sub, correct through dyad | Less-shared refinement |
| --- | ---: | ---: | ---: |
| E2–B2 | 82.4 Hz | 96% | 96% |
| D2–A2 | 73.4 Hz | 79% | 79% |
| C2–G2 | 65.4 Hz | 47% | 47% |
| B1–F♯2 | 61.7 Hz | 66% | 66% |
| A1–E2 | 55.0 Hz | 41% | 41% |
| Bass E1–A1 | 41.2 Hz | 0% | 0% |
| Bass B0–D2 | 30.9 Hz | 34% | 34% |

The 150-cent bend median/p95 error was 22.9/69.2 cents versus 22.7/68.3 for the original
refinement. Recorded GuitarSet dyad correctness stayed at 69.6% to one decimal place.
Removing overlapping partials from the fine estimate does not solve the wrong coarse identities.
Keep these modes on the host and evaluate low-register identity with real guitar and bass DI
before choosing any continuous refinement for firmware.

The follow-up [recorded bass benchmark](BASS-DI-BENCH.md) measures the same host modes with
fingerstyle and picked bass down to native B0. Harmonic verification helps when the low note
plays alone. Tracking the sustained lowest note under a second note remains unreliable.

### Low register checklist for pitch-dependent effects

Use E2, drop D2, drop C2, B1, A1, bass E1 and bass B0 in future tests. The last two should
include soft fingerstyle, weak fundamentals, and close notes under a held string. Instrument
coverage and useful shifted output are separate checks. At −12, bass E1 becomes 20.6 Hz and
B0 becomes 15.4 Hz. Some rigs will reproduce little of those fundamentals. A sub frequency
policy should be explicit per effect.

| Path | Current low-register behavior |
| --- | --- |
| Shared tracker in PWM HQ, Square HQ and the tracked acoustic voices | Minimum fundamental of 60 Hz. It excludes A1, bass E1 and bass B0. B1 sits close to the floor. Bass Synth HQ also uses this tracker in the standard firmware. |
| v51 test Bass Synth HQ / host Lowest | The resonator bank reaches B0. Bass Synth HQ separately folds subs below 35 Hz up an octave, and the candidate figures above show why detection range alone is insufficient. |
| Mono Synth period tracker | The raw period test rejects fundamentals at or below 55 Hz. |
| Legacy two-period HQ detector and its host benchmark | The period guard has a 48 Hz lower limit. |
| HQ octave effects including String Machine, Organ HQ and Poly Oct Fuzz | These transform the audio. Their first band is centered at 60 Hz and has a finite response below it. Check wet fundamental level, arrival and artifacts below that band. The fixed Range control's lowest labeled cutoff is B1. |
| Standard octave, 12-String, Baritone, Whammy and other delay shifters | They have no explicit note-frequency cutoff in the delay algorithm. Check low-register splice artifacts and wet level. |
| Comparator and divider paths including standard Bass Synth and Octaver | Verify crossings, octave stability, and low-register filtering rather than relying on a tracker cutoff. |

This audit identifies code limits and test cases. It does not establish that every effect handles
each tuning correctly. No firmware setting changed.
