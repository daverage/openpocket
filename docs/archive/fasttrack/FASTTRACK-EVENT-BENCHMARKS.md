# FastTrack event benchmarks: legacy chain vs FOLLOW isolation vs the event controller (host only)

Data: Guitar-TECHS P1-P3 DI, 9623 picks (777 fast = <150 ms after the previous pick), **development data**, truth timing +-10 ms. Same audio, same scorer for every row (`tests/archive/fasttrack/ft_compare.py`).
Not firmware, not pushed beyond the audit commits, no preset or patch changed. Reproduce: see "Commands" at the end.

## 1. Baseline reconciliation
The outcome bins of `FASTTRACK-EVENT-AUDIT.md` are exhaustive: they sum to 9623 / 9623 for both CLASSIC and SWEEP (checked in code). The audit's table looked like ~91 % because dead-pick / mute-undo / hold-undo were listed as counts (82 / 36 / 22) not percentages (1.5 % together) and glide-only was a separate row. 
"Median 20.6 ms / p90 30 ms" in the audit was for successful attacks only. Missed picks are now counted as infinite latency (**p90\* = inf** everywhere below: at least 10 % of all picks, and far more than 10 % of fast picks, never produce an attack, so no variant has a finite p90 over all picks).

## 2. Three separate questions per pick
`pitch right` = the voice is at a pitch of the pick (held 3 trace samples) within [onset-30 ms, end]; `event` = the tracker told the synth about a note (note-on, repick or legato) at that pitch; `attack` = the synth envelope restarted at that pitch.

| variant | pitch right all / fast | event all / fast | attack all / fast | attack latency med (fast med) | false attacks /min: wrong-initial, spurious |
|---|---|---|---|---|---|
| legacy chain, CLASSIC (FOLLOW on, production) | 91.0 / 78.5 % | 79.9 / 52.4 % | **59.7 / 31.9 %** | 20.6 (18.3) ms | 0.8, 1.5 |
| legacy chain, FOLLOW forced off (same tracker decisions) | 91.0 / 78.5 % | 79.9 / 52.4 % | **77.9 / 51.0 %** | 20.5 (17.2) | 2.1, 5.2 |
| legacy chain, SWEEP (FOLLOW off, production) | identical to the row above | | | | |
| legacy chain, SWEEP with FOLLOW forced on | identical to CLASSIC | | | | |

Stage 2 conclusion (FOLLOW isolation; patch is irrelevant, only the flag matters): FOLLOW changes **only** articulation. Disabling it raises audible attacks by 18.2 points (19.1 on fast picks) with the same pitch and the same recognised events, **and raises false attacks from 2.3 to 7.3 per minute**: FOLLOW was hiding false notes as well as real ones. "Pitch right" is 11 points higher than "event": those notes' pitch arrives by continuous following (legato, slides, hammer-ons, or a repeated pitch still ringing), without any event.
Even with FOLLOW off, only 52.4 % of fast picks become recognised events: the loss that remains is upstream of the adapter (SWEEP control case, as you noted).

## 3. Event controller (host-only research code: `tests/archive/fasttrack/ft_ctrl.h`, `ft_ctrl_trace.c`)
One authority; consumes the unchanged tracker's evidence (`t->mono`, `t->conf`, `t->onset`, waveform checks); does **not** call `pxt_layer_run` or `px_mono_run`; contains no amplitude-relative slur, no mute lockout, no dead-pick veto, no 35 ms confirm, no pitch hold that can block a pick. Admission = leaky vote on the strongest pitch + waveform confirmation (`pxt_confirm_pick` / `pxt_pick_fundamental`), bounded at 45 frames, rejected candidates leave no state. Events: NOTE_ON, REPICK, LEGATO_CHANGE, PITCH_UPDATE, DAMP, NOTE_OFF, REJECT (+reason). Rendered through a neutral oscillator (sine+2 harmonics, restarts its envelope on every NOTE_ON/REPICK).
Ablations (one change at a time; `FTC_*` macros):

| controller variant | pitch right all / fast | event all / fast | attack all / fast | att. latency med (fast) | false/min wrong-init, spurious |
|---|---|---|---|---|---|
| legacy, FOLLOW off (reference) | 91.0 / 78.5 | 79.9 / 52.4 | 77.9 / 51.0 | 20.5 (17.2) | 2.1, 5.2 |
| v0: admit from 8 frames, any flux pick | 92.1 / 80.2 | 76.2 / 53.9 | 70.9 / 46.7 | 4.3 (3.0) | **8.2, 26.9** |
| v1: + pick needs a level rise (fast level > 1.3x its 8-frame minimum) | 91.4 / 79.8 | 74.4 / 52.4 | 69.0 / 45.0 | 4.3 (3.0) | 7.4, 15.1 |
| v1, rise 1.6 | 90.9 / 79.8 | 73.8 / 52.1 | 68.2 / 44.4 | 4.3 (3.0) | 6.9, 11.0 |
| v1, rise 2.2 | 89.2 / 78.1 | 72.1 / 50.5 | 65.8 / 42.3 | 4.4 (3.0) | 6.2, 8.5 |
| v1 rise 1.3, earliest admission 14 frames | 91.3 / 79.8 | 80.4 / 55.2 | 76.8 / 49.0 | 11.5 (8.6) | 4.1, 11.0 |
| **v1 rise 1.3, earliest admission 20 frames** | 91.1 / 79.9 | **82.7 / 57.0** | **79.5 / 51.2** | 20.2 (17.3) | 2.8, 8.7 |
| v1 rise 1.3, earliest admission 28 frames | 90.8 / 79.9 | 83.2 / 57.7 | 80.3 / 52.1 | 31.8 (28.8) | 2.4, 6.7 |
Bound-admit (`FTC_BOUND_ADMIT=1`, admit an unconfirmed pitch at 45 frames, the legacy behaviour) changed nothing measurable (+0.1 pt).

