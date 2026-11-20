# Synth2: final report (s1)

Branch `synth2`, from `synth` at `06358805ab94d486ad25bab1a2536cab3acf5804`. Test package: `PocketX_901.fwsc` / `BlackBox_901.fwsc` (`make package-synth2`), SHA-256 `f94c4864…30f0c2` (Pocket X) and `6c0f0deb…13dc85` (BlackBox), `build/SHA256SUMS-synth2`. Nothing has been flashed. Nothing has run on a pedal.

## What exists

| Thing | State |
|---|---|
| POLY OCTAVE HQ (OCTAVE block) | kept; 936 hashed runs (single notes, dyads, chords, transients, bass, loops; all control sets; chains; wet lanes; 4 call sizes; preset changes) **bit-identical to v73**, and every other block alone (408 runs) identical |
| POLY OCT FUZZ (SYNTH block) | kept; same harness, identical |
| Duo tracker, Pulse/Saw/Arcade/FM Poly, String Duo, acoustic voices and the other 22 SYNTH types | removed; their 27 ids are tombstones: bypassed, stored type and settings kept, skipped by the UI, the editor and MIDI (`docs/research/synth2/SYNTH2-DEPENDENCIES.md`) |
| Mono tracker | the existing one (`px_track.h`) kept; Cycfi Q BACF fails the rule on every set (`docs/research/synth2/SYNTH2-TRACKER.md`); two ranges (guitar 60 Hz, bass 38 Hz), interface `engine/px_mono.h` |
| ANALOG (SYNTH id 28) | Felucca ANALOG, 10 patches, 5 macros (`docs/research/synth2/SYNTH2-ANALOG.md`) |
| ARCADE (SYNTH id 29) | Felucca LOFI, 10 patches, MIX against the guitar (`docs/research/synth2/SYNTH2-ARCADE.md`) |
| Polyphonic texture (stage 5) | **not built.** It would need listening to show it adds anything; no new algorithm was invented for it |
| BACF/SOLA octave experiment | not run (optional, and the HQ octave is not in doubt) |

## What was tested, and where (these levels are not interchangeable)

| Claim | Source inspected | Host tested | Simulator / WASM tested | Target compiled | Pedal measured | Heard |
|---|---|---|---|---|---|---|
| HQ octave and fuzz unchanged | yes | **bit-identical** (936 + 408 runs) | the sound test passes on the WASM build | yes (links, call-range check) | no | no |
| Retired ids safe | yes | all 256 byte values: bypassed bit for bit, settings kept; MIDI slices; editor logic | WASM: a retired type is the block off | yes | no | no |
| Tracker choice | yes | five data sets, same scoring as before (GuitarSet, Guitar-TECHS electric DI, IDMT bass) | no | yes | **no** | no |
| ANALOG, ARCADE: pitch, retrigger, release, bounds, call-size independence | yes | synthetic plucks (smoke test) and the bundled loops | WASM plays both | yes | no | **no** |
| Felucca engines unchanged | yes | upstream's golden hashes: 58 renders identical | (same source) | yes | no | no |
| Resources | yes | host ratios only | - | **measured from the link map** | no CPU figure | - |

## Resources (v73 baseline against s1)

| Resource | v73 | s1 | Change | Remaining |
|---|---|---|---|---|
| openpocket.bin (XIP code and constants) | 315 976 B | 214 368 B | -101 608 B | |
| RAM code window: code | 61 896 B | 60 964 B | -932 B | **25 040 B free** (was 24 112; the 4 KB reserve holds) |
| Low RAM | 13 104 B | 12 044 B | -1 060 B | 12 532 of 24 576 B |
| Pool tail before the RAM code | 90 928 B | 103 848 B | +12 920 B | 8 KB reserve holds |
| RAM | 44 876 B | 44 092 B | -784 B | 54 212 B free |
| Cache RAM | 0 | 0 | | 28 672 B |
| CPU0 / CPU1 worst case | not measured | **not measured** | | the 70 % target is **unverified** |

Host CPU (this machine, not the pedal): ANALOG or ARCADE costs about 1.5-2 µs per 64 frames, a third of POLY OCT FUZZ's 5.6-6 µs; the tracker is about 1.1-1.5 µs of it and Felucca's render about 0.4 µs. The earlier pedal measurement of this tracker was about 20 % of a core (`docs/archive/NOTE-TRACKING.md`), so the pedal figure is likely to be dominated by the tracker; it must be measured (`docs/SYNTH2-HARDWARE-TEST.md`).

## Decisions made on the way (change any of them)

* Preset ids: never renumbered; ANALOG and ARCADE are appended as 28 and 29; the factory default SYNTH type is now POLY OCT FUZZ (off) instead of the retired ORGAN.
* PATCH is a 0-100 value in fixed bands of 10; the other four settings are macros around the patch (50 = as written). The brief's names were kept: ANALOG PATCH / CUTOFF / RESONANCE / ENVELOPE / LEVEL, ARCADE PATCH / CHARACTER / SWEEP / DECAY / MIX.
* ANALOG has no dry-guitar blend (LEVEL only); ARCADE has MIX. SYNTH BASS is not a type: the bass sounds are patches 40-79 of ANALOG (an octave down).
* MIDI model slices count live types only (docs/MIDI.md, docs/pxmidi.js, `px_midi.h`, one test each side).
* SFX `SQUARE SYNTH` and `SQUARE SYNTH HQ` (other block, same tracker) were left alone.
* The Felucca adapter lives in `engine/`, in the engine's translation unit, so the pedal, the host tools and the WebAssembly simulator run the same synthesis code. The WebAssembly source hash now covers the vendored Felucca files.

## Open, and what stops me

* **Everything about how it sounds and plays**: wrong notes, octave jumps, missed attacks, stuck notes, release, aliasing, CPU artefacts. These need the pedal (`docs/SYNTH2-HARDWARE-TEST.md`), which I did not and may not flash.
* A synthetic E1 in the bass range is unstable on the host (21 retriggers in 2 s); real bass DI scores better. The 38 Hz range is not wired to any patch, and v71's guitar regression at that floor is still unexplained.
* The first 10-80 ms of a low note can carry a wrong octave (host, synthetic notes); the synth follows the corrected pitch.
* Level trims of the 20 patches are first guesses from the host loops.
* A release version number: the default build still says v73 (`openpocket.c`); do not publish s1 as 73. Only the 901 test channel was built.
* Playwright is not installed here, so the page tests were skipped (as in the baseline).

## Commits (logical stages)

`be902bc` stage 0 plan and baseline, test channel; `01dd003` protected-effect harness; `3bdca5d` stage 1 clean sweep; `feed88f` stage 2 tracker; `c5dc415` stage 3 ANALOG; `d80bda2` stage 4 ARCADE; then stage 6 consolidation.
