# Synth2 stage 2: the one mono tracker

**Decision: keep the existing OpenPocket tracker** (`engine/px_track.h`: `pxt_run` and its note layer, the v49 band-bank tracker). Cycfi Q's BACF does not meet the
replacement rule on any set. Evidence level: host benchmark on recorded audio (source inspected, host tested). Nothing here was measured on a pedal.

## The rule (from the brief, fixed before looking at the new results)

BACF replaces the incumbent only with at least 3 percentage points more notes right, or at least 25 % fewer errors in an important class (octave errors, false
triggers), without a median onset more than 5 ms and a 90th percentile more than 10 ms later than the incumbent's.

## Results (`tools/research/mono_compare.py`, candidate `eng_v73` is this tree's tracker through `tools/research/mono_track_bench.c`; Q is `tools/research/q_bench.cpp` with Q's own conditioning, cycfi/q `efe26114`, MIT)

`eng_v73` reproduces the earlier v70 results to the digit on all five sets, which shows the new bench is the same tracker path. Notes right / octave errors / median and 90th-percentile onset latency (ms after the annotated onset).

| Set | This tracker (60 Hz) | Q BACF | Verdict |
|---|---|---|---|
| Guitar-TECHS electric DI (48 takes, 9 069 notes) | **89.5 %** / 1.4 % / 4, 19 | 66.1 % / 1.6 % / 34, 50 | Q worse on accuracy and latency |
| GuitarSet development (players 00-01, 6 420 notes) | **83.6 %** / 2.0 % / 7, 47 | 72.2 % / 3.0 % / 31, 71 | worse |
| GuitarSet test (players 02-05, frozen, 9 639 notes) | **94.4 %** / 1.7 % / 6, 34 | 85.9 % / 2.1 % / 35, 65 | worse |
| IDMT bass lines (948 notes) | 62.7 % / 12.9 % / 43, 83 | 40.8 % / 5.2 % / 55, 99 | Q fewer octave errors, far fewer notes right |
| IDMT single bass notes (960) | 55.4 % / 4.9 % / 62, 298 | 55.5 % / 2.0 % / 81, 307 | tie on notes right; Q's octave errors are 59 % fewer but it is 19 ms later (limit 5 ms) |

On the frozen GuitarSet test set the scoring was done once per finished candidate, as before (docs/archive/phase2b.md). Not run: a bootstrap of the single-note difference (the intervals above do not overlap for the guitar sets).
GuitarSet is an acoustic guitar's pickup mix, so the electric and bass sets carry the decision. Q was benchmarked on the host with its own front-end; it was not ported, because it failed the rule first.
Q's CPU and RAM were not measured for the same reason.

## One tracker, two ranges

The tracker's pitch floor is now a field (`pxt_set_range`, default 60 Hz = B1), so bass is a configuration of the same tracker, not a second one. Notes right / octave errors on the same sets:

| Set | 60 Hz (guitar) | 38 Hz (bass) |
|---|---|---|
| IDMT bass lines | 62.7 % / 12.9 % | **84.2 %** / 2.0 % |
| IDMT single bass notes | 55.4 % / 4.9 % | **64.5 %** / 0.8 % |
| Guitar-TECHS electric | 89.5 % / 1.4 % | 89.3 % / 1.5 % |
| GuitarSet dev / test | 83.6 / 94.4 % | 83.5 / 94.4 % |

The runtime 38 Hz output is bit-identical to the compile-time 38 Hz build; the default range is bit-identical to v73 (`tests/protected.sh`, which covers the SFX synth that uses the tracker).
The earlier 38 Hz experiment (v71) was reverted after it let guitar notes drop an octave **on the pedal**, although the host benchmark showed no guitar loss. That regression is not explained, so the bass range is only for bass sounds
(selected by the sound, never the default) and stays unvalidated on a pedal until you try it.

## Interface

`engine/px_mono.h`: `px_mono_init(engine, mono, fmin)`, `px_mono_run(engine, mono, x, &out)` per sample; `out` has `f0` (continuous), `vel`, `expr`, `conf`, `gate`, `onset`, `retrigger`, `release`. The input must be the clean guitar after the DC blocker and before the gate and every effect.
`tests/engine/mono_test.c` checks the interface on synthetic plucks. It is a smoke test only.

Observed on synthetic notes (so not evidence of playing quality):

* The pitch at `onset` is the first frames' guess and can be wrong for low notes (A2 reported 217 Hz, E2 67 Hz at the onset), settling within about 80 ms (`PXT_SETTLE`). A voice must follow `f0` after the onset and not freeze it. The recorded-guitar median time to the first right frame is 4 ms on electric guitar, so the typical note starts right.
* A synthetic E1 with strong harmonics is unstable in the bass range: it alternates between E1 and a partial and retriggers 21 times in 2 s. The recorded bass DI figures above are better, but this is the case to listen for on a pedal.
* A repicked note retriggers (onset with the gate already on), a slide is one note, silence and a very quiet note (-66 dB) do not trigger.

## Not done

Pedal validation (on-device logging or listening); release behaviour (the tracker holds the gate through 58 % of the gaps after the annotated note-offs, with strings still ringing: a synth envelope should be tuned by listening); SFX `SQUARE SYNTH` / `SQUARE SYNTH HQ` still use their own `pxs_trk` instance of the same tracker.
