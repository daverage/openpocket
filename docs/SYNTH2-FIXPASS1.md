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
