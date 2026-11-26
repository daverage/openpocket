# Synth2 stage 3: ANALOG (Felucca) on the guitar

Status: source inspected, host tested (synthetic plucks, recorded loops through the engine), target compiled and linked. **Not run on a pedal, not listened to by anyone.**

## What it is

SYNTH type 28, `ANALOG`: one note, followed from the guitar by the mono tracker (docs/research/synth2/SYNTH2-TRACKER.md), played by Felucca's ANALOG engine
(two band-limited oscillators, noise, drive, a trapezoidal state-variable filter; MIT, `third_party/felucca-engines`, pinned upstream `d0260074`).
Not polyphonic: it plays one note at a time, the tracker's. Nothing here transcribes chords.

| Setting | Meaning |
|---|---|
| PATCH | one of 10 patches in fixed bands of 10 (0-9 CLASSIC, 10-19 FATSAW, 20-29 PULSE, 30-39 PWM, 40-49 SQ BASS, 50-59 SUB, 60-69 MOOG, 70-79 ACID, 80-89 SWEEP, 90-100 SYNC) |
| CUTOFF | the patch's filter cutoff, +-63 of Felucca's 127 steps around it (50 = as written) |
| RESONANCE | the patch's resonance, the same way |
| ENVELOPE | attack and release times x1/4 .. x4 around the patch's (50 = as written) |
| LEVEL | output level: 0 silent, noon 0 dB, 100 +6 dB (as POLY OCT FUZZ) |

The five bytes are the whole sound: no hidden state (`px_synth_adapter.h: px_s2_set`). Saved and loaded, they play the same patch (`tests/engine/synth_test.c`: same settings, same output, bit for bit).
Patches are table entries (`engine/px_synth_patches.h`): Felucca's ANALOG parameters, envelope, filter-envelope amount, a transposition and a level trim. The bass patches play an octave below the guitar's note.
They are sound-design targets built from Felucca's ANALOG engine, not models of any vintage instrument.

## The adapter (`engine/px_synth_adapter.h`)

* The tracker runs on the **clean guitar** (after the DC blocker, before the gate, the octave and every other effect, whatever the effect order is). The synth's output replaces the signal where the SYNTH block stands in the order.
* Every 32 input samples (Felucca's block) the adapter turns the tracker's pitch, onset, retrigger, release and pick strength into Felucca's voice and block modulation, renders 32 samples, and returns them one block later.
  That is a fixed 32 samples (0.73 ms) on top of the tracker's own latency; it makes the result independent of the call size (checked for 64, 48, 16 and 1 frame calls: bit-identical).
* Pitch is continuous: 1/16 semitone plus Felucca's fine residual from the tracker's Hz, smoothed over about 1.5 ms. Bends, slides and vibrato are not snapped to semitones (a 3 s E3-G3 slide is followed within 40 cents in the test).
* A new note restarts the envelope from the level the voice has now (no click); a repick or slur retriggers a sounding voice and keeps its phases and filter (Felucca's rule); the end of the note starts the release.
* Dynamics: pick strength sets the level (0.35 .. 1) and the string's own decay shapes it (x0.6 .. 1).
* Fixed point stays inside Felucca: Q15 amplitude, int32 render, one conversion to float per block at the output. No conversion of the guitar input.
* The tracker's range is the guitar's (60 Hz). The bass range of the same tracker (38 Hz) is not wired to any patch: it is for a bass guitar, it is unvalidated on a pedal, and a patch cannot ask for it yet.

## Verified (host)

* Upstream's own golden renders for the vendored engine: 43 ANALOG renders identical (`third_party/felucca-engines/tests/felucca_ref.sh`).
* Pitch within 20 cents on plucked E2, E3, E4 (CLASSIC, and an octave down for SQ BASS); a slide is followed; a repick re-enters; the note releases after the guitar stops (-40 dB or more within 2.5 s of the end of the guitar).
* 810 combinations of patch and macros (0 / 50 / 100): no NaN, peak at most 1.2 (full resonance and +6 dB LEVEL can exceed 1.0: the engine's output is not limited here); silence in gives exact silence out.
* The other blocks, POLY OCTAVE HQ and POLY OCT FUZZ are still bit-identical to v73 (`tests/protected.sh`).

Output level on the bundled loops, gated RMS against the guitar's -15 dBFS: CLASSIC -16.5, FATSAW -17, PULSE -17.6, PWM -17.5, SQ BASS -14 to -17, SUB -19 to -14, MOOG -16 to -19, ACID -16 to -20, SWEEP -17, SYNC -17.5 dBFS (these depend on the notes the tracker follows).
The trims are first guesses to be set by ear.

## Resources (stage 3 against stage 1)

| Resource | Stage 1 | Stage 3 | Change |
|---|---|---|---|
| openpocket.bin (XIP code and constants) | 198 748 B | 211 072 B | +12 324 B (Felucca's tables are about 3.5 KB of it) |
| RAM code window: code / free | 53 164 / 32 840 B | 59 804 / 26 200 B | +6 640 B; 26 KB free (the 4 KB reserve holds) |
| Low RAM | 8 592 B | 12 036 B | +3 444 B of 24 576 (the tracker's and the synth's state) |
| Pool tail left / RAM | 103 848 / 43 996 B | 103 848 / 44 092 B | unchanged |
| CPU, host (this machine, ns per 64 frames, riff loop) | POLY OCT FUZZ 5 600-6 000 | ANALOG 1 800-2 000 (tracker 1 500, Felucca 400) | a ratio, not a pedal figure |
| CPU, pedal | not measured | **not measured** | needs the pedal; the earlier measurement of this tracker was about 20 % of a core (docs/archive/NOTE-TRACKING.md) |

The 70 % worst-case target is therefore **not demonstrated**: the pedal's counters must be read with ANALOG on, in a full chain with octave, modulation, delay and reverb.
The felucca tables are read from flash (XIP); whether that stalls the audio path was not measured.

## Known limits and open decisions

* No dry guitar in the mix: the block plays the synth only (the brief's control set has no mix). Say if you want a blend.
* One tracker range. Bass guitar below B1 needs the 38 Hz range, which has not been tried on the pedal since v71's guitar regression.
* The note's first frames can be a wrong octave on low notes (docs/research/synth2/SYNTH2-TRACKER.md): the synth follows the corrected pitch, so a wrong first 10-80 ms is possible. Only listening will say how much it matters.
* The synth rings its release when the string is still ringing but the gate has fallen: the tracker holds the gate through most gaps, so a quick staccato may sound longer than played.
