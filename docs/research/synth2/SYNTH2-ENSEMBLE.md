# Synth2 ENSEMBLE, Stage A: instrument patches on the ANALOG engine

Status: source inspected, **host tested**, target compiled and linked (test package 907). **Not flashed, not run on a pedal, not listened to by anyone.**
Fix pass 1 (builds 902-906) answered a first pedal test (docs/research/synth2/SYNTH2-FIXPASS1.md); no hardware result for the fixed builds is recorded in the repository, so this stage does not assume they were approved.

## What it is

SYNTH type 31, `ENSEMBLE` (after ARCADE SPLIT = 30; no id is reused). **Mono guitar-controlled synthesis**: one note at a time, the tracker's
(docs/research/synth2/SYNTH2-TRACKER.md), played by Felucca's ANALOG engine through the same adapter as ANALOG. A chord does not make a string section; a pad
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
| CPU, host | same engine and adapter as ANALOG (docs/research/synth2/SYNTH2-ANALOG.md); no separate measurement |
| CPU, pedal | **not measured**; inferred only to be ANALOG's class, which was itself not measured on the pedal |
| Latency | unchanged: tracker + one fixed 32-sample block |

## Findings and open points

* **`tests/protected.sh 0635880` reports POLYOCT_HQ differing at HEAD.** It differs with or without this change (HEAD and the working tree hash identically), so it arrived with the earlier Synth2 commits (RANGE / LOWEST OCTAVE HQ work, 0f07f7c..b841a6c). It was not investigated here. `protected.sh` also cannot take a Synth2 commit as REF: its archive omits `third_party/`.
* `engine/px_render` / `make render` writes silence for ANALOG, ARCADE and ENSEMBLE at HEAD as well (the host renderer; the tests and wasm are fine). Not looked into; comparison audio was made with a small renderer that sets the preset up as the tests do.
* The pad and strings release is long by design (Felucca REL 90-95: a time constant of about 1.4-1.7 s at RELEASE 50). Brass was told not to chirp: it rides the pitch-confirmation wait from fix pass 1; only listening can say whether the brass attack is breath-like.
* Not done in Stage A, as instructed: WHEEL, PHASE, TRIO, the full-chord research. No other upstream engine was imported.

## For the listening test

Flash `docs/firmware/synth2/PocketX_907.fwsc` (or BlackBox) only on your say-so (`make flash` with `--dry-run` first). Set SYNTH to ENSEMBLE and try each patch at PATCH 5, 15, 25, 35, 45, then TONE, ATTACK and RELEASE. Judge: the brass attack, repeated notes (no stuck or doubled note), slides and bends, release on a hard mute, loudness against ANALOG and ARCADE, and whether the pads feel sluggish to play.

# Stage B: the WHEEL organ (test build 912)

Status: source inspected, host tested, target compiled and linked. **Not flashed (the pedal is on 911), not heard.** Still mono: one note from the guitar tracker, not a polyphonic organ.

## Evaluation of Felucca WHEEL (`src/eng_wheel.c`, pinned `d0260074`, vendored unmodified)

