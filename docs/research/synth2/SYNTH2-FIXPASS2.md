# Synth2 fix pass 2 (test build 908): hard-mute re-trigger, ENSEMBLE beating

Status: host tested, built (`PocketX_908` / `BlackBox_908`). **Not flashed, not heard.** Reported on the pedal with build 907: "hard mute on all synths can trigger another note" and "they all sound like a heavy phaser".

## 1. A hard mute plays another note (all mono synths)

Cause (reproduced on the host): after the string is damped the tracker keeps the note open for about 120 ms. A muting thump in that window is read as a new pick, and the tracker finds a junk pitch in it (193, 481, 98 Hz in the test). Before the fix a thump 13 dB under the note re-triggered.

Fix, in the mono wrapper only (`engine/px_mono.h`; the shared tracker `px_track.h` and the octave effects are untouched):
* The damping is detected: the fast level (1 ms attack, 10 ms release) falls 20 dB below the last 150 ms's peak. The voice is **released at once** (about 28 ms after the damping, where it used to wait for the gate to fall, about 120 ms).
* Until the note ends, a "new note" is accepted only if its 6 ms attack peak is within -10.5 dB of the level before the mute (300 ms memory); a rejected one leaves the tracker exactly as it was (pitch, state, velocity).
* Limit: a thump louder than about -10 dB under the note looks like a real pick and still plays. A thump that arrives after the gate has fallen starts a new note as before (not changed).
Test: `tests/engine/synth_test.c`, thumps of about -19, -16 and -13 dB after a hard mute on 110 and 196 Hz: no re-trigger, released within 60 ms.
This applies to ANALOG, ARCADE, ARCADE SPLIT and ENSEMBLE, which share the wrapper.

## 2. "A heavy phaser" (ENSEMBLE; and a cause to check on ANALOG and ARCADE)

