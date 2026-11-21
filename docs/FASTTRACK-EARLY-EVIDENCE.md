# FastTrack: what limits fast playing, onset evidence or early pitch evidence? (diagnostic, host only)

Branch `synth3-fasttrack`. Controller v0/v1 (`tests/fasttrack/ft_ctrl.h`) is frozen as an experimental baseline; **no rule was added** in this stage. `engine/`, patches, presets, firmware untouched; nothing pushed.
Data: Guitar-TECHS P1-P3 DI (9623 picks, 777 fast), **development data**. Truth = hexaphonic MIDI shifted by a per-take lag; the ground-truth stage measured +-25 ms as the working bound (pitch dependent: low notes annotated about 15 ms later than high ones). Oracle runs are **upper bounds, not implementable**.
Reproduce: `ft_ctrl_trace.c` with `FT_ORACLE_ONSET=<lag ms>` / `FT_ORACLE_PITCH=1`; `ft_early.c` + `early_analysis.py`, `early_rules.py`, `onset_study.py`.

## 1. Oracle experiments (same controller: leaky vote + one admission decision, level-rise pick evidence 1.3x)
A = actual onset + actual pitch; B = annotated onset time + actual pitch; C = actual onset + annotated pitch (admitted at the earliest-admission frame, the share/confirm gates bypassed); D = both annotated.
Event = tracker reports a note at that pitch; attack = the neutral oscillator's envelope restarts at that pitch. False attacks per minute are wrong-initial / spurious. Earliest admission 20 frames (29 ms), then 8 frames.

| config (min 20 frames) | event all / fast | attack all / fast | att. latency med | false /min wrong-init, spurious |
|---|---|---|---|---|
| A actual / actual | 82.7 / 57.0 % | 79.5 / 51.2 % | 20.2 ms | 2.8, 8.7 |
| B oracle onset | 89.3 / 77.0 % | 87.1 / **73.6 %** | 28.3 | 7.4, 0.0 |
| C oracle pitch | 85.2 / 58.0 % | 81.6 / **52.0 %** | 20.4 | 0.0, 13.1 |
| D both oracle | 99.8 / 99.9 % | 99.8 / 99.9 % | 28.3 (p90 28.9) | 0, 0 |

