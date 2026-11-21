# ENHANCE (SFX): the Better Box, a compander harmonic enhancer

Status: **v5**, host-tested; **not heard, not measured on the pedal; every parameter is a guess** fitted to the maker's words. An SFX type (id after the retired APPLAUSE tombstone). Two controls like the Better Box it follows: **BETTER** 0-100 and **LOUDER** 0-100 (a clean 0..+12 dB boost). See also `docs/PEDALS.md` (the other analogue emulations).
History: v1/v2 modelled the NE570 gain cell's tiny static distortion (inaudible, builds 924/926); v3 was an Aphex-style exciter on a fixed curve (build 927: no audible difference); v4 was a compander with a knee at -28 dBFS, which only worked on the loudest notes. v5 has no knee.

## What the maker says (not a circuit)
TWA/Godlyke pages: an NE570 compander, "even and odd order harmonics **without distortion** / without diode clipping", "emphasizes even-order multiples of the fundamental being played, with a light dusting of specific odd-order harmonics in the upper registers", "subtle compressor", "evens out frequency response and creates punchy, tight pick attack"; BETTER "adjusts even/odd harmonic content". Nothing says how.

## What v5 is (the hypothesis)
A compander gain cell works at every level: its control is the rectified signal (in the log domain), so the gain follows the level as level^-1/2, the chip's classic 2:1. A gain controlled by a FAST rectified copy of the signal also modulates the signal with its own ripple: half-wave detection ripples at f (2f: even harmonics), full-wave at 2f (3f: odd). Per sample, no libm:
`d = 0.5|x| + max(x,0)` -> `env` (attack 0.1 ms, release 2 ms) -> `g = 1 + B (sqrt(ENV0 / (env + FLOOR)) - 1)` -> `y = x g` -> 10 Hz DC block -> LOUDER. `B = BETTER/100`, `ENV0 = 0.03` (unity gain at about -30 dBFS), `FLOOR = 0.003` (the gain stops rising 10 dB above unity so silence and noise are not raised without limit). Constants: `PXE_ENV0`, `PXE_FLOOR`, `PXE_MIX` in `engine/px_enhance.h`.

## Measured (host, 200 Hz sine, BETTER 100)
| amplitude | gain | H2 | H3 |
|---|---|---|---|
| 0.003 | +6.9 dB | -25.6 dB | -35.3 dB |
| 0.01 | +3.6 | -22.0 | -31.4 |
| 0.03 | -0.5 | -20.5 | -29.9 |
| 0.08 (guitar level) | -4.5 | -20.0 | -29.3 |
| 0.20 | -8.3 | -19.8 | -29.1 |
The second harmonic is level independent (about -20 dB), the gain falls with level (quiet notes raised 7 dB, loud ones tamed 8 dB). The owner's own H2 is about -19 dB re the fundamental, so at BETTER 100 it roughly doubles.

## Tests
`tests/engine/enhance_test.c` (in `tests/run_tests.sh`): catalogue, unity, silence, no knee (quiet notes processed), H2 range and dominance over H3, a falling gain, H2 rising with BETTER, no DC, LOUDER +12 dB, bounded on square / 3x / noise, no click on switching or a knob jump, stored APPLAUSE bypassed. Study tools: `tests/enhance_sim.mjs` + `tests/enhance_sim_measure.py` (the simulator's engine on the bundled loops), `tests/enhance_reamp.py` (re-amp A/B on the pedal).

## Open
1. Everything is a guess: detector mix, times, ENV0, the 2:1 law, how BETTER maps. The only real Better Box audio is the owner's earlier clips (no clean on/off).
2. ENV0 is in engine units: guitar peaks about 0.14 or 0.07 (unresolved), which moves it by 6 dB.
3. CPU on the BlackBox: not measured (`node tools/op.mjs dsp`).
