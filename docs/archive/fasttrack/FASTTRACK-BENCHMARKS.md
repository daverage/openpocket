# FastTrack benchmarks (host only)

All numbers: `tests/archive/fasttrack/` (harness notes in `docs/archive/fasttrack/FASTTRACK-BASELINE.md`). Algorithm version `base` = production `engine/px_mono.h` at synth3 a35ca9b, built `-fno-builtin -ffp-contract=off`.
Every set below is **development data** (nothing was reserved before experimentation). Latency is measured on the note-on the synth would receive, nothing back-dated.

## 1. IDMT-SMT-Guitar V2 added to the corpus
Source: Zenodo 7544110 (Fraunhofer IDMT; Kehling, Abesser, Dittmar, Schuller, DAFx 2014). **Licence CC BY-NC-ND 4.0**: attribution required, non-commercial, no derivatives. The zip and extracted audio stay in `build/datasets/idmt-guitar/` (git-ignored, never committed); `build/stage5/sets/idmt_*/` hold **symlinks to the unchanged WAVs** plus our own note CSVs extracted from the dataset's XML. Re-create with `python3 tests/archive/fasttrack/idmt_inventory.py`.

### Inventory
| subset | content | recordings | annotated onsets | signal path |
|---|---|---|---|---|
| dataset1 | single notes, chords (one pluck per file) | 400 | 796 | direct (Line-In) |
| dataset2 | 12 licks x 3 guitars x technique variants, plus 63 "init" files (fret scans, muted5, prototypes) | 252 (189 licks) | 4278 | direct, 24-bit mono |
| dataset3 | 5 short pieces (3 with polyphony) | 5 | 90 | direct |
| dataset4 | 64 pieces x slow/fast tempo, Career SG and Ibanez RG2820 | 236 annotated | 19341 (onsets only, no pitch) | direct |
| dataset4 acoustic_mic / acoustic_pickup | the same pieces on an acoustic | 256 wavs | **no onset annotations** | microphone / piezo: excluded |

Pitch, onset, offset, string, fret, plucking style (PK/FS/MU) and expression (NO/BE/SL/VI/HA/DN/ST/FL/TR) exist only for datasets 1-3. Dataset 4 has onsets, chords and mono/poly texture; it is scored on onsets only. The 12 licks repeat across three guitars and several playing styles, so the distinct musical content is small (12 licks, 5 pieces).

### How many genuinely fast, annotated passages exist
A run = at least 5 consecutive attacks (chord attacks within 40 ms collapsed to one), every inter-onset interval under 1/rate.
| material | runs >= 8 /s | runs >= 12.5 /s | runs >= 20 /s | fastest 5-note window |
|---|---|---|---|---|
| IDMT licks (pitch-annotated, 189 files) | **0** | 0 | 0 | 5.1 notes/s |
| IDMT pieces (pitch-annotated) | **0** | 0 | 0 | 2.6 notes/s |
| IDMT dataset 2 init files | 0 | 0 | 0 | 1.7 notes/s |
| IDMT dataset 4, fast tempo, direct (onsets only) | 4 runs, 22 notes, longest 6 | 0 | 0 | 12.5 notes/s |
| Guitar-TECHS P1-P3 (pitch-annotated, for comparison) | 16 runs, 100 notes, longest 11 | 11 runs, 63 notes, longest 7 | 0 | 21.2 notes/s (P3_music_07, _12) |

**IDMT contains no shred-speed playing.** Its licks are at most 5 notes/s; the only 8/s runs are four short ones in two dataset-4 `metal_8_125BPM` recordings and they carry no pitch labels. For fast runs the best labelled material remains Guitar-TECHS P3_music, whose runs of 8-12.5/s are short and whose fastest windows are about 21/s but not sustained. No annotated recording anywhere in the corpus sustains 20 notes/s. IDMT is useful for: string changes (Lick12, about 120 changes/min), repeated notes (Lick5), bends/vibrato (Lick5 `BVDN`), muted and dead notes (Lick10 `MN`, Lick12), harmonics, slides, a second player/guitars/room, and the genre pieces of dataset 4 for onset behaviour.

Ranked manifest: `build/fasttrack/idmt/manifest.csv` (columns for fast, string-change, repeat, bend and mute scores; `python3 tests/archive/fasttrack/idmt_bench.py manifest` prints the top lists). Top fast: `Career SG/fast/metal_8_125BPM`, `Ibanez 2820/fast/metal_8_125BPM`, `Ibanez 2820/fast/metal_4_180BPM` (12.5/s but only 5-note windows).

