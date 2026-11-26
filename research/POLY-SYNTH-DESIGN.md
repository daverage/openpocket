# Chord synth voicing and two-note tracking

## Start with the guitar signal

String Machine can reuse the shaped unshifted signal, swell, and three modulated delay taps from STRINGS. Give the octave and ensemble independent controls. At zero, each optional layer can be skipped. Keep the shaped center signal present so the sound holds together when the notes beat or the ensemble is off. The Solina reference uses three modulated delays for its ensemble colour. Its wet-only blend is a useful audition point alongside the proposed mix. [Arturia Solina V manual, pp. 51–52](https://dl.arturia.net/products/solina-v/manual/solina-v_Manual_2_10_1_EN.pdf)

For Poly Brass, keep the guitar as the excitation. Use the body filter to retain note centres, then bring in bite briefly on attacks. Moderate the drive on the summed chord. As an inference from the waveshaper, the quadratic term applied to two notes produces both difference and sum components. The saturation stage adds further intermodulation as it approaches clipping. Compare gain-matched renders with single notes, power chords, and minor-second and major-third dyads, checking that the bite adds a distinct attack without masking either note. BOSS's own guide is a precedent for a chord synth from standard guitar input. Its guide describes the playing result and controls. [BOSS SY-300 sound creation](https://articles.boss.info/sound-creation-with-the-sy-300-guitar-synthesizer/)

For Drawbar Organ, use ORGAN HQ with 8′ carrying the pitch, 4′ adding definition and 16′ adding weight. Start with subdued 16′ on dense voicings so the low octave leaves room for the chord. This is an OpenPocket preset choice based on the drawbar relationship. [Hammond SK PRO manual, Harmonic Drawbars](https://www.suzuki-music.co.jp/app/uploads/00457-40208_V160_20210519Web.pdf)

## Check candidate quality before tuning the allocator

OpenPocket already implements a form of Klapuri's iterative harmonic salience. It scores fundamentals from partials, selects one, removes the partials it explains, and repeats. In `px_track.h`, candidate frequency and salience are paired in `notes[]` and `sal[]`, sorted by increasing pitch. The first slot therefore denotes the lowest estimate, which can be spurious. The mono output additionally applies period correction and refinement. Evaluate the raw candidates separately from mono and from the allocator. Log which partials support each candidate on misses. Binary removal can discard a useful shared harmonic of the second note. A bounded comparison would retain some overlapping evidence, or score the best few candidate pairs jointly with a penalty for explaining the same peak twice. [Klapuri, ISMIR 2006](https://doi.org/10.5281/zenodo.1416740)

The repo baseline reports 61% precision and 45% recall on GuitarSet chord frames, and 47% lowest-note accuracy. Those are broad chord results. First score a two-note subset separately with one-to-one matching within 50 cents. Report note and frame precision/recall, octave errors, signed second-note acquisition delay, and any dropouts of the held note. Use mono pickup mixes as the input and time-aligned contours or per-string notes as reference. Mute and repick either note in a controlled recording so the event order and note identity are known. Include octave pairs, quiet notes under louder ones, and bent notes. [OpenPocket note tracking](../archive/NOTE-TRACKING.md) · [GuitarSet data and known annotation errors](https://zenodo.org/records/3371780)

A fresh host run of the default `track_bench.c` against all 180 GuitarSet comping pickup mixes found **both notes in 15.2% of 580,986 settled dyad frames**. Candidate precision was 49.5%, recall 45.9%, and the lowest candidate matched the bass in 54.8%. Both notes matched in 0.6% of 56,196 octave frames, 15.8% of perfect-fifth frames, and none of 1,619 semitone frames. The tracker also produced at least two candidates in 42.8% of 545,876 frames with one reference pitch. The run scored raw candidates before any allocator. It treated a pitch as active from 30 ms into its annotation until its end, counted distinct reference pitches, and matched within 50 cents without reusing a candidate. These frame-weighted results include dyads occurring within comping passages. Deliberate two-note performances and their attacks still need separate measurement.

## Two voices

At each tracker update, match existing voices to distinct candidates by pitch distance, with a bounded tolerance for drift. With just two slots, compare the two assignments directly. Give a new candidate a short confirmation window. Hold an established note across brief misses and release individually after a longer absence. Keep oscillator phase continuous through release and calculate increment from the accepted frequency. OpenPocket's existing pulse helper advances a quarter-rate phase and scales the phase and step together for a fundamental pulse. Reuse that convention or use a full-rate phase consistently with the saw helper. Try Pulse Poly first. Use the same allocator for Saw Poly and Arcade Poly once the note handling and pedal load are acceptable.

The current onset is global, so it cannot tell which of two held notes has been repicked. A repick estimate should combine the global attack with a local rise in evidence around the matched pitch and its relatively unshared harmonics. Ambiguous attacks should leave both envelopes alone. A unison or an octave pair with dominant shared partials can be truly ambiguous at the single mixed pickup: one onset cannot reliably identify which string changed. Continuous per-note contours and string/fret inference are useful research comparisons, with different complexity and runtime assumptions from this embedded allocator. [FretNet paper](https://arxiv.org/abs/2212.03023)

For bass follow, treat choosing the lowest validated note as a separate evaluation. Hold the previous choice briefly across a dropout and reduce the shifted output when confidence is low. Measure incorrect bass substitutions before comparing the result on the pedal against fixed RANGE. Keep all of these tracker experiments separate from judging the input-derived chord voices, which can be auditioned without pitch estimates.

## First implementation pass

STRING MACHINE uses the STRINGS HQ swell and tone filters. The direct voice supplies the base tone, OCTAVE adds the
+12 layer, and ENSEMBLE crossfades into the three modulated taps. POLY BRASS keeps the audio-derived BRASS path, with
a shorter envelope, lower waveshaping drive, and a separate BITE amount. ORGAN HQ already provides the drawbar patch.
These are starting voicings for a pedal listening pass, especially on close dyads and fast stabs.

To audition the host oscillator prototype, compile `tools/archive/legacy-synth/track_bench.c` as described in its header and run
`build/track_bench input.wav notes.csv 1 pulse.f32`. Use a 44.1 kHz mono DI. CSV columns `v1` and `v2` show the assigned pitches, and `pick1` and `pick2` mark voice starts or retriggers. The render is mono 44.1 kHz native float32. The allocator acquires candidates after
five agreeing frames, holds a missing voice for eight frames, and uses a rise in that candidate's salience as
additional evidence for a repick. A synthetic 110/196 Hz pair demonstrated independent arrival and muting, with
brief lingering tracker candidates after the final mute. Real guitar repicks, close dyads, octave pairs, and the
processor's peak block time remain acceptance checks. Saw and arcade variants can share the allocator once it
reliably tracks two notes.

A second synthetic run added independent repicks while both notes decayed. It produced a false second voice on
the first attack, lost an assigned voice around the second onset, and did not reliably retrigger the intended
voice. Keep the allocator on the host. Compare candidate extraction, pitch-specific onset evidence and assignment
separately before trying another oscillator preset.

## Candidate sweep after v50

The first 24 GuitarSet comping takes give 59,172 settled two-pitch frames and 48,520 settled single-pitch frames.
All rows use the same recordings, candidate matching within 50 cents, and the existing decimator.

| Host configuration | Both notes right | Lowest note right on dyads | Extra candidates on single pitches |
| --- | ---: | ---: | ---: |
| 15 bands, current default | 20.7% | 45.7% | 53.0% |
| 22 bands | 20.5% | 45.6% | 53.3% |
| 29 bands | 20.3% | 45.5% | 53.3% |
| Minimum second-note salience 35%, up from 20% | 16.3% | 43.6% | 42.8% |
| Leave 35% residual energy on shared upper harmonics | 25.8% | 46.0% | 67.0% |
| Shared residual and higher salience threshold | 19.3% | 43.9% | 46.1% |

To reproduce, compile the benchmark with `-DPXT_NB=22`, `-DPXT_SECOND_REL=0.35f`, or
`-DPXT_SHARED_RESIDUAL=1` as appropriate:

```sh
cc -O2 -Iengine -Ifirmware tools/archive/legacy-synth/track_bench.c firmware/px_math.c -lm -o build/track_bench
python3 tools/research/track_eval.py guitarset 24
```

Use a distinct `SUFFIX` for each build if retaining its CSVs. The larger bank cost did not help on this subset.
Partial reuse recovered some real dyads but admitted many more false notes. The next comparison should require
evidence specific to a proposed second fundamental while allowing shared upper harmonics to support it.
Test causal resonator candidates against the same recordings and include deliberate guitar and bass repicks.
The current 60 Hz minimum structurally excludes bass low E and low B. A bass benchmark needs a lower
frequency analysis path and must report the extra onset delay separately.

## v56 Pulse Poly on Full

The opt-in `pulse-test` channel adds **PULSE POLY** at the end of the SYN
catalogue. It keeps the shared ladder and uses a separate full-rate PolyBLEP
phase, envelope and running pitch for each of the two voices. The quarter-rate
resonator bank proposes pitches and phase differences refine their frequency.
The allocator confirms new pitches across three reports, keeps existing
voices across brief dropouts and uses a rise in absolute note power as repick
evidence. Independent second and third partials can support a bass fundamental
that misses the raw peak shortlist.

The controls are **WIDTH**, **PWM**, **CUTOFF**, **RESO** and **RELEASE**.
Pulse has its own resonator state, including when Lowest Octave HQ is in the
same preset. Existing SYN type IDs and the five-byte parameter format are
preserved. The test build keeps AMP, NAM, CAB, user IR and Lowest Octave HQ.

| GuitarSet, 24 held-out comping takes | Raw resonator | Pulse voices |
| --- | ---: | ---: |
| Standard tuning dyads, both pitches within 50 cents | 42.2% | 33.6% |
| Standard tuning single-note frames with extra pitches | 37.4% | 37.1% |
| Standard tuning second-note entries within 100 ms | 160/270 | 109/270 |
| Five-semitone-lower DI proxy, both dyad pitches within 50 cents | 37.4% | 29.2% |
| Five-semitone-lower DI proxy, single-note frames with extra pitches | 34.4% | 32.7% |

Voice scoring uses oscillator gain above 0.065 and looks at settled frames
from 30 ms after annotation onset. The lower guitar set uses varispeed on the
same recordings. It is a proxy for physical down-tuning.

Two voices scored 93–97% on synthetic staggered fourth, fifth and octave
sustains. Semitone sustain scored 0%. A synthetic two-semitone bend under a
held upper note stayed within 50 cents in 88.1% of the bend window, with a
20-cent median error. On four recorded round-robin 4 bass pairs, the first
correct low voice appeared 54–65 ms after attack. Both-note sustain was
0–20% across those B0/E1 and E1/A1 pairs. The wet trace includes the oscillator
and envelope timing and excludes converters. Soft repicks, close intervals,
individual mutes and bass sustain still need work before this allocator is
ready for Saw Poly or Arcade Poly.

### Build and audition

```sh
OP_CHANNEL=pulse-test sh firmware/build.sh
cc -O2 -Iengine -Ifirmware tools/archive/legacy-synth/pulse_poly_bench.c firmware/px_math.c -lm -o build/pulse_poly_bench
build/pulse_poly_bench notes.csv < mono-44100.f32 > pulse.f32
```

Both packages are 675,968 bytes and use a 482,416-byte app. Full v56 leaves
144 bytes in the RAM code window, 864 bytes in low RAM, 5,720 bytes in
general RAM and 1,456 bytes in the DSP pool. The per-report peak picker
runs from flash in this channel. The native sweep passed 3,010 configurations,
the Pulse replay passed ASan and UBSan, and the standard engine matched the
previous revision bit for bit in 1,022 equivalence runs.

The BlackBox updated successfully and reported `BlackBox_956`. All three
saved presets matched their v55 checksum. The editor read back all five
controls at 0, 50 and 100, and the bypass toggle passed. The probe restored
and read back the original sound.

| BlackBox v56 smoke test | Peak callback load |
| --- | ---: |
| PWM Synth HQ | 22% |
| Pulse Poly | 18% |
| Pulse Poly with Smart Gate | 23% |

Core 2 stayed active with zero late blocks and no safety bypass. The input
was uncontrolled. Active-playing peak load and audible onset still need a
guitar-input test.


## Next voicings

Build **Saw Poly**, **Arcade Poly** and **FM Poly** in that order on the
two-voice allocator. Saw Poly adds controllable analogue-style oscillator
detune and note-driven filter sweep. Arcade Poly adds chiptune waves and
optional semitone stepping. FM Poly uses a two-operator sine voice inspired
by early digital synths such as the Yamaha DX7, with a per-note modulation
envelope for electric pianos, metallic bells and other digital textures.
Benchmark the FM bandwidth limit and peak callback load at maximum index.
Monophonic orchestral ensembles are a separate line of work.

## v57 Saw, Arcade and FM Poly on Full

The opt-in `poly-test` channel appends three SYN types after Pulse Poly.
Each uses the same two-note allocator and independent voice envelopes.
The new voicings inherit v56's tracking limits on close intervals,
soft repicks and recorded low bass.

| Effect | Controls | Voicing |
| --- | --- | --- |
| Saw Poly | DETUNE, OCTAVE, CUTOFF, SWEEP, RELEASE | PolyBLEP saw, second saw when detune is raised, per-note filter sweep into the shared ladder |
| Arcade Poly | WAVE, OCTAVE, CRUSH, DECAY, MIX | Square, triangle, noise and narrow pulse, each with an optional semitone-stepped setting |
| FM Poly | RATIO, INDEX, DECAY, RELEASE, LEVEL | Two sine operators per note with a separate decaying modulation envelope |

FM uses phase modulation with ratios from 1:1 to 5:1, including fractional
ratios. Its index falls as the predicted sidebands approach Nyquist. That
makes the brightest patches gentler at the top of the register. The bench
compares the emitted 44.1 kHz oscillator with a 12× exact-sine reference
low-passed to Nyquist, using the same index for each comparison. Error
includes foldback and the firmware sine approximation.

| Carrier and ratio | Maximum index, uncapped → capped | Relative RMS error, uncapped → capped |
| --- | ---: | ---: |
| 110 Hz, 2:1 | 5.000 → 5.000 | −51.9 → −51.9 dB |
| 880 Hz, 5:1 | 5.000 → 2.010 | −9.1 → −42.9 dB |
| 1,250 Hz, 5:1 | 5.000 → 0.764 | −0.3 → −55.6 dB |
| 1,318 Hz, 5:1 | 5.000 → 0.611 | −0.3 → −58.9 dB |

On the same arpeggio DI, the four voicings took 1.02–1.04 ms of host CPU
time per second of audio. The maximum-index FM patch took 1.03 ms. These
are medians of five native renders and do not show AC791N callback load.
On isolated oscillator tests, Saw split a 110 Hz note around 109 and 111 Hz.
Arcade followed a 112 Hz target and stepped to 110 Hz with STEP SQUARE.
The standard engine matched the previous revision across 1,022 equivalence
runs. The v57 sweep passed 3,091 configurations, and the isolated oscillator
tests passed ASan and UBSan.

```sh
OP_CHANNEL=poly-test sh firmware/build.sh
cc -O2 -Iengine -Ifirmware tools/archive/legacy-synth/pulse_poly_bench.c firmware/px_math.c -lm -o build/pulse_poly_bench
build/pulse_poly_bench - fm 40 74 40 60 95 < mono-44100.f32 > fm.f32
build/pulse_poly_bench --fm-oscillator 1250 5 capped > fm-high.f32
```

Both 675,968-byte packages contain a 484,080-byte application. The XIP
window includes the flash image of the initialized RAM sections.

| Linker region | Used | Free |
| --- | ---: | ---: |
| XIP application | 484,080 B | 97,484 B |
| RAM code window, including data and alignment | 85,448 B | 568 B |
| General RAM | 92,632 B | 5,672 B |
| Low RAM | 23,712 B | 864 B |
| DSP pool | 256,592 B | 1,456 B |

The three new voicings add 1,664 bytes to the application and 48 bytes of
general RAM versus v56. Their waveforms run from flash. The allocator and
voice loops are shared, so another small voicing can reuse that state.
An effect that needs its own large buffers or RAM-resident processing has
much less room than the XIP figure suggests.

### BlackBox v57 smoke test

The update from v56 completed and all three saved presets matched their
pre-update checksum. The editor read back the five controls at 0, 50 and
100 on all three new types. The bypass toggles passed and the probe restored
the original sound. Arcade value labels use the editor protocol's seven
characters, such as `STEP PU` for stepped pulse.

| Effect | Smart Gate off | Smart Gate on |
| --- | ---: | ---: |
| Pulse Poly | 19% | 23% |
| Saw Poly | 18% | 22% |
| Arcade Poly | 17% | 21% |
| FM Poly | 17% | 21% |

Every pass had zero late blocks, no safety bypass and an active second core.
Input was uncontrolled. Playing load and listening through the guitar input
remain open.

## Phase 2 baseline: v57 frozen

v57 is the reference for the tracking work that follows. The tracker and
allocator are shared by all four Poly voicings, so one score covers them.
`tools/archive/legacy-synth/poly_score.py` reruns every figure below from
`tools/archive/legacy-synth/pulse_poly_bench.c` in about five seconds:

```sh
cc -O2 -Iengine -Ifirmware tools/archive/legacy-synth/pulse_poly_bench.c firmware/px_math.c -lm -o build/pulse_poly_bench
PYTHONPATH=tools/research python3 tools/archive/legacy-synth/poly_score.py build/pulse_poly_bench --guitar
PYTHONPATH=tools/research python3 tools/archive/legacy-synth/poly_score.py build/pulse_poly_bench --guitar --transpose -5
PYTHONPATH=tools/research python3 tools/archive/legacy-synth/poly_score.py build/pulse_poly_bench --bass-di DIR
```

| v57 voices, GuitarSet takes 24–47 | Standard | −5 semitones |
| --- | ---: | ---: |
| Dyads, both pitches within 50 cents | 33.6% | 29.2% |
| Lowest pitch right | 66.7% | 62.9% |
| Single-note frames with an extra voice | 37.1% | 32.7% |
| Second-note entries within 100 ms | 109/270 | 114/324 |
| Comping precision / recall | 76.1% / 51.6% | 74.2% / 48.5% |

| v57 synthetic sustain | Both notes sounding | Low repick, both sounding |
| --- | ---: | ---: |
| Fourth | 93.9% | 51.4% |
| Fifth | 96.5% | 0.0% |
| Octave | 93.0% | 0.0% |
| Semitone | 0.0% | 9.3% |
| Bass fifth (B0/F#1) | 15.8% | 0.0% |

The bend case stays within 50 cents for 88.1% of the window. Its median error
is 20 cents. The recorded bass pairs give 0–20% both-note sustain. A
replacement tracker must beat 33.6% standard dyads. Extra voices on single
notes must stay at 37.1% or lower. The pedal smoke-test loads above are the
CPU baseline: 17–19%, or 21–23% with Smart Gate.

## v58 shared poly tracker

`engine/px_poly_track.h` now holds the tracker and allocator. That covers
the resonator candidates, phase-difference frequency refinement, bass
recovery, the two note identities and their gain envelopes. The Poly
voicings keep only oscillator phase, colour envelope and rendering in
`engine/px_pulse_poly.h`. The tracker reports `PXP_ACQUIRED` and
`PXP_RESTRUCK` so a renderer can restart its own state on note events.
Bench audio and voice traces are bit-identical to v57. That holds for all
four voicings at four knob settings on loops and a GuitarSet take.

The link leaves 400 bytes of RAM code window, 168 fewer than v57, because
the note step is now inlined into the RAM-resident voice loop.
`node tools/poly_pedal_check.mjs BlackBox_958` passed on the BlackBox. Peak
load was 18–20%, or 22–24% with Smart Gate, with no late blocks, no safety
bypass and the second core active. The three saved presets kept their hash.

## v59 harmonic-sum picker

The shared `pxr_pick` keeps the six strongest local maxima above 15% of the
peak energy. On the dev takes (GuitarSet comping 0–23), both notes of a dyad
are local maxima of the bank in 70.7% of frames, but only 45.1% also clear
that threshold. In 28.5% of frames the strongest band is not a played note.
The allocator then dropped every candidate below 20% of the peak energy.

v59 gives the poly tracker its own picker, `pxp_pick`. It considers local
maxima down to 15% of the peak amplitude, about 2% of the energy. It ranks
them by the band plus its 2nd–5th harmonic bands, each weighted half the
previous. A second note needs 20% of the first note's sum. Bands already
claimed as the first note's harmonics count at 35%. The allocator's start
threshold falls to 1% of the peak energy. Confirmation and hold stay at
three and four reports, so the start latency does not change.
`pxr_advance` now updates the bank and `pxr_pick` still serves Lowest
Octave HQ, which is unchanged.

The parameters were swept on the dev takes and chosen on the held-out takes
24–47. Neither set was used to choose the other's figures.

| GuitarSet takes 24–47, voices | v57 | v59 |
| --- | ---: | ---: |
| Dyads, both pitches within 50 cents | 33.6% | **45.7%** |
| Lowest pitch right | 66.7% | 72.7% |
| Single-note frames with an extra voice | 37.1% | **36.0%** |
| Second-note entries within 100 ms (median) | 109/270 (22.6 ms) | 125/270 (22.6 ms) |
| Comping precision / recall | 76.1% / 51.6% | 81.6% / 54.5% |
| −5 semitones: dyads / extra voices | 29.2% / 32.7% | 37.9% / 31.2% |

On the synthetic fixtures, the fifth's low repick now keeps both notes 97.2%
of the time, up from 0%. The octave's upper note enters at 69 ms instead of
40 ms. The bend is unchanged. Semitone sustain is still 0%. At E2/F2 the
bank's fixed 14 Hz bandwidth cannot separate the fundamentals or second
harmonics. A conditional rule accepted a merged band when two or more of its
harmonic bands were resolved peaks. It reached 38.6% semitone sustain but
removed octaves entirely and lowered guitar dyads, so it was not kept. The
recorded bass pairs drop from 0–20% to 0–6% both-note sustain. The GuitarSet
bass measure rises from 51.3% to 56.9%.

`tools/archive/legacy-synth/poly_diag.py` breaks dyad accuracy down by interval, on the dev,
held or remaining takes. `PXP_SET="RHO=0.3 ACQ=0.05 …"` overrides the picker
and allocator constants in `tools/archive/legacy-synth/pulse_poly_bench.c` for sweeps.

BlackBox v59: peak 18–21%, or 23–24% with Smart Gate. There were no late
blocks, no safety bypass and the second core stayed active. The saved
presets kept their hash. The RAM code window has 392 bytes left.

## v66 owner listening on v65, and the fixes

The owner's verdicts on BlackBox v65: the Poly synths trigger randomly, and
latency is high. Saw and Pulse track acceptably, except Pulse on low notes.
FM is fun but tracks badly. STRING DUO's violin rarely sounds. STRING and
BRASS ENSEMBLE track badly. CELLO ENSEMBLE's latency is good. STRING MACHINE
and POLY OCT FUZZ are good. POLY OCTAVE HQ and OCT −2 are fine.

`tools/archive/legacy-synth/poly_latency.py` scores GuitarSet solos. On v65, 58% of voice
starts were on pitches nobody played, 196 a minute on the dev takes. Most
were a second voice starting while the first held a note: on that note's
octave (517), within a semitone of it, as during a bend (350), or on
whatever was still ringing. v66 changes the allocator in four ways:

- A second note, started while the held note still carries half the
  level, needs 6 reports instead of 3.
- A voice never starts on a harmonic of the held note.
- A voice never starts within 7.5% of the held note's pitch.
- The quieter free voice takes a new note first, so it does not wait for
  a releasing voice to dip.

| Held-out GuitarSet | v65 | v66 |
| --- | ---: | ---: |
| Solo: false voice starts | 181/min (54%) | **118/min (45%)** |
| Solo: latency to the right pitch, median / p90 | 12.5 / 44.6 ms | 11.8 / 45.2 ms |
| Comping: extra voices on single notes | 36.0% | **28.9%** |
| Comping: dyads, both pitches right | 45.7% | 40.9% |

Shorter bank decays (10–18 ms instead of 22.7 ms) did not reduce latency
and cost up to two-thirds of the dyads. The remaining latency is the
three-report confirmation and the resonators' build-up. Synthetic fixtures
show the cost: a held octave's upper note now enters after 142 ms, and
fourths and fifths after 55–66 ms.

STRING DUO's violin now plays the upper of two notes at its own pitch, and
a single note from G3 up. The ensembles' support attack falls from 70 to
20 ms (strings) and from 25 to 12 ms (brass). The support follows only
pitch that has held for 8 ms. POLY OCT FUZZ LEVEL now reads 0–100, from
silent through 0 dB at noon to +6 dB.

BlackBox v66 passed the pedal check: 31 measurements, no late blocks.
LOWEST OCTAVE HQ, the lowest-note mode from v54 that the `poly-test`
channel keeps, measured 17% for the PITCH block.
