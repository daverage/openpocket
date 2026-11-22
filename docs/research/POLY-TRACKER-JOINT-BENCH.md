# Joint candidate fit and Essentia comparison

The host prototype reuses OpenPocket's ERB partials. It keeps up to 12 candidate fundamentals, scores all
one-note and two-note hypotheses, counts each spectral partial only once, and requires some distinct
support for each note. It runs after the existing mono layer in the benchmark so the test isolates raw
polyphonic candidates. It does not change the engine or the firmware.

## Recorded guitar comparison

The first 24 GuitarSet comping pickup mixes contain 59,172 settled two-note frames and 48,520 settled
single-note frames at the OpenPocket tracker cadence. The scorer matches frequencies within 50 cents.
An extra candidate on a single-note frame can produce a false oscillator voice. No voice allocation or
temporal smoothing is included in the figures.

| ERB candidate selector | Both pitches right | Lowest pitch right on dyads | Extra candidates on single pitches |
| --- | ---: | ---: | ---: |
| Existing, 15 bands | 20.7% | 45.7% | 53.0% |
| Joint fit, 12% pair penalty | 20.0% | 40.3% | 61.9% |
| Joint fit, 23% pair penalty | 17.7% | 39.2% | 53.2% |
| Joint fit, 35% pair penalty | 14.8% | 37.5% | 44.9% |
| Joint fit, 23% pair penalty, 29 bands | 17.6% | 38.8% | 54.2% |

The pair fit adds up to 66 pair comparisons per frame and an 864-byte shortlist on a float32 build.
It does not improve the accuracy/false-candidate trade-off. Leave it host-only and compare a different
source of partial evidence before refining the allocator or spending more pedal DSP on this scorer.

## Past-only Essentia comparison

Essentia 2.1-beta6-dev and OpenPocket were scored at the same timestamps with a 512-sample
(11.6 ms) scoring step. Each Essentia frame used only audio available at its timestamp. With a 2,048-sample
window the set has 7,404 settled dyad frames. The 4,096-sample comparison starts later and has 7,394.
OpenPocket rounds to the same percentages on either grid.

| Detector | Both pitches right | Lowest pitch right on dyads | Extra candidates on single pitches | Second note found within 100 ms | Median second-note time on hits |
| --- | ---: | ---: | ---: | ---: | ---: |
| OpenPocket | 20.6% | 45.6% | 52.6% | 62/179 | 17.9 ms |
| Joint fit, 23% pair penalty | 17.7% | 39.3% | 52.9% | 50/179 | 18.3 ms |
| Essentia, 2,048 samples (46.4 ms) | 13.0% | 24.3% | 83.7% | 48/179 | 13.1 ms |
| Essentia, 4,096 samples (92.9 ms) | 14.6% | 31.7% | 69.0% | 59/179 | 35.3 ms |

An entry has one other annotated pitch sounding for at least 30 ms before the new note. Both must
persist for at least 100 ms after entry. The reported delay is the first scored frame containing both
expected pitches. It is quantized by the scoring step and does not measure actual oscillator onset.
Read the medians with the hit counts: a low median can accompany many misses. Neither Essentia
configuration improves on the embedded detector on this set. These are host candidate scores on guitar.
Bass low notes, controlled repicks and mutes, and peak AC791N DSP load remain unverified.

The next comparison should test a causal resonator bank with better low-frequency separation.
Score identified low fundamentals independently from upper harmonics, and include low E and low B.
The existing ERB partials omit information needed to distinguish some close notes. Changing the
pair penalty or adding more overlapping upper bands did not fix that in this benchmark.

## Reproduce

```sh
cc -O2 -Iengine -Ifirmware tools/archive/legacy-synth/track_bench.c firmware/px_math.c -lm -o build/track_bench
BENCH=build/track_bench SUFFIX=_review15 python3 tools/research/track_eval.py guitarset 24
cc -O2 -DPXT_HOST_JOINT -Iengine -Ifirmware tools/archive/legacy-synth/track_bench.c firmware/px_math.c -lm -o build/track_bench_joint
BENCH=build/track_bench_joint SUFFIX=_joint23 python3 tools/research/track_eval.py guitarset 24
python tools/research/essentia_track_eval.py --takes 24 --frame 2048 --hop 512 --compare-suffix _review15 --compare-suffix _joint23
```

Set `-DJOINT_PENALTY=0.12f` or `-DJOINT_PENALTY=0.35f` for the other penalties, and `-DPXT_NB=29`
for the wider bank. Use a separate Python environment with Essentia and NumPy. The Essentia driver timestamps
each frame at its right edge and compares with the most recent OpenPocket result available at that time.
It uses the upstream library as a host reference and does not copy its implementation.
[Essentia API](https://essentia.upf.edu/reference/std_MultiPitchKlapuri.html) ·
[Essentia source and license notice](https://github.com/MTG/essentia/blob/master/src/algorithms/tonal/multipitchklapuri.cpp)
