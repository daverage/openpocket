# ENHANCE (SFX): an NE570-inspired clean harmonic enhancer

Status: host-tested; test packages built (not flashed); **not heard, not measured on the pedal.** An SFX type (id after the retired APPLAUSE tombstone), so it cannot run together with another SFX. Two controls: **BETTER** 0-100 (default 35: the share of added harmonics) and **LOUDER** 0-100 = 0 to +12 dB clean boost (shown in dB). A diagnostic build adds **SENS** and **VOICE** (below).

## What it is
A behavioural model of two documented NE570 gain-cell properties (research package "openpocket-ne570-prototype", GPL-3.0-or-later, reviewed and integrated; the datasheet figures quoted there were not re-checked against the onsemi PDF here): the gain cell's offset gives an approximately **parabolic** (second-harmonic) error, and the full-wave rectifier's ripple (time constant 10 kOhm x C, a virtual 70 nF = 0.7 ms) modulates the gain, which adds a weak, frequency-dependent **third harmonic**. A parallel path: the dry signal is never clipped or filtered; the sidechain is conditioned, passed through the gain-cell model, DC-servoed and low-passed, and blended back. All of the conditioning below applies to the sidechain only. Not a traced Better Box circuit; nothing here has been compared with a real one.
Cost: 132 bytes of state, a handful of one-pole filters and multiplies per sample, no buffers, no FFT, no pitch tracking, no latency in the dry path. Not profiled on the BlackBox.

## Input processing (v2)
1. **Adjustable sensitivity**: the gain cell works at a virtual input gain, scaled back out afterwards (the dry path and unity gain are untouched): **LOW 2x** (hot pickups, boosted signals), **NORMAL 3.5x** (the reference), **HIGH 5x** (lower-output pickups), **EXTRA 7x** (very quiet signals). The research's calibration is a 0 dBm of 0.5 peak; real guitar at the pedal is far quieter, so with no gain the prototype added only -57 dB (default) / -48 dB (max) re dry on the owner's DI clips. **No automatic level compensation**: the harmonics following the picking intensity (an amplitude law: 20 dB less playing gives about 10x less second harmonic) is part of the effect.
2. **Voicings**, so the original stays as the reference:
   - **A**: the research prototype bit for bit (two 3.5 kHz one-poles, a hard clamp of the cell input at +-1). Tested bit-exact against a copy of the prototype's maths.
   - **B**: A with a **smooth overload**: a cubic soft limiter (identity at small signals: 2 % low at the 0.5 reference; zero slope where it saturates at 1.0) in place of the hard clamp, so a hot pick, humbucker, active pickup or a compressor before ENHANCE does not clip the harmonic generator's input. Measured: on a hot sine (virtual input 2.1) the added signal's H7..H15 content is lower than with the hard clip.
   - **C**: B plus **input shaping**: one-pole high-pass 60 Hz (`PXE_HPF_HZ`), a presence emphasis of about +4 dB at 2 kHz against 330 Hz (`PXE_PRES_DB`, measured +3.9 dB, a broad hump 0.6-4 kHz), anti-alias low-pass 5.5 kHz (`PXE_LPF_HZ`, instead of 2 x 3.5 kHz).
3. **Input diagnostics**: `pxe_diag` / `px_enhance_diag(e, &peak_dbfs, &rms_dbfs, &over)`: the guitar's peak and RMS (dBFS) as it reaches the effect, and the share of time the cell input exceeded the calibrated 0.5. **Host only for now: it is not shown on the pedal** (that needs a screen or protocol field).
Deviation from the proposal: the high-pass is **60 Hz, not 90 Hz**. Measured cost in added second harmonic (110 Hz-region notes, peak 0.14, BETTER 100, change vs voicing B; C also adds presence and bandwidth, which give +0.2..+2.9 dB with no high-pass at all):
| note | no HPF | 55 Hz | 60 Hz | 65 Hz | 90 Hz |
|---|---|---|---|---|---|
| E2 82 Hz | +0.2 dB | -2.8 | **-3.3** | -3.8 | -6.3 |
| A2 110 Hz | +0.4 | -1.5 | -1.8 | -2.1 | -3.9 |
| E3 165 Hz | +0.9 | 0.0 | -0.2 | -0.4 | -1.3 |
| A3 220 Hz | +1.5 | +0.9 | +0.8 | +0.7 | +0.1 |
| E4 330 Hz | +2.9 | +2.6 | +2.6 | +2.5 | +2.2 |
60 Hz costs the low E 3.3 dB of enhancement and nothing above the A string; 90 Hz would cost 6.3 dB on the low E. Whether it also buys a clearer chord (less low-frequency intermodulation) is a listening question.

