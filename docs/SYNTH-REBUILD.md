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

## Stage 3: the synth layout and the buttons

Nine blocks in signal order, numbered as the engine runs them: 0 GATE, 1 COMP, 2 OCTAVE (the pitch block), 3 SYNTH,
4 SFX, 5 EQ, 6 MOD, 7 DELAY, 8 REVERB. A preset is 86 bytes now, down from 110. The store slot keeps its 112
bytes and the footswitch byte stays in its place.

| Button | Opens | Light |
| --- | --- | --- |
| AMP | SYNTH | on while SYNTH is on |
| CAB | OCTAVE (hold: STOMP mode, as before) | on while OCTAVE is on |
| FX | INPUT and TONE in chain order: GATE, COMP, SFX, EQ (hold: MIDI page) | on while any of them is on |
| MOD, DLY, REV | MOD, DELAY, REVERB | as before |

The play screen shows the SYNTH type and the OCTAVE type under the preset number. The tiles are 5 + 4. In
STOMP mode, holding a footswitch and pressing FX steps through GT, CMP, SFX, EQ, BYPASS and nothing.

MIDI keeps its scheme by block index: on/off 20..28, type 52..60, toggles 8..16, and the settings in the same
runs as before (`docs/MIDI.md` is regenerated from `docs/pxmidi.js`).

Editor: protocol 6, 9 blocks. The Models section (NAM captures, cabinet IRs, TONE3000) is gone, along with
`pxlib.js`, `pxnam.js`, `pxir.js`, `pxt3k.js` and `edmodels.js`. Preset files no longer convert v33 types or
v34 delay times. PING carries one load figure per block.

Fixed along the way: `engine/wasm.sh` wrote nothing to `px_engine.wasm.sources`, because a comment sat inside
its pipeline (stage 2). The hosted-simulator test now polls for the tuner instead of sleeping for a fixed time.

`tests/run_tests.sh` passes. The browser page tests (editor page, pedal skin, installer page) were not run,
because Playwright is not installed here. They still describe the old layout.

| | v70 stage 2 | v70 stage 3 |
| --- | ---: | ---: |
| Flash image | 323,432 B | 323,768 B |
| Main RAM free | 44,652 B | 44,828 B |
| RAM-code window free | 20,424 B | 20,000 B |