Measured, not assumed: on a perfectly steady note the ANALOG engine's two oscillators, detuned by DTN cents and mixed about equally, beat at 1-3 Hz; the output's peak level swings 5-10 dB (CLASSIC 5.4 dB, PULSE 8.9, FATSAW 5.2; SOFTPAD / STRINGS / BRASS about 8 dB). With DTN at 0 it falls to about 1.5 dB. Felucca expects its chorus send to hide this; OpenPocket's SYNTH block has no chorus after it.
* ENSEMBLE: the second oscillator's MIX is 16 instead of 64 on all five patches (DTN as written). Swing on a steady note: 3-4 dB. Test: under 5 dB on every patch at 82, 165 and 330 Hz.
* **ANALOG is not changed** (its patches are the user's accepted tones): CLASSIC, FATSAW, PWM, SWEEP and others have the same beating. Say if you want the same treatment (a smaller second oscillator) there.
* **ARCADE at MIX 50** adds the clean guitar to the synth. The synth is the tracker's latency (about 5-20 ms) plus one 32-sample block late, at the same pitch, so the two comb-filter against each other: a flanger/phaser sound that is not in the synth. MIX 100 (synth only) removes it. Not changed here; to be judged by ear. ARCADE's first patch also has a deliberate 5.4 Hz vibrato (VIB 20).
* Steady-note ARCADE pitch is clean (0.3 cents rms) with the vibrato off, so the tracker is not adding wobble on a clean synthetic note. Real playing was not measured.

## Verified (host)
`make test` (the two sweep2 problems are the same two that FIXPASS1 records as failing before it); protected harness 937 lines identical to HEAD; ANALOG / ARCADE / ARCADE SPLIT 90 hashed renders identical to HEAD; `make wasm`, `make package-synth2`, `make ble`. RAM code 61 892 B, 24 112 B free. Pedal CPU not measured.

# Fix pass 3 (test build 909): the pitch moves up on a hard mute

Pedal result of build 908 (user): the stray note on a hard mute was still there every time (ANALOG, ARCADE, ENSEMBLE); a gentle damp did not trigger; a bleep every few picks on chugs and tremolo; **on a hard mute the note moves up about a semitone** and the ring that follows is at the new pitch. ENSEMBLE: less or no phasing on BRASS, SOFTPAD, BRSLEAD, a little on SLOWSTR; ANALOG CLASSIC still phasey (not changed); ARCADE at MIX 100 no phase.

Finding: fix pass 2 chased a re-trigger that the synthetic thumps produced. The real cause of the "semitone up" is not an onset: while a damped string's level collapses, the tracker keeps following its strongest component, which drifts. On the Guitar-TECHS DI palm-mute recordings (P2) a note's tracked pitch stepped 60-83 cents up inside the note with no onset, and sometimes an octave or more away. A synthetic string sharpening 100 cents while damped reproduces it: the tracker followed it by +110 cents (110 Hz), +110 (196), +53 (330).

Fix (`engine/px_mono.h`): once a note has settled (40 tracker frames, 58 ms after it spoke), its pitch is held while the fast level is under half of the last 150 ms's peak, until the level recovers or a new note speaks. Held-pitch synthetic test: worst error -2, +3, +10 cents. Real recordings (143 / 134 notes with MIDI truth, `directinput_PalmMute.wav` P1 / P2): notes whose pitch was off by more than 60 cents in the first 0.5 s 2 -> 1 (P1) and 11 -> 4 (P2); notes with a pitch step inside them 0 -> 0 and 7 -> 1; onsets found 128 / 120 of 143 / 134 and unchanged. Thresholds tried: 0.5 and 0.3 of the peak, settle 40, 100 and 200 frames (a hold from the first frame hurt P1: it blocks the early octave corrections).

Not fixed, and known from the same data: the tracker reports extra onsets on palm-muted playing (20 and 105 extra against 143 and 134 real notes) and a few notes speak at a wrong pitch (4 of 97 on P2): these are the likely "bleeps" on chugs and tremolo, and they were there before fix pass 2. The mute detector of fix pass 2 doubles the number of releases on palm-muted playing (the voice ends at each damped chug), which is what a damp should do, but may shorten palm-muted notes more than wanted.

## Fix pass 3b (test build 910): the bleeps, and the ring after a hard mute

Input: the user's own DI recording `My Audio/bleep.wav` (21 s, 44.1 kHz, palm mutes, chugs, tremolo, hard mutes; not committed) and the Guitar-TECHS DI sets with MIDI truth (palm mutes P1 and P2, all single notes).

**Findings from the recording.** 48 of its 54 tracker onsets sit behind a real pick (level rise): the bleeps are not phantom notes. They are picks where the note speaks the wrong pitch for a short time and corrects itself: 78 % of the onsets spoke a pitch more than 50 cents from where the note settles (often an upper harmonic: 528 Hz on an 82 Hz string), for 28 ms on average and up to 100 ms. Cause: a note below 200 Hz spoke after two stable tracker frames (3 ms), while the pick transient is still dominated by upper partials.
Second finding: after a hard mute the synth did not stop. ANALOG CLASSIC stayed near -25 dB for over a second after the guitar was silent (its release time constant is about 470 ms; the pad and strings patches are longer).

**Fixes.**
* `PX_MONO_STAB 10`, `PX_MONO_WAIT_LO 14`, `PX_MONO_MINWAIT 20` (`engine/px_mono.h`; two optional fields on the note layer in `engine/px_track.h`, default 0 = unchanged, so the octave effects are untouched): a note speaks after its pitch has been stable for 10 tracker frames (14.5 ms), and not sooner than 14 frames (20 ms) after the pick, 20 frames above 200 Hz (was 17).
  Grid on bleep.wav and the three truth sets (stable frames / wait low / wait high -> wrong-pitch onsets on bleep.wav; hit rate on P1 palm / P2 palm / single notes; median and 90th-percentile latency change against the old setting):
  old 2 / 0 / 17: 78 %; 73 / 67 / 71 %; 0 / 0 ms. 8 / 0 / 17: 34 %; 85 / 90 / 88 %; 0-3 / +2-8 ms. **10 / 14 / 20: 19 %; 87 / 91 / 96 %; +2-4 ms median, +9-16 ms 90th percentile.** 12 / 20 / 24: 16 %; 89 / 93 / 96 %; +7-10 median, +16-26 p90. 16 / 20 / 24: 15 %; 90 / 93 / 96 %; +9-13 median, +25-34 p90.
  The first wrong pitch spoken on a truth note went from 16 / 23 / 18 % to 10 / 9 / 2 %. The cost is a later note start (10-20 ms): the user should say if it feels slower.
* A hard damp now stops the voice with a 40 ms release (`PX_S2_DAMP_MS`, `engine/px_synth_adapter.h`) whatever the patch's release. On the recording the synth output after a mute is under -80 dB within 300 ms (ANALOG CLASSIC and ENSEMBLE STRINGS; before: -25 dB for more than a second).

**Left.** 19 % of the onsets on the recording still speak briefly at a wrong pitch (6 of 32); 90-96 % of the notes on the truth sets are found at the right pitch. The ARP patch test now samples 60 windows instead of 20, because the later start shifts which arpeggio steps fall in the old window. Chords still jump between chord tones (not looked at). Pedal CPU not measured; RAM code 62 188 B, 23 816 B free.

## Fix pass 4 (test build 911): ANALOG's detuned patches

At the user's request the ENSEMBLE treatment (a smaller second oscillator) is applied to the ANALOG patches that beat: CLASSIC, FATSAW, PULSE, PWM, SWEEP, MIX 16 (was 64, 64, 40, 30, 64). The peak-level swing on a steady 110 / 196 / 330 Hz note falls from 5.4-6.3 / 5.9-6.1 / 8.8-10.1 / 4.8-6.6 / 7.0-7.7 dB to 2.7-3.0 / 2.5-2.9 / 3.1-4.4 / 3.4-3.9 / 3.8-4.1 dB. The detune is as written. Their trims are lowered by the loudness the change adds (CLASSIC 64 -> 54, FATSAW 56 -> 47, PULSE 60 -> 52, PWM 60 -> 56, SWEEP 60 -> 49) so the levels the user accepted stay (gated RMS within about 0.3 dB). Not changed: SQ BASS, SUB, MOOG, ACID, SYNC (no detune beating: their level swings are the resonant filter, or the sync mode's own pair), and all ARCADE types. Verified: only those five ANALOG patches differ from the previous commit (15 of 90 hashed renders; the other 75 identical); protected effects identical (937 lines); `make test`; a new swing test (SWEEP allowed 6.5 dB: its filter envelope is slow by design).