### Annotation reliability
Annotated licks onsets vs the audio's energy rise (999 notes, 2 ms RMS): median +20 ms, p10 -22 ms, p90 +32 ms. Absolute latencies against IDMT truth are therefore only good to about +-25 ms, which is why `base` shows about 60 ms median on licks against about 20 ms on Guitar-TECHS. Compare variants against each other on the same truth, not across datasets. Dataset 4 states that some onset/texture annotations are missing.

### Baseline: production mono tracker (`base`)
Command: `tests/archive/fasttrack/run_set.sh base idmt_licks; python3 tests/archive/fasttrack/idmt_bench.py pitched base` (also `idmt_pieces`; for dataset 4 `run_set.sh base idmt_d4; idmt_bench.py d4 base`).
| subset | groups | recall | latency med/p90/p99 ms | wrong_init | extra | spurious /min | wrong-pitch s/min |
|---|---|---|---|---|---|---|---|
| licks, all | 2657 | .825 | 61.6 / 95.5 / 276.8 | 268 | 182 | 7.8 | 12.3 |
| licks, mono-only (126 rec) | 1575 | **.950** | 60.0 / 73.3 / 103.3 | 73 | 88 | 6.3 | 7.9 |
| licks with chords (63 rec) | 1082 | .642 | 68.9 / 131.4 / 413.0 | 195 | 94 | 9.7 | 17.6 |
| licks with bends/vibrato | 627 | .919 | 60.1 / 76.5 / 106.2 | 48 | 32 | 7.7 | 11.7 |
| licks with muted/dead notes | 1093 | .850 | 61.5 / 82.5 / 135.6 | 150 | 126 | 15.0 | 12.4 |
| licks with slides | 474 | .943 | 59.5 / 74.3 / 102.8 | 28 | 21 | 7.1 | 11.6 |
| pieces, mono-only (2 rec) | 22 | 1.000 | 39.2 / 60.8 / 62.5 | 0 | 3 | 2.3 | 1.9 |
| pieces with chords (3 rec) | 32 | .594 | 56.2 / 75.4 / 335.0 | 10 | 0 | 2.0 | 19.6 |
| dataset 4 direct, onsets only | 19341 | .551 (fast <150 ms: .665) | 41.1 / 84.0 / 119.1 | n/a | 0.59 note-ons per annotated onset | n/a | n/a |

Reading: single-note lines are largely solved at 5 notes/s (recall .95). The losses are chords (a one-voice tracker; recall counts any pitch of a strum, so these are octave/harmonic and missed-attack errors), and muted/dead-note licks, which have the highest spurious-note rate (15/min). Dataset 4 recall is low because its annotations include dead-note control signals and palm-muted attacks that a gated tracker is meant to ignore; the file cannot say which misses are wrong, so it is a relative benchmark only (use it to compare variants, and by ear).

## 2. Mute-thump threshold (A2 experiment, overlay build, `mk*` variants)
Hypothesis: the wrapper rejects a real note after a damp when its first-6-ms peak is below 0.3x (-10.5 dB) the pre-mute peak (`px_mono.h`). Evidence: in `My Audio/bleep3 di.wav`, three sustained notes at 957, 1153 and 1362 ms are swallowed with `mute=1` latched from the previous decay. Result with `FT_MUTE_K` (production 0.3):
- bleep3 di (pseudo-reference from offline librosa/pYIN, not ground truth): note-ons 25 -> 28, matched picks 18 -> 21 (K 0.15), 22 (K 0.04).
- `fast play di`: no change (another mechanism).
- Guitar-TECHS (full corpus): recall .887 -> .887, wrong_init 310 -> 323 (K 0.15) / 315 (K 0.04), spurious/min 2.8 -> 2.9 / 3.1. No gain, small cost: **not adopted**; the mute rule is a cause of misses only on this kind of fast, damped playing, which the labelled corpora barely contain.
- Also tried, no gain on the two DI clips: a short-window YIN-style period replacing a partial candidate (`FT_YIN`), and a leaky stability vote (`FT_LEAKY`): pick matches 18 -> 19 on both.
Diagnosis worth keeping: after a pick the ERB estimator reports a wrong strongest note for 20-40 frames (466, 433, 151, 159, 775, 315, 150 Hz for a 150 Hz note) before settling, so the layer's 10-consecutive-frame stability rule takes ~80 ms or never completes before the repick window closes; the YIN-style period is valid only from frame ~20 because the pick transient is not periodic.

## Known limitations
- No sustained >=12.5 notes/s labelled material exists in this corpus; the fast-run claim cannot be tested at shred speed until new recordings (spec to come) are labelled.
- Annotation timing +-25 ms (IDMT), lag-fit +-10 ms (Guitar-TECHS).
- Recording-level stratification uses annotation tags; chord-containing licks are scored by "any pitch of the strum", which flatters a mono tracker.