* **What it needs from the host (source inspection).** Its own per-block step `.block = wheel_block` (bar gains, percussion, click, drive, the two rotors) **must be called once per 32-sample block before the render**, or the engine renders nothing: the adapter never called `.block` (ANALOG and LOFI have none) and now does when the engine has one. Its state is private (`drw_t[NPART]`, `drw_v[NPART][NVOICE]`: phases, rotors, percussion, click; `voice_t` is unused), so the adapter zeroes it with the rest on a reset, and the retrigger's "keep phases" rule does not apply to it (the wheels run free, as designed). It uses `m->inc` for pitch and the adapter's envelope through `voice_amp`.
* **The rotary needs no extra processing.** It is inside the engine: two rotors (a high one above about 800 Hz, a low one below) apply an amplitude and a Doppler pitch modulation to the partials in the per-block gains and increments (no delay line, no per-sample cost). `ROTR` 0 off / 1 slow / 2 fast. Measured on a 110 Hz note: sidebands within 9 Hz of the 8' bar -41 dB (slow) and -36 dB (fast) under it, against -60 dB with it off; the 20 ms RMS swing rises about 1 dB. So it is a **mild** rotary. If a stronger one is wanted, OpenPocket's MOD block (ROTARY) can follow the SYNTH block; not tried.
* **Against a basic organ built from the ANALOG engine** (a filtered square, flat sustain; comparison audio in `build/ensemble-stageB/`): that one has only the odd harmonics (8', 2 2/3', 1 3/5'; every other bar more than 80 dB down), no 16' or 5 1/3' bar, no percussion, no click, no rotary: a clarinet-like tone. WHEEL gives the nine bars independently (measured: FULL has all nine within 1 dB; JAZZ three bars plus a decaying third-harmonic percussion; ROCK the three low bars and the top three; REED a falling slope; SOFT one flute bar). Clearly distinct and better as an organ. **Kept.**
* **Cost.** Host, ns per 64-frame call with the tracker: ANALOG CLASSIC 2 471, ENSEMBLE STRINGS 1 877, WHEEL ORGAN 2 026, JAZZORG 1 871, ROCKORG 1 950, REEDORG 1 809, SOFTORG 1 725 (the tracker is about 1 500 of each): the same class as ANALOG. Target: RAM code 62 232 B (+44 B), 23 776 B free; openpocket.bin 220 220 B (+2 960 B); low RAM 12 112 B. **Pedal CPU not measured**: read the DSP meter with an organ patch on.
* **Aliasing.** Under 0.1 % of the energy lies off the harmonic series on notes at 330, 659, 988 and 1318 Hz (partials above about 6 kHz fold down an octave by design).
* **Verification gap.** Upstream's `golden.txt` has no WHEEL lines, so there is no bit-exact reference for this engine; it is verified by OpenPocket's own tests only.

## What it is in the pedal

ENSEMBLE slots 5-9 (PATCH 50-100), the Felucca presets as the starting points (the `ENG` column of the patch table, `eng = 1`):

| PATCH | Name | e[] REG SUB BODY TOP PERC CLICK DRV ROTR | Source |
|---|---|---|---|
| 50-59 | ORGAN | FULL (all nine bars), 0 0 0, no perc, click 30, drive 20, slow | Felucca FULL ORGAN |
| 60-69 | JAZZORG | 3BAR, third-harmonic percussion, click 50, drive 8, slow | Felucca JAZZ PERC |
| 70-79 | ROCKORG | ROCK (body and the top three), click 70, drive 100, fast | Felucca ROCK DRIVE |
| 80-89 | REEDORG | REED (a falling slope), click 20, drive 10, no rotary | the REED registration, my patch |
| 90-100 | SOFTORG | FLUTE, body -2, click 10, slow | Felucca SOFT FLUTE |

Controls on the organs: TONE tilts the bars (up: upper bars +5, body +1.5, sub bars -5; down the other way; 50 = as written; one level is about 3 dB; a bar cannot pass 8 or 0, so the FULL registration can only lose bars). ATTACK x1/4..x4 around 7 ms (2-28 ms), RELEASE x1/4..x4, LEVEL as the others. The trims are set to about -18 dBFS gated RMS on the bundled loops (-17.1 to -19.0), like the ANALOG-engine patches. Percussion strikes on every pick (a retrigger counts as a new note: this engine's "single trigger" rule has nothing to compare with in a mono voice).

## Capacity: ENSEMBLE is full

Ten slots of ten values (0-100), all defined. **Stage C (PHASE) and stage D (TRIO) cannot add patches to ENSEMBLE without breaking the identity rule.** Options: a second SYNTH type (`ENSEMBLE 2`, appended after id 31) with its own ten slots, or shorter bands for new patches only. Decision needed before stage C.
A preset saved with ENSEMBLE PATCH 50 or more in builds 907-911 played BRSLEAD; in 912 it plays an organ.

## Verified (host)
`tests/engine/synth_test.c` (WHEEL block): the played note (the 8' bar) within 25 cents on E2, E3, E4 for all five; the structure of each registration; the percussion decays (JAZZ); TONE tilts (the 1' bar over the 16' bar changes by more than 20 dB between TONE 0 and 100); ATTACK 100 slower and RELEASE 100 longer than 0; the rotary leaves the 20 ms RMS within 4 dB; level between -22 and -14 dBFS; the hard-mute fast release also on ORGAN and SOFTORG; all 810 patch x macro combinations over ten patches finite and bounded. ANALOG, ARCADE, ARCADE SPLIT and ENSEMBLE patches 0-4: bit-identical to the previous commit (120 hashed renders; only slots 5-9 of ENSEMBLE differ); the protected effects identical (937 lines); `make test`; `make wasm`; `make package-synth2` (912); `make ble`.
