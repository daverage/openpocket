# PolyTrack X — host benchmark (branch `synth3`)

Status: **host prototype on synthetic guitar only. No pedal measurement, no real-guitar ground truth, no literature review has been done.**
Phase 1 (literature/licence review) of `POLY-TRACKING-EXPERIMENT.md` was not performed; the three candidate families below come from the brief's own research areas A–C, not from a source review. Reproduce with `tests/archive/polytrack/README.md`.

## What was built

| Id | Method | Brief area |
|----|--------|-----------|
| `hs` | Harmonic summation salience over semitone grid, peak picking (baseline 4) | A |
| `klap` | Iterative pick-best / subtract-harmonics (Klapuri-style) (baseline 5) | B |
| `nnls` | One joint non-negative sparse fit of the 4096-sample spectrum by harmonic templates, per frame | C |
| `pt` | **Candidate.** Notes are *born* only when the joint fit of the **difference spectrum** (new energy) explains them, then *tracked* by a restricted joint fit over live notes only; every track must pass a **leave-one-out test** (removing it must worsen the spectral residual by ≥3 %) and an amplitude test, 2-frame confirm, 4-frame release. Each note has 3 harmonic-slope atoms (profile adaptation). | C + E |
| `yin` | Mono YIN, stand-in for the existing mono tracker (the real one in `engine/` was not run) | D |

All causal, window ends at the frame time, hop 256 samples (5.8 ms), 44.1 kHz.

## Protocol