### What the controller experiments show
1. **One decision can replace the stack, at equal speed.** With a 20-frame earliest admission (legacy's own MINWAIT) the controller, with none of SLUR/CONFIRM/HOLD/MUTE/DEAD/STAB, recognises more events (82.7 vs 79.9 %) and makes more attacks (79.5 vs 77.9 %) than the legacy chain with FOLLOW off, at the same latency. Fast-pick attacks are the same (51.2 vs 51.0 %).
2. **It does not beat the legacy chain on false notes** (at the as-recorded input level: spurious notes scale with level, and at -12 dB the two are equal, see `FASTTRACK-LEVELS.md`). Spurious attacks 8.7 vs 5.2 per minute (+67 %), wrong-initial 2.8 vs 2.1. **This hits the stop condition "false note events increase significantly".** I did not add compensating thresholds.
3. **Speed is limited by the estimator, not by the event logic** (rule 6 exposed): the wrong-initial rate falls 7.4 -> 4.1 -> 2.8 -> 2.4 /min as the earliest admission moves 8 -> 14 -> 20 -> 28 frames. Admitting at 8-11 ms gives the 4 ms median latency (about 16 ms faster than legacy) but 3.5x the wrong notes: the ERB estimator's first 20-40 frames after a pick are unreliable and the short-window waveform check passes on upper partials (372 Hz admitted for a 105 Hz note). The next DSP problem is a better early fundamental, not another event rule.
4. **Where false notes come from:** weak spectral-flux "picks" at note ends and decays (the legacy chain suppressed these with hold-undo, dead-pick and mute-thump rules; here only the level-rise evidence does), which the v1 variants reduce (26.9 -> 8.5 spurious/min at rise 2.2) at a cost in recall. v0 alone made 5399 false attacks against 6818 correct ones (3260 of them from strong flux picks, 2139 from weak ones; 73 % of all false attacks (3958 of 5399) lie more than 150 ms from any truth onset).
5. **The largest remaining recall loss for fast picks is classification, not pitch:** with the 20-frame controller, 27 % of fast picks (212 of 777) reach the right pitch with **no note event**: 165 by a pitch move with no pick evidence (hammer-ons, pull-offs, slides, or picks the flux missed) and 47 repeated pitches that were never re-attacked. The controller follows steps up to 120 cents continuously (as the legacy layer does) and reports them as nothing. A legato transition detector (step vs bend) is the missing event; whether those notes were picked is not labelled in Guitar-TECHS, so I cannot count them as misses or as correct legato.
6. Rejected candidates are rare (18 of 9623 picks; 0.2 %): the bound and the reject path are not a significant loss route.

## 4. Real fast clips (no ground truth; raw counts only)
`My Audio/bleep3 di.wav` (offline reference: 37 detected onsets): legacy CLASSIC makes **14** envelope attacks (25 tracker note-ons), legacy FOLLOW-off 25, controller (20-frame, rise 1.3) **39**. `fast play di.wav` (41 offline onsets): legacy CLASSIC **6** attacks, FOLLOW-off 25, controller **28**. The controller figure includes false attacks that these clips cannot separate without labels; **listen** to `build/fasttrack/renders/*.ctrl2b_neutral.wav` against `*.legacy_classic.wav` (local, not committed) before reading anything into it. The legacy CLASSIC renders are the real Felucca voice; the controller renders are a plain voice, so the comparison is about which notes sound, not tone.

## 5. Status against the acceptance criteria and stop conditions
| criterion | status |
|---|---|
| fast-run note-on recall improves | **No**: 51.2 vs 51.0 % (FOLLOW off); the +18 points over production CLASSIC is the FOLLOW flag, not the controller |
| false audible notes do not increase | **No**: spurious 8.7 vs 5.2 /min, wrong-initial 2.8 vs 2.1 at equal attack rate |
| no valid note suppressed because it follows a mute or is quieter | Yes by construction (no amplitude-relative rule); not yet verified note by note on bleep3 (counts and renders only) |
| repicks while the previous voice sounds | Yes (REPICK/NOTE_ON interrupt; 181 fast+slow repeats still unmatched) |
| bends and legato work | Not shown: bends/vibrato scored (see `--cat`), legato step detection missing (finding 5) |
| tail latency no worse | Median equal at 20 frames; p90 is infinite for every variant (misses) |
| mute-to-next-note, noisy mute, synthetic cases, queue test | **Not yet run** |
| separately testable from Felucca | Yes (`ft_ctrl_trace.c`, no adapter) |
Stop conditions hit: **false note events increased significantly**; the estimator cannot supply a trustworthy early pitch. Stopping here for your decision, as instructed.

## Commands
```
python3 tests/archive/fasttrack/mk_overlay.py        # overlay of engine/ with hooks (engine/ itself is never edited)
FT_SRC=tests/archive/fasttrack/ft_synth.c FT_OVERLAY=1 tests/archive/fasttrack/build.sh synth_base -DFT_HOOKS            # legacy chain; synth_cl_f0 adds -DFT_FOLLOW=0; run SWEEP with FT_PATCH=80
FT_SRC=tests/archive/fasttrack/ft_ctrl_trace.c tests/archive/fasttrack/build.sh ctrl2b -DFTC_RISE=1.3f -DFTC_ADM_MIN=20
tests/archive/fasttrack/run_set.sh VARIANT gtechs ; python3 tests/archive/fasttrack/ft_compare.py synth_base=label ctrl2b=label [--cat scales]
python3 tests/archive/fasttrack/ft_attrib.py VARIANT --cats                  # per-pick loss bins for the legacy chain
```
