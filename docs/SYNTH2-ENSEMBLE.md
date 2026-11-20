# Synth2 ENSEMBLE, Stage A: instrument patches on the ANALOG engine

Status: source inspected, **host tested**, target compiled and linked (test package 907). **Not flashed, not run on a pedal, not listened to by anyone.**
Fix pass 1 (builds 902-906) answered a first pedal test (docs/SYNTH2-FIXPASS1.md); no hardware result for the fixed builds is recorded in the repository, so this stage does not assume they were approved.

## What it is

SYNTH type 31, `ENSEMBLE` (after ARCADE SPLIT = 30; no id is reused). **Mono guitar-controlled synthesis**: one note at a time, the tracker's
(docs/SYNTH2-TRACKER.md), played by Felucca's ANALOG engine through the same adapter as ANALOG. A chord does not make a string section; a pad
patch plays the one note the tracker follows. Nothing here is polyphonic.

| Setting | Meaning |
|---|---|
| PATCH | one of the patches below, in fixed bands of 10 values (`slot = value / 10`) |
| TONE | the patch's filter cutoff, +-63 of Felucca's 127 steps around it (50 = as written) |
| ATTACK | attack time x1/4 .. x4 around the patch's (50 = as written) |
| RELEASE | release time x1/4 .. x4 around the patch's (50 = as written) |
| LEVEL | output level: 0 silent, noon 0 dB, 100 +6 dB (as ANALOG) |

| Slot | PATCH values | Name | Source |
|---|---|---|---|
| 0 | 0-9 | SOFTPAD | Felucca ANALOG preset `SOFT PAD`, parameters as upstream |
| 1 | 10-19 | STRINGS | Felucca ANALOG preset `STRINGS`, as upstream |
| 2 | 20-29 | BRASS | Felucca ANALOG preset `BRASS`, as upstream (the filter opens on each note) |
| 3 | 30-39 | SLOWSTR | variation of STRINGS: ATK 70 -> 100, REL 90 -> 105, CUT 70 -> 62 |
| 4 | 40-49 | BRSLEAD | variation of BRASS: ATK 35 -> 15, REL 45 -> 35, CUT 45 -> 55, RES 20 -> 25, filter envelope 40 -> 48 |
| 5-9 | 50-100 | reserved | play slot 4 until filled |

**Patch identity rule.** The table is the whole sound: the five stored bytes reproduce it (`tests/engine/synth_test.c`: same bytes, same output, bit for bit,
for any call size). Capacity is 10 slots. A defined slot is never reassigned, reordered or renamed; new patches go into slots 5-9 only. Until then values 50-100
read and sound as BRSLEAD, so a preset saved with PATCH 70 will change when slot 7 is filled: **choose PATCH 0-49 in presets you want to keep**.
Felucca's own chorus / reverb sends and pattern hints in those presets are not used; MOD, DELAY and REVERB follow the SYNTH block in the chain.

## Changes

* `engine/px_dsp.h`: `PX_SYN_ENSEMBLE` (31), live and mono. `engine/px_dsp.c`: name, five control names, patch-name text.
* `engine/px_synth_patches.h`: `PX_ENSEMBLE_PATCHES`, `px_ens_slot`. `engine/px_synth_adapter.h`: ENSEMBLE mapping of the five settings. The ARCADE test was
  `type != ANALOG` and is now an explicit `ARCADE || ARCADE SPLIT`; ANALOG and ARCADE code paths are otherwise untouched.
* The pedal screen, the web editor and MIDI read type names, parameter names and value text from the engine / wasm, so no separate table needed editing. MIDI CCs are the generic per-setting ones (docs/MIDI.md).
* `tools/sim/px_engine.wasm` rebuilt. Test channel `package-synth2` version 7 -> `PocketX_907` / `BlackBox_907` in `docs/firmware/synth2/` (never the installer default).

## Verified (host)

