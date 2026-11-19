# OpenPocket Synth2: plan and baseline

Branch `synth2`, created from `synth` at `06358805ab94d486ad25bab1a2536cab3acf5804` (v73 source plus the String Duo bench commit).
`synth` stays unchanged as the reference. A detached worktree at that commit (`../openpocket-ref-synth`) is the reference build
for equivalence runs. Nothing here is flashed automatically.

## Identity

Synth2 builds are a test channel: `make package-synth2` (OP_CHANNEL=synth2, OP_TEST_VERSION=1) writes
`docs/firmware/synth2/PocketX_901.fwsc` and `BlackBox_901.fwsc`, boot screen `v1 SYNTH2`. Release files in `docs/firmware/` are not touched.
Later builds raise OP_TEST_VERSION (s1 = 1, s2 = 2, ...). The pedal's package identity is `PocketX_9NN`; releases so far are 9xx 24..73, and
901 is not a published release, but a pedal flashed with 901 reports a lower number than v73; the installer does not compare versions (checked in docs/pxota.js).

## Baseline (source SHA above, measured by `make build` in the pinned container; host tests `make test` exit 0, page tests skipped: no Playwright)

| Resource | v73 baseline |
|---|---|
| openpocket.bin | 315 976 B (package 675 968 B) |
| RAM code window | code 61 896 B, data 0, **free 24 112 of 86 016 B** |
| Pool tail | 1 024 B, 90 928 B before the RAM code |
| Low RAM | 13 104 of 24 576 B |
| RAM | 44 876 of 98 304 B (53 428 free) |
| Cache RAM | 0 of 28 672 B |
| CPU0 / CPU1 worst case | not measured on a pedal in this session |

The RAM code window is not the 568 B of the v57 notes: it has 24 KB free at v73. The brief's reserve targets (4 KB RAM-code window, 8 KB pool)
are checked against these figures at every stage.

## Preset and type policy

* Preset format unchanged: TYPE + 5 params (0..100) per block, block ids unchanged.
* SYNTH enum: existing ids are never renumbered or reused. `PX_SYN_OCT_FUZZ` keeps its id. Every other legacy id becomes a reserved
  tombstone: the block resolves to bypassed (type OFF behaviour), the stored type and parameters are kept as they are until the user
  saves a new type. New Synth2 types are appended after `PX_SYN_STRING_DUO`.
* No `type % PX_SYN_COUNT` reinterpretation: an id outside the catalogue is also a tombstone.
* Synth2 types: ANALOG (patches: Saw Lead, Pulse Lead, PWM Lead, Square Bass, Deep Sub, Resonant Bass, Acid Bass) and ARCADE
  (Felucca LOFI). PATCH is a 0..100 value in fixed bands, so a patch keeps its band when more are added.

## Protected effects (Stage 1 must keep them bit-identical)

* POLY OCTAVE HQ (`PX_PIT_POLYOCT_HQ`, OCTAVE block): `erb_octaves2`, `erb_bank`, `erb_halfband`, `erb_layout`, `erb_prepare`, the half-band filters, ERB band state.
* POLY OCT FUZZ (`PX_SYN_OCT_FUZZ`): `erb_octaves`, `fuzz_setup`, `syn_fuzz_reset`, `drive_buffer`, oversampling filters (`px_hb`), FUZZ_FACE voicing, tone and output gain.
* Both share `erb_*`, so neither can go with the retired voices. The detailed per-symbol map is in `docs/SYNTH2-DEPENDENCIES.md`.

## Stages

0 baseline and plan (this file) -> 1 clean sweep (tombstones, delete Duo and the retired voices, protected equivalence harness) ->
2 mono tracker (existing `px_track.h` against Cycfi Q BACF on the host) -> 3 Felucca ANALOG -> 4 Felucca LOFI ->
5 optional polyphonic texture -> 6 consolidation and the hardware test package (not flashed).

Every result is labelled: source inspected / host tested / simulator tested / target compiled / pedal measured / user listening approved.

## Stage 1 result: clean sweep (source inspected, host tested, target compiled; not run on a pedal)

Removed: the Duo tracker and allocator, Pulse/Saw/Arcade/FM Poly, String Duo, the acoustic voices and their sample bank, and every other SYNTH type
except POLY OCT FUZZ. Retired ids are tombstones (docs/SYNTH2-DEPENDENCIES.md). The SYNTH block's type list in the UI, editor and MIDI skips them.

| Resource | v73 baseline | Stage 1 | Change |
|---|---|---|---|
| openpocket.bin | 315 976 B | 198 748 B | -117 228 B |
| RAM code window: code | 61 896 B | 53 164 B | -8 732 B |
| RAM code window: free | 24 112 B | 32 840 B | +8 728 B |
| Pool tail, left before the RAM code | 90 928 B | 103 848 B | +12 920 B |
| Low RAM | 13 104 B | 8 592 B | -4 512 B |
| RAM | 44 876 B | 43 996 B | -880 B |
| CPU0 / CPU1 worst case | not measured | not measured | (no pedal) |

Checks run: `tests/protected.sh 0635880` (POLY OCTAVE HQ and POLY OCT FUZZ, 936 runs, and every other block alone, 408 runs: all bit-identical),
`tests/engine/syn_ids_test.c` (all 256 ids), `make test` (exit 0; page tests skipped: no Playwright), `make wasm`, `make package-synth2` (PocketX_901).
Changed on purpose: the factory SYNTH default type (ORGAN, off, is now POLY OCT FUZZ, off), `px_render`'s synth argument is now a type id, MIDI model slices
count live types only, `tests/test_audio.mjs` used id 18 for "fuzz" (it was CELLO; fixed to 16).