## Fix pass 5 (test build 913): the organ's bleeps and blips

User result (build 912): the organ has the worst bleeps and blips; all patches sound good.

Why the organ shows them most: it starts at full level at once (its first 15 ms are within 1 dB of its body, against 29 dB down on STRINGS), so any wrong or false note start is fully audible. Measured on `My Audio/bleep.wav`, three causes, all in the mono wrapper (`engine/px_mono.h`; the shared tracker and the octave effects are untouched):
1. **A new note killed on its own start (4-6 % of notes).** The mute detector judged a new, softer note against the previous loud note's decaying peak and damped it at once, and the adapter let a release on the same sample cancel a new onset. 2 of 45 note starts on bleep.wav, 8 of 141 and 5 of 173 on the Guitar-TECHS palm-mute sets. Fixed: a started note is judged against its own level and never triggers a damp on its own sample, and a new onset wins over a release (`px_synth_adapter.h`). Zero cases now.
2. **Dead picks.** Nearly all the short notes (under 80 ms) start when the sound is already 15-40 dB below the pick's own attack peak, usually at a high junk pitch (660, 540, 483 Hz). A note start whose fast level is under 0.1 x its attack peak (-20 dB) is dropped and the pitch stays where it was. Threshold 0.1 chosen from 0 / 0.1 / 0.2 / 0.3: short notes on bleep.wav 9 -> 5 and on P2 49 -> 40; wrong first pitch on the truth sets P1 10 -> 5 %, P2 9 -> 7 %; hits P1 87 -> 85 %, P2 91 -> 90 %, single notes 96 -> 95 % (0.2 would lose real palm-muted notes: P1 73 %).
3. **Harmonic-ratio starts.** A start whose pitch is x2, x3, x4 or /2, /3, /4 of the note played in the last 800 ms is the tracker locking on an upper partial of the transient (corrected within 3-20 ms). It is held back 35 ms with the previous pitch kept. Wrong-pitch starts on bleep.wav 6 -> 4 of 32; the truth sets unchanged except 90th-percentile latency +6 ms on P2.
Tests (`synth_test.c`): a dead burst plays no note; 160 random damped picks never release a note on its own onset sample (both fail on the 912 tracker).

