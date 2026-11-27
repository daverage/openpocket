# synth4: a quicker, cleaner response of the guitar synths

> **Historical (superseded in v96).** These are the mono-tracker synths' response notes. In v96 those synths were retired in favour of waveform-following types that track nothing ([WHATS-NEW-v96.md](WHATS-NEW-v96.md)).

Branch `synth4` (= `synth2-enhance` + `synth3-declick`). Goal: improve mono tracking use without clicks and pops, slurs, slow attacks and releases, and latency. Everything below was measured on the host with the FastTrack harness (`tests/archive/fasttrack`, the real tracker + adapter + Felucca chain); **none of it is heard or measured on the pedal yet**.

## Baseline (before)
* Tracker: a correct note is audible about 26 ms after a synthetic pluck (`patch_latency.py`); the tracker's confirmation wait is most of it (`FASTTRACK-BASELINE.md`: removing every wait gives about 1 ms but 4x the wrong starts).
* Clicks and pops: the H1-style declick evaluation (`FASTTRACK-DECLICK-BENCHMARKS.md`) found nothing worth correcting in the mono path (two real DI recordings changed by at most 0.00002 of full scale). Not adopted.
* Slow attacks: INSTRUMENT / ENSEMBLE patches 0-5 took 125-220 ms to half level, patches 30-35 about 460-475 ms.
* Slow releases: after a hard mute every patch is 20 dB down within 115 ms (the DAMP path, 40 ms); after the string dies away naturally the longest patches kept sounding: INSTRUMENT up to 191 ms to -20 dB and 1.19 s to -40 dB after the guitar was already 50 dB down, ENSEMBLE up to 0.84 s.

## 1. Attack: speak at once, swell as the patch says (`PX_S2_ATK_HEAD`, engine/px_synth_adapter.h)
A note that starts from silence begins at a share of full level that grows with the patch's attack time (none below 30 ms, 35 % from 90 ms, rising to 50 % for attacks of 600 ms and more), then ramps as before. A voice already sounding keeps its level, so retriggers and slurs are untouched. `PX_S2_ATK_HEAD 0` restores the old behaviour.
| | before | after |
|---|---|---|
| INSTRUMENT half-level, p90 / max | 222 / 460 ms | 58 / 70 ms |
| ENSEMBLE half-level, p90 / max | 173 / 475 ms | 52 / 52 ms |
| audible onset on the slow patches | 30-39 ms | 26 ms (the tracker's own) |
The ATTACK knob now sets the length of the swell to full level (time to 90 %: ATTACK 0 0.17 s, ATTACK 100 0.34 s on STRINGS). Click check (`/tmp` script, see commit message): the first 3 ms step is 1.5-3 % of peak on the slow patches, 0.1-0.5 x the steady waveform's own step; the fast patches are unchanged.
Tests: `tests/engine/synth_test.c` (speaks within 60 ms of the pick, ATTACK 100 swells slower than 0); the old half-level check was replaced because it measures what the head start deliberately changes.

## 2. Release: no hung tails (`PX_S2_REL_KNEE` / `PX_S2_REL_SPAN`)
The release time constant is bounded smoothly: unchanged up to 50 ms, flattening toward 120 ms (-20 dB in about 0.28 s, -40 dB in about 0.55 s). Hard-mute behaviour is unchanged (DAMP). `patch_release.py [--natural]` measures both cases.
| after the guitar is 50 dB down | before | after |
|---|---|---|
| INSTRUMENT: max to -20 dB / -40 dB | 191 ms / 1191 ms | 0 / 166 ms |
| ENSEMBLE: max to -40 dB | 839 ms | 101 ms |
RELEASE 100 on STRINGS: -20 dB in 0.104 s (was 0.447 s); RELEASE 0 0.029 s: the knob still works inside the bound. Test: `synth_test.c`.

## 3. Wrong starts: a confidence shortcut (negative result; code kept, off)
`PX_MONO_FASTCONF` (engine/px_track.h, default 0 = off): a note whose pitch is stable and whose frame confidence is at least the threshold may speak after `PX_MONO_FASTWAIT` frames. Scored on the Guitar-TECHS corpus (9623 note groups, `ft_score.py`):
| variant | latency med / p90 | recall | wrong starts | extra notes |
|---|---|---|---|---|
| production (off) | 20.2 / 33.4 ms | 0.887 | 310 | 559 |
| confidence >= 0.5 | 15.9 / 30.5 | 0.887 | **400** | 571 |
| confidence >= 0.7 | 19.7 / 33.2 | 0.887 | 312 | 561 |
| confidence >= 0.85 | 20.2 / 33.4 | 0.887 | 310 | 559 |
The estimator's confidence does not separate a true fundamental from an upper partial of the pick transient: the 4.3 ms gain at 0.5 costs 90 more wrong starts, the same trade as shortening MINWAIT. The estimator already tests sub-multiples of each candidate (`pxt_salience`, px_track.h), so a separate subharmonic check would repeat that. **Not adopted**; production is unchanged. The remaining latency is the wait that protects against wrong notes.

## 4. CHOIR clicked at every note change (found by the owner on build 930)
Not caused by the envelope changes above (the same step with them off, and in the engine from before synth4). Measured over 10 intervals and 5 patches: at a note change on a sounding voice the output stepped 3-9 times the voice's own pulse edges, 26-34 ms after the pick. Two causes in Felucca's formant `note_on`: (1) it takes a voice for a fresh one when `v->env` and `v->env_out` are zero (the adapter keeps this engine's envelope in its own floats), so it zeroes the phases and the whole filter state `s[0..6]` under the sound (the adapter restored only the phases); (2) it re-picks the random vowel `s[7]`, moving every formant target in one block. Fix (`px_s2_noteon`): restore `s[0..6]`; the new random vowel becomes a target the vowel slides to over 15-30 ms, so a new note still gets a new vowel. Worst step now 1.3-1.7x the voice's own edges on every patch (`tests/engine/choir_click_test.c`, 50 cases; it fails 35 of them on the old adapter). An H1-style block-boundary declick was tried first and removed: it treated a symptom and changed nothing.

## Not done
Heard on the pedal; the level of the head start and the release bound are choices (constants above); `tests/equiv.sh` was not re-run, so the INSTRUMENT / ENSEMBLE hashed runs are expected to differ from older builds.
