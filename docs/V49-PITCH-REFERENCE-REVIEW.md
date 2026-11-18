# v49 pitch reference review

OpenPocket's two polyphonic pitch problems have different outputs. The octave effect transforms the pickup waveform without deciding which notes are present. The oscillator synth needs separately validated pitch candidates, then a way to associate those candidates with continuing voices. The octave tests and the multi-F0 tests should therefore remain separate.

## Octave up

**OpenPocket POLY OCTAVE HQ** uses 29 complex biquads from 60 Hz to about 2.05 kHz. The bank runs at 11.025 kHz after two half-band decimators, with matching interpolation back to 44.1 kHz. Each band produces the real part of the squared analytic sample divided by its magnitude. This doubles phase and keeps the envelope for a single component. If multiple components occupy a band, phase scaling can produce beating or intermodulation. Narrower bands trade such artifacts against longer settling and additional band work. Source and state layout are in [px_dsp.c](../engine/px_dsp.c) and [px_dsp.h](../engine/px_dsp.h).

**ChowDSP BYOD** uses a separate 32-band full-rate analytic bank for each upward voice in its V2 processing. Its +12 path uses the same magnitude-normalized phase doubling, with different ERB-grid and filter choices, and x86 or Arm SIMD paths when available. At 44.1 kHz one upward bank alone updates 1.41 million bands/s, versus OpenPocket's 320 thousand shared bank updates/s. This is a structural workload comparison, since the filter realizations and vector widths differ. Use its wet-only +12 output as a reference. Use unmodified plugin processing if comparing sound, since it applies a 20 Hz DC blocker and gain smoothing after the core shifter. [Router and preparation](https://github.com/Chowdhury-DSP/BYOD/blob/main/src/processors/other/poly_octave/PolyOctave.cpp) · [V2 bank size and state](https://github.com/Chowdhury-DSP/BYOD/blob/main/src/processors/other/poly_octave/PolyOctaveV2FilterBank.h) · [V2 bank implementation](https://github.com/Chowdhury-DSP/BYOD/blob/main/src/processors/other/poly_octave/PolyOctaveV2FilterBankImpl.cpp)