Still open: soft pick within about 120 ms after a hard damp is rejected as a thump (by design); about 1 start in 8 on this recording still speaks briefly at a wrong pitch; chords. Comparison audio of the organ on bleep.wav, 912 against 913: `build/ensemble-stageB/bleep_*_912.wav` and `_913.wav`. Pedal CPU not measured; RAM code 62 828 B, 23 176 B free.

## Fix pass 6 (test build 917): dynamics, slurs and glide, as the original synths had them

Why: the review of the original firmware (docs/research/TRACKING-AND-SYNTH-REVIEW.md) found that its synths hid tracking errors by *how the voice behaved*: the level followed the pick strength over 18 dB, a pitch change in a ringing note was a 12 ms glide, and a re-detected pick never restarted the note. The user asked for the glide and the dynamics.

* **Dynamics** (`engine/px_synth_adapter.h`): `lvl = exp((expr - 0.90) x dyn_dB x 0.115)`, capped at x1.6, with `expr` the note layer's expression (the pick strength, falling with the string's own decay). The default range is 18 dB; the organs have 6 dB and ARCADE 8 dB (`px_patch.dyn` overrides; 0 = the type's default). The reference 0.90 keeps hard picks at the old level: gated RMS on the loops is within +-1.2 dB of build 916 for every type (mean +0.09 dB), the loudest peak -4 dBFS (CHOIR / ENSEMBLE organ), and softer picks play quieter. Test: a pick 24 dB softer is 12.7 dB quieter on STRINGS, 4.2 dB on the organ, 5.6 dB on ARCADE.
* **Slur** (`engine/px_mono.h`, `px_mono_out.slur`): a pick re-detected while the note rings is a re-attack only if its strength is more than 0.12 above the note's expression **as it was 70 ms before** (the layer's own has already risen with the pick by the time it speaks: a first version used it and classed a real second note as a slur); otherwise the pitch moves, the layer's reference is left as it was and no onset goes to the synth (no new attack, no WHEEL click or percussion, no TALK restart). On the Guitar-TECHS sets and the user's recording it **never gives up a real re-attack** (0 slurs near a true note, margins 0 to 0.2) and neutralises 6 junk retriggers; the 33 junk starts on the soft palm-mute set are not retriggers but the softest picks arriving from silence, which the dynamics law quietens.
* **Glide**: the pitch of a ringing note follows each new tracker value over 12 ms (`PX_S2_GLIDE_MS`, the original MONO's) instead of about 1.5 ms; a note starting from silence takes its pitch at once.
* The soft attack floor for pads and strings asked about in the review needs nothing: with Felucca's own envelope (fix of build 915) their attacks are already 160 ms or more.

Measured with the same loudness test as the review (notes under 80 ms within 12 dB of the loud notes, the synth output's peak): the user's recording 5 -> 3 audible short notes (34 -> 26 notes), P2 palm mutes (soft junk) 29 -> 14, P1 52 -> 52 (loud palm-muted notes are short by nature). Tests (`legato_suite`): the dynamics range per patch kind; a very soft pick on a ringing note is a slur and a hard pick a new attack; a 7-semitone change glides (largest step in a 0.7 ms block under 1 semitone; 2.7 with the old smoothing): the last two fail with the slur rule off or the old smoothing. `make test`, the protected effects (937 lines) and the target build pass; RAM code 62 888 B (23 120 B free), low RAM 13 420 B (+0.8 KB: the expression history). **Not heard, pedal CPU not measured.** Existing sounds change on purpose (every synth's level now follows the pick strength).