* Synthetic electric-guitar DI (`synth.py`): additive partials with string inharmonicity, pluck-position comb, per-partial decay, pick click, pickup low-pass, noise at −70 dBFS. Exact note on/off/bend ground truth.
* Tuning split: instruments A,B, seeds 0–2, 8 scenarios. Test split: instruments **C,D** (different B, pluck position, decay, pickup), seeds 100–102, 15 scenarios. 6 takes per scenario.
* Parameters and the acceptance criteria were written to `protocol.json` **before** the test split was run; the test run was done once.
* Framewise match: ±50 cents. The first 100 ms after an onset and 60 ms after an offset are not scored framewise (covered by latency). "Octave" and "harmonic" count false positives that sit 12/24 or 12/19/24/28/31 semitones from a sounding note.
* Latency = first frame in which the correct semitone appears, minus true onset (includes the 4096 window and the detectors' debounce, nothing downstream).

## Held-out results (instruments C,D; framewise F1, with octave+harmonic false-positive frame counts)

| scenario | hs | klap | nnls | **pt** | pt oct+harm FP | pt latency med/p90 ms |
|---|---|---|---|---|---|---|
| mono | 0.98 | 0.94 | 0.86 | 0.93 | 106 | 36 / 62 |
| 2 notes | 0.92 | 0.97 | 0.93 | 0.97 | 2 | 51 / 59 |
| power chord | 0.77 | 0.78 | 0.83 | 0.84 | – | – |
| 3 notes | 0.80 | 0.91 | 0.94 | **0.97** | 64 | 53 / 84 |
| 4 notes | 0.69 | 0.81 | 0.89 | **0.98** | 55 | 66 / 82 |
| 5 notes | 0.55 | 0.72 | 0.82 | 0.78 | 1302 | 57 / 80 |
| 6 notes | 0.64 | 0.71 | 0.85 | 0.79 | 674 | 52 / 82 |
| arpeggio | 0.73 | 0.89 | 0.89 | 0.93 | – | – |
| note added to chord | 0.66 | 0.85 | 0.84 | 0.86 | 75 | 47 / 67 |
| note released | 0.73 | 0.80 | 0.87 | 0.89 | – | – |
| bend while others ring | 0.80 | 0.88 | 0.90 | 0.85 | 14 | 51 / 67 |
| hammer-on/pull-off | 0.98 | 0.82 | 0.84 | 0.78 | – | – |
| palm mute | 0.71 | 0.85 | 0.81 | 0.93 | – | – |
| quiet (−36 dB) | 0.78 | 0.90 | 0.94 | 0.89 | 449 | 53 / 87 |
| bass, 2 notes | 0.64 | 0.82 | 0.67 | 0.80 | 912 | 69 / 71 |

`yin` (mono only) scored 0.85 on mono and ≈0 on every polyphonic scenario, as expected. Full per-scenario numbers (precision, recall, event P/R, retriggers) are in `tests/archive/polytrack/results.json`.

### Pre-registered acceptance check

| criterion (from `protocol.json`) | result | |
|---|---|---|
| poly3 and poly4 beat best baseline by ≥ 0.03 F1 | 0.97 vs 0.94 (nnls), 0.98 vs 0.89 | **pass** (poly3 margin is 0.03, 6 takes per scenario, so it is not established beyond noise) |
| fewer octave+harmonic FP than baselines on poly3/poly4 | 64 / 55 vs 142 / 619 (best baseline) | pass |
| mono F1 ≥ 0.95 | 0.93 | **fail** |
| chord latency median < 30 ms, p90 < 50 ms | 53–66 / 82–84 ms | **fail** |

## Ablations (held-out, PT with frozen parameters)

| variant | mono | 2 | 3 | 4 | add note | arpeggio |
|---|---|---|---|---|---|---|
| full `pt` | 0.93 | 0.97 | 0.97 | 0.98 | 0.86 | 0.93 |
| no leave-one-out test | 0.58 | 0.58 | 0.60 | 0.54 | 0.66 | 0.56 |
| no onset gate (fit whole spectrum for births) | 0.92 | 0.97 | 0.96 | 0.97 | 0.84 | 0.93 |
| single harmonic profile (no adaptation) | 0.96 | 0.98 | 0.98 | 0.97 | 0.89 | 0.92 |
| + fast 1024 window stacked | 0.73 | 0.97 | 0.87 | 0.78 | 0.78 | 0.58 |

## What is supported, what is not

Supported by these runs (synthetic, 6 takes/scenario):
1. A joint fit validated by **leave-one-out residual gain** is what removes harmonic/octave false notes; without it every method of this family collapses (F1 ≈ 0.55). The simpler iterative canceller needs far less compute but loses 3–4 note accuracy (0.81 at 4 notes).
2. Onset-gated birth and tracked identity give a modest gain on 3–4 notes and fewer false retriggers (PT 0–5 vs klap 1–64), but the onset gate on its own was within noise (0.97 vs 0.96).
3. 5–6 note chords are **not** reliably recovered by `pt`; the ungated joint fit (`nnls`) is better there (0.82/0.85).

Not supported / failed:
* Latency target. ≈50–65 ms median is dominated by the 4096-sample window plus the 2-frame confirm. The stacked fast window made things worse as built, so the multi-resolution advantage in the brief is **not demonstrated**.
* Profile adaptation did not help; the synthetic notes are close to the template family by construction, so this says little about real guitars.
* Mono is slightly below the plain harmonic-summation baseline.
* Event-level precision is poor for every method (0.1–0.3): attack frames produce short wrong-pitch events that the framewise metric forgives. A downstream voice allocator would see these.

Limits of the evidence: synthetic strings share the template family with the detector, so absolute scores are optimistic; real DI (`realdi.py`) shows clearly different behaviour in the sustained-chord section of `My Audio/scale + duo + power chords di.wav` (PT and Klapuri disagree on the final open chord, and neither is verified without ground truth). The pitch error column is ≈0 only because detectors report integer semitones; continuous frequency refinement, bend following and string hints are **not implemented**. The 0.6× real-time figure is numpy on a laptop, not pedal time.

## Verdict (Phase 6 view, provisional)

Not ready for firmware. Promising for 3–4 note accuracy on clean synthetic input; fails latency and mono criteria; 5–6 notes unsolved. Required before any embedded work: real labelled guitar recordings (GuitarSet-style, BlackBox-path captures), a latency-first redesign (shorter/growing windows from onset), continuous pitch and bend tracking, and an operation-count estimate for the AC791N (NNLS over ~150 atoms per hop is almost certainly too heavy per core without restructuring; this was **not measured**).

## Addendum: against the production mono tracker (`engine/px_mono.h`)

`tests/archive/polytrack/mono_cli.c` runs the real tracker (built to `build/polytrack_mono`; `cc -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/archive/polytrack/mono_cli.c firmware/px_math.c -lm -o build/polytrack_mono`), `vs_mono.py` compares on the same held-out takes (instruments C,D; no retuning). Same synthetic-data caveats. `hyb` = mono note plus any PT note that is not the mono note. Framewise F1 / precision / recall, onset latency median ms:

| scenario | mono (prod) | pt | hyb |
|---|---|---|---|
| single note | **0.99** / 0.99 / 1.00, 30 ms | 0.93 / 0.87 / 1.00, 36 ms | 0.93 |
| hammer-on / pull-off | **0.98**, 28 ms | 0.78 | 0.77 |
| 2 notes | 0.37 / 0.55 / 0.28, 80 ms | **0.97** / 0.95 / 1.00, 51 ms | 0.87 |
| 3 notes | 0.39 / 0.77 / 0.26 | **0.97** | 0.90 |
| 4 notes | 0.29 | **0.98** | 0.91 |
| note added to chord | 0.41 | **0.86** | 0.79 |
| arpeggio | 0.25 | **0.93** | 0.84 |
| bend while others ring | 0.25 | **0.85** | 0.81 |
| palm-muted chord | 0.42 | **0.93** | 0.86 |
| quiet (−36 dB) chord | 0.40 | **0.89** | 0.87 |

Conclusions (supported on synthetic data only):
* On a single note the production tracker is clearly better than `pt` (0.99 vs 0.93, and 0.98 vs 0.78 on hammer-ons). `pt` must not replace or override it.
* On two or three notes the mono tracker reports one pitch, which is often not a note actually played in the chord (precision 0.55 at two notes), with recall 0.26–0.28. `pt` finds them (F1 0.97).
* The naive union (`hyb`) is worse than `pt` alone, because the mono tracker's wrong chord pitches are kept. A real hybrid has to decide, per frame, who is in charge (e.g. mono when `pt` sees one note, `pt` when it sees two or more) and was not built.
* Latency: `pt` is about 20–35 ms slower than the mono tracker on chords; it is the same order as the mono tracker's own median on these takes (30 ms), but not shown to be fast enough for a playable voice.

## Addendum 2: real electric-guitar DI (Guitar-TECHS, `tests/archive/polytrack/gtechs_eval.py`)

Real DI with hexaphonic-MIDI ground truth (`build/stage5/sets`, from `tools/research/prep_sets.py`; the MIDI lag is estimated and removed there, so timing and note-edge scores carry that uncertainty). 4 chord takes (mean 2.8 notes sounding) and 4 scale / single-note / music takes (mean 1.2), 30 s excerpt each, **no tuning on these**, `pt` and `klap` use the frozen `protocol.json` parameters. Same framewise metric as above (±50 cents), so figures are not comparable with the mono tracker's own benchmark in `docs/research/synth2/SYNTH2-TRACKER.md` (89.5 % notes right, different metric).

| set | tracker | F1 | P | R | onset latency med / p90 |
|---|---|---|---|---|---|
| chords | mono (prod) | 0.26 | 0.54 | 0.17 | 24 / 151 ms |
| chords | klap | 0.59 | 0.92 | 0.44 | 32 / 98 |
| chords | **pt** | **0.88** | 0.89 | 0.88 | 41 / 109 |
| scales, single notes, music | mono (prod) | 0.64 | 0.85 | 0.51 | 21 / 89 |
| scales, single notes, music | klap | 0.72 | 0.79 | 0.65 | 14 / 69 |
| scales, single notes, music | pt | 0.69 | 0.61 | 0.80 | 51 / 130 |

* On real chords `pt` clearly beats both the mono tracker and the Klapuri baseline (0.88 against 0.26 and 0.59). This is the first real-recording support for the synthetic result.
* On fast single-note material `pt` finds more notes (recall 0.80) but at much lower precision (0.61, mostly octave/harmonic false notes), and it is the slowest. It is not better than the mono tracker there. Together with `bleep3 di.wav` this settles that `pt` must be limited to the chord case.
* Eight takes only; takes within a player are not independent. Treat as indicative.

## Addendum 3: MONO/CHORD state machine (`tests/archive/polytrack/switch.py`, `cache_real.py`)

One tracker is authoritative per frame (no union of note lists): the production mono tracker's note in MONO; `pt`'s notes in CHORD. Enter CHORD when ≥2 `pt` notes have each been continuously present for `age` frames (5.8 ms each) for 6 more frames; leave when fewer than 2 are stable for 8 frames. Parameters picked on DEV (Guitar-TECHS player P1, 46 takes) by an objective of F1 minus 2× false-CHORD rate, then scored once on TEST (players P2, P3: 9 chord takes, 22 scale/music/single/technique takes, 30 s excerpts). The objective ignored latency, which is why it chose the slowest setting; the faster settings are reported too (chosen by hand from the DEV sweep, not frozen beforehand). A mono-pitch-change "busy" block was tried and did not help.

Framewise F1 (precision/recall), TEST:

| set | mono (prod) | `pt` alone | switch age 90 | switch age 60 | switch age 40 |
|---|---|---|---|---|---|
| chords | 0.28 | **0.91** | 0.84 | 0.87 | 0.89 |
| scales / music / singles / techniques | 0.77 | 0.73 | **0.83** | 0.82 | 0.81 |
| false CHORD on frames with ≤1 true note (scales etc.) | – | – | 1.1 % | 6.9 % | 14.3 % |
| CHORD entry delay after 2nd note, median / p90 (chords) | – | – | 557 / 603 ms | 383 / 400 | 267 / 279 |

* The switch beats the mono tracker on both sets and `pt` alone on the non-chord set, but the price is the entry delay: at 1.1 % false chords the chord is only recognised about half a second after the second note, at 14 % false chords about a quarter of a second. Neither meets the 30 ms target; this is a stability-versus-speed trade-off of the "age" evidence, not tuned away.
* The 24.7 % "false CHORD" on the chord takes counts frames where the reference has ≤1 note (gaps, rings between chords); do not read it as the single-note false-activation rate.
* A faster, better classifier than "notes have persisted" has not been found. Independent-polyphony evidence (residual and harmonic independence) is the open work. Not done: a comparison against Essentia's MultiPitchKlapuri (the pip install of essentia was attempted; see below), real chord-to-scale transitions, audible multivoice output.

## Addendum 4: independent-polyphony classifier (`poly_feat.py`, `poly_clf.py`, `latency_breakdown.py`)

Question B of the review: can we tell, from the audio, that more than one independent fundamental is sounding, without waiting for `pt` notes to age? Host-only, Guitar-TECHS DI, no firmware.

**Evidence (per frame, 4096 window, hop 256, causal; uses only the audio and the production mono tracker's note, never `pt`'s note list, ages or counts):**
* C, incremental joint fit: best 2-note NNLS explanation vs best 1-note one (multi-profile harmonic atoms): absolute and relative residual gain, a Gaussian-likelihood gain penalised for the 3 extra parameters (`Neff·ln(r1/r2) − 3·ln Neff`), and the 1-note residual relative to the spectral noise floor.
* B, independent harmonic support: how many of the second note's first 8 harmonics are not near a harmonic of the first and stand ≥4× above the floor, their energy share, octave flag, level.
* A, mono-explained residual: the mono tracker's note is *fitted* (not subtracted), then how much the best extra note improves the fit.
A logistic regression on these 11 numbers gives the chord probability, followed by a short rule with separate entry and exit thresholds. Trained and tuned on DEV only (player P1, 28 takes, takes held out in 5 folds); the rule was frozen by "minimum median entry latency subject to DEV false activations per minute ≤ the slow persistence switch" (`poly_protocol.json`: enter at p>0.95 for 4 frames, leave at p<0.2 for 6).

Out-of-fold frame AUC for chord vs not, DEV: C 0.963, B 0.954, A 0.923, all 0.979. Each family alone is already strong; none was shown to be unnecessary.

**FINAL** = new 30 s excerpts (80–110 s) of players P2/P3 and chord takes not used before (35 takes: 17 chord, 12 scales, 6 single notes/techniques). Caveat: the same players and sessions as DEV/INSPECTED, so this tests new material, not new players. Mode is MONO (production tracker) or CHORD (`pt`); never a union. FA = a CHORD segment that is mostly outside any true 2+ note period (+300 ms grace); minutes are audio minutes.

| FINAL, all 35 takes | new classifier | persistence slow | persistence fast | mono only | `pt` only |
|---|---|---|---|---|---|
| chord entry delay after 2nd note, median / p90 | **87 / 1965 ms** | 563 / 627 | 270 / 337 | – | – |
| chord exit delay, median | 70 ms | 75 | 75 | – | – |
| false CHORD activations per minute | 1.72 | **0.97** | 1.49 | 0 | – |
| false-CHORD share of single-note frames | **2.4 %** | 3.9 % | 4.9 % | 0 | – |
| CHORD recall (frames with ≥2 true notes) | 84.8 % | 80.3 % | **89.5 %** | 0 | 100 % |
| framewise F1 | 0.85 | 0.82 | 0.85 | 0.44 | 0.84 |
| note-event P / R | 0.34 / 0.36 | 0.30 / 0.28 | 0.35 / 0.32 | 0.20 / 0.28 | 0.20 / 0.26 |
| false retriggers | 93 | 177 | 138 | 255 | 27 |

By material (FINAL, framewise F1 / false activations per minute): chord takes 0.84 / 0.47 (`pt` alone 0.85, mono 0.23); scales 0.93 / 2.5 (identical F1 to mono; slow switch has 0); single notes + techniques 0.79 / 3.7 (mono 0.80; slow switch 5.3). Chord recall by true note count: 2 notes 67 %, 3 notes 88 %, 4+ notes 86 % (INSPECTED excerpt: 46 / 84 / 92 %).

What this supports:
* Detection is no longer the slow part: the median chord is recognised **87 ms** after its second note onset (slow switch 563 ms), with an exit delay of ~70 ms, F1 equal to the fast switch and roughly the same chord recall, and a *lower* share of wrong-mode frames than either persistence switch. This beats the 100 ms milestone at the median; it does not reach 50 ms.
* It is not free of false activations: 1.7 per minute against 1.0 for the slow switch, mostly 0.1–0.3 s long (0.2 s per minute in total), concentrated in scales (2.5/min). That is above the "near or below the slow switch" criterion, so the requirement is **not met**. An F1-neutral result on scales and singles (equal to mono) says the wrong-mode frames are short.
* The p90 entry delay is ~2 s: roughly one chord in ten is not recognised within 2 s, mostly two-note chords (recall 46–67 %). Dyads remain the weak case, as predicted.
* The chord-vs-single decision is separate from the note output: while in CHORD the notes are still `pt`'s, with its own 40–60 ms confirmation. Chord-entry latency above is the mode flag, not the time the second synth voice sounds; the voice cannot sound before `pt` confirms it.

**Latency breakdown of `pt` (synthetic DEV, one change at a time, median onset latency, `poly3 / poly4 / add_note`):** frozen 30 / 53 / 38 ms; confirmation 2→1 frame 24 / 42 / 24 ms with no F1 loss (−6 to −14 ms); shortening the difference lag (1536→512) changes nothing and costs accuracy; removing the birth gate changes latency not at all but adds 2–3× octave/harmonic false notes; shrinking the 4096 window to 2048 saves nothing meaningful and loses 0.18–0.20 F1 (low notes). The window and the time a second note's energy takes to appear above the first dominate; confirmation is the one cheap reduction (one frame).

**CPU:** feature extraction measured 2.2 s of host time per audio second on one core of this laptop while 18 processes were running (full NNLS over 135 atoms plus ~20 small refits per hop). That is not a pedal figure, and it is clearly far too heavy for the AC791N as built; no embedded estimate was made.

**Not done:** chord-to-scale and scale-to-chord transitions (no such real takes; no synthetic splices were built), audible multivoice output, voice-handoff rules beyond "one tracker is authoritative", real-time cost reduction, Essentia MultiPitchKlapuri (deferred as asked; an unbounded run was stopped). Arpeggios are not a separate category in this dataset.


## Addendum 5 — revised scope: real polyphonic synth control (two-level tracker, voices, dyads, embedded)

**Scope change (from the user).** Virtual capo, pitch shifting and harmoniser work are out. The question is now: can the notes actually played on a mono pickup drive independent synth voices (strings, brass, choir, organ, analogue) responsively and without false voices? Everything below is host-only; no firmware was touched. All frozen settings are in `tests/archive/polytrack/*_protocol.json`; tuning used P1 (out-of-fold where a classifier is involved); results are on FINAL (35 new excerpts/takes of P1–P3; same players, so this is not a new-player test).

### A. Two-level tracker (`twolevel.py`, `twolevel_eval.log`)
Mono lead always sounds. An extra voice is added only if it is a pt note, not a duplicate/octave/12th of the lead, present ≥ k frames, and the independent polyphony probability p ≥ th_x. When the classifier says CHORD, pt is authoritative (lead replaced). Frozen from DEV: th_x 0.3, k 8, authoritative.

DEV finding that matters: **authoritative hand-off is essential.** Lead + validated extras *without* replacing the lead in genuine polyphony scored F1 0.74–0.80 with 155–195 bad-voice events/min, versus 0.86–0.88 / 46–87 with hand-off. This confirms that the joint estimator must own pitch identity during polyphony.

FINAL, all takes: mono F1 0.44; pt F1 0.84; classifier switch 0.85; two-level 0.86. Dyad recall (per-note frame recall, n=2) 0.18 / 0.87 / 0.78 / 0.82. Time until the output holds two correct pitches after a note is added: classifier switch 104 / 2000 ms (median / p90), two-level 52 / 1010 ms, pt alone 17 / 81 ms. Single-note frames: lead recall 0.94 (mono 0.92). **Not better on false voices:** bad-voice events 77/min vs 67/min for the classifier switch. Scales and single notes: no gain (scales F1 0.92 vs 0.93 mono). So the two-level idea buys speed of the second voice and a small F1 gain at no extra protection against false voices. Progressive acquisition works (lead sounds at mono latency ≈ 23 ms; extras follow); it does not by itself make false voices rarer.

### B. Audible voice benchmark (`voices.py`, `voices.log`, WAVs in `build/polytrack/voices/`)
Frames → one voice per pitch (released after 3 or 8 absent frames) → detuned-saw render; each chord take has `_mono_only`, `_classifier_switch`, `_two-level` and `_TRUTH` renders. **I have not listened to these; only the numbers below are verified.** Voice-level scoring is much harsher than framewise F1 because short blips (inaudible in a frame count) become clicks. FINAL chord takes (17 takes, 450 true notes), release hold 3:

| system | voices | false voices | voices < 80 ms | missed notes | onset med/p90 |
|---|---|---|---|---|---|
| pt only | 1777 | 1301 | 1100 | 44 | 37/69 ms |
| classifier switch | 1597 | 943 | 952 | 58 | 46/193 ms |
| two-level | 1846 | 1201 | 1133 | 44 | 46/128 ms |

Deferring a voice's start until its pitch has persisted N frames (11.6 ms each) is a cheap filter: defer 8 frames (≈ 93 ms) cut classifier-switch false voices 943 → 268 and two-level 1201 → 353, at onset median ≈ 105 ms and 5–6 extra missed notes. Defer 4 frames: 943 → 425 / 1201 → 545, onset ≈ 70 ms. On scales the classifier switch (mono lead) with defer 4 has 199 false voices / 856 voices, onset 44 ms. Conclusion: **an unfiltered tracker is not usable for audible polyphony; voice-onset confirmation is mandatory and costs 25–90 ms.** This is the real latency/false-voice trade-off, and it is far worse than the framewise numbers suggested. (The "false" test counts a voice false if no true note of that pitch overlaps within 100 ms; bends and vibrato beyond 0.7 semitones also count as false, so single-note numbers are pessimistic.)

### C. Dyads (`dyads.py`, `dyad_gate.py`)
With a perfect (oracle) gate, pt outputs both notes in 83% of dyad frames, so the estimator is not the main limit; the gate was (two-level frozen: 63%, all sets). By interval, pt alone: major 3rd 0.95, minor 6th 0.96, major 6th 0.92, minor 3rd 0.91, fifth 0.80, 2nd/7th/tritone 0.76, fourth 0.62, **octave 0.53**. Through the gate: octave 0.03. Octaves share every harmonic of the upper note, so almost nothing independent remains; I do not expect to fix this with a gate. A dyad-weighted gate (sample weight 6 on exactly-two-note frames, frozen from DEV: th_in 0.8, E 4, th_out 0.2) on FINAL: dyad both-notes 0.77 → 0.83, second-voice delay 52/1010 → 29/318 ms, bad voices 77 → 68/min, F1 0.86 → 0.85. This is the best two-note result so far. Octave dyads remain a known gap.

### D. Embedded feasibility (analytic, NOT measured on the pedal)
Measured on host: pt live notes per frame mean 1.85, p95 4, max 8; dictionary 917 bins × 153 atoms, 17 k nonzeros (≈ 118 per atom); a precomputed atom Gram matrix is 78% dense (≈ 91 KB float32), so it does not fit the RAM budget (pool 248 KB essentially used since v40, `docs/HARDWARE-HANDBOOK.md`; SRAM has no data cache). Atoms therefore have to be generated on the fly from the harmonic formula. Rough per-hop operation counts (hand estimates): 4096-point real FFT ≈ 0.11 MFLOP (the current 8192 zero-padded one is ≈ 2.2×); birth fit during the 12 frames after an onset ≈ 0.1 MFLOP; live-note restricted fits with leave-one-out for ~4 notes ≈ 0.1 MFLOP; classifier features ≈ 0.05 MFLOP. About 0.3 MFLOP per hop; at hop 512 (86 fps) ≈ 26 MFLOP/s, at hop 256 ≈ 52 MFLOP/s. On a 360 MHz core that is roughly 8–25% (hop 512) or 15–50% (hop 256) of one core, assuming 1 flop per 1–3 cycles with no vector unit. RAM ≈ 30–40 KB (input ring, FFT buffer, few spectra, no Gram). **Verdict: plausible on core 2 beside a light synth, not established.** Unknowns: hop 512's effect on confirmation latency (adds ≈ 6–12 ms; untested), fixed-point/float cost on the real FPU, and whether the 30–40 KB can be found. The host Python feature extraction (2.2 s per audio second) is irrelevant for this; it uses dense SciPy NNLS. Next step for this item would be a C reference of the fit loop timed with the repo's TIMER4 methodology, which needs a pedal run the user must authorise.

### E. Literature (search summaries only; I did not read the full papers)
- Klapuri 2006 (ISMIR), summing harmonic amplitudes: direct, iterative-cancellation and joint variants; iterative and joint did about as well as a far more complex reference; whitening helps. Our `klap` is a simplified version of this, not a reproduction.
- Yeh & Röbel (DAFx 2004): joint scoring beats iterative cancellation because an early F0 error propagates; joint search is costly. Matches our result that independent per-note validation (leave-one-out) was needed.
- Duan, Pardo & Zhang (TASLP 2010): maximum-likelihood with greedy one-at-a-time F0 search, avoiding combinatorial cost; also estimates polyphony.
- Cont (2006) sparse non-negative decomposition with known pitch templates, real-time Max implementation; Sha & Saul (NIPS 2004) real-time NMF pitch; Schramm & Visi (NIME 2018) real-time factorisation on a Raspberry Pi, mono input. Our `pt` is essentially in this family; the novelty here is note birth from difference spectra and leave-one-out validation.
- Spotify Basic Pitch (ICASSP 2022): small neural multipitch/onset/note model, faster than real time in batch but no verified streaming-latency figure; a candidate reference/teacher, not benchmarked here.
- Commercial mono-pickup polyphonic conversion (Jam Origin MIDI Guitar): vendor-stated latency of about 6–11 ms, user reports say fast strummed chords are the hard case; unverified vendor claims.
Gaps: no Essentia MultiPitchKlapuri run (deferred as agreed); no neural reference; not searched: guitar-specific string-wise modelling and hexaphonic-free commercial hardware.

### F. Where this leaves the five-point priority list
1. Detect 2–4 notes: pt F1 0.85 on chord takes; dyads 0.83 with dyad gate; octaves unresolved.
2. False voices: unsolved without a confirmation delay; the audible metric is 2–3× worse than framewise.
3. Speed: lead 23 ms; second voice median 29–52 ms before confirmation filter, ≈ 70–105 ms after it.
4–5. Stability and chord edits: voice fragmentation 1.2–1.5 voices per note; added/removed notes only tested via synthetic scenarios and real chord changes inside takes, no dedicated edit set.
7. Sounding like a synth: renders exist, unlistened.
8. CPU/RAM: estimate only.