| config (min 8 frames) | event all / fast | attack all / fast | att. latency med | false /min |
|---|---|---|---|---|
| A | 74.4 / 52.4 % | 69.0 / 45.0 % | 4.3 | 7.4, 15.1 |
| B | 88.3 / 75.4 % | 85.2 / 71.4 % | 11.1 | 9.3, 0.0 |
| C | 76.3 / 48.0 % | 59.6 / 38.2 % | 4.4 | 0.0, 31.6 |
| D | 99.9 / 99.9 % | 99.9 / 99.9 % | 10.9 | 0, 0 |
(D's latency is the annotated onset plus the earliest-admission frames, 28.3 = 29 ms: the plumbing checks out. Annotation lag sensitivity of B: shifting the oracle onset +10 / +20 ms leaves fast attacks at 71.6 / 70.3 %.)

By material (min 20; fast-pick attacks A / B / C / D): **clean scales** 97.6 / 100 / 95.1 / 100 %; **music (chords and mixed lines)** 9.8 / 43.1 / 15.0 / 100 %; **bends and vibrato** 2.0 / 86.3 / 2.0 / 98.0 %; palm mute has no fast picks.

### What this establishes
1. **Fast-pick recall is limited by onset evidence, not by pitch.** Giving the controller the true pitch changes fast attacks by +0.8 points (51.2 -> 52.0); giving it the true onset changes them by +22.4 points (-> 73.6). In the bend/vibrato takes fast notes are found 2 % of the time and 86 % with the true onset: the flux detector simply does not fire for them.
2. **False spurious notes are an onset problem too:** they disappear (8.7 -> 0.0) with the true onset, and get worse (8.7 -> 13.1) when the pitch gate is bypassed, which shows the pitch-confirmation gate was silently filtering false picks as well as wrong pitches.
3. **Wrong initial pitches are a pitch problem:** oracle pitch brings them to 0.0; oracle onset leaves them at 7.4.
4. With both, the pipeline is near perfect (99.8 %), so nothing else in the controller is losing notes. The 10-18 point remaining gaps in B and C are the other input.
5. A 20-frame admission floor is a pitch-evidence workaround, and the early-pitch study below shows how much of it is needed.
6. Caveat: on chord-containing music the "annotated pitch" is the group pitch nearest the estimator's candidate, which is generous; the music-set pitch ceiling (D 83 % pitch right) shows continuous following drifts between chord tones. Hammer-ons and slides are labelled as notes in the hexaphonic MIDI but have no pick; the bend/vibrato "fast" notes may be largely such legato notes, so a missing onset there is not necessarily an error. The data cannot separate the two.

## 2. Early pitch evidence (9256 single-note groups, first 60 ms after the annotated onset)
| stratum (previous attack) | correct pitch is the tracker's strongest (held 3 frames) | in the candidate set at all | waveform repeats at the true period (d<0.2) | wrong strongest candidate seen in the first 25 ms | legacy found / controller A found |
|---|---|---|---|---|---|
| isolated (>= 300 ms), n=7991 | **96.4 %**, median -0.2 ms, p90 7.2 | 97.9 % | 91.3 %, at 12.3 / 16.5 ms | 43.7 % of groups | 86.5 % at 20.6 / 32.3 ms; 88.0 % at 20.5 / 23.8 |
| fast (< 150 ms), n=678 | 80.4 %, -1.6 / 5.1 | 87.2 % | 77.3 %, 13.3 / 17.0 | 44.7 % | 58.8 %; 59.3 % |
| medium (150-300 ms), n=587 | 56.0 % | 70.9 % | 56.9 % | 78.0 % | 32.2 %; 35.3 % |

- **The correct fundamental is available almost immediately** (within the annotation uncertainty) for 96 % of isolated notes and is the strongest candidate before the 20-frame admission floor. It is in the candidate set for 98 %. Admission comes **about 19 ms after it is already the strongest** (controller 18.9 ms median, legacy 18.8 ms): that interval is the wait, not estimator absence. The first-60-ms "wrong strongest" frames are the hazard that the wait protects against: 44 % of groups show at least one wrong frame in the first 25 ms.
- The wrong early candidates are mostly **not upper partials**: 45 % are below the true pitch (sub-octave or lower), 40 % non-harmonic, 13 % x2, 2 % x3.
- **Controller A missed 1611 single-note groups**: the correct pitch was the strongest candidate at some point in 959 (60 %: available but not admitted or no pick evidence), only in the candidate set in 237, and absent from the estimator in 415 (26 %). So the two failure types the brief asked to separate both exist, but the available-but-not-admitted one is larger.

### Causal early-admission rules on that evidence (onset assumed known, pitch evidence only; nothing waits a fixed time)
Right / wrong (wrong initial pitch) / none, latency of the right ones (ms after the annotated onset):
| rule | isolated | fast |
|---|---|---|
| legacy-like: 10 agreeing frames and not before 20 ms | 92.7 / 5.0 / 2.3 %, 20.8 ms | 73.7 / 18.7 / 7.5 %, 20.8 |
| agree 3 frames | 74.5 / 25.4 / 0.0 %, 1.6 | 67.6 / 32.4 / 0.0 %, 1.2 |
| agree 5 frames | 84.7 / 15.2 / 0.1 %, 5.1 | 73.0 / 27.0 / 0.0 %, 3.8 |
| **agree 3 + waveform repeats at the candidate (d<0.2)** | 86.5 / **7.5** / 5.9 %, **12.8** | 72.0 / 22.6 / 5.5 %, 13.7 |
| agree 5 + d<0.1 | 86.1 / 5.5 / 8.5 %, 13.1 | 70.5 / 16.5 / 13.0 %, 13.7 |
| agree 8 + d<0.2 | 87.3 / 4.8 / 7.9 %, 13.7 | 71.7 / 17.4 / 10.9 %, 14.1 |
- Waveform evidence buys about **8 ms** (20.8 -> 12.8) for **+2.5 points of wrong initial pitches** on isolated notes (5.0 -> 7.5) with fewer correct (92.7 -> 86.5 %, the rest "none"). It is not free, and it does not make the early pitch trustworthy: the wrong admits are consistent and periodic.
- What they are (603 wrong admits, isolated): only 15 % are the previous note's pitch; ratios to the true pitch are octave-below 10 %, twelfth-below 8 %, two-octaves-below 5 % and **73 % non-harmonic, dominated by a semitone below (138) and above (39)**, then -12, -19, -24. A semitone error is 100 cents; this is not partial confusion. Likely sources (not isolated here): the annotated note differs from what the string sounds (bends and slides: 140 of the 511 are in the technique takes, the scales set still contributes 193), or the pitch of the attack transient. The cause remains open and needs a recording with known intonation.
- 5 ms / 10 ms / 15 ms later evidence start (to model annotation lag) leaves the wrong rate within 6.2-6.5 %: the errors are not the previous note leaking in.
- I did **not** run an independent YIN/MPM, sevagh or aubio comparison: the `d<` rules are a YIN-style normalised difference on the tracker's own 2.76 kHz history, and its results above indicate that the wrong admits are not rejected by periodicity alone. Whether a full-rate YIN/MPM on the first 10-20 ms changes the semitone errors is the first experiment worth running next.

## 3. Onset evidence (18579 flux picks)
**Correction (see `FASTTRACK-GROUND-TRUTH.md`): "false" below means "not matched to an annotated onset in [-15, +45] ms". Reclassified, 26.5 % of all picks are precursors before a pluck, 13.3 % are below the level gate, 8.7 % are genuine picks with an offset annotation and only a small share are decay-type events; the annotation is also incomplete on technique takes.** Against the annotations 8315 picks (45 %) are matched and **10264 (55 %) are unmatched**: no-note 4266, note-end 3951, in-note 2047; palm-mute, bend, vibrato and pinch-harmonic takes hold 3415 of the false ones (techniques are annotated as notes, so some "false" picks are real unannotated events; the annotation cannot say). Weak-flag picks are 75 % false, strong-flag 42 %.
AUC (a genuine pick scores above a false pick) per single feature: flux strength **0.83**, **level rise over the previous 8 frames 0.85**, waveform non-repeat 0.70, flux over the running average 0.61, level over the 150 ms peak 0.56, tracker confidence 0.43. Among picks on a still-ringing string (genuine repicks vs note-end/in-note false picks): level over the 150 ms peak 0.73, flux strength 0.73, level rise 0.69.
All features together (logistic regression, 5-fold by take): **AUC 0.88**; at 5 / 10 / 20 % of false picks let through, 45 / 68 / 84 % of genuine picks are kept; on ringing strings AUC 0.77. This is a ceiling for these features with a linear combination: **no combination of the current evidence separates picks cleanly**, which is why every earlier rule either loses real notes or lets false ones through. A different kind of evidence is needed (for example per-band energy rise at the new pitch rather than broadband flux, or the periodicity of the new energy), not another threshold. I did not test those.

## 4. Not done in this stage (honest list)
- Mute-to-next-note recovery: there is no labelled hard-mute-then-pick recording; `My Audio/` clips have no truth. Needs controlled recordings (specification still to be written).
- Rejected/silently expired candidate tracing beyond the controller's 18 rejections and the legacy `lost_layer` bin.
- Independent YIN/MPM, ERB-phase consistency, restricted competing-fundamental hypotheses, attack-to-sustain consistency.
- Labelled separation of picked vs legato notes (Guitar-TECHS has no such label).

## 5. Where the next improvement belongs
**D, with separate responsibilities, onset first.**
1. **Onset detection (A)** is the dominant limit on fast-pick recall (+22 points with the true onset vs +0.8 with the true pitch) and on false notes (spurious 8.7 -> 0). The present flux detector fires on many events that are not note starts (the ground-truth stage shows most of them are precursors and below-gate noise, not decays) and combinations of its own features top out at AUC 0.88. Work here: new per-band, pitch-aware evidence (does energy appear at a new fundamental, rather than "something changed"), and labelled picked-vs-legato data to score it fairly.
2. **Early fundamental (B)** is the dominant limit on wrong initial pitches and on latency (about 19 ms of the 20 ms median is waiting for evidence that is already there). Work here: understand the semitone-offset errors before changing admission; then test whether a full-rate YIN/MPM over 10-15 ms beats the tracker's 2.76 kHz history.
3. **Controller admission (C)** is not limiting: with both oracles the same controller reaches 99.8 %. Keep it frozen until 1 and 2 supply better evidence.
