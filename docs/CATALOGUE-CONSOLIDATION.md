# SYN catalogue consolidation (v63, poly-test)

The phase 2 plan asks for one good implementation per sound. Each
replacement must pass regression and listening comparisons before the
older code is removed. v63 measures each old/HQ pair. It retires only the
one pair where the HQ version wins on every measure, and only in the
`poly-test` channel. The listening comparisons are still to be done, so no
code has been removed and the standard firmware is unchanged.

## Measurement

`tools/sim/syn_compare.py` renders each type fully wet, with the other
knobs at noon, through `tools/sim/ensemble_bench.c` on 20 GuitarSet takes.
It measures the share of the output energy, 60 Hz – 4 kHz, that lies within
30 cents of the harmonics of the notes the guitarist played. Harmonics are
taken from the type's lowest voice: ½ for the organs, strings and choir, ¼
for BASS SYNTH, 1 for PWM. Solo takes use the middle half of each note of
at least 150 ms. Comping takes use 93 ms windows with the same two or more
notes held throughout. Host time is the render time for the 20 takes.

| Pair | Solo notes on pitch (p25) | Chords on pitch (p25) | Host time | Decision |
| --- | --- | --- | --- | --- |
| ORGAN / ORGAN HQ | 58.6% (36.6) / 58.4% (38.0) | 50.4% (41.0) / 51.2% (40.2) | 0.75 / 1.23 s | listen: equal on pitch, HQ costs more |
| CHURCH ORGAN / HQ | 62.1% (39.9) / 59.2% (38.6) | 56.1% (45.1) / 53.0% (39.4) | 0.98 / 1.31 s | listen: original measures cleaner |
| STRINGS / STRINGS HQ | 63.1% (46.8) / 62.1% (45.9) | 66.4% (57.7) / 66.1% (56.5) | 0.88 / 1.18 s | listen |
| CHOIR / CHOIR HQ | 65.3% (50.1) / 65.2% (51.5) | 70.4% (60.7) / 71.7% (62.0) | 0.97 / 1.42 s | listen |
| PWM SYNTH / PWM SYNTH HQ | 54.7% (29.1) / 49.2% (7.1) | 43.6% (31.8) / 34.1% (6.6) | 0.66 / 1.06 s | keep the original; HQ tracks one note and misses chords |
| BASS SYNTH / BASS SYNTH HQ | 36.5% (12.8) / **40.2% (15.4)** | 26.5% (13.4) / **30.1% (13.5)** | 0.67 / 1.11 s | **retire BASS SYNTH in poly-test** |

This score counts energy at pitches the player did not play, such as
intermodulation, wrong octaves and tracking errors. It does not capture
the delay-line shifter's warble or level wobble. That is why the
organ/strings/choir HQ versions exist, and why those pairs need listening.
`tools/sim/octave_banks.c` measures wobble for the HQ bank alone.

## What v63 changes (poly-test only)

`PX_CONSOLIDATE_TEST` maps `PX_SYN_BASS` to `PX_SYN_BASS_HQ` in
`px_type_canonical` and gives BASS SYNTH an empty catalogue name.
TRUMPET MODEL was retired the same way. The store (`st_get`), the editor
(`px_editor.h`) and the engine already canonicalise types on load, so:

- A saved preset with BASS SYNTH plays BASS SYNTH HQ. The five controls
  (OCT -1, OCT -2, CUTOFF, ENV, DRY) mean the same, so nothing in flash
  is rewritten.
- The pedal's MODEL list and the web editor skip unnamed types.
- `node tools/poly_pedal_check.mjs BlackBox_963 consolidated` sends a type-6
  preset and checks that it comes back as BASS SYNTH HQ.

## Still to decide (needs listening)

- Organs, strings and choir: pick one implementation each. Equal on-pitch
  scores favour the cheaper original unless the HQ warble fix is audible.
- PWM SYNTH: keep the comparator original for chords. MONO SYNTH, now on
  the note layer (v60), may serve as the tracked mono pulse.
- SQUARE SYNTH / SQUARE SYNTH HQ (SFX block) and Poly Octave / Poly Octave HQ
  (PITCH block): not yet measured with this score.
- MONO SYNTH: switch the standard build to `PX_MONO_LAYER` and delete
  `syn_track` once v60 has been heard.
