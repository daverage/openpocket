# HQ octave experiments (v62, poly-test)

The HQ octave voices are POLY OCTAVE HQ, POLY OCT FUZZ, and the HQ organs,
strings and choir. They double or halve each band's phase in a 29-band bank
at 11.025 kHz. The bank starts at 60 Hz and its spacing grows as
37.5 Hz + 4% of frequency. The note tracker reads the first 15 bands of the
same bank. v62 tests four changes against that engine, at matched wet level.
Scores come from `tools/sim/octave_banks.c`: energy on the shifted partials
of synthetic single notes and chords, the arrival time of a low note, and
the level wobble each version adds to recordings.

## 1. Band layout (multi-resolution): kept, octave voices only

A sweep covered 60 layouts: low-end spacing 30–60 Hz, growth 2–5% and width
0.7–0.9 of the spacing, each scored up and down. Narrower low bands, the
starting hypothesis, did worse. **60 Hz + 2% apart, 0.7 wide (26 bands)**
was best across both directions:

| Octave up | 29-band engine | 26-band v62 |
| --- | ---: | ---: |
| Single notes E2–E5 on target | 99.4% | 98.9% |
| Chords on target | 77.5% | **78.7%** |
| Low-note arrival D2–E3, mean / worst | 16.9 / 17.6 ms | **11.1 / 12.4 ms** |
| 1.5 kHz arrival | 8.5 ms | 7.4 ms |
| Added wobble: chug, riff, chords, arpeggio, lead (dB) | 1.21 1.43 2.28 0.95 2.88 | **0.87 1.32 2.02 0.78 2.54** |
| Added wobble: owner DI mvave, mvave2 (dB) | 1.25 1.45 | **1.16 1.10** |

| Octave down | 29-band engine | 26-band v62 |
| --- | ---: | ---: |
| Single notes E2–E5 on target | 99.9% | 99.9% |
| Chords on target | 96.3% | 95.4% |
| Low-note arrival D2–E3, mean / worst | 9.8 / 13.6 ms | 9.7 / 13.6 ms |
| 1.5 kHz arrival | 5.7 ms | 3.9 ms |
| Added wobble: chug, riff, chords, arpeggio, lead (dB) | 2.30 1.92 3.43 1.45 3.30 | **1.92 1.56 2.36 1.36 2.70** |
| Added wobble: owner DI mvave, mvave2 (dB) | 1.80 2.07 | 1.77 2.11 |

The note tracker keeps its own 29-band bank. With the 26-band layout,
MONO SYNTH fell from 84.4% to 74.0% right on `mvave2` and from 74.2% to
71.4% on `mvave`, while GuitarSet solo was unchanged. The octave voices
therefore get a separate table (`erb_oband`), costing 1,056 bytes of general
RAM in the test build. New gains (up 0.631, down 1.451) put the new bank's
wet output within 0.02 dB of the old bank's on the five loops.

BlackBox v62, idle input: POLY OCT FUZZ with both octaves up peaks at 44%
(SYN block 41%). STRINGS HQ peaks at 32%. There were no late blocks.

## 2. Second sub-octave (−24): in POLY OCTAVE HQ as OCT −2 (v64)

`tools/sim/sub2_proto.c` counts each band's wraps modulo 4 and plays
|z| cos((a + 2πk)/4). This is a genuine two-octave-down from the same bank:
no second bank and no delay line. On synthetic notes 99.9% of the output
lands on target, and 96.5% on chords. v64 (`PX_SUB2_TEST`) puts it in the
engine as POLY OCTAVE HQ's fifth control, **OCT −2**, in a slot older
presets did not use. A band whose −24 would sound under 35 Hz fades out
over 10 Hz, so a low E keeps its upper partials and drops its 20.6 Hz
fundamental. The level factor (1.79) brings OCT −2 to OCT −1's wet RMS on
the loops at the same setting.

| GuitarSet, 20 takes, fully wet | OCT −1 | OCT −2 |
| --- | ---: | ---: |
| Solo notes: energy on the voice's harmonics | 46.9% | 34.7% |
| Held chords | 32.1% | 20.7% |
| Added wobble on the loops (chug riff chords arpeggio lead, dB) | 2.33 1.82 3.69 1.43 3.79 | 3.69 1.99 4.65 2.63 4.82 |

As expected, two octaves down is less clean than one. BlackBox v64: with
OCT −1 alone the PITCH block runs at 28% (peak 30%), with OCT −2 alone at
33% (peak 40%), and with all three voices at 37–38%. There were no late
blocks.

## 3. Phase coherence: tested, not kept

The host experiment weights each band's octave by its dominance over its
neighbours, (p_i / max(p_i−1, p_i, p_i+1))^β. This mutes a band that only
sees the skirt of a partial owned by its neighbour, whose shifted phase
would beat against it. Results, ENGINE NOW with the v62 bank, wobble
averaged over the five loops and the owner's two DI recordings:

| β | Up: chords / wobble / click | Down: chords / wobble / click |
| --- | --- | --- |
| 0 (v62) | 78.7% / 1.40 / 0.70 dB | 95.4% / 1.97 / 0.44 dB |
| 0.25 | 77.7% / 1.39 / 0.66 | 94.4% / 1.89 / 0.32 |
| 0.5 | 76.4% / 1.40 / 0.58 | 93.3% / 1.85 / 0.23 |
| 1 | 73.8% / 1.43 / 0.43 | 91.5% / 1.81 / 0.11 |
| 2 | 69.6% / 1.48 / 0.26 | 88.6% / 1.78 / 0.00 |

Each step of β trades chord accuracy for a little less wobble down and less
click. No setting improves one without costing the other, so the engine
keeps β = 0. Run `BETA=0.5 build/octave_banks …` to reproduce.

## 4. Transient handling: tested, not kept

The host experiment ducks the octave voices at each detected pick: a 1 ms
envelope more than twice the 40 ms one. The voices then recover over
DUCK ms from a depth of DUCKD. The click score is the wet output's energy
above about 2.5 kHz in the 20 ms after each pick, relative to its average,
in dB.

A detector that may fire again during the attack mutes the voice for tens
of milliseconds. Arrival rose from 11 to 42–53 ms and wobble tripled. With
one duck per pick (60 ms refractory):

| Duck | Up: arrival / wobble / click | Down: arrival / wobble / click |
| --- | --- | --- |
| off (v62) | 11.1 ms / 1.40 / 0.70 dB | 9.7 ms / 1.97 / 0.44 dB |
| 6 ms from 60% | 11.3 / 1.43 / 0.44 | 10.1 / 1.96 / 0.25 |
| 12 ms from 60% | 11.5 / 1.48 / −0.05 | 10.5 / 1.98 / −0.21 |
| 12 ms from 0% | 12.4 / 1.98 / −0.70 | 11.9 / 2.34 / −0.32 |

The click falls by at most 0.75 dB. Arrival rises 0.2–2 ms and wobble up to
0.6 dB. That is not enough to ship. If a listening test finds the pick
click objectionable, 12 ms from 60% is the setting to try. Reproduce with
`DUCK=12 DUCKD=0.6 CLICK=1 build/octave_banks …`.
