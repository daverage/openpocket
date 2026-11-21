# ENHANCE (SFX): the Better Box, a compander harmonic enhancer

Status: **v5.1**, host-tested; **not heard, not measured on the pedal; every parameter is a guess** fitted to the maker's words. An SFX type (id after the retired APPLAUSE tombstone). Two controls like the Better Box it follows: **BETTER** 0-100 and **LOUDER** 0-100 (a clean 0..+12 dB boost). See also `docs/PEDALS.md` (the other analogue emulations).
History: v1/v2 modelled the NE570 gain cell's tiny static distortion (inaudible, builds 924/926); v3 was an Aphex-style exciter on a fixed curve (build 927: no audible difference); v4 was a compander with a knee at -28 dBFS, which only worked on the loudest notes. v5 has no knee. v5.1 (owner feedback: good, but "unnatural clipping, like digital interference" as BETTER nears 100): the harmonics and the intermodulation between the notes of a chord come from the same gain ripple, so at the full depth the junk between two notes was only 3 dB below the second harmonic (-23 dB re the notes). The detector attack is now 0.3 ms (0.1 ms slammed the gain down within a fraction of a cycle on a pick: a limiter edge) and the top 30 % of the knob is flattened: BETTER up to 70 is exactly as before, 100 stops at 0.745 of the full depth (`PXE_KNEE`, `PXE_TOP`).

## What the maker says (not a circuit)
TWA/Godlyke pages: an NE570 compander, "even and odd order harmonics **without distortion** / without diode clipping", "emphasizes even-order multiples of the fundamental being played, with a light dusting of specific odd-order harmonics in the upper registers", "subtle compressor", "evens out frequency response and creates punchy, tight pick attack"; BETTER "adjusts even/odd harmonic content". Nothing says how.

## What v5 is (the hypothesis)
A compander gain cell works at every level: its control is the rectified signal (in the log domain), so the gain follows the level as level^-1/2, the chip's classic 2:1. A gain controlled by a FAST rectified copy of the signal also modulates the signal with its own ripple: half-wave detection ripples at f (2f: even harmonics), full-wave at 2f (3f: odd). Per sample, no libm:
`d = 0.5|x| + max(x,0)` -> `env` (attack 0.1 ms, release 2 ms) -> `g = 1 + B (sqrt(ENV0 / (env + FLOOR)) - 1)` -> `y = x g` -> 10 Hz DC block -> LOUDER. `B = BETTER/100` up to 0.7, then rising slowly to 0.745 at 100; attack 0.3 ms, release 2 ms; `ENV0 = 0.03` (unity gain at about -30 dBFS), `FLOOR = 0.003` (the gain stops rising 10 dB above unity so silence and noise are not raised without limit). Constants: `PXE_ENV0`, `PXE_FLOOR`, `PXE_MIX` in `engine/px_enhance.h`.

## Measured (host, 200 Hz sine, BETTER 100)
| amplitude | gain | H2 | H3 |
|---|---|---|---|
| 0.003 | +5.7 dB | -27.1 dB | -36.3 dB |
| 0.01 | +3.0 | -23.8 | -32.9 |
| 0.03 | -0.2 | -23.1 | -32.2 |
| 0.08 (guitar level) | -2.9 | -23.8 | -32.8 |
| 0.20 | -5.2 | -25.2 | -34.2 |
The second harmonic is level independent (about -24 dB; v5 had -20 dB at the full depth). A chord (199 + 252 Hz, `tests/engine/enhance_test.c`): the energy that is not a harmonic, sum or difference of order <= 3 of the two notes is -27.8 / -27.4 / -27.0 dB re the notes at BETTER 70 / 85 / 100 (it was -22.8 dB at 100, rising steeply over the top of the knob), and the energy above 5 kHz fell from -70 to -85 dB. Loudness against bypass on the five guitar loops: -0.3 dB at BETTER 100.

## Tests
`tests/engine/enhance_test.c` (in `tests/run_tests.sh`): catalogue, unity, silence, no knee (quiet notes processed), H2 range and dominance over H3, a falling gain, H2 rising with BETTER, no DC, LOUDER +12 dB, bounded on square / 3x / noise, no click on switching or a knob jump, stored APPLAUSE bypassed. Study tools: `tests/enhance_sim.mjs` + `tests/enhance_sim_measure.py` (the simulator's engine on the bundled loops), `tests/enhance_reamp.py` (re-amp A/B on the pedal).

## Open
1. Everything is a guess: detector mix, times, ENV0, the 2:1 law, how BETTER maps. The only real Better Box audio is the owner's earlier clips (no clean on/off).
2. ENV0 is in engine units: guitar peaks about 0.14 or 0.07 (unresolved), which moves it by 6 dB.
3. CPU on the BlackBox: not measured (`node tools/op.mjs dsp`).
