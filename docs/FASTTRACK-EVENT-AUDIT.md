# FastTrack event audit: what happens to a physical pick between the guitar and the synth envelope

Branch `synth3-fasttrack` (base synth3 a35ca9b). **Stage 1 only: audit and frozen baseline. No behaviour was changed; `engine/` is untouched** (diagnostic hooks live in a build-time overlay copy, `tests/fasttrack/mk_overlay.py`, macro `FT_HOOKS`, and change no output).
Reproduce: `FT_SRC=tests/fasttrack/ft_synth.c FT_OVERLAY=1 tests/fasttrack/build.sh synth_base -DFT_HOOKS; tests/fasttrack/run_set.sh synth_base gtechs; python3 tests/fasttrack/ft_attrib.py synth_base --cats` (the SWEEP variant: copy the binary to `ft_synth_sweep`, run with `FT_PATCH=80`).

## 1. The actual event path (file:line at a35ca9b)
| # | Question | Where it is decided | What decides it |
|---|---|---|---|
| 1 | Onset detected | `px_track.h:213-226` `pxt_frame`: spectral flux > threshold -> `t->onset`, `refractory = 20` frames (29 ms), or a "weak" flux pick (`sensitive_picks`, `weak_refractory = 20`). Also `px_track.h:475-479` `pxt_layer_run`: the envelope `rise` from silence. | flux vs running average; a 29 ms refractory after every strong pick |
| 2 | Pitch hypotheses | `px_track.h` `pxt_frame`: ERB band instantaneous frequencies -> partials -> harmonic salience (Klapuri) -> `t->mono`, `t->conf`, `t->notes[]` | one strongest note per 1.45 ms frame; the first frames after a pick are often an upper partial |
| 3 | Fundamental chosen | `px_track.h:283-322` octave check by waveform (`pxt_period`, `pxt_refine`); `px_track.h:504` `pxt_pick_fundamental(stab_f)`: **may return 0, and then `px_track.h:505` `if (pitch <= 0.0f) return;` leaves the pick pending with no note, silently** | waveform repeats <0.5 (>300 Hz) else a lower period <0.3 else fail |
| 4 | Note confirmed | `px_track.h:497-510`: `stab_n >= stabreq` (10 **consecutive** frames within 30 cents; one odd frame resets it to 1) AND `wait_n >= minwait_lo` (14) AND for stab_f above 200 Hz `wait_n >= minwait` (20 frames, 29 ms); weak picks must also pass `pxt_confirm_pick`; weak picks expire at `wait_n > 28` (`:501`) | stacked: stability + two waits + fundamental check + confirm_pick + expiry |
| 5 | Slur or new note | (a) `px_track.h:538` a different steady pitch without a pick takes over after `PXT_SLUR`=4 frames (octave: 20); (b) `px_mono.h:143-148` `PX_MONO_SLUR`: a re-detected pick with `vel + 0.15 < expr` of the ringing note (amplitude only) becomes `slur_now`; `px_mono.h:184` turns the onset into `slur=1`; **the adapter ignores `slur` entirely** | pick strength relative to the previous note |
| 6 | Pitch hold during decay | `px_mono.h:103` `PX_MONO_HOLD` 0.5 after `PX_MONO_HOLD_AFTER` 40 frames: freezes `f0` and clears the candidate votes while the fast level is under half of the 150 ms peak; `px_mono.h:104-115` the same threshold undoes a **weak repick** (`lay.onset = 0`, restores f0/vel/peak_ref/since) | level relative to a 150 ms memory |
| 7 | Mute | `px_mono.h:117-127` fast level < 0.1 x 150 ms peak -> `mute = 1`, `rel = 1` (release + damp), `mute_pk = pkl`; `px_mono.h:129-142` until the note ends, a new onset is accepted only if `onset_peak >= 0.3 x mute_pk` (-10.5 dB of a 300 ms memory), else the whole layer state is restored from `sv` (`:137` "a thump, not a pick") | level relative to a 300 ms memory; `mute` latches until `!gate` or an accepted pick |
| 8 | Onset suppressed | `:104` weak-repick hold-undo (H), `:137` mute thump undo (M), `:143` slur (S), `:153-168` harmonic-ratio confirm hold 35 ms for a start at x2,x3,x4 or /2,/3,/4 of the note played in the last 800 ms (C), `:169` dead pick `fe < 0.1 x onset_peak` (D); `px_synth_adapter.h:348` FOLLOW (F); `:381-382` pending flags (P, Q) | five wrapper rules + the adapter |
| 9 | Retrigger / note-off emitted | `px_mono.h:185` `o->retrigger = onset && was`; `o->release = (was && !gate) || rel` (`rel` = the mute detector); `damp` = the mute detector | |
| 10 | Synth envelope started or suppressed | `px_synth_adapter.h:348-352` FOLLOW: if `follow` and voice 0 is in attack or decay (stage 1 or 2), the note-on is consumed: `pick_accent = 0.25 x vel`, `pend_on = 0` -> **no envelope restart**; `:380-382` three single flags `pend_on/pend_re/pend_off` per 32-sample block (0.73 ms); `:382` a note-on clears `pend_off`; a release after a note-on in the same block starts the release stage straight after the attack (`:362`) | patch (`follow` is on for 64 % of patch slots: the leads, bass, organ) |

