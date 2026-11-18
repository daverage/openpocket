# Recorded bass benchmark for Lowest Sub

The host benchmark uses native notes from two CC0 electric-bass libraries. It tests the v51
resonator and synthesized sub with Smart Gate off. Isolated notes come from recordings. Dyads,
repicks and mutes are controlled edits of those recordings. The test has known note boundaries,
and a simultaneous performance on two physical strings remains a separate check.

## Sources

- [FreePats Yamaha RBX](https://freepats.zenvoid.org/ElectricGuitar/clean-electric-bass.html).
  Use `E.wav` and `A.wav` from the fingerstyle and picked WAV packs, at their original pitches.
  The pack maps some lower keys to a transposed E. The benchmark uses the original low E.
- [Karoryfer Black and Blue Basses v1.001](https://github.com/sfzinstruments/karoryfer.black-and-blue-basses/releases/tag/v1.001).
  Use the regular round-robin 1 samples for Baby Blue (pick, bridge pickup) and Dark Black
  (fingerstyle, neck pickup). The file names and SFZ key centers in this release place these
  samples an octave above their measured pitches. Use MIDI 23, 24, 26, 28 and 33 for files
  labeled `b1`, `c2`, `d2`, `e2` and `a2`. The measured B fundamentals were about 31 Hz with
  harmonic peaks near 62 and 93 Hz. No pitch shifting was applied.

These are clean instrument samples. The capture chain and edits differ between sample libraries.
The audio and generated renders are kept outside the repository.

## Results

The upper note enters 240 ms after the low note. The low note is faded out over 6 ms, then
repicked at a lower level with the upper note still held. Each pair uses equal peak-normalized
input levels and an upper note 7.6 dB quieter. Output counts as correct when the oscillator
is within 50 cents of the intended pitch and its internal gain exceeds 0.002. The dyad window
starts 80 ms after the upper note enters. Percentages include missed output.

| Host variant | Correct during settled solo | Correct during settled dyad | Wrong pitch during settled dyad |
| --- | ---: | ---: | ---: |
| Original sub | 3.2% | 7.6% | 82.8% |
| Harmonic verification | 52.9% | 14.5% | 77.5% |
| Verification and continuous refinement | 51.8% | 14.6% | 78.8% |
| Refinement using less-shared harmonics | 52.0% | 14.7% | 78.6% |

These are means of 16 fixture runs per variant, from eight pairs at two level ratios.
They are fixture scores. The takes reuse the same isolated samples.

| Equal-level fixture | First correct / first four correct reports, verified sub | Correct dyad, original → verified | Wrong dyad, verified |
| --- | ---: | ---: | ---: |
| Yamaha finger E1–A1 | 24.7 / 24.7 ms | 0.0 → 11.4% | 77.2% |
| Yamaha pick E1–A1 | 45.1 / 45.1 ms | 0.0 → 3.5% | 91.2% |
| Baby Blue B0–E1 | 10.2 / 10.2 ms | 0.0 → 0.0% | 87.7% |
| Dark Black B0–E1 | 13.1 / 13.1 ms | 19.3 → 21.1% | 71.1% |
| Baby Blue D1–A1 | 126.3 / 207.6 ms | 0.0 → 7.0% | 87.7% |
| Dark Black D1–A1 | 79.9 / 79.9 ms | 28.1 → 26.3% | 60.5% |
| Baby Blue B0–C1 | 10.2 / 10.2 ms | 0.0 → 0.0% | 89.5% |
| Dark Black B0–C1 | 13.1 / 13.1 ms | 2.6 → 2.6% | 89.5% |

The timestamp for the four-report column is the start of the first correct run.
Reports are 2.90 ms apart. These onset measurements use oscillator state and the gain
threshold. Audible wet onset, converter time and hardware latency require separate tests.

The Yamaha pick fixture put out the wrong octave of E1 in 65.8% of held dyad reports with
verification. On Dark Black B0–C1 it played the upper C1 in 45.6% of reports. On Dark Black
B0–E1 it mostly emitted other pitches when wrong. Suppressing octave jumps alone would only
address part of the failure. Across the equal-level fixtures, four-report lock could still be
followed by long wrong-pitch passages, and first correct repick ranged from 57.8 ms to a miss.

Keep strict Lowest Sub experimental. Test candidate identity below 60 Hz before changing the
oscillator. Use the benchmark to score whether the correct low note is present in the candidate
list, whether it survives an upper note entering, and whether it clears when the low string
stops. Reserve further recordings and physical two-string DI for validation. Peak DSP and
RAM use on the AC791N are unmeasured for these hosts.

## Reproduce

The script requires Python with NumPy and FFmpeg. Values for `finger` and `pick` are
the directories containing the FreePats WAVs. Set `blue` to `Samples/babyblue/reg` and
`black` to `Samples/darkblack/reg` in the extracted Karoryfer archive.

```sh
cc -O2 -Iengine -Ifirmware tools/sim/lowest_octave.c firmware/px_math.c -lm -o /tmp/lowest_octave
python3 tools/sim/bass_di_eval.py /tmp/lowest_octave \
  --pair yr-finger 28 "$finger/E.wav" 33 "$finger/A.wav" \
  --pair yr-pick 28 "$pick/E.wav" 33 "$pick/A.wav" \
  --pair blue-b0-e1 23 "$blue/babyblue_b1_f_rr1.wav" 28 "$blue/babyblue_e2_f_rr1.wav" \
  --pair black-b0-e1 23 "$black/darkblack_b1_mf_rr1.wav" 28 "$black/darkblack_e2_mf_rr1.wav" \
  --pair blue-d1-a1 26 "$blue/babyblue_d2_f_rr1.wav" 33 "$blue/babyblue_a2_f_rr1.wav" \
  --pair black-d1-a1 26 "$black/darkblack_d2_mf_rr1.wav" 33 "$black/darkblack_a2_mf_rr1.wav" \
  --pair blue-b0-c1 23 "$blue/babyblue_b1_f_rr1.wav" 24 "$blue/babyblue_c2_f_rr1.wav" \
  --pair black-b0-c1 23 "$black/darkblack_b1_mf_rr1.wav" 24 "$black/darkblack_c2_mf_rr1.wav" \
  --output /tmp/openpocket-bass-bench
```

`+12` and `upper` count wrong pitch due to octave and upper-track selection respectively.
`held` is the correct fraction of the settled repick. `tail` is the last correct low
output between the mute and repick, in milliseconds since the mute. A zero tail can
also occur when the detector had already lost the low note.

## Candidate diagnosis and sharper low-band probe

Run the same command with `--candidates --mode sub-harmonic` in place of `--output` to
separate the raw candidate list, its minimum, the harmonic proposal and the stabilized
selection. Across the original 16 fixture runs, the correct lowest pitch was anywhere
in the raw candidate list on **9.3%** of settled dyad reports. It was the minimum every
time it appeared. Harmonic recovery raised the correct proposal rate to **14.8%** and
the stabilized selection rate to **12.3%**. The picker and harmonic evidence lose far
more notes than the final selection step.

`sub-low` looks for evidence of B0 through A♯1 without requiring an octave above to reach
the raw candidate list. It checks the fundamental and at least two of the first four
overtones, including an odd harmonic. `sub-low-smooth` gives that probe a sharper response
by adding a phase-compensated complex pole with a 16 ms time constant at the existing
2.90 ms report interval. Both retain the original verified selector when the probe
has insufficient evidence. With the extra pole, the correct proposal and stabilized
selection rates rose to **23.6%** and **19.0%** on the original dyads.

| Fixtures and mode | Correct solo | Correct dyad | Wrong dyad | Correct settled repick |
| --- | ---: | ---: | ---: | ---: |
| Original 16, harmonic verification | 52.9% | 14.5% | 77.5% | 12.7% |
| Original 16, low-band probe | 38.6% | 12.7% | 78.0% | 12.5% |
| Original 16, sharper low-band probe | 50.4% | 26.2% | 65.2% | 32.0% |
| New 12, harmonic verification | 56.7% | 16.2% | 75.5% | 16.2% |
| New 12, low-band probe | 32.7% | 15.2% | 76.0% | 15.6% |
| New 12, sharper low-band probe | 41.2% | 26.1% | 61.1% | 33.0% |

The new runs use the same six Karoryfer pairs and level settings, substituting round-robin 2
for both recorded notes and omitting the two Yamaha pairs. Reproduce using
`--mode sub-harmonic --mode sub-low --mode sub-low-smooth` and `rr2` file names.
These recordings are fresh plucks from the same instruments and sample session.

The sharper probe helped most when the upper note was quieter. On the original Yamaha
fingerstyle E1 fixture it raised correct dyad coverage from 19.3% to 77.2% at that level
ratio. On the equal-level fixture it raised it from 11.4% to 28.1% and moved the first
four-report lock from 24.7 to 74.1 ms. The longest tail in the original set grew from
45.2 to 56.8 ms. Several B0 fixtures stayed at or near zero correct under an equal-level
upper note. Isolated-note accuracy also fell on the new plucks.

Keep this host-only. The added pole updates 66 complex bands per report, about 22,700
band updates per second at 44.1 kHz. The prototype also stores an extra verification
state and repeats the phase calculation. AC791N load, firmware code size and pedal
response have yet to be measured. The next tracker test should use separate rules
for identifying a new low note and retaining an already identified one, with a release
test that detects when the low string goes quiet while the higher string is still ringing.

## Separate acquisition and hold

`sub-low-hold` confirms a low-band proposal twice before latching it. The latch can
survive without a new proposal while the fast bank still sees a fundamental and one
corroborating harmonic at the frequencies supported by the smoothed bank. A three-report
grace period bridges short losses of fast evidence. A persistently strong fresh proposal
can confirm the pitch again. This is an experiment in reducing note substitutions in
the sustained segment.

| Bass fixtures | Correct dyad, sharper probe → hold | Correct repick, sharper probe → hold | Longest post-mute tail, sharper probe → hold |
| --- | ---: | ---: | ---: |
| Original 16 | 26.2 → 31.5% | 32.0 → 39.3% | 56.8 → 56.8 ms |
| Round-robin 2, 12 | 26.1 → 33.9% | 33.0 → 38.0% | 39.4 → 42.3 ms |
| Round-robin 3, 12 | 19.9 → 27.2% | 26.5 → 32.4% | 59.7 → 88.8 ms |

On round-robin 3, settled solo correctness was **22.7%** with the hold rule and **63.7%**
with the original harmonic verification. Wrong pitch occupied **63.8%** of the
settled dyad window. Temporarily preventing the latch from reacquiring after its fast
evidence expired lowered correct dyad coverage on the original set from 31.5% to
24.1% while the longest tail stayed at 56.8 ms. The fast check still reads states
from the same 22.7 ms resonators. It can confuse residual or overlapping harmonics
with continuing excitation.

A guitar regression run used 24 separate GuitarSet comping takes at offset 24.
Correct lowest-note output was **70.4%** with harmonic verification, **67.6%**
with the sharper bass probe, and **67.4%** with its hold rule across 44,883
settled dyad frames. The probe can introduce false subharmonics even on recordings
where the instrument cannot play the proposed pitch.

Run the bass command with `--mode sub-harmonic --mode sub-low-smooth --mode sub-low-hold`
and use `rr3` files for the last set. For guitar, use
`python3 tools/sim/lowest_octave_eval.py /tmp/lowest_octave --takes 24 --offset 24`.
The hold variant stays host-only. The next change needs better independent
evidence of the lowest note and of its decay under another string. Extending
the hold interval further would preserve both valid and incorrect notes.

An isolated AC791N rebuild of the existing v51 resonator channel produced a
byte-identical 472,968-byte application and both byte-identical installer packages.
The target build used 23,344 of 24,576 bytes of low RAM and left 3,672 bytes in the
RAM code window. Those figures apply to the v51 baseline. The experimental
Lowest Sub modes were exercised on the host only.

## Harmonic fit and residual

`sub-fit` fits the strongest harmonic series, subtracts it from the window, and fits
a second series against the residual. It searches B0–C4 with three possible tunings
per note and up to eight harmonics. Analysis uses the last 176 samples at 2,756.25 Hz
(63.9 ms), updated every 256 input samples (5.80 ms). Each forward transform reuses
two of the engine's existing packed 128-point FFTs. Amplitude comes from each fit's
fundamental and its first two overtones. This version runs without the resonator bank.

`sub-fit-hybrid` keeps the verified resonator path and supplements it when the fit
finds a sufficiently strong fundamental below 60 Hz in two consecutive reports.
It can therefore play from the resonator during the earliest part of an attack.

| Round-robin 4, 12 runs per entry timing | Correct staggered dyad | Correct simultaneous dyad | Correct staggered repick | Longest staggered mute tail |
| --- | ---: | ---: | ---: | ---: |
| Harmonic verification | 22.1% | 15.1% | 12.4% | 39.4 ms |
| Sharper resonator plus hold | 37.1% | 30.9% | 32.0% | 53.9 ms |
| Harmonic fit | 63.5% | 84.4% | 68.5% | 48.2 ms |
| Harmonic fit below 60 Hz plus resonator | 62.4% | 77.3% | 64.6% | 48.1 ms |

The simultaneous benchmark starts both samples together and measures the same settled
window as the staggered runs. Different relative pluck alignment changes the mixture, so
the two rows of timing data should be kept separate. Round-robin 4 was a fresh set of plucks
from the same sample libraries.

On staggered bass the hybrid scored 92.7% when the upper note was quieter and 32.0%
at equal normalized peaks. The first correct emitted pitch appeared between 13.1
and 77.0 ms after the isolated pluck, according to oscillator state and gain.
On the same 24 GuitarSet takes used above, hybrid correctness was 69.3% versus
70.4% with harmonic verification. The fit by itself scored 27.2% in its restricted
range. The hybrid preserves much more of the guitar result, and still needs work
on equal-level low notes and on detection before the window has filled.

Use `rr4` files, `--mode sub-fit --mode sub-fit-hybrid`, and add `--simultaneous`
for the simultaneous runs. The C prototype passed address and undefined-behavior
sanitizers on those bass recordings. The packed transform matched a direct DFT
within 0.000005 on a deterministic two-tone test. Its new state is 2,532 bytes before
the oscillator, shared tables and stack. That alone exceeds the 1,232 bytes of
low RAM free in the baseline v51 build with the resonator. Memory placement, peak
pedal DSP load and audible latency remain unverified. The v51 installer does not
contain the harmonic fit.

## v52 Lowest Octave HQ target build

The opt-in `lowest-test` channel adds **LOWEST OCTAVE HQ** to PITCH. Its three
controls are DRY, OCT −1 and OCT +1. OCT −1 plays the synthesized lowest pitch.
OCT +1 uses the existing full-chord HQ shifter. POLY OCTAVE HQ and its RANGE
control remain available. The test channel adds a type at the end of the catalog
and keeps the preset layout. Use a temporary patch when comparing builds because
older firmware does not have this type.

The target schedules the two harmonic fits across the report interval. The
window and report spacing stay at 63.9 and 5.80 ms. Completing the staged
analysis takes an additional 176 input samples, or 3.99 ms, after the window
ends. Independently, a strong isolated-note fit can settle a semitone or octave
swap in the resonator's output. With firmware math and the actual PITCH path,
isolated synthetic tones from B0 to A4 produced the correct sub. The block and
scalar paths agreed bit for bit on those fixtures. Dry-only matched the
bypassed block and octave-up-only matched the existing full-range HQ octave-up.
Both ASan and UBSan passed for these tests and the round-robin 4 bass renders.

| v52 host benchmark | Correct settled output |
| --- | ---: |
| Bass, staggered, all round-robin 4 mixes | 61.5% |
| Bass, staggered, equal normalized peaks | 30.8% |
| Bass, staggered, upper note 7.6 dB quieter | 92.3% |
| Bass, simultaneous, all round-robin 4 mixes | 77.2% |
| GuitarSet, 24 comping takes (44,883 settled dyad frames) | 69.9% |

The longest correct-pitch tail after a staggered bass mute was 51.0 ms.
These are host results for the same detector and oscillator used in v52.
They include controlled mixes of recorded samples and omit pedal converter
latency, playback-system response, and measured AC791N callback load. The exact
sub of B0 is 15.4 Hz, which many instruments' playback rigs reproduce weakly.

Build with `OP_CHANNEL=lowest-test OP_TEST_VERSION=52 sh firmware/build.sh`.
The JieLi build produced a 477,540-byte application, 4,572 bytes above the v51
resonator build. It uses 23,344 of 24,576 bytes of low RAM, 88,264 of 98,304
bytes of general RAM, and leaves 976 bytes free in the RAM code window.
Both installation packages are 675,968 bytes and matched the saved v52
manifest at the time of testing.

For peak-load verification with an active guitar or bass input, first test
OCT −1 on its own and then repeat with OCT +1 enabled. The probe uses a
temporary patch with Smart Gate and restores the old patch afterward.

```sh
SOLO_SMART_GATE=1 TYPES='PITCH:LOWEST OCTAVE HQ@50/60/0/0/0,PITCH:LOWEST OCTAVE HQ@50/60/60/0/0' node tools/core2_probe.mjs 20
```

Compare a low note by itself, a second note entering at comparable volume, a
soft repick under the higher note, and a hard low-string mute while the higher
string rings. Record the dry and pedal returns to assess pitch changes and
audible onset. Hardware playback, peak callback load, and equal-volume bass
tracking remain open.

### BlackBox v52 smoke test

The USB MIDI updater finished and the pedal came back as `BlackBox_952`.
The temporary probe used Smart Gate and one Pitch effect at a time for eight
seconds each. The second core stayed active in all three runs, with zero late
blocks and no safety bypass. The probe then restored the previous sound.

| Effect | Combined callback peak | Pitch block reported |
| --- | ---: | ---: |
| Poly Octave HQ, OCT −1 | 35% | 27% |
| Lowest Octave HQ, OCT −1 | 34% | 16% |
| Lowest Octave HQ, OCT −1 and OCT +1 | 44% | 36% |

The input was uncontrolled and unrecorded. This verifies the package and basic
execution on the pedal. It does not establish peak load on repeatable active DI
or input-to-output latency.

## v53 candidate with independent partial hold

Once a strong isolated note below 60 Hz is confirmed, the candidate can retain
that pitch under a new upper note if at least two of its partials remain in
the residual, away from the upper note's harmonic series. Four missing fit
reports clear the incumbent, and a newly confirmed lower note can take its
place. The tighter initial confidence check prevents most false bass holds on
recorded guitar. The existing weak-note acquisition and resonator paths still
run before a hold has been established.

| Test | v52 | v53 candidate |
| --- | ---: | ---: |
| Equal-level staggered bass, correct lowest | 33.7% | 52.8% |
| Quieter upper bass, correct lowest | 91.1% | 92.1% |
| Equal-level settled repick, correct lowest | 36.3% | 42.9% |
| Quieter upper settled repick, correct lowest | 86.7% | 74.9% |
| Longest correct-pitch tail after a mute | 51.0 ms | 71.3 ms |
| GuitarSet lowest note, 44,883 settled frames | 69.9% (31,381) | 69.9% (31,374) |

The bass results pool 26 fixtures per level setting across the Yamaha and
four Karoryfer round robins. On six more equal-level mixes made with different
fingerstyle dynamics, the candidate moved held-note correctness from 40.5%
to 51.9%. The candidate's measured gain on equal-note sustain comes with a
longer mute tail and a repick regression under quieter upper notes.

The v53 JieLi target build linked and packaged both models. Its application is
478,316 bytes. The link leaves 440 bytes in the RAM code window, 9,944 bytes
in general RAM, and 1,232 bytes in low RAM. The host sweep covered 2,983
configurations with zero problems. The alternate-dynamics bass replay passed
under ASan and UBSan. The pedal smoke test above was for v52. v53 remains
a candidate pending faster low-string release and a replay on real hardware.

## v54 release and repick candidate

v54 keeps the independent partials used for sustain. A sharp drop in the
held note's level starts the oscillator fade when the strongest fit has moved
away from that note. A confirmed lower note can still take over the output.
Establishing a persistent hold requires the isolated-note confidence check.
This prevents a brief false low fit during a mute from surviving into a repick.

All three builds processed the same 104 PCM16 bass fixtures from the existing
Yamaha and Karoryfer recordings at 44.1 kHz. Each condition has 26 fixtures
and includes native E1 and B0. GuitarSet adds 24 comping takes at offset 24 and
eight isolated pitch excursions from the solo takes. The five-semitone-lower
guitar run used varispeed on the same DI and annotations. Physical down-tuned
guitar and live two-string bass remain unverified.

| Oscillator-state score | v52 | v53 | v54 |
| --- | ---: | ---: | ---: |
| Staggered equal-level sustain | 33.7% | 52.8% | 56.3% |
| Staggered quieter-upper sustain | 91.1% | 92.1% | 90.8% |
| Staggered equal-level soft repick | 36.3% | 42.9% | 38.7% |
| Staggered quieter-upper soft repick | 86.7% | 74.9% | 86.7% |
| Simultaneous equal-level sustain | 55.5% | 60.8% | 52.1% |
| Simultaneous quieter-upper sustain | 96.7% | 91.9% | 91.7% |
| GuitarSet settled dyads | 69.92% | 69.90% | 69.81% |
| GuitarSet down five semitones, settled dyads | 66.24% | 65.54% | 65.30% |
| Recorded guitar pitch excursions within 50 cents | 59.71% | 59.71% | 59.71% |

For the wet waveform, the benchmark measures each 128-sample interval at a
fixed −54 dBFS RMS threshold and uses the oscillator trace to label pitch.
A note within 50 cents is correct. Wrong duration counts sounding intervals
at the wrong pitch while an input note is expected, including upper-only
sections after individual mutes. Wrong level is the RMS during those intervals.
Onset searches the first 240 ms, and repick searches from 900 ms to the second
low mute. Medians exclude missed onsets. These figures include the rendered
oscillator and its envelope. They omit converters, speakers and the listener's
frequency-dependent hearing threshold.

| Median wet outcome across fixtures | v52 | v53 | v54 |
| --- | ---: | ---: | ---: |
| Isolated bass onset, both staggered groups | 40.7 ms | 40.7 ms | 40.7 ms |
| Staggered equal-level repick onset | 83.9 ms | 92.6 ms | 79.5 ms |
| Staggered quieter-upper repick onset | 65.0 ms | 66.5 ms | 62.1 ms |
| Simultaneous equal-level onset | 16.0 ms | 14.6 ms | 14.6 ms |
| Simultaneous quieter-upper onset | 36.3 ms | 36.3 ms | 36.3 ms |
| Staggered equal-level wrong duration and level | 612.4 ms, −32.0 dBFS | 454.2 ms, −33.5 dBFS | 438.3 ms, −32.9 dBFS |
| Staggered quieter-upper wrong duration and level | 233.7 ms, −35.9 dBFS | 255.4 ms, −36.3 dBFS | 229.3 ms, −36.2 dBFS |
| Simultaneous equal-level wrong duration and level | 728.5 ms, −31.3 dBFS | 552.9 ms, −33.5 dBFS | 656.0 ms, −33.4 dBFS |
| Simultaneous quieter-upper wrong duration and level | 223.5 ms, −35.6 dBFS | 233.7 ms, −36.7 dBFS | 226.4 ms, −36.2 dBFS |

v54 missed two initial attacks in the 26 simultaneous equal-level fixtures and
two repicks each in the simultaneous and staggered equal-level groups. The
longest staggered quieter-upper low-string tail by wet RMS was 45.2, 71.3 and
51.0 ms for v52, v53 and v54 respectively. In the equal-level staggered set the
longest low-string tails were 51.0, 65.5 and 65.5 ms. The longest complete mute
tails across all four groups were 45.7, 45.7 and 39.9 ms. The recorded pitch
excursions had a v54 median error of 17.3 cents and a 95th percentile of
205.4 cents on active frames. Quantised pitch, close intervals near B0 and weak
simultaneous attacks remain limitations. Use `--mode sub-fit-hybrid --waveform`
with the bass benchmark to reproduce the wet metrics for an individual build.

### v54 AC791N packages

The `lowest-test` channel builds v54 by default. Set `OP_TEST_VERSION=53`
with this channel to select the v53 anchor and release logic at compile time.
Use separate build directories to keep both package manifests. The local v53
fallback is in `build/lowest-v53/target/docs/firmware/lowest-test/`.

```sh
OP_CHANNEL=lowest-test sh firmware/build.sh
OP_CHANNEL=lowest-test OP_TEST_VERSION=53 sh firmware/build.sh
```

Both models use a 478,396-byte application, 80 bytes above v53. Each installation
package is 675,968 bytes. The candidate packages and a matching manifest are in
`build/lowest-v54/target/docs/firmware/lowest-test/`.

| Linker region | Used | Capacity | Free |
| --- | ---: | ---: | ---: |
| General RAM | 88,360 | 98,304 | 9,944 |
| Low RAM | 23,344 | 24,576 | 1,232 |
| DSP pool | 256,240 | 258,048 | 1,808 |
| RAM code window | 85,608 code + 48 data + 8 alignment | 86,016 | 352 |
| Cache RAM reserved for vector use | 0 | 28,672 | 28,672 |
| NOINIT | 448 | 15,696 | 15,248 |

| Package | SHA-256 |
| --- | --- |
| BlackBox_954.fwsc | `a8a18ef8af8d736647a43af22f6f7c799453f67ca3d5fb1238d3ece8a44b21d6` |
| PocketX_954.fwsc | `5186301c596d3d83bc743144b728f4462dfcd0e8a7bc46f1e819042157f59dd8` |

The engine sweep passed all 2,983 configurations. The Lowest Note replay
passed ASan and UBSan. Through the PITCH engine, dry-only matched bypass and
octave-up-only matched POLY OCTAVE HQ bit for bit on the replay. Editor,
installer and flasher tests passed. Clean HEAD also reproduces the F17
Noise Gate screen assertion that stops the host suite, and the NAM integer
overflow reported by the separate sanitizer sweep.

### Pedal replay

The 17.87-second `build/lowest-v54/lowest-note-replay.wav` contains E1 and B0
fixtures, staggered and simultaneous pairs, soft repicks, individual mutes,
the lowered guitar excerpt and two recorded guitar pitch excursions. SHA-256
`9db58dc3d12fe835a0d3429d2f10a2cf22fe54091ce5c3d6f5678658bfbf01d2`.

Replay at a fixed interface and reamp level into the guitar input. Record a
bypass reference and the wet return from the same source. USB playback feeds
the monitor mix after DSP. With `SOLO_SMART_GATE=0` the probe leaves only PITCH
active. With `SOLO_SMART_GATE=1` it adds Smart Gate for noise reduction. At
host levels, RANGE E3 with OCT −1 at 38 and LOWEST with OCT −1 at 60 matched
within 0.1 dB RMS on v54. Use dry at zero and verify the wet match on the pedal
before the load and listening comparison.

```sh
SOLO_SMART_GATE=0 TYPES='PITCH:POLY OCTAVE HQ@0/38/0/42/0,PITCH:LOWEST OCTAVE HQ@0/60/0/0/0,PITCH:LOWEST OCTAVE HQ@0/60/60/0/0' node tools/core2_probe.mjs 22
```

The read-only package check identified the connected BlackBox as v52 and verified
the v53 and v54 packages. Repeatable peak callback load, late blocks, bypass,
core status and measured pedal latency are pending routed playback.
