# FastTrack baseline: production mono tracker, latency attribution and ablations

Branch `synth3-fasttrack`, base `synth3` a35ca9b (research checkpoint 15d74b6). Host only; no firmware touched.
Status: Phase 1 baseline and single-knob ablations. Candidates A/B/C and the external-source review are NOT done yet.

## Infrastructure (`tests/archive/fasttrack/`)
- `ft_trace.c`: runs the real `engine/px_mono.h` tracker (pedal numerics: `-fno-builtin -ffp-contract=off`) and logs every stage:
  R pick begins, H first pitch hypothesis, S pitch stable, U layer note-on + what the wrapper did with it (spoken / slur / held by confirm / dead), C confirm release, N synth note-on, L slur, X release; plus a pitch/gate trace every 64 samples.
- `ft_score.py`: scores on note groups (onsets within 40 ms = one event). Found = voice reaches a pitch of the group within 50 cents, held 3 trace samples; nothing is back-dated. Reports recall, latency median/p90/p99, fast subset (previous onset < 150 ms), wrong-initial note-ons, spurious note-ons per minute, extra (fragment/retrigger) note-ons, audible wrong-pitch seconds per minute.
- `build.sh`, `run_set.sh`, `one.sh`, `ablate.sh`: build variants with `-DPX_MONO_*`, run all takes in parallel, score. Reproduce: `tests/archive/fasttrack/ablate.sh` (about 3 min).

## Data and limits
Guitar-TECHS DI, `build/stage5/sets/gtechs` (P1-P3: scales, single notes, techniques, music; 48 takes, 149 min, 9623 groups). P1-P3 are **development evidence, not a holdout**. Truth is hexaphonic MIDI shifted by a per-take lag fitted to the DI, so latency carries a reference error of roughly +-10 ms (some fast-subset medians are negative for that reason). The "music" take set contains chords and simultaneous notes that a one-voice tracker cannot score as recall; scales are slow (about 0.5 s a note), so genuinely fast single-note runs are scarce in this corpus (777 fast groups, mostly in music and bends). Real fast-run recordings with labels are still needed (see recording spec to come). Not yet run: GuitarSet, `My Audio/` recordings (no ground truth), listening renders.

## Baseline (production defaults, `base`)
| stratum | groups | recall | latency med/p90/p99 ms | wrong_init | spur/min | wrong s/min |
|---|---|---|---|---|---|---|
| all | 9623 | 0.887 | 20.2 / 33.4 / 166.5 | 310 | 2.8 | 6.9 |
| scales | 6837 | 0.979 | 20.2 / 28.5 / 42.8 | 55 | 2.0 | 3.2 |
| music | 1479 | 0.530 | 23.0 / 114.8 / 298.3 | 187 | 1.8 | 19.1 |
| single notes | 273 | 0.989 | 19.4 / 31.8 / 62.5 | 2 | 1.1 | 1.0 |
| bend+vibrato | 518 | 0.772 | 16.3 / 32.9 / 74.7 | 24 | 5.0 | 7.9 |
| palm mute | 271 | 0.926 | 17.2 / 26.4 / 53.4 | 15 | 1.9 | 3.5 |
| harmonics | 245 | 0.580 | 53.8 / 450.8 / 571.6 | 27 | 5.9 | 23.4 |

On this corpus the production tracker already meets the research latency targets for ordinary notes (median about 20 ms, p90 about 33 ms), within the reference uncertainty. The weakness is elsewhere: fast-subset recall is 0.725 and music recall 0.53.

## Where the delay comes from (observed in the event logs)
The estimator's first hypothesis `H` is frequently an upper partial (e.g. 700, 490, 572, 656, 703 Hz for notes of 124-220 Hz); the correct fundamental arrives 25-50 ms after the pick. The wait (PX_MONO_MINWAIT 20 frames = 29 ms for notes over 200 Hz, WAIT_LO 14 = 20 ms below) exists to hide this. So the latency floor is set by estimator acquisition, and the safeguards are compensating for it. Reducing acquisition error at the source (a fast independent period estimate such as YIN/MPM on the first 10-20 ms) is the only way to cut latency without adding wrong notes.

