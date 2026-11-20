# Synth2: the hardware test package (not flashed)

Nothing in this branch has been installed on a pedal. This is what to do, in order, and what to write down. Flashing is yours to start: the build only prepares files.

## The package

`make package-synth2` writes `docs/firmware/synth2/PocketX_901.fwsc` and `BlackBox_901.fwsc` (version 1, boot screen `v1 SYNTH2`, SHA-256 in `build/SHA256SUMS-synth2`). It is a test channel: `docs/firmware/` (the installer's default, v73) is untouched.
The recovery safeguards, loader, boot guard, codec set-up and dual-core scheduling are the ones of v73 (nothing in them changed; `make test` includes the package, loader and update-path tests against the simulated pedal).
Read `.claude/skills/openpocket-flash/SKILL.md` first. Dry run before any write: `node tools/op.mjs flash --dry-run --package docs/firmware/synth2/<your model>_901.fwsc` (the `manifest.json` next to it is the one built with it). Keep the SD-card recovery (docs/RESCUE.md) at hand. The release identity v73 is not reused: this build reports 901.

## What to try (about 20 minutes)

1. Boot and the SYNTH block's list: POLY OCT FUZZ, ANALOG, ARCADE only. A preset saved on v73 with another SYNTH type shows RETIRED TYPE and the block is bypassed; its settings are untouched.
2. POLY OCTAVE HQ and POLY OCT FUZZ against v73 by ear (they are bit-identical on the host; the pedal's codec and the second core are the same code, so they should sound the same).
3. ANALOG on a single-note lead (high strings) and on a low-string riff, patches 0-9 (PATCH 5, 15, ... 95). Listen for: wrong notes, octave jumps (a wrong first 10-80 ms on low notes is known), missed attacks, stuck notes after muting, a late release, gaps, clicks on patch change, aliasing.
4. The same with a slide, a bend, vibrato, a repeated note and a palm mute. The synth should glide with the string, restart on a repick, and stop when you mute.
5. ARCADE: ARCADE, CHIP, 8BIT BS, WAVE, STEP, ARP, ZAP, PLUCK, NOISE, MIN ARP. Judge whether the chip character is the one you like; MIX at 50 layers the guitar.
6. A realistic chain: gate, comp, POLY OCTAVE HQ (OCTAVE block), ANALOG or ARCADE, modulation, delay, reverb. Look for dropouts and the overload bypass.
7. CPU: with the pedal on USB, `node tools/op.mjs dsp --seconds 10` (the DSP's load, late blocks and bypass per preset, over USB; see its options for choosing presets) and, playing while it runs, `TYPES="ANALOG,ARCADE,POLY OCT FUZZ,PITCH:POLY OCTAVE HQ" node tools/core2_probe.mjs 10` (the load and whether the second core still runs for each type). The goal is a worst case below about 70 % on either core configuration; nothing has been measured on a pedal yet.
8. Switch presets and patches while playing.

## What to write down for the report

Per sound: wrong notes / octave jumps / missed attacks / stuck notes / gaps / glitches (yes, no, how often), the CPU readings, whether ANALOG's trims and the Arcade character are right (these were set on the host against the bundled loops), and whether you want a dry-guitar blend on ANALOG and the 38 Hz bass range on a bass patch.
A DI recording of the guitar and the pedal's output for the same take would let the host tracker be compared with what the pedal played (docs/research/synth2/SYNTH2-TRACKER.md: pedal validation is open).