Mechanisms that change note identity, timing or the decision to sound, in order of the signal path: refractory (1), pick fundamental veto (3), stability/wait/confirm_pick/expiry (4), pitch takeover votes (5a), amplitude slur (5b), pitch hold (6), weak-repick undo (6), mute latch + thump undo (7), harmonic confirm (8C), dead veto (8D), FOLLOW (10), pending-flag collapse (10).

## 2. Instrumentation
`ft_synth.c` runs the real `px_synth_run` (tracker layer, `px_mono` wrapper, `px_synth_adapter`, Felucca ANALOG) per sample and logs: R pick began, H first pitch hypothesis, S pitch stable, U layer note-on, W reason codes (H M S C D at the wrapper; P Q F in the adapter; N L X what the tracker told the synth), and **A: a synth envelope attack started** (voice 0 note-on counter). `ft_attrib.py` follows each truth group down the chain and puts it in exactly one bin, so every lost note is tied to the rule that lost it. 64-sample pitch/gate trace and optional rendered WAV (`FT_WAV`).

## 3. Frozen baseline (legacy chain, full audible path, Guitar-TECHS P1-P3 DI, 9623 picks, 777 of them fast: <150 ms after the previous)
Annotation caveat: truth timing +-10 ms (lag fit). Development data. All numbers are ANALOG through the real adapter.

| outcome of a physical pick | ANALOG CLASSIC (a FOLLOW patch) | ANALOG SWEEP (restarts per note) |
|---|---|---|
| **synth envelope attack at the right pitch** | **5748 = 59.7 %** (fast: 248 = 31.9 %) | **7492 = 77.9 %** (fast: 396 = 51.0 %) |
| right pitch reached by glide, no attack | 700 = 7.3 % | 648 = 6.7 % |
| attack at a wrong pitch | 168 = 1.7 % | 369 = 3.8 % |
| tracker emitted the right note-on, **FOLLOW consumed it** | **1738 = 18.1 %** (fast 148) | 0 |
| amplitude slur (S) | 208 = 2.2 % | 206 = 2.1 % |
| 35 ms harmonic confirm (C) still pending at the end of the window | 51 = 0.5 % | 11 = 0.1 % |
| dead pick (D) / mute thump (M) / hold undo (H) | 82 / 36 / 22 | 82 / 36 / 21 |
| layer never admitted a note after a pick (R without U) | 338 = 3.5 % (fast 50) | 232 = 2.4 % |
| no pick evidence at all (no R) | 532 = 5.5 % (fast 143) | 526 = 5.5 % |
| attack latency, annotated onset -> envelope attack, median / p90 / p99 | 20.6 / 30.2 / 49.7 ms | 20.5 / 31.5 / 58.1 ms |

By category (CLASSIC): clean scales 69.6 % attacks (**21.1 % eaten by FOLLOW**, 2.4 % slur); single notes 95.2 %; palm mute 87.1 %; bend+vibrato 59.7 % (19.5 % without any pick evidence: legato/slides/bends that the tracker follows as pitch); music (chords and lines, 346 fast groups) 9.5 % attacks, 25 % no pick evidence, 17 % never admitted.

### What this shows
1. **The largest single loss on a FOLLOW patch is not the tracker.** In 18.1 % of all picks (21 % of scale notes, 19 % of the fast ones) the tracker had emitted a correct note-on and the adapter discarded it. Switching to a restarting patch raises audible attacks from 59.7 % to 77.9 % with identical tracking. This is the "accurate detected pitches lost" the build 917 listening reported, and it is a patch-level suppression, not a detection failure.
2. **The wrapper's veto rules together lose about 4 % (S 2.2, D 0.9, M 0.4, C 0.5, H 0.2)** on this corpus, a smaller share than they are blamed for on fast clips; their cost is concentrated in fast, damped playing (bleep3: three sustained notes lost to the mute-thump rule, `FASTTRACK-BENCHMARKS.md` section 2).
3. **2.4-3.5 % of picks start a pick in the layer and never become a note** (`lost_layer`): the stability counter needs 10 consecutive agreeing frames, the estimator's first 20-40 frames after a pick are unstable, and `px_track.h:505` drops the pick without a trace if the fundamental check fails. This is the acquisition cost the stacked waits were hiding.
4. **5.5 % have no pick evidence at all**, mostly legato/hammer-on/slide notes and chords. The tracker cannot call them notes; whether the pitch followed is counted separately (glide).
5. Fast playing is hit hardest: only 31.9 % of fast picks (<150 ms apart) become an audible attack on CLASSIC, 51.0 % on SWEEP, against 59.7/77.9 % overall.
6. Not yet measured in this baseline: the 32-sample pending-flag collapse (P, Q) produced no losses on this corpus, so the corpus is not stressing it (chains of events inside 0.73 ms need a synthetic test); false audible note-ons per minute and mute-to-release times for the full chain are in the next stage's benchmark.