**Terrarium Poly Octave** is a closer embedded reference. It runs 80 narrower rotated low-pass biquads at 8 kHz, using staged ×3 and ×2 rate changes around a native 48 kHz Daisy callback. The analysis centers span 60 Hz to roughly 1.69 kHz. It sums phase-scaled bands and applies two shelves to the interpolated wet mix. It performs about 640 thousand band updates/s and computes all three shifted voices, so neither its host cost nor its Daisy performance predicts the JieLi budget. Compare bare shifters to isolate the bank geometry, then separately audition the full voicing including its shelves. [Pedal callback](https://github.com/schult/terrarium-poly-octave/blob/main/main.cpp) · [Multirate stages](https://github.com/schult/terrarium-poly-octave/blob/main/util/Multirate.h) · [Band placement](https://github.com/schult/terrarium-poly-octave/blob/main/util/OctaveGenerator.h) · [Phase scaling](https://github.com/schult/terrarium-poly-octave/blob/main/util/BandShifter.h)

**Thuillier** measured a maximum near 3 ms for ERB-PS2 in the **3–8 kHz bands of the shifted output**, using a laboratory prototype. The corresponding improvements reported against the original EHX POG and Micro POG were 11 and 17 ms. The thesis also documents out-of-band components and a substantial roughness spike at a particular spacing of interference tones. The 3 ms figure therefore cannot establish low-E attack latency, chord artifact performance, or total pedal response. Measure pick arrival at the shifted fundamental as well as in the upper bands. [Thesis, §§7.2 and 9](https://aaltodoc.aalto.fi/bitstreams/ff9e52cf-fd79-45eb-b695-93038244ec0e/download)

## Octave down

- **OpenPocket HQ and Terrarium** both halve subband phase and remember its sign through a branch crossing. Terrarium repeats the operation for −24. OpenPocket has the simpler −12-only real-output path and tracks the sign for each band even while its mix contribution is zero. The RANGE control weights bands and does not identify a chord's lowest actual string. Compare the two banks for spurious subharmonics, cancellations between band outputs, polarity coherence after a mute, and weak repicks. [OpenPocket core](../engine/px_dsp.c) · [Terrarium branch tracking](https://github.com/schult/terrarium-poly-octave/blob/main/util/BandShifter.h)
- **BYOD** routes −12 to two linearly interpolated delay read heads, offset by half a grain cycle, with complementary raised-cosine windows. Its ring length is 60 ms and each head owns a duplicated ring. At 44.1 kHz this is 42,336 bytes of float ring samples per mono processor before scratch and control state. It need not estimate an individual pitch or take complex square roots. Grain phase can still move or smear attacks and introduce amplitude modulation. Sixty milliseconds is the ring duration, not a measured audible delay. [BYOD delay implementation](https://github.com/Chowdhury-DSP/BYOD/blob/main/src/processors/other/poly_octave/DelayPitchShifter.h)
- **OpenPocket standard POLY OCT and the variable shifters** already use two moving delay taps. They share an 8,192-byte ring, use a 1,400-sample down sweep (31.75 ms at 44.1 kHz) and can realign a newly quiet tap against the other with a bounded local similarity search. The standard mode makes a useful independent comparator to BYOD. A test with ±align and multiple start offsets separates the benefit of alignment from sweep duration and the modulation imposed by the window. [OpenPocket delay shifter](../engine/px_dsp.c) · [Prior host octave-down comparison](POLYPHONIC-PITCH-RESEARCH.md#octave-down-and-capo-prototype)

## Multi-F0 and arbitrary transposition references

**Essentia MultiPitchKlapuri** extracts spectral peaks and uses harmonic summation with an optimization stage to estimate note count and fundamentals. Its defaults are 2,048 samples of signal per frame and a 128-sample hop, or 46.44 ms and 2.90 ms at 44.1 kHz. Its default floor is 80 Hz, so explicitly lower it for bass comparisons. Treat batch output as an offline accuracy reference. Measure a causal implementation's timestamps separately. OpenPocket uses streaming ERB partials, harmonic scoring and iterative removal, with a 64-sample (1.45 ms) result cadence. This cadence excludes settling, temporal confirmation, assignment, and output generation. Its 60 Hz floor excludes bass low E and low B. The existing real-DI results show enough error to prioritize candidate quality before revising the allocator or adding voices. [Essentia API and defaults](https://essentia.upf.edu/reference/std_MultiPitchKlapuri.html) · [Essentia source](https://github.com/MTG/essentia/blob/master/src/algorithms/tonal/multipitchklapuri.cpp) · [OpenPocket tracker](../engine/px_track.h) · [Existing candidate benchmark](POLY-SYNTH-DESIGN.md)

**Signalsmith Stretch** is a C++ STFT-based quality reference for arbitrary pitch ratios. Its default uses a 120 ms block and 30 ms interval, and its cheaper preset uses 100 ms and 40 ms with split computation. At 44.1 kHz these are 5,292/1,323 and 4,410/1,764 samples respectively. Actual timing should report both input and output latency getters, with the additional interval included in output latency when split computation is used, plus a same-DI arrival measurement. Its STFT plans, bins, histories, and work buffers are beyond what a block length alone implies. Use target build/map and allocation instrumentation before inferring firmware footprint. [API and dependency](https://github.com/Signalsmith-Audio/signalsmith-stretch) · [Configuration and latency source](https://github.com/Signalsmith-Audio/signalsmith-stretch/blob/main/signalsmith-stretch.h)

## Host measurements

Both octave A/Bs now use the same five dry DI clips: chug, riff, chords, arpeggio and lead. Level-matched wet
renders are generated by the commands below. The clips do not have annotated note-by-note pickup timing, so the
numerical pitch and arrival measurements use controlled signals. A dedicated recorded bass, bend, repick and
individual-string mute set remains necessary for pedal acceptance.

### Octave up

| Host shifter | Low E response | 1.5 kHz input response | Exact target energy, Drop D / low E / close low dyad / high chord |
|---|---:|---:|---:|
| OpenPocket HQ | 16.8 ms | 8.8 ms | 84.8 / 62.3 / 26.8 / 92.7% |
| BYOD V2 +12 core | 23.0 ms | 4.9 ms | 82.2 / 81.0 / 20.4 / 87.7% |

The BYOD host adapter uses its upstream bank implementation and +12 configuration without the plugin's gain
smoothing or DC filter. OpenPocket measurements use the complete isolated pitch block in WASM. Neither is a
pedal input-to-output timing. Response is the midpoint of the first window reaching 50% of settled wet RMS.
The 1.5 kHz input is a 3 kHz shifted component. It does not represent the response of a low note.

A separate layout bench puts the compact 27-band prototype through the same quarter-rate filter and reconstruction
path as OpenPocket. Its low-note response averaged **13.4 ms instead of 16.9 ms** across D2 to E3, with worst
cases of 14.2 and 17.6 ms respectively. Energy within two FFT bins of expected chord components declined from
77.5% to 75.7%. Added level wobble on the five DI clips fell by 0.19 to 0.29 dB. The Terrarium geometry
comparison hit 97.3% of near-target chord energy, but took 65.8 ms on the low-note group and added more wobble.
This is its 80-band layout evaluated at full rate in the host harness, without Terrarium's native 8 kHz path,
gain stages or shelves. It does not establish the real Terrarium pedal's timing or sound.

### Octave down

| Host shifter | Low E response | Exact target energy, Drop D / low E / close low dyad / high chord |
|---|---:|---:|
| OpenPocket standard | 17.4–32.5 ms | 1.2 / 1.1 / 20.9 / 5.2% |
| OpenPocket HQ | 12.2–12.3 ms | 96.4 / 88.2 / 78.2 / 96.3% |
| BYOD delay core | 12.2–49.3 ms | Below 0.05% on these four fixtures |

The exact-target test isolates stable target lines from sidebands. BYOD's delay adapter uses the upstream two
read heads with standard cosine in place of its approximation. On a sustained 82 Hz sine, the strongest output
lines were near 32 and 49 Hz around the intended 41 Hz (roughly −430 and +300 cents). The displacement is
consistent with sidebands from the grain cycle. The result is specific to the wet core and test tones.
OpenPocket HQ's weakest repeated-pick energy was 95.4% of its strongest, versus 4.6% in standard mode.
The compact layout did not improve low-note octave-down attack timing (9.8 ms mean for both in the separate
layout bench). Its near-target chord energy dropped from 96.3% to 95.6%.

### Size and pedal status

The compact host prototype leaves the production arrays at 29 bands. State remains 1,020 bytes per instance,
with about 1,312 bytes of shared coefficient and setup storage. No RAM reduction or AC791N load is claimed
for it. A separate disabled-voice arithmetic prototype rendered bit-identical results on a DI chord clip
including control transitions. At the same build settings, its AC791N application image grew from 471,884
to 471,944 bytes, with unchanged linker RAM totals. Host time improved for restricted-range down only.
Up-only, full-range down and combined processing took longer, so the change was dropped. The standard v50
packages are 675,968 bytes each. Published v49 notes report about 29% core load for HQ octave on BlackBox.
Peak callback time, output latency, and headroom with Smart Gate still need fresh hardware measurements.

## Ranked recommendation

1. **Keep HQ phase halving for octave down.** It was the most stable low-latency down path tested. Keep the
   compact band layout as an octave-up audition candidate. Compare it level matched before replacing the shared bank.
2. **Improve and measure simultaneous candidates for synths.** Reuse the ERB front end and compare a bounded
   joint one-note/two-note fit with Essentia offline. The prior 180-take GuitarSet pass found both notes in
   only 15.2% of settled dyads. A 64-sample tracker update interval is 1.45 ms, independent of the time needed
   to acquire either note. Lower the analysis range for bass and score false harmonics, repicks, and individual
   release before implementing independent production oscillator voices.
3. **Keep Signalsmith Stretch as a desktop arbitrary-transposition reference.** Test a short-window setting
   against the existing delay shifter and the spectral host experiment. Neither stock Signalsmith settings nor
   desktop timing justify an AC791N port. Its DSP and Essentia were reviewed from source and were not run here.

Reproduce the OpenPocket tests with:

```sh
cc -O3 -Iengine -Ifirmware tools/sim/octave_banks.c firmware/px_math.c -lm -o build/octave_banks
TERRARIUM=1 build/octave_banks assets/loops/{chug,riff,chords,arpeggio,lead}.wav
DOWN=1 build/octave_banks assets/loops/{chug,riff,chords,arpeggio,lead}.wav
node tools/sim/erb_octave_prototype.mjs /tmp/openpocket-v49-up-review
node tools/sim/pitch_down_prototype.mjs /tmp/openpocket-v49-pitch-review
```

Optional `BYOD_UP` and `BYOD_DOWN` point at host adapters reading and writing mono 44.1 kHz float32 via
standard input and output. No upstream DSP code or built adapters are bundled.

## Licenses

BYOD identifies its open distribution as GPLv3 under a dual license. Terrarium and Signalsmith Stretch are MIT, including the separate Signalsmith Linear dependency under its own MIT terms. Essentia's source file identifies AGPLv3-or-later and remains an external reference for this comparison. No external code was copied into the firmware by this review. Preserve each applicable license and copyright statement if later importing source. [BYOD license statement](https://github.com/Chowdhury-DSP/BYOD#license) · [Terrarium MIT](https://github.com/schult/terrarium-poly-octave/blob/main/LICENSE) · [Signalsmith Stretch MIT](https://github.com/Signalsmith-Audio/signalsmith-stretch/blob/main/LICENSE.txt) · [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) · [Essentia source notice](https://github.com/MTG/essentia/blob/master/src/algorithms/tonal/multipitchklapuri.cpp)
