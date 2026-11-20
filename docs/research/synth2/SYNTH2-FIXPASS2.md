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
