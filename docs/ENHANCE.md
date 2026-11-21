# ENHANCE (SFX): an NE570-inspired clean harmonic enhancer

Status: host-tested and built into a test package (`PocketX_923` / `BlackBox_923`, channel `enhance`, not flashed), **not heard, not measured on the pedal.** An SFX type (id after the retired APPLAUSE tombstone), so it cannot run together with another SFX. Two controls: **BETTER** 0-100 (default 35: the share of added harmonics) and **LOUDER** 0-100 = 0 to +12 dB clean boost (shown in dB).

## What it is
A behavioural model of two documented NE570 gain-cell properties (research package "openpocket-ne570-prototype", GPL-3.0-or-later, reviewed and integrated; datasheet figures quoted there were not re-checked against the onsemi PDF here): the gain cell's offset gives an approximately **parabolic** (second-harmonic) error, and the full-wave rectifier's ripple (time constant 10 kOhm x C, here a virtual 70 nF = 0.7 ms) modulates the gain, which adds a weak, frequency-dependent **third harmonic**. The effect is a parallel path: the dry signal is never clipped or filtered; the sidechain is low-passed (3.5 kHz), DC-servoed (20 Hz) and low-passed (7 kHz) and blended back. It is not a traced Better Box circuit and nothing here has been compared with a real one.
Cost: one float state of 72 bytes, 6 one-pole filters and a few multiplies per sample, no buffers, no FFT, no pitch tracking, no latency in the dry path. Not profiled on the BlackBox.

## What changed from the research prototype (and why)
The prototype calibrates the gain cell's virtual 0 dBm to **0.5 digital peak**. Real guitar at the pedal is far quieter (`tools/calib_levels.py`: hard playing about -17 dBFS = 0.14 at the converter). Measured on the prototype's own code at realistic levels the added signal was **-57 dB re dry at the default and -48 dB at BETTER 100 on the owner's DI clips (peaks -14 dBFS)**: inaudible. `PXE_REF` (default 3.5) puts the gain cell at 3.5 x the signal (hard playing at the pedal reaches the virtual 0.5) and scales the added part back by 1/3.5: the dry path and unity gain are unchanged; `PXE_REF 1.0` reproduces the prototype exactly (its `results.txt` reproduces bit for bit). The sidechain input is guarded at +-1 so a hot signal cannot blow up the excitation.
Measured with PXE_REF 3.5 (RMS of wet minus dry re dry, active frames): owner clips -46 dB (BETTER 35) / -37 dB (100); Guitar-TECHS scales -48 / -39; hot IDMT material -37 / -27. 110 Hz sine at peak 0.14, BETTER 100: H2 1.6 %, H3 0.8 %, fundamental unchanged (0.05 dB); 20 dB less level gives 10x less H2 (an amplitude law: harder playing is richer).
Intermodulation (equal two tones, combined peak 0.14): difference and sum products -46 dBc at BETTER 35 and -37 dBc at BETTER 100 (about 5 dB above the harmonics, as any x^2 term gives). Chords at BETTER 100 on hard playing may sound congested: that needs listening.

## Engine integration
`engine/px_enhance.h` (header only); `PX_SFX_ENHANCE` appended after `PX_SFX_APPLAUSE` (existing ids unchanged); catalogue entry with two parameter names; the setup case sets the targets (the filter state is kept on a knob move: the mix and level glide over 10 ms); the processing case runs per sample in the SFX loop; the state is initialised in `px_engine_init` and whenever the SFX type or its on/off changes (so entering or leaving the effect starts from unity with clean filters, no click); `SFX_DB` trim 0 dB (unity by design); LOUDER displays in dB. MIDI model slices and the editor count live types generically, so the new type shifts the SFX model slices like any added type.
Tests: `tests/engine/enhance_test.c` (in `tests/run_tests.sh`): the catalogue, unity at 0/0, silence, even over odd harmonics, H2 of 1-3 % at the pedal's hard-playing level and the amplitude law, fundamental within 0.2 dB, LOUDER +12 dB, bounded and finite for a full-scale square and input above full scale, no click on switching or a knob jump, a stored APPLAUSE still bypassed.

## Firmware build (test channel `enhance`, version 23; not flashed)
The container build links, including its RAM guard. Same build recipe, three commits (RAM code in the 86016-byte window; image size):
| build | RAM code | free window | image |
|---|---|---|---|
| test build 922 (ddc5d0c, APPLAUSE) | 65660 B | 20344 B | 235488 B |
| APPLAUSE removed | 63324 B | 22680 B | 231340 B |
| + ENHANCE (this branch) | 63448 B | 22560 B | 232180 B |
APPLAUSE's removal frees 2336 B of RAM code and 4148 B of image; ENHANCE costs 124 B of RAM code and 840 B of image. Net against 922: 2212 B more free in the code window. Data RAM is unchanged (45716 of 98304 B). The packages are in `docs/firmware/enhance/` (untracked). **CPU on the BlackBox is not measured**: run `node tools/op.mjs dsp` on a connected pedal with ENHANCE on in the worst-case chain before trusting the under-3 % estimate.

## Open (needs ears and the pedal)
Level-matched listening on real DI guitar (`tests/fasttrack/level_match.py`) against a clean boost and a conventional exciter; chord clarity and intermodulation at high BETTER; pick attack and sustain; base-rate versus 2x oversampled processing (the rectifier is not alias-free at base rate); the real callback load in the worst-case chain; the right BETTER law and default. Do not describe it as a Better Box match until measured against one.