## Measured (host, real DI; added = RMS of wet minus dry over active frames, dB re dry; BETTER 35 / 100)
| set | A NORMAL | B NORMAL | B HIGH | B EXTRA | C NORMAL | C HIGH |
|---|---|---|---|---|---|---|
| owner clips | -45.9 / -36.7 | -46.2 / -37.0 | -44.1 / -34.9 | -42.6 / -33.5 | -40.3 / -31.2 | -38.2 / -29.1 |
| owner clips, -6 dB (the ADC's 0.5 scale) | -51.8 / -42.7 | -51.9 / -42.8 | -48.9 / -39.8 | -46.1 / -37.0 | -45.5 / -36.4 | -42.7 / -33.6 |
| Guitar-TECHS scales | -48.3 / -39.1 | -48.4 / -39.3 | -45.5 / -36.4 | -42.9 / -33.8 | -40.5 / -31.4 | -38.0 / -28.9 |
| IDMT dataset 4 (hot, peaks -1 dBFS) | -36.6 / -27.5 | -38.0 / -28.9 | -38.9 / -29.8 | -41.2 / -32.1 | -36.3 / -27.2 | -38.6 / -29.5 |
- B equals A on real clips (-46 vs -46): the soft limiter only acts on hot signals.
- **C adds about 6 dB more than B at the same BETTER** (presence and bandwidth feed more into the quadratic term): compare A/B/C at matched added level, not matched BETTER.
- **On hot material higher sensitivity adds less** (IDMT: B NORMAL -28.9, EXTRA -32.1): the limiter saturates and the result is scaled back by 1/gain. That is why LOW suits hot pickups.
- At the pedal's ADC scaling (-6 dB) the default BETTER 35 adds only -52 dB and BETTER 100 -43 dB on the owner's clips at NORMAL: **inaudible-to-subtle; HIGH or EXTRA is probably the right operating point there.** Unresolved: `tools/calib_levels.py` says hard playing peaks about -17 dBFS "at the converter"; whether that is 0.14 in the engine's units (assumed here) or 0.07 (the ADC is read as 0.5 full scale) is not settled from the code. The diagnostics exist to read it on the pedal.
- Two-tone intermodulation (combined peak 0.14, 110/147 Hz): products about 5 dB above the harmonics: -46 dBc at BETTER 35, -37 dBc at BETTER 100 (prototype gain 3.5). Whether hard chords at high BETTER sound congested needs listening; compare B and C there.

## A/B/C on the pedal
A diagnostic firmware (`OP_DSP_EXTRA=-DPXE_DIAG sh firmware/build.sh`, test channel) gives ENHANCE four controls: BETTER, LOUDER, **SENS** (0-19 LOW, 20-59 NORMAL, 60-79 HIGH, 80-100 EXTRA) and **VOICE** (0-24 A REF, 25-74 B SOFT, 75-100 C SHAPE); 50 / 50 = NORMAL, B. Recommended test chain: ENHANCE immediately after the noise gate with the compressor, octave and synth bypassed (otherwise the fixed calibration sees an already processed signal); LOUDER 0 dB; compare A, then B, then C at matched loudness. The normal build has only BETTER and LOUDER and is compiled as B at NORMAL (`PXE_SENS`, `PXE_VOICE`).
Listening files: `tests/enhance_study.py --renders` writes level-matched renders (dry, A, B, B high, C, C high at BETTER 100) of the owner's clips to `build/enhance/matched/`.

## Engine integration
`engine/px_enhance.h` (header only); `PX_SFX_ENHANCE` appended after `PX_SFX_APPLAUSE` (ids unchanged); catalogue entry; the setup case sets the targets (filter state is kept on a knob move: the mix and level glide over 10 ms); the process case runs per sample in the SFX loop; state initialised in `px_engine_init` and whenever the SFX type or its on/off changes; `SFX_DB` trim 0 dB; LOUDER reads in dB; MIDI model slices and the editor count live types generically.
Tests: `tests/engine/enhance_test.c`, run in a normal and a `-DPXE_DIAG` build (`tests/run_tests.sh`): the catalogue, unity, silence, even over odd harmonics, the harmonic level at the pedal's hard-playing level and its amplitude law, the fundamental within 0.2 dB, LOUDER +12 dB, bounds on a full-scale square and above full scale, no click on switching or a knob jump, voicing A bit-exact against the research maths, sensitivity scaling (H2 follows the virtual gain within 0.4 dB), the limiter's smoothness (no kink, unity slope at 0, zero where it saturates), less high-order content than the hard clip, C's measured response, the diagnostics, and the diagnostic knobs through the engine.

## First pedal test (BlackBox_924, owner's ears): too weak
Reported: with LOUDER 0 and BETTER 100, no audible difference across SENS LOW / NORMAL / HIGH / EXTRA and VOICE A / B / C; only LOUDER (a plain boost) changed anything (`My Audio/enhances.wav`, 12 strums).
Consistent with the numbers: on that recording the settings add, at BETTER 100 (dB re the signal; 1 % = -40, 3 % = -30): A -45 / -40 / -37 / -34 (LOW..EXTRA), B the same within 1 dB, C -38 / -33 / -31 / -29; with the ADC's half-scale input 6 dB lower. Even the strongest setting is a 3.5 % addition and most are 1 % or less: below what reads as a distinct effect, and the earlier claim that HIGH or EXTRA would be "the right operating point" was wrong. The voicings differ by a few dB at that level, so switching them is not audible either.
The wet strength is now a compile-time parameter (`PXE_A2`, `PXE_RIPPLE`, defaults unchanged = the flashed behaviour). On the owner's DI clips at NORMAL sensitivity, added signal at BETTER 35 / 100: x1 (as flashed, B) -46 / -37 dB; x3 (+9.5 dB) -37 / -28; x6 (+15.6 dB) -31 / -22; voicing C is about 6 dB stronger again (x6: -25 / -16). Level-matched audition renders (dry, B x1, B x3, B x6, C x3, C x6, C x6 at BETTER 35) of the three DI clips are in `build/enhance2/matched/`. Next: pick the strength by ear, then rebuild and reflash.

**Build 926 (flashed to the owner's BlackBox):** the diagnostic image at x6 wet strength (`OP_DSP_EXTRA='-DPXE_DIAG -DPXE_A2=0.408f -DPXE_RIPPLE=0.24f'`, test channel `enhance-diag`, version 26); SENS and VOICE knobs kept. Expected added signal re dry on the owner's DI clips at NORMAL sensitivity: voicing B -31 dB (BETTER 35) / -22 dB (100); voicing C -25 / -16 dB; about 6 dB lower if the ADC's half-scale input applies. Bounded: the largest added peak on full-scale input is 0.26 (LOW sensitivity), far less on guitar.

## Open (needs ears and the pedal)
Level-matched listening of A/B/C at matched added level; chord clarity and intermodulation at high BETTER; pick attack and sustain; the right sensitivity per guitar (and whether an automatic sensitivity is ever wanted); base-rate against 2x oversampled processing (the rectifier is not alias-free at base rate); the real callback load; the true engine-unit level of the guitar (above); showing the diagnostics on the pedal; the BETTER law and default. Do not describe it as a Better Box match until measured against one.