## 4. Old rule -> proposed replacement (design, NOT yet implemented)
| old rule | failure it was added for | measured effect here | proposed replacement | what must be shown before it is accepted |
|---|---|---|---|---|
| `px_track.h` refractory 20 frames (29 ms) | double detection of one pick | not isolated | per-pick evidence window decided by the controller; a repick inside it requires new flux **and** periodicity reset | repicks 45 ms apart found |
| `PX_MONO_STAB` 10 consecutive frames + `WAIT_LO` 14 + `MINWAIT` 20 | wrong first pitch (78 % of onsets on bleep.wav) | STAB protects: wrong starts 310 -> 607 without it | **one admission decision**: accept when pitch evidence (agreement among frames, leaky vote, periodicity, harmonic support) crosses a threshold that depends on confidence, with a hard latency bound, never "N frames" | wrong first pitch rate <= legacy (19 % on bleep.wav) at lower latency |
| `px_track.h:505` silent return | fundamental check failed | 2.4-3.5 % of picks vanish | candidate stays pending with an explicit reject reason, and a bounded retry | no `lost_layer` without a logged reason |
| `PX_MONO_SLUR` (amplitude) | pick re-detections during a decay chirp | S: 2.2 % of picks, +221 extra note-ons if removed | LEGATO_CHANGE only when pitch moves with no pick evidence; a pick is a REPICK/NOTE_ON regardless of loudness | quiet-after-loud notes (test 4) pass; false repicks/min not higher |
| `PX_MONO_CONFIRM_MS` 35 ms harmonic hold | octave/fifth locks on a partial of the previous note | ~0.5 % of picks held; <=0.1 point on every metric on Guitar-TECHS | harmonic relation to the previous note becomes an evidence term in the admission score (not a fixed delay) | bleep.wav wrong pitches not worse |
| `PX_MONO_HOLD` | decay drifts to harmonics / pitch rises 60-80 cents on a mute | removing it: +161 notes found, +419 extra note-ons | hold the **pitch** (not the onsets) while confidence is low; onsets are evaluated independently | hold does not suppress any pick; palm-mute pitch error not worse |
| mute latch + thump undo (0.3 x mute_pk) | damp thump read as a pick | M: 0.4 %; swallows soft notes after a damp (bleep3) | DAMP releases the voice; a following onset is judged on its own periodicity and attack evidence; a rejected candidate leaves no state behind | hard mute then pick at 20/40/80 ms all play; noisy mute alone plays nothing |
| `PX_MONO_DEAD` veto | pick whose sound is gone when pitch is known | D: 0.9 % | dead-ness stays a **feature** in the admission score | short palm-muted notes survive |
| adapter FOLLOW consumes the note-on | the original tracked synths glided on legato | **18.1 % of all picks** (21 % scale notes) | tracker always emits NOTE_ON/REPICK; adapter articulation (accent only vs full attack) is an explicit per-patch setting applied **after** the event exists; compatibility render of old FOLLOW behaviour kept as a mode | every pick appears in the event log; CLASSIC under "legato" articulation sounds as before |
| single `pend_on/pend_re/pend_off` flags per 32 samples | one control block | no corpus loss; collapse proven by reading the code (`:381-382`, `:362`) | fixed-capacity ordered event queue with sample offsets and defined overflow | synthetic two-events-in-one-block test fails on legacy, passes on new |

## 5. Stop-and-report checks at this point
- No protected shared DSP (px_track.h, POLY OCTAVE HQ/FUZZ), preset, patch or tone was modified.
- The legacy decision layer is isolated from the estimator at one seam: `pxt_layer_run()` consumes `px_track` outputs (`t->mono`, `t->conf`, `t->onset`, `t->weak_pick`, `t->sal[]`, `t->notes[]`) and the decimated waveform (`pxt_period`, `pxt_refine`). All of it is available to a new controller without a second frequency tracker.
- Risk that decides stage 3: the estimator's first 20-40 frames after a pick (wrong strongest note, e.g. 466/433/159/775/315 Hz for a 150 Hz note) are the reason every legacy rule exists. If the single admission decision cannot beat them, that is the next DSP problem and must be reported as such (rule 6), not hidden.
