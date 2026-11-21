# ENHANCE (SFX): a compander harmonic enhancer

Status: **v4**, host-tested; **not heard, not measured on the pedal; every parameter is a guess** fitted to the maker's words. An SFX type (id after the retired APPLAUSE tombstone), so it cannot run with another SFX. Two controls, like the Better Box it follows: **BETTER** 0-100 (amount of harmonics and of compression) and **LOUDER** 0-100 (a clean 0..+12 dB boost, readout in dB).
History: `docs/ENHANCE-RESEARCH.md`. v1/v2 modelled the NE570 gain cell's tiny static distortion (inaudible, builds 924/926); v3 was an Aphex-style exciter on a fixed curve (build 927: the owner heard no difference between WARM/BOTH/BRIGHT; the simulator on the bundled loops shows why: no loudness change, under 3 dB in the top bands). Both lacked what the maker describes.

## What the maker says (not a circuit)
TWA/Godlyke pages: an NE570 compander, "even and odd order harmonics **without distortion** / without diode clipping", "emphasizes even-order multiples of the fundamental being played, with a light dusting of specific odd-order harmonics in the upper registers", "subtle compressor", "evens out frequency response and creates punchy, tight pick attack"; BETTER "adjusts even/odd harmonic content". Nothing says how.

## What v4 is (the hypothesis)
A gain cell whose control is a **fast rectified copy of the signal itself** modulates the signal with its own ripple: a half-wave detector ripples at f (so the output has 2f: even), a full-wave one at 2f (3f: odd). It clips nothing, compresses above a knee, and a fast attack follows the pick. Per sample, no sqrt/exp/log:
`d = 0.5|x| + max(x,0)` (an equal mix of full- and half-wave: the guess) -> `env` (attack 0.1 ms, release 2 ms) -> `g = 1 / (1 + S * max(env - THR, 0) / THR)` -> `y = x * g`, a 10 Hz DC block, then LOUDER. `S = 0.6 * BETTER/100`, `THR = 0.04` (-28 dBFS in engine units; clean below). About 40 bytes of state. Constants: `PXE_THR`, `PXE_SMAX`, `PXE_MIX` in `engine/px_enhance.h`.

## Measured (host, sine, BETTER 100)
| 200 Hz amplitude | gain | H2 | H3 |
|---|---|---|---|
| 0.01 (-40 dBFS) | -0.0 dB | below -100 | below -100 |
| 0.03 | -0.2 | -35.8 | -38.7 |
| 0.08 (about guitar level) | -4.1 | -16.2 | -25.5 |
| 0.20 | -10.6 | -14.8 | -23.9 |
Offline prototype (`/tmp`-only, not kept): detector full-wave -> H2 below -110 dB (odd only); half-wave -> H2 -18 / H3 -29 dB; the 50/50 mix -> H2 -21.7 / H3 -31 dB. The owner's own H2 is about -19 dB re the fundamental, so at BETTER 100 the second harmonic of a note is roughly doubled in level and the note is compressed about 4 dB: a thickening and levelling, not a tone change.

## Engine integration
`engine/px_enhance.h`; `PX_SFX_ENHANCE` after `PX_SFX_APPLAUSE` (ids unchanged); catalogue BETTER / LOUDER. Tests (`tests/engine/enhance_test.c`, in `tests/run_tests.sh`): catalogue and text, unity, silence, clean below the knee, H2 range and H2 dominating H3, falling gain, H2 rising with BETTER, no DC, LOUDER +12 dB, bounded on square / 3x / noise, no click on switching or a knob jump, stored APPLAUSE bypassed. Study tools: `tests/enhance_sim.mjs` + `tests/enhance_sim_measure.py` (the simulator's engine on the bundled loops), `tests/enhance_reamp.py` (re-amp A/B on the pedal; its selftest targets the v3 renderer).

## Open (needs ears and the pedal)
1. Everything above is a guess: detector mix, times, knee, slope. The only real Better Box audio is the owner's earlier clips (no clean on/off); a capture or DI A/B of a real unit would fit them.
2. Knee in engine units: guitar peaks about 0.14 or 0.07 (unresolved), which moves the knee by 6 dB.
3. A BETTER that moves the detector mix (even/odd balance) is a guess; v4 uses it only for amount.
4. CPU on the BlackBox (`node tools/op.mjs dsp`): not measured.