## Single-knob ablations (each row changes one setting; all on the full corpus)
| variant | recall | miss | lat med/p90 | fast recall | wrong_init | extra | spur/min |
|---|---|---|---|---|---|---|---|
| base | .887 | 1083 | 20.2 / 33.4 | .725 | 310 | 559 | 2.8 |
| MINWAIT 20->5 | .887 | 1086 | 14.4 / 28.4 | .723 | 414 | 579 | 2.9 |
| WAIT_LO 14->0 | .888 | 1082 | 20.2 / 33.5 | .723 | 311 | 572 | 2.8 |
| STAB 10->2 | .888 | 1077 | 19.2 / 24.1 | .722 | 607 | 591 | 5.6 |
| CONFIRM_MS 35->0 | .888 | 1081 | 20.1 / 32.7 | .725 | 313 | 558 | 2.9 |
| DEAD off | .889 | 1067 | 20.2 / 33.4 | .726 | 366 | 562 | 3.0 |
| SLUR off | .890 | 1057 | 20.2 / 33.4 | .728 | 324 | 780 | 3.7 |
| HOLD off | .904 | 922 | 19.9 / 35.2 | .757 | 311 | 978 | 2.8 |
| MINWAIT 5 + WAIT_LO 0 | .887 | 1087 | 14.0 / 28.2 | .722 | 438 | 606 | 2.9 |
| ... + STAB 2 | .858 | 1363 | 1.6 / 15.6 | .650 | 1259 | 706 | 16.5 |
| all fix passes off (v73-like) | .850 | 1439 | 1.3 / 14.6 | .678 | 1415 | 1904 | 23.6 |

Reading (answers to brief section 2, on this corpus only):
1. **Rules that suppress genuine errors:** STAB (wrong starts 607 -> 310, spurious 5.6 -> 2.8/min, for +1 ms median, +9 ms p90), SLUR (extra note-ons 780 -> 559), DEAD (wrong starts 366 -> 310). MINWAIT is worth about 100 wrong starts for 6 ms.
2. **Rules that delay correct notes:** MINWAIT (6 ms median) and STAB (9 ms p90). Both are doing real work; removing them just converts delay into wrong notes.
3. **Rules that cause missed notes:** HOLD. Without it recall rises .887 -> .904 (fast .725 -> .757, 161 fewer misses), at the cost of 419 more extra note-ons. HOLD trades missed fast notes for fewer false retriggers; a better decay-vs-repick discriminator is a candidate target.
4. **Redundant work:** CONFIRM_MS (35 ms hold for harmonic-ratio starts) and WAIT_LO change almost nothing here (<=0.1 point on every metric). Both are candidates for removal or replacement, to be checked on bleep-type recordings (`My Audio/bleep.wav`) before deciding, since the corpus may not contain the bleeps they were added for.
5. Estimator vs note layer: the wrong first hypothesis is an estimator artefact; the note layer hides it with delay. The remaining recall loss in "music" (chords, octave errors 293 of 319) is the estimator/mono limitation, not a latency rule.

Negative result: all delays removed gives about 1 ms latency but 4x the wrong starts, 6x spurious notes per minute, and lower recall. Removing delays is not an improvement.

## Next
1. Review external sources (SoundShed, Cycfi Q, sevagh, aubio, YIN, MPM, Marock) from actual code; write `FASTTRACK-RESEARCH.md`.
2. Candidate A: smallest changes to HOLD and MINWAIT behaviour; Candidate B: short-window YIN/MPM check at the pick to replace the fixed wait (target: keep wrong_init at or below 310 while cutting the 29 ms wait).
3. Listening renders with a simple synth driven by the N/L/X events, and the recording specification for real fast runs.
