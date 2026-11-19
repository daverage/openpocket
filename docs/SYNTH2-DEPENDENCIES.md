# Synth2: what was kept, removed and why (stage 1)

Source: `synth` at `06358805`. Checked by compiling every removal (`-Wall`), the host test suite, and bit-for-bit hashes of the protected effects
(`tests/protected.sh 0635880`: 936 runs of POLY OCTAVE HQ and POLY OCT FUZZ on synthetic and recorded signals, plus 408 runs of every other
block alone; all identical).

## Kept (protected, shared)

| Code | Used by |
|---|---|
| `erb_prepare`, `erb_layout`, `erb_bank`, `erb_octaves`, `erb_octaves2`, `erb_halfband`, `erb_inverse_root`, the `erb_*` statics, `px_erb_octave` state | POLY OCTAVE HQ (`erb_pitch`), POLY OCT FUZZ (`erb_syn`) |
| `fuzz_setup`, `drive_buffer`, `drive_block`, `os_up/os_down`, `hb*` half-bands, `px_drive_state`, `FUZZ_FACE` | POLY OCT FUZZ; DRIVE and AMP (shared oversampled section) |
| `syn_fuzz_reset`, `rc_syn` (now fuzz only), `octave_up_level`, `octave_down_level`, `syn_fuzz_level_db` | POLY OCT FUZZ |
| `ps_voice_run`, `ps_step`, `ps_align`, `pitch_run`, `lowest_pitch_*`, `px_resonator_test.h` (`pxr_advance`) | OCTAVE block (WHAMMY, HARMONY, DETUNE, LOWEST OCTAVE HQ, POLY OCTAVE HQ) |
| `px_track.h` (`px_track`, `pxt_*`), `px_track_notes.h`, `px_synth_tracking`, `synth_hq_pulse`, `polyblep` | SFX block (SQUARE SYNTH, SQUARE SYNTH HQ); also the mono-tracker Candidate A for stage 2 |
| `px_dsp.c` pipeline, wet lanes, glides/fades, `px_clock` | everything |

## Removed

| Code | Was used by |
|---|---|
| `engine/px_pulse_poly.h`, `engine/px_poly_track.h` (the Duo tracker: picker, allocator, `PXP_*`) | PULSE/SAW/ARCADE/FM POLY, STRING DUO |
| `engine/px_acoustic.h`, `engine/px_acoustic_bank.h`, `tools/mk_acoustic_bank.py`, `tests/engine/acoustic_test.c` | TRUMPET, CELLO, CELLO SECTION, STRING/BRASS ENSEMBLE |
| `syn_run`, `syn_organ_run`, `syn_keys*`, `syn_shift`, `syn_ladder*`, `syn_tap`, `ps_voice_al`, `syn_voice`, `syn_octave_hq`, the old `rc_syn` cases, their param-text and level-helper functions | ORGAN, CHURCH, STRINGS, CHOIR (+HQ), MONO, PWM, BASS HQ, 12-STRING, ACOUSTIC, SITAR, BARITONE, BRASS, STRING MACHINE |
| `px_engine` state: `syn_buf`, `syn_cb`/`syn_ac`, `syn_ph/off/inc/W`, ladder, tracker, sitar and envelope state, `syn_track_hq`, `px_acoustic`, `PX_SYN_CB` | the above |
| `tools/sim`/`tools/research` benches and scripts of the retired voices and the Duo | moved to `tools/legacy-synth/` (they do not build against this tree; they build on the `synth` branch) |

## Not decided here

* SFX `SQUARE SYNTH` / `SQUARE SYNTH HQ` use the old mono tracker. They are not SYNTH-block types and were left alone; stage 2 decides their fate with the tracker.
* `px_resonator_test.h` has a test-only wrapper pair (`pxr_reset`, `pxr_run`) now unused.

## Preset ids

The 28 SYNTH ids are unchanged (`PX_SYN_X_*` = tombstone, `PX_SYN_OCT_FUZZ` = 16; the retired ids keep their numbers). `tests/engine/syn_ids_test.c`
checks all 256 byte values: a retired or unknown id is bypassed bit for bit, never another sound, and `px_block_canonical` leaves it
alone. The factory default SYNTH type is POLY OCT FUZZ (off).