* `tests/engine/synth_test.c` (ENSEMBLE suite): pitch within 25 cents on E2, E3, E4 for all five patches; follows a slide; sounds and releases (no stuck note: the pad, strings and brass tails fall, and RELEASE 0 ends within 2.8 s of the guitar stopping);
  ATTACK 100 slower than 0 (0.18 s -> 0.36 s to half level, measured from the pick); RELEASE 100 longer than 0 (to -20 dB: 0.73 s -> 3.7 s); deterministic; call-size independent; 810 patch x macro combinations finite, loudest peak 0.96, silent on silence.
* **ANALOG, ARCADE and ARCADE SPLIT are bit-identical to HEAD (858ce76)**: 90 hashed renders (3 types x 10 patches x 3 macro settings), old tree against new.
* Protected harness: the working tree equals HEAD on all 937 lines of `tests/engine/protected.c` (POLY OCTAVE HQ, POLY OCT FUZZ, every other block). `tests/syn_ids_test`: ids 28-31, tombstones unchanged.
* `make test` passes (page tests skipped: Playwright is not installed here). `make wasm`, `make package-synth2`, `make ble` build.
* Output level, gated RMS in dBFS on the bundled loops (synth block only, all macros at 50): lead / riff / chords
  CLASSIC (ANALOG) -17.7 / -16.7 / -18.9; SOFTPAD -19.5 / -19.8 / -19.3; STRINGS -18.9 / -18.9 / -18.8; BRASS -18.4 / -17.8 / -19.6; SLOWSTR -20.0 / -20.9 / -19.9; BRSLEAD -18.2 / -17.2 / -19.4.
  The slow patches sit 2-3 dB under CLASSIC; the trims are untouched upstream-faithful first guesses, to be set by ear.
* Comparison audio: `build/ensemble-stageA/` (each patch and ANALOG CLASSIC / SUB on the three loops, plus the guitar). Not committed.

## Resources (test build 907)

| Resource | Value |
|---|---|
| openpocket.bin | 216 576 B (stage 3 ANALOG: 211 072 B; the 906 figure was not kept) |
| RAM code window | 61 512 B code, 24 496 B free of 86 016 (stage 3: 59 804 / 26 200) |
| RAM | 44 108 of 98 304 B; pool tail left 103 848 B (unchanged) |
| CPU, host | same engine and adapter as ANALOG (docs/SYNTH2-ANALOG.md); no separate measurement |
| CPU, pedal | **not measured**; inferred only to be ANALOG's class, which was itself not measured on the pedal |
| Latency | unchanged: tracker + one fixed 32-sample block |

## Findings and open points

* **`tests/protected.sh 0635880` reports POLYOCT_HQ differing at HEAD.** It differs with or without this change (HEAD and the working tree hash identically), so it arrived with the earlier Synth2 commits (RANGE / LOWEST OCTAVE HQ work, 0f07f7c..b841a6c). It was not investigated here. `protected.sh` also cannot take a Synth2 commit as REF: its archive omits `third_party/`.
* `engine/px_render` / `make render` writes silence for ANALOG, ARCADE and ENSEMBLE at HEAD as well (the host renderer; the tests and wasm are fine). Not looked into; comparison audio was made with a small renderer that sets the preset up as the tests do.
* The pad and strings release is long by design (Felucca REL 90-95: a time constant of about 1.4-1.7 s at RELEASE 50). Brass was told not to chirp: it rides the pitch-confirmation wait from fix pass 1; only listening can say whether the brass attack is breath-like.
* Not done in Stage A, as instructed: WHEEL, PHASE, TRIO, the full-chord research. No other upstream engine was imported.

## For the listening test

Flash `docs/firmware/synth2/PocketX_907.fwsc` (or BlackBox) only on your say-so (`make flash` with `--dry-run` first). Set SYNTH to ENSEMBLE and try each patch at PATCH 5, 15, 25, 35, 45, then TONE, ATTACK and RELEASE. Judge: the brass attack, repeated notes (no stuck or doubled note), slides and bends, release on a hard mute, loudness against ANALOG and ARCADE, and whether the pads feel sluggish to play.
