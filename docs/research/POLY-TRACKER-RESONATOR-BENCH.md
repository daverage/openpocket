# Causal resonator tracker benchmark

The host adapter uses the upstream guitar-pitch-detection Rust detector's resonator, peak selection and octave
suppression directly. It omits Cycfi Q's separate mono technique analysis, which does not contribute to the
polyphonic output. No upstream code is bundled or added to the pedal. The external checkout's GPLv3 terms apply
to the host binary built with that code. The comparison used upstream commit
`325114842cfc93c515a55fa776b4e72b11a8cc13`.
[Upstream detector and license](https://github.com/santzit/guitar-pitch-detection-/tree/325114842cfc93c515a55fa776b4e72b11a8cc13)

The reference uses one complex resonator per semitone and reports tempered pitches. A pitch that bends across
a semitone may switch to a different grid point. The local-maximum rule cannot emit two adjacent semitones
at once. Its octave suppression can also mute a real octave when the lower note is stronger. The measured
improvement is for candidate selection, not continuous oscillator pitch or per-note tracking through bends.

## Results

The input is the same GuitarSet mono pickup mix for both detectors. OpenPocket is scored using the most recent
of its results available at the reference timestamp. Each reported hit must match the ground truth within
50 cents. The quarter-rate path uses a causal FIR and retains its 32 input-sample delay in the results.

| Configuration | Both notes right | Lowest on dyads | Extra candidates on single notes | Second-note entries within 100 ms |
| --- | ---: | ---: | ---: | ---: |
| First 24 takes: OpenPocket | 20.6% | 45.7% | 52.6% | 62/179 |
| Resonator, full rate | 40.1% | 53.4% | 58.7% | 106/179 |
| Resonator, quarter rate | 40.5% | 53.8% | 58.4% | 105/179 |
| Quarter rate, shorter 12 ms decay | 31.2% | 45.6% | 60.8% | 107/179 |
| Quarter rate, 30% energy threshold | 26.7% | 49.4% | 47.2% | 79/179 |
| Quarter rate, two reports to confirm extra notes | 34.4% | 53.3% | 44.5% | 88/179 |
| Next 24 takes: OpenPocket | 19.9% | 56.9% | 39.6% | 75/270 |
| Resonator, quarter rate | 49.7% | 69.2% | 48.3% | 171/270 |
| Quarter rate, 30% energy threshold | 35.3% | 65.7% | 37.5% | 126/270 |

Unless specified, the resonator decay time constant is 22.66 ms, threshold is 15% of maximum resonator energy,
and detections are reported as soon as they pass upstream peak selection. These rows use a 512-input-sample
report interval (11.6 ms). Median second-note entry time among successful holdout hits was 15.8 ms for the
quarter-rate reference, 16.4 ms at the stricter threshold, and 19.1 ms for OpenPocket. The report interval,
resonator decay, and note entry time are different quantities. None measures audible pedal onset.

On the holdout, reporting every 128 input samples (2.90 ms) and confirming secondary notes across
three consecutive reports preserved much of the improvement while reducing false extra notes:

| Holdout, matched 128-sample grid | Both notes right | Lowest on dyads | Extra candidates on single notes | Second-note entries within 100 ms | Median successful entry |
| --- | ---: | ---: | ---: | ---: | ---: |
| OpenPocket | 20.1% | 56.8% | 39.9% | 97/270 | 15.5 ms |
| Quarter-rate resonator, three-report confirmation | 42.1% | 69.3% | 37.4% | 159/270 | 15.4 ms |

Comparisons on this grid are separate from the 512-sample results above. The finer scoring grid gives both
detectors more opportunities to register a hit. Confirmation may reject short bass notes and changes in
energy ranking, so this guitar setting still needs a bass-specific validation pass.

In the holdout, the unmodified quarter-rate reference found both notes in 829 of 1,461 minor-third frames,
1,269 of 2,409 fifths, and 267 of 1,056 true octaves. It missed all 44 whole-tone frames in that subset.
Close notes remain a distinct test case.

The synthetic bass fixtures use pluck-like partials, known attacks, staggered mutes and a soft repick with
the upper note held. The quarter-rate reference scores 19/22 isolated B0 frames and 20/22 isolated E1 frames.
With the second note present it scores 31/64 and 36/64 respectively. The last low candidate was still present
31.9 ms after the B0 mute and 20.3 ms after the E1 mute. The shorter resonator decay lost most bass dyad matches.
These are controlled signals and do not establish performance on real bass DI, pickup noise, or palm mutes.

## Embedded direction

Test a fixed-size C implementation using OpenPocket's existing decimator and a measured candidate report
interval. Quarter-rate results hold up against the tested full-rate configuration and reduce 53 resonators
from 2.34 million to 584 thousand updates per second. A four-float state and coefficient layout plus one
energy float projects to 1,060 bytes for 53 guitar notes, or 1,320 bytes for a 66-note guitar-plus-bass range.
These estimates exclude decimator, picker, confirmation and engine state. Actual code size, RAM headroom,
peak AC791N block time with Smart Gate and real input-to-audible latency still need target measurement.

Use the resonator as a note identity candidate and test separate frequency refinement for bends. Measure
adjacent-note separation and echoes after mutes before putting this detector in the oscillator allocator.
Use the measured three-report configuration as the first guitar configuration to time on the AC791N.
Keep the existing firmware default until a pedal comparison validates the trade-off.

## Reproduce

The adapter links the upstream source locally rather than vendoring it:

```sh
git clone https://github.com/santzit/guitar-pitch-detection-.git /tmp/resonator-upstream
git -C /tmp/resonator-upstream checkout 325114842cfc93c515a55fa776b4e72b11a8cc13
mkdir -p /tmp/resonator-adapter
cp tools/research/resonator_reference.rs /tmp/resonator-adapter/
ln -s /tmp/resonator-upstream/src /tmp/resonator-adapter/upstream
rustc -O --edition 2021 /tmp/resonator-adapter/resonator_reference.rs -o /tmp/resonator-adapter/bench
cc -O2 -Iengine -Ifirmware tools/archive/legacy-synth/track_bench.c firmware/px_math.c -lm -o build/track_bench
SUFFIX=_review15 python3 tools/research/track_eval.py guitarset 24
SUFFIX=_held15 python3 tools/research/track_eval.py guitarset 24 24
python3 tools/research/resonator_track_eval.py /tmp/resonator-adapter/bench --decimate 4
python3 tools/research/resonator_track_eval.py /tmp/resonator-adapter/bench --decimate 4 --offset 24 --compare-suffix _held15
python3 tools/research/resonator_track_eval.py /tmp/resonator-adapter/bench --decimate 4 --hop 128 --confirm 3 --offset 24 --compare-suffix _held15
```

The evaluation needs NumPy and the GuitarSet mono pickup and annotation files under `build/guitarset`.
