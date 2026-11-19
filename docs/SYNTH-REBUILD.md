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

## Next round (owner's requests)

- Done: lights. HOME is lit while the pedal plays and blinks in the tuner. With BYPASS on, every light is off.
- Done: the retired types are removed: BASS SYNTH, PWM SYNTH HQ and TRUMPET MODEL (SYNTH), and the delay-line
  POLY OCTAVE (OCTAVE). SYNTH has 28 types now, OCTAVE 6. The 78 named types render bit-identically to before,
  matched by name. The trumpet model stays for the Bluetooth test image, which has no sample bank. Type numbers
  after the removed slots shifted, so a preset saved on an earlier v70 build plays a different type.
- Watch Cello Ensemble, Trumpet and the ensembles in a full chain: one late block and 95-105 % peaks with no
  guitar playing (tools/synth_probe.mjs).
- Install Playwright and update the browser page tests for the 9-block layout.
- Stage 5: tracker research and benchmarks.

## Stage 5: tracker research (in progress)

### Mono trackers, first comparison (GuitarSet development players 00-01, 60 solo takes, 6,420 notes)

`tools/research/mono_compare.py` runs each candidate as published on the same takes and scores it the same way.
Each candidate's output is stamped at the moment it could be known. The neural models are stamped late by their
measured look-ahead: SwiftF0 152 ms, PESTO 62 ms.

| Candidate | Notes right (95 % CI) | Octave errors | Latency median / 90th | Raw pitch acc. | Voicing false alarm |
| --- | ---: | ---: | ---: | ---: | ---: |
| Engine v70 (MONO SYNTH) | 83.6 % (78.3-86.7) | 2.0 % | 7 / 47 ms | 77.2 % | 90.5 % |
| YIN (librosa, 46 ms causal) | 80.0 % | 2.0 % | 16 / 54 ms | 72.9 % | 64.9 % |
| PESTO 2.0 | 75.5 % | 0.6 % | 47 / 76 ms | 62.2 % | 42.2 % |
| pYIN (offline reference) | 73.2 % | 2.5 % | 45 / 87 ms | 65.5 % | 74.7 % |
| Cycfi Q | 72.2 % | 3.0 % | 31 / 71 ms | 65.4 % | 75.5 % |
| SwiftF0 0.3 | 33.7 % | 0.7 % | 138 / 163 ms | 46.4 % | 74.5 % |

What it shows:

- The engine's tracker is the most accurate and the fastest on this data.
- The engine keeps a note sounding through most annotated gaps (voicing false alarm 90 %). Note release is
  the first thing to fix.
- PESTO makes a third of the octave errors. It is a candidate for an octave check, not a replacement.
- SwiftF0 cannot be a pedal tracker: it needs 152 ms of look-ahead.
- GuitarSet is an acoustic guitar. The electric (Guitar-TECHS, CC BY 4.0) and bass (IDMT-SMT-Bass and
  Bass-Single-Track, CC BY-NC-ND 4.0, kept in build/ and never committed) sets are next.
- Range: the engine's tracker covers 60 Hz to 1.4 kHz (B1 to F6) without a semitone grid, so detuned and dropped
  tunings track. Bass E1 (41 Hz) and A1 (55 Hz) are below its floor.

### Mono trackers on electric guitar and bass

Sets (tools/research/prep_sets.py): Guitar-TECHS electric DI, 48 takes and 9,069 notes from E2 to A6 (scales,
single notes, bends, vibrato, palm mutes, harmonics, music); its Triple Play MIDI runs 8 to 26 ms late and is
re-aligned per take. IDMT bass lines: 17 lines, 948 notes. IDMT single bass notes: 960, from all playing and
expression styles. Notes right / octave errors / median and 90th-percentile latency / voicing false alarm:

| Candidate | Electric guitar | Bass lines | Single bass notes |
| --- | --- | --- | --- |
| Engine v70 | **89.5 %** / 1.4 % / **4, 19 ms** / 58 % | 62.8 % / 12.9 % / 43, 83 ms / 80 % | 55.4 % / 4.9 % / 62 ms |
| YIN (causal) | 85.7 % / 1.7 % / 11, 21 ms / 38 % | **85.3 %** / 2.4 % / 34, 73 ms / 32 % | 65.1 % / 0.8 % / 58 ms |
| Cycfi Q | 66.1 % / 1.6 % / 34, 50 ms / 36 % | 40.8 % / 5.2 % / 55, 99 ms | 55.5 % / 2.0 % / 81 ms |
| SwiftF0 | 81.2 % / 0.9 % / 144, 155 ms / 43 % | 30.2 % / 2.2 % / 169 ms | 68.9 % / 0.1 % / 192 ms |
| PESTO | 98.4 % (3 takes only) / 0 % / 52, 59 ms / 20 % | 14.0 % / 0.1 % / 97 ms | 45.4 % / 4.0 % / 107 ms |

Bass split at the engine's 60 Hz floor (notes right / octave errors):

| | E1 to A#1 (under 60 Hz) | B1 and up |
| --- | --- | --- |
| Engine, bass lines | 1 % / 39 % | 90 % / 2 % |
| YIN, bass lines | 82 % / 0 % | 87 % / 3 % |
| Engine, single notes | 5 % / 26 % | 65 % / 1 % |
| YIN, single notes | 64 % / 1 % | 65 % / 1 % |

### Mono decision (development data; the frozen test set is still unscored)

1. Keep the engine's own tracker. It is the most accurate and the fastest on electric guitar (89.5 % of notes,
   4 ms median) and on acoustic guitar. No published candidate beats it at pedal latency.
2. Lower its floor below 60 Hz. Under 60 Hz it fails, getting 5 % of single notes right or fewer and playing an
   octave up instead. Above 60 Hz it matches YIN on bass. The target is 38 Hz (E1 with room for a
   down-tuned bass) without losing the guitar figures.
3. Fix its release. It keeps sounding through 58 % of the gaps after the Guitar-TECHS players' own note-offs,
   against 38 % for YIN and 20 % for PESTO.
4. Neural trackers are not the mono tracker. SwiftF0 needs 152 ms of look-ahead. PESTO is accurate on electric
   guitar with the fewest octave errors, but it adds 62 ms, fails on bass (14 %) and is slow to run. It stays a
   possible offline reference only.
5. Cycfi Q trails everywhere.
6. Single bass notes cap every tracker near 65 %: the set includes dead notes and harmonics, whose labelled pitch
   is not what sounds.
