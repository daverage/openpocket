# Note tracking and the HQ octave bank (v49)

In v49 the tracked voices and the HQ octave modes share one analysis: a bank of complex band-pass filters. The bank changed shape to fix the octave's warble and gargle and to halve its cost. A new tracker (`engine/px_track.h`) reads the same kind of bank to find notes. The TRUMPET and CELLO voices use it. The literature and the open-source options are in [NOTE-TRACKING-RESEARCH.md](../research/NOTE-TRACKING-RESEARCH.md). The octave method comes from [POLYPHONIC-PITCH-RESEARCH.md](../research/POLYPHONIC-PITCH-RESEARCH.md).

## The octave bank

**What was wrong.** POLY OCTAVE HQ, STRINGS HQ, CHOIR HQ, ORGAN HQ, CHURCH ORGAN HQ and POLY OCT FUZZ all double or halve the phase of each band (Thuillier's method). Up to v48 the bank had 43 bands spaced on the ERB scale and 1.5 times an ERB wide, from 65 Hz to 4 kHz at 22.05 kHz. Above a few hundred Hz a band was wider than a guitar note's harmonic spacing: about 360 Hz wide at 2 kHz, against 82 Hz between a low E's harmonics. Doubling the phase of several partials at once mixes them. That gives the gargle and ring-mod texture and the warble, and the octave drops out where neighbouring bands cancel. The owner heard this in every octave mode, on single notes and chords. The MIT-licensed [Terrarium Poly Octave](https://github.com/schult/terrarium-poly-octave) uses the same phase doubling (`util/BandShifter.h`) with 80 narrow bands from 60 Hz to 2.1 kHz. The bank is what differs.

**v49.** The bank now has 29 bands from 60 Hz to 2.05 kHz. Each band is 37.5 Hz plus 4% of its frequency from the next, and 0.8 of that spacing wide: 32 Hz at the bottom, 95 Hz at the top. It runs at 11.025 kHz after two half-band stages. Each band is narrower than the harmonic spacing of the notes that put real energy there. The width grows with frequency, so the top bands stay fast. The coefficients are now shared statics rather than engine fields, which freed 1.3 KB of the engine pool. `tools/research/octave_banks.c` compares the candidate banks.

Host measurements, with energy at exactly twice each partial (±10 cents) and arrival as the first one-output-cycle RMS at half the settled level:

| Octave up | v48 bank | v49 bank |
| --- | ---: | ---: |
| Single notes E2 to E5, on target (mean of 13) | 85.4% | 91.0% |
| Chords E5, A5, G (mean) | 74.5% | 73.8% |
| Arrival, D2 to E3 mean / worst | 17.8 / 29.3 ms | 16.9 / 17.6 ms |
| Arrival at 1.5 kHz | 4.9 ms | 8.5 ms |

| Engine harnesses (`erb_octave_prototype.mjs`, `pitch_down_prototype.mjs`) | v48 | v49 |
| --- | ---: | ---: |
| Up: drop D / low E / close dyad / high chord | 75 / 67 / 21 / 74% | 85 / 62 / 27 / 93% |
| Down: drop D / low E / close dyad / high chord | 94 / 88 / 52 / 86% | 96 / 88 / 78 / 96% |
| Down: weakest repick against the strongest | 84% | 95% |

The engine harness's 82 Hz figure for v48 (7.7 ms) was a lucky alignment of that one note with a band. Averaged over D2 to E3, v48 was slower than v49. The octave voices' levels are matched to v48's within about 1 dB on the bundled loops.

**On the pedal** (BlackBox, AMP and CAB off, `tools/core2_probe.mjs`): STRINGS HQ went from 59% to 34% of a core, POLY OCT FUZZ from 70% to 35%, and POLY OCTAVE HQ measures 29%.

## The tracker

`engine/px_track.h` works on the same bank and rate, with its own state, in this order:

1. **Phase advance.** Each band's phase advance against its centre, power-weighted and averaged over about 6 ms.
2. **Partials.** Local maxima of band power per hertz, so that a wider band does not count as a louder partial.
3. **Salience.** Klapuri's harmonic salience picks the notes. Candidates come from the partials' first five subharmonics. The best note's harmonics are removed and the search repeats, up to six notes.
4. **Weak fundamentals.** A note an octave, twelfth or two octaves lower wins when its own fundamental is present and it explains nearly as much.
5. **Low notes from the waveform.** On a 2.76 kHz copy, a normalised-difference check decides octave or twelfth errors (a guitar pickup's weak fundamental), and for notes below 330 Hz the period search refines the frequency in the style of YIN.
6. **Picks.** Log-band spectral flux finds picks, including a repick of a ringing string.
7. **The note layer (`pxt_layer`).** A note speaks once its pitch has held for 3 ms. It follows bends and vibrato, takes a slur after 6 ms by a leaky vote, and accepts an octave jump only after 29 ms. It also gives pick strength and expression.

Scores against annotations come from `tools/archive/legacy-synth/track_bench.c` and `tools/research/track_eval.py`. A note is right when the voice's pitch is within 50 cents for most of the middle half of the time it is the newest note:

| Notes right | v48 note layer | v49 note layer | Old BASS HQ tracker |
| --- | ---: | ---: | ---: |
| GuitarSet solo, 48 takes (4,686 notes) | — | 81.4% | 62.3% |
| GuitarSet solo, 12 takes | 67.7% | 84.6% | 62.7% |
| Owner's M-VAVE recording, runs to D6 (217 notes) | 54% | 78% | 44% |
| Owner's low-string riffs (77 notes) | 44% | 84% | 61% |

On GuitarSet the v49 layer reaches the right pitch in a median 6.6–8 ms after the onset, with 90% within 41–45 ms. v48 took 16 ms median and 79 ms at the 90th percentile. On chords (GuitarSet comping, 48 takes) it finds notes with 61% precision and 45% recall, and gets the lowest note right in 47% of frames. That supports a bass-note option, but is not yet good enough for per-string voices.

[GuitarSet](https://zenodo.org/records/3371780) (CC BY 4.0) is the mono pickup mix with its per-string note annotations. It is downloaded into `build/guitarset`. The owner's recordings have no annotations. `tools/research/track_ref.py` gives them an offline reference from librosa's pYIN and onsets. That reference is automatic and only approximate. The bundled lead loop is scored against its dataset transcription (`tools/archive/legacy-synth/acoustic_truth.py`).

**Cost on the pedal.** TRUMPET now uses 27% of a core, TRUMPET MODEL 29%, CELLO 40% and CELLO SECTION 30%. The tracker itself is about 20%. Its per-sample parts (`pxt_run`, `pxt_layer_run`) run from RAM. That is too much for the BASS SYNTH HQ, PWM SYNTH HQ and SQUARE SYNTH HQ voices, which cost 9–12% on their zero-crossing tracker, so they keep it in v49. The research ranks Cycfi Q's bitstream autocorrelation as the cheap mono tracker to try for them next.

## The cello's response

On the pedal, the owner found CELLO slow to answer. The host breakdown, with a plucked DI from E2 to E4 at soft and hard picks, showed that the tracker triggers in 9–22 ms. The voices themselves were slow:

- **CELLO** (the bowed model) reached 12 dB under its peak 43–227 ms after the pick. Its bow speed rose with a 10–110 ms time constant, and a bowed string builds its motion from rest over many cycles.
- **CELLO SECTION** reached it in 37–61 ms, because its recordings bow in slowly, over as much as 0.36 s.

v49 makes three changes:

- **Faster bow:** the bow's attack is now 4–34 ms.
- **The string starts moving:** each new note starts it with one period of sawtooth at the bow's speed (`PXA_KICK`).
- **Shorter recorded attack:** CELLO SECTION skips 75% of each recording's attack (`PXA_SKIP`), with a 5 ms fade-in instead.

The time to 12 dB under the peak is now 10–54 ms for CELLO and 25–39 ms for CELLO SECTION. The bowed note then keeps swelling to full level over a few hundred ms, as a bow does. A stronger kick only made a spike followed by a dip.

## The HQ synths, a lighter tracker, and the 2' stop

- **The HQ synths follow the shared tracker.** BASS SYNTH HQ, PWM SYNTH HQ and SQUARE SYNTH HQ now take their pitch from the note layer (`pxt_drive`). BASS SYNTH HQ's old zero-crossing tracker got 61% of the owner's low-string notes right; the note layer gets 84%. SQUARE SYNTH HQ has its own tracker instance, because the SFX and SYN blocks can play together.
- **The tracker reads only the 15 bands up to 790 Hz (`PXT_NB`).**

  | Notes right | 29 bands | 15 bands |
  | --- | ---: | ---: |
  | Owner's M-VAVE recording | 78.3% | 76.0% |
  | Owner's low-string riffs | 84.4% | 84.4% |
  | GuitarSet, 24 takes | 74.9% | 74.6% |

  The cost is about half.
- **Cycfi Q's bitstream autocorrelation detector** (MIT) was the research's cheap candidate. With its author's own conditioning it got 51–59%, 57–60% and 68.5% of notes right on the same three sets, with 33–70 ms median time to pitch, so it was not adopted. The bench is `tools/research/q_bench.cpp`.
- **CHURCH ORGAN HQ's 2'** came from the old delay-line shifter, which warbles two octaves up. It now comes from the band bank, by quadrupling each band's phase (Re z⁴/|z|³) in the bands below 1.25 kHz, at the shifter's level. The bank also now mixes its voices itself, so it needs one output stream instead of two.

On the pedal (BlackBox, AMP and CAB off): BASS SYNTH HQ and PWM SYNTH HQ use 21% of a core, up from 9–12%. TRUMPET uses 20%, down from 27%; CELLO 26%, down from 40%; CELLO SECTION 20%, down from 30%. STRINGS HQ stays at 34%.

## Chords under a one-note voice, and the bass synth's floor

- **Two notes at once made the one-note voices jump.** The owner heard CELLO SECTION go "a bit crazy" and SQUARE SYNTH HQ "fall apart". On GuitarSet's comping takes the voice's pitch jumped 19 times a second and sat on a sounding note 64% of the time. While two notes or more of comparable salience sound (the second at least 60% of the first, for at least two frames), a new strongest note without a pick now needs 15 ms to take over instead of 6 ms. That cut the jumps to 11 a second.
- **Two stronger versions were rejected.** One held the note through the chord: 79% on a sounding note and 6 jumps a second. The other started a chord on its bass. Both cost the owner's single notes 6–17 points, because their harmonics and ringing notes look like chords, so neither was kept (`PXT_CH_HOLDMODE`, `PXT_CH_TRIG`).
- **The sample voices change recording at most every 60 ms.**
- **BASS SYNTH HQ has a sub floor.** A sub that would sound under 35 Hz plays an octave higher. A low E's two octaves down is 20.6 Hz, which the owner heard as "falling apart" on the lowest notes.

## The cello's squeak

On some picks the owner heard a squeak, a narrow tone at 800–1,600 Hz lasting 30–150 ms, on repeated C3 notes (`My Audio/squeek.wav`). That is six to twelve times the note. A host test of eight repicks of a ringing C3 showed the cause. The note layer sometimes spoke on a high partial, because a low note's fundamental takes about 10 ms to settle in the narrow low bands while its partials appear at once. The bowed string then played that high note until the layer corrected it.

Two changes fix it:

- **A short wait for high candidates.** A note above 200 Hz waits 5 frames (7 ms) after its pick before it may speak (`PXT_MINWAIT`).
- **The fundamental takes over at once.** In the first 80 ms, a pitch at a whole fraction of the note's (½ to ⅛), held for two frames, takes over without a vote (`PXT_SETTLE`).

In the repick test, the wrong note now lasts 7 ms instead of 13 ms on the first pick, and none at all instead of 36 ms on the octave-wrong repick. Notes right stay at 76%, 84% and 74% on the owner's two recordings and GuitarSet. GuitarSet's median time to pitch falls from 8.8 to 6.9 ms.

## The bank at rest

With every voice level at zero (STRINGS HQ's OCT at 0, for example) the bank's output is silent, but up to now it ran anyway. `erb_octaves2` now stops after `ERB_IDLE` (64) samples at zero, longer than the 32-sample half-band tails, and clears its state. When a voice comes back the bands build up from zero over 10–17 ms. On the host, the largest sample step at the moment OCT goes from 0 to 50 is unchanged (0.0099, against 0.0097 before), so the warm-up adds no click. The output is bit-identical whenever a level is not zero. On the pedal, STRINGS HQ at OCT 0 measures 14% of a core, down from 34% (`TYPES="STRINGS HQ@50/50/0/50/0" node tools/core2_probe.mjs`; the probe now takes the five parameters after `@`). The standard STRINGS still runs its delay-line shifter at OCT 0 (18%).

## Not done

- MONO SYNTH still uses its own tracker (`syn_track`).
- The tracker's per-frame work (salience and the waveform checks) runs every 1.45 ms from flash, and could run less often.
- No bass-note option for POLY OCTAVE yet.
- No measurement of the tracker with NAM running beside it.
- The second core's earlier shutdown on v48 was not reproduced, and its cause is unknown.

## v60: MONO SYNTH on the note layer (poly-test)

MONO SYNTH was the last voice on `syn_track`. That tracker took zero
crossings of a fixed 110 Hz low-pass, with the median of three periods. With
`PX_MONO_LAYER=1`, which the `poly-test` channel sets, it plays the shared
`pxt_layer` note instead, as the HQ synths and acoustic voices do. The gate,
pick and pitch come from the layer. A pick, a slur or a corrected octave
jumps to the new pitch. Bends and vibrato glide. The oscillator and ladder
are unchanged. The standard build keeps `syn_track` and stays bit-identical
until the voice has had a listening comparison.

`tools/archive/legacy-synth/mono_bench.c` renders what MONO SYNTH plays. `tools/research/mono_eval.py`
scores it: a note counts as right when the voice is within 50 cents over most
of its middle half. Wrong frames play a pitch that is not a sounding note.

| Material | `syn_track` right | layer right | Time to pitch, median / p90 | Wrong-pitch frames |
| --- | ---: | ---: | --- | --- |
| GuitarSet solo takes 30–89, 6,187 notes (unseen) | 59.2% | **91.5%** | 46.5/72.1 → 6.3/40.0 ms | 42.5% → 11.3% |
| GuitarSet solo takes 0–29, 2,631 notes | 46.0% | 72.7% | 40.2/90.1 → 10.4/57.6 ms | 46.1% → 19.0% |
| Lead loop, 27 notes | 48.1% | 96.3% | 26.3/35.6 → 1.3/10.6 ms | 54.0% → 6.6% |
| Owner DI `mvave.wav`, 217 notes | 36.9% | 74.2% | 60.9/101.6 → 27.6/81.2 ms | 59.6% → 30.3% |
| Owner DI `mvave2.wav`, 77 notes | 63.6% | 84.4% | 42.8/97.3 → 47.9/85.6 ms | 42.6% → 32.6% |

Octave errors rise from 71 to 120 of the 6,187 unseen notes. On the owner's
DI the layer sometimes speaks on a high partial before the fundamental
arrives. The `mvave` references are automatic pYIN transcriptions, not
annotations. Release lingering is unchanged, because both versions release
from the same level gate.

BlackBox v60: MONO SYNTH peaks at 25% with the SYN block at 18%. There were
no late blocks and no safety bypass, and the second core stayed active. The
RAM code window has 496 bytes free, because the inlined `syn_track` path
left the RAM loop.
