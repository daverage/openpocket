# OpenPocket Synth rebuild log

The working record for the brief in [neural research.md](neural%20research.md). Branch `synth`, firmware v70.
Nothing here has been flashed to a pedal.

## Stage 1: one engine recipe (commit 9f2d0cf)

The v66 poly-test flags (`PX_LOWEST_TEST`, `PX_PULSE_TEST`, `PX_POLY_TEST`, `PX_ENSEMBLE_TEST`,
`PX_CONSOLIDATE_TEST`, `PX_SUB2_TEST`, `PX_MONO_LAYER=1`, `PX_ERB_LAYOUT2`) are part of the engine source
now. The pedal, the host tests, `make render` and the WebAssembly simulator compile the same engine. The
resonator-test and v53 branches are gone. The v67 experiment switches stay in the source for the tracking
research and are off.

The v70 image keeps the same RAM-code sections as the v66 poly-test build.

## Stage 2: amp, cabinet, drive and NAM removed

Removed: the NAM A2 network and its factory models, the two-core NAM pipeline and layer split, the fitted
amps, the cabinets and their fine structure, user cabinet IRs, the drive pedals, the integer filter path,
the NAM and IR flash stores and editor commands (`ED_NAM_*`, `ED_IR_*` get no reply, `ED_PROTO` 6), and
the model-fitting tools. The DRIVE, AMP and CAB block slots stay in the enum with one empty type until
stage 3 replaces the block layout.

Kept: POLY OCT FUZZ's Fuzz Face clipper and ×4 oversampler (`fuzz_setup`, `drive_buffer`), now on the
shared oversampling buffers.

### Sound

Every remaining block and type renders bit-identically to commit 9f2d0cf (the v66 recipe). The check
covered 9 blocks, 66 types and 3 settings each. POLY OCT FUZZ was checked separately with 75 settings.

### Resources (linker map, PocketX image)

| | v66 poly-test | v70 stage 2 | Change |
| --- | ---: | ---: | ---: |
| Flash image | 488,344 B | 323,432 B | −164,912 B |
| Main RAM used (of 98,304) | 93,768 B | 53,652 B | −40,116 B |
| RAM-code window free (of 86,016) | 312 B | 20,424 B | +20,112 B |
| Pool region free before RAM code | 1,392 B | 91,064 B | +89,672 B |
| Low RAM used (of 24,576) | 23,712 B | 14,496 B | −9,216 B |
| Cache RAM free (of 28,672) | 0 B (NAM vector unit) | 28,672 B | +28,672 B |

The Bluetooth test image builds too: 473,116 B, with 24,328 B of main RAM free.

### Presets

The store is format 6. A copy written by the multi-effects firmware (formats 2 to 5) keeps its MIDI,
USB AUDIO and STOMP settings. Its presets are not converted, so every slot starts as its placeholder.
The first save writes format 6, and later updates keep the saved presets. The 52 factory slots are
placeholders named PRESET 01 to PRESET 52, with the gate on, everything else off and LEVEL at 0 dB.

### Tests

`tests/run_tests.sh` passes. Known issues:

- `tests/engine/sweep2.c` reports 3 problem configurations, down from 4 in the v66 baseline. They
  predate this work.
- `tests/test_sim.mjs` "HOME held 0.7 s" fails about one run in three under load. It holds the button
  for 760 ms against a 700 ms threshold.
- Playwright is not installed here, so the page tests were skipped.
