# Synth2 stage F: ENSEMBLE, the INSTRUMENT patches played by a stack of voices

Status: host tested, built (test build 914: `PocketX_914` / `BlackBox_914`). **Not flashed (the pedal is on 912), not heard, pedal CPU not measured.**

## Naming

SYNTH type 31 was called ENSEMBLE while it held single instruments (one voice per tracked note). It is now **INSTRUMENT** (display name only: id 31, its sound and its saved presets are unchanged). The name **ENSEMBLE** is the new type, id 32, which plays those instruments with several voices.

## What it is

One note from the mono tracker drives up to five voices of the same INSTRUMENT patch. A **VOICING** (a fixed stack table, `PX_STACKS` in `engine/px_synth_patches.h`) gives each voice an interval in semitones, a small fixed detune, a level and an attack stretch. This is how the original v61 STRING / BRASS ENSEMBLE worked (one tracked note, an octave layer and detuned saws), with Felucca's engines in place of the sampled voices. It is **not polyphonic tracking**: a chord you play gives the one note the tracker finds, with the voicing's intervals above and below it; chord quality comes from the VOICING, not from your fingers.

| Setting | Meaning |
|---|---|
| PATCH | one of the ten INSTRUMENT patches (bands of 10: SOFTPAD, STRINGS, BRASS, SLOWSTR, BRSLEAD, ORGAN, JAZZORG, ROCKORG, REEDORG, SOFTORG) |
| VOICING | one of ten stacks in bands of 10: UNISON (centre + two drifting copies), OCT UP, OCT DN, OCTAVES (-12, 0, +12), FIFTH (0, +7), POWER (-12, 0, +7), MAJOR (0, +4, +7), MINOR (0, +3, +7), MAJ 7 (0, +4, +7, +11), OPEN (-12, 0, +7, +12, +16) |
| SPREAD | the detune and the drift of each voice: x0 (none) .. x1 (50, as written) .. x3 (100) |
| TONE | as INSTRUMENT: the filter of the ANALOG patches, the drawbar tilt of the organs |
| LEVEL | as the others (0 silent, noon 0 dB, 100 +6 dB) |

Patch and voicing slots are final once defined (the identity rule). The patch's own ATTACK and RELEASE are as written (INSTRUMENT's ATTACK and RELEASE settings are not in ENSEMBLE).

## The adapter (`engine/px_synth_adapter.h`)

`NVOICE` is now 5 (was 1). The per-voice part of the control block is a function run once per voice (`px_s2_voice`, `px_s2_noteon`); voice 0 uses exactly the old state and arithmetic and every other type runs voice 0 alone. **Bit-identical to the previous commit: ANALOG, ARCADE, ARCADE SPLIT and INSTRUMENT, 120 hashed renders; the protected effects, 937 lines.**
Details that matter: the voices' levels are normalised so their powers add up to one voice's (per-stack trims then set the level, -18 to -20 dBFS gated RMS on the loops); Felucca's per-block step (WHEEL's bar gains and rotors) runs once per block, not once per voice; WHEEL strikes its percussion once per note, on the first voice; a hard damp or a release acts on every voice; a smaller stack silences the voices it no longer plays.

## The beating, and why each voice drifts

Copies of a note detuned by a fixed number of cents beat, and on low notes the beat is slow and periodic: that is the phaser-like swell the user reported on ANALOG. Stacks of fixed detune were measured worse than the single voice (UNISON 8-12 dB peak swing against 3-4 dB for INSTRUMENT STRINGS). Each voice therefore also has its own slow random pitch wander (a deterministic filtered xorshift, 8 cents rms at SPREAD noon, about 1.5 Hz): the beating becomes irregular. Measured on steady notes (82, 165, 330 Hz), the strongest autocorrelation of the 10 ms RMS envelope at lags of 0.25-2 s ("periodicity"; 1 is a pure repeat): INSTRUMENT STRINGS 0.67-0.82 (the single voice the user called less phasey), stacks without drift 0.46-0.82, with drift 0.15-0.5. The test (`ensemble_suite`) requires under 0.6 and under 15 dB peak swing; it fails 12 checks with the drift off. **The peak swing of the stacks is still 6-14 dB** (three voices sometimes cancel): this is a chorus-like fluctuation, not a periodic swell, but whether it sounds right is for the ear. SPREAD 0 removes all of it (the voices then sum coherently).

## Cost

Host, ns per 64-frame call with the tracker (noisy at this size): INSTRUMENT STRINGS 2 425 (1 voice), ENSEMBLE STRINGS 1 978-2 552 (2-5 voices: the ANALOG voices are cheap), INSTRUMENT ORGAN 2 063, ENSEMBLE ORGAN MAJOR 2 892 (3 voices), OPEN 3 685 (5 voices): each WHEEL voice adds about 0.4 us. Target: RAM code 62 224 B (23 784 B free), low RAM 12 352 B (+240 B), openpocket.bin 222 744 B. The v61 measurement on the pedal was 2-3 % of a core per layer for the old saws; the new voices are not measured on the pedal: read the DSP meter with OPEN on the organ (the worst case).

## Verified (host)
`synth_test.c` (ensemble_suite): the names; every voicing's intervals are present and a note not in the chord is not (ten voicings, STRINGS on 110 Hz); 600 patch x voicing x spread x tone combinations finite and bounded (peak 0.81); silence in, silence out; deterministic and call-size independent; irregular beating and no deep dips (above); every voice releases after the guitar stops, and a hard mute stops all five (ORGAN OPEN, STRINGS OPEN: 30 dB down within 400 ms); patch and voicing switched while playing stay finite. Listening files: `build/ensemble-stageF/` (nine patch and voicing pairs on six guitar sources, plus the guitars).

## Limits
One note only (see above); a chord gives the tracker's one note (it can jump between chord tones, as before); the voices' attack stagger is a stretch of the patch's attack, not a delay; no per-voice vibrato (only the drift); the cost on the pedal is unknown.
