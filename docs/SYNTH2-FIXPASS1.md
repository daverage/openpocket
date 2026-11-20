# Synth2 fix pass 1 (s2, PocketX_902 / BlackBox_902)

Three audible defects from the first pedal test. Not flashed yet; nothing here is verified by ear.

## 1. Note-on beep/chirp (ANALOG and ARCADE)
- **Cause (confirmed on real DI, `tools/sim/note_on_diag.c`):** the tracker's first stable pitch is often a high partial (low E: 429 Hz first, 82 Hz 14 ms later). The note layer corrects it downward after the note has started (`engine/px_track.h`, the sub-multiple branch), which is right for tracking and audible as a short high note in a synth. On `scale + duo + power chords di`, 23 of 31 notes were corrected by an octave or more within 150 ms. It is not the synth: `tools/sim/synth_onset_diag.c` plays a clean pluck through all 20 patches and the first 100 ms show no attack spike. The correction does not retrigger the envelope (checked: only `state 1/3 -> 2` sets `onset`).
- **Change:** `px_mono_init` sets `lay.minwait = PX_MONO_MINWAIT (17 frames, 24.7 ms)`; a note whose first pitch is above 200 Hz waits that long after the pick before it speaks (new `minwait`/`minwait_f` fields in `pxt_layer`, 0 = the old constants, so the SFX SQUARE SYNTH HQ and everything else are unchanged).
- **Result (host):** octave-or-more corrections 23 -> 6 of 31 notes; rise-to-onset on those notes 12 -> 21 ms (unchanged below 200 Hz). A wait of 25 frames gives 4 of 30 at 26 ms; 12 frames gives 11. Chosen 17; one define to retune.
- **Not fixed:** low notes (E2) start up to ~270 cents sharp and settle in ~13 ms (96 -> 85 -> 82 Hz); that needs about 35 ms of wait, which I did not add. Power-chord material still produces some wrong first pitches.
- **Latency:** +8 ms mean on notes above 200 Hz. CPU/RAM: none measurable (one compare per frame, 8 bytes of state).
- **Tests:** `synth_test.c` pass1_suite (first pitch, no correction, start within 45 ms on synthetic notes).
- **Listen for:** is the chirp gone on the high strings and on fretted notes; does picking feel slower.

## 2. Lowest Octave HQ level
- Named `PX_LOWEST_HQ_GAIN = 2.0` (+6 dB), applied to `ps_lvl[0]` only when the type is LOWEST_HQ (`engine/px_dsp.c`, rc_pitch). POLY OCTAVE HQ and its constants are untouched.
- Measured (wet only, dry off, blend 100): +6 dB on three DI files; the soft limiter (above 0.8) never worked on them; worst wet peak 0.37 (input peak 0.13). A hot input (peak above ~0.3) will reach the limiter at blend 100 with the dry off; `-DPX_LOWEST_HQ_GAIN=1.4125f` is the +3 dB fallback.
- Tests: `lowest_gain_test.c` (ratio 2.0 against the v73 level, POLY OCTAVE HQ unchanged), run from `tests/run_tests.sh`. `tests/protected.sh` now leaves out PITCH type 5 (the intended exception).
- Latency/CPU/RAM: none.
- **Listen for:** level beside the dry guitar on E, A, D, G, power chords, palm mutes, at several blends.

## 3. ARCADE NOISE
- Differences found: the old Arcade Poly noise was a 32-bit xorshift stepped on phase wraps (a uniform sample); Felucca's is a 15-bit LFSR with a 1-bit output (buzzier), and the patch had sustain 0.
- Change: the NOISE patch now uses the adapter's own xorshift voice (`px_s2_noise`, `engine/px_synth_adapter.h`; same hold, 4-bit quantiser, TONE filter and amplitude path as `lofi_render`; Felucca's files untouched), env `{0, 40, 90, 30}` (sustain 0.7), trim 84 -> 140 (the uniform sample is 4.8 dB lower). Other patches unchanged.
- Test: a held 196 Hz note still plays at 1.2 s at more than half its level at 0.3 s.
- No old-versus-new spectrum comparison was made (the old renderer lives on the `synth` branch and was not rebuilt); the algorithm is the same, the rest is for an ear. The trim is a first guess.
- **Listen for:** character against the old NOISE, how long it holds, level against the other patches.

## Checks run
`make test` passes except the two PROBLEM lines in `sweep2.c` (POLY OCTAVE HQ missed doubled chord frequencies; POLY OCT FUZZ lost distortion at minimum LEVEL), which also fail on 9fa123f, before this pass. `tests/protected.sh 0635880`: 936 runs identical, 400 alone-block runs identical. `make wasm`, `make package-synth2` (OP_TEST_VERSION 2), `make ble` build. Pedal CPU not measured.

## Addendum: POLY OCTAVE HQ RANGE restored (s3, PocketX_903)
- **Reported:** RANGE no longer stops OCT -1 / -2 above a note, as the Boss OC-3's RANGE does (worked on v71/v72).
- **Cause (confirmed in the history):** v73 (`31d4201`) replaced the note cut (B1..E5 taper, `erb_range_band`) with a "follows the lowest sounding band" rule. That is not a Synth2 change, but it is in the 0635880 baseline.
- **Change (`engine/px_dsp.c`, rc_pitch and px_param_text):** POLY OCTAVE HQ's RANGE is the v72 note cut again, with the note names. LOWEST OCTAVE HQ keeps the v73 lowest-band rule and its gain. The protected reference is now `2f3519b` (the commit before v73): POLY OCTAVE HQ and POLY OCT FUZZ, and every other block alone, are bit-identical to it (936 + 400 runs).
- **Consequence:** the quiet-chords effect v73 removed in Poly Octave HQ's RANGE is back for RANGE settings other than FULL (v73's own finding: 27-35 dB quieter chords above the cut). That is how the cut behaves.

## Addendum 2 (s4, PocketX_904): RANGE on LOWEST OCTAVE HQ, gain x0.7
- LOWEST OCTAVE HQ now has four settings: DRY, OCT -1, OCT +1, RANGE (same note cut and note names as POLY OCTAVE HQ; FULL = 0 = every band, as before). A stored preset of this type whose hidden fourth setting was not 0 will now cut: re-set it to FULL.
- Its gain: `PX_LOWEST_HQ_GAIN` 2.0 -> 1.4 (30 % less than the +6 dB of s2, about +2.9 dB over v73). Read as "reduce this effect's gain by 30 %"; say if you meant something else.
- Tests: `lowest_gain_test.c` (gain x1.4, POLY OCTAVE HQ unchanged, a note above the RANGE note is cut from 0.160 to 0.007 RMS). Protected: POLY OCTAVE HQ, POLY OCT FUZZ and other blocks identical to `2f3519b`.
