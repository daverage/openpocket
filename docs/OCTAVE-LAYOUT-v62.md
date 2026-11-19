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

## 2. Second sub-octave (−24): prototype only

`tools/sim/sub2_proto.c` counts each band's wraps modulo 4 and plays
|z| cos((a + 2πk)/4). This is a genuine two-octave-down from the same bank:
no second bank and no delay line. Its octave-down path reproduces the engine
at 99.9% on single notes and 95.4% on chords.

| −24 from the 26-band bank | On target |
| --- | ---: |
| Single notes E2–E5 | 99.9% |
| Notes E2–G3 | 99.9% |
| Chords | 96.5% |

The cost is about two more inverse square roots per band. Before it ships,
it needs a floor like BASS SYNTH HQ's rule, because E2 two octaves down is
20.6 Hz. It also needs a control: POLY OCTAVE HQ has no free knob. Wobble
on recordings and pedal load are not yet measured.

## 3. Phase coherence and 4. transient handling: not started

Neighbouring-band phase alignment and onset-aware attacks still need a
metric for warble and attack clicks. The wobble score only partly captures
them. Next step: score the octave voice's attack against a dry reference
octave, a synthetic note an octave away, on the GuitarSet comping takes.
