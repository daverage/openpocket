# ENHANCE (SFX): a clean harmonic and tonal enhancer

Status: **v3**, host-tested; **not heard, not measured on the pedal.** An SFX type (id after the retired APPLAUSE tombstone), so it cannot run with another SFX. Three controls: **BETTER** 0-100 (overall amount, default 35), **TONE** 0-100 (0 WARM = warmth only, 50 BOTH, 100 BRIGHT = presence only), **LOUDER** 0-100 = 0 to +12 dB clean boost (shown in dB).
History and why it was redone: `docs/ENHANCE-RESEARCH.md`. Short version: v1/v2 were a model of the NE570 gain cell's parabolic distortion (research package, reviewed). Its numbers match the datasheet (0.34 % H2 per mV at 0 dBm, +8 dBm saturation) but the chip's own distortion is small by design, the package had removed its dynamic-gain function, and on the owner's guitar (own second harmonic -19 dB re the fundamental) the best v2 setting moved the second harmonic by +1.9 dB and could not be pushed further (the cell saturates): inaudible on the pedal (builds 924 and 926).

## What v3 is
Two parallel side paths mixed into a dry signal that is never filtered or clipped. Built the way the classic exciters are built (Aphex US 4,150,253; BBE; SPL), adding energy **where the guitar is weak**:
- **WARMTH:** band-pass 80-400 Hz, **full-wave rectifier** (a pure even-harmonic generator with nothing to saturate), 4-pole high-pass 100 Hz (the rectifier's DC and, on chords, its difference tones: 196 + 247 Hz leave a 51 Hz tone louder than the intended harmonic; the test caught that and moved the filter from a 70 Hz two-pole), low-pass 1.2 kHz. The added second harmonic is a fixed share of the note whatever the playing level.
- **PRESENCE:** 2.5 kHz two-pole high-pass, a **one-sided soft clip with a threshold relative to the band's running level** (1.45 x its mean magnitude: only the louder peaks and pick transients are rounded; the Aphex's "only transients clip"), 10 kHz low-pass. Also level-relative.
Both are level independent, so **no input-sensitivity setting is needed** (the v2 problem). Cost: six biquads, a rectifier, an envelope follower and a soft clip per sample; no buffers; about 200 bytes; no sqrt or powf in the sample loop.

## Measured (host; owner's DI clips, median of three; added = RMS of wet minus dry re dry)
| BETTER | WARM | BOTH | BRIGHT |
|---|---|---|---|
| 35 | -24.8 dB | -24.4 dB | -32.7 dB |
| 100 | -15.7 dB | -15.2 dB | -23.5 dB |
On the owner's single notes (own H2 -19.1 dB re the fundamental): WARM at BETTER 100 moves it to -9.3 dB (+10 dB); at BETTER 35 about +4.5 dB. PRESENCE at BETTER 100 lifts the 2.4-4.8 kHz band by about +2.3 dB and 4.8-9.6 kHz by +3.4 dB (a synthetic pluck: +2.5 dB at 3.2-8 kHz, the same at a level 20 dB lower). For comparison v2's best was +1.9 dB of second harmonic and ~0 in the high bands. WARM changes nothing above 1.5 kHz and not the fundamental; BRIGHT changes nothing below 400 Hz.
`tests/enhance_proto.py` is the offline (numpy/scipy) prototype this was ported from; `tests/enhance_study.py [--renders]` reproduces the table and writes level-matched listening renders to `build/enhance5/matched/` (dry, BOTH 35, WARM 100, BRIGHT 100, BOTH 100, WARM 35, BRIGHT 35).

## Engine integration
`engine/px_enhance.h`; `PX_SFX_ENHANCE` appended after `PX_SFX_APPLAUSE` (ids unchanged); catalogue entry BETTER / LOUDER / TONE; the setup case sets the targets (filter state is kept on a knob move: the mixes and level glide over 10 ms); the process case runs per sample in the SFX loop; state initialised in `px_engine_init` and whenever the SFX type or its on/off changes; `SFX_DB` trim 0 dB; LOUDER reads in dB and TONE as WARM / BOTH / BRIGHT. MIDI model slices and the editor count live types generically.
Tests (`tests/engine/enhance_test.c`, in `tests/run_tests.sh`): the catalogue and its text; unity at BETTER 0 / LOUDER 0; silence; WARM's second harmonic (-16..-11 dB re the note at BETTER 100), the same 20 dB lower, the fundamental untouched, nothing at 4 kHz; PRESENCE's lift and its level independence, nothing below 400 Hz; TONE 100 adds no second harmonic to a pure low note; LOUDER +12 dB; finite and bounded on a full-scale square, 3x full scale and noise; the chord difference tone held down; no click on switching or a knob jump; a stored APPLAUSE still bypassed.

## Open (needs ears and the pedal)
1. **Listening**: do WARM, BRIGHT and BOTH sound like "presence and warmth without distorting"; the right amounts and the BETTER law; whether BETTER 35 is a good default; whether hard chords at high BETTER are congested.
2. **Aliasing**: the presence clip generates harmonics up to its 10 kHz low-pass; at 44.1 kHz a little can alias. A 2x oversampled clip is the fix if it is audible.
3. **Dynamics**: the manufacturer's "tighter pick attack" and the BBE's dynamic high band suggest an envelope-controlled side-path gain (more on transients, less on sustain); not in v3. The transient-discriminate generator (US 5,424,488) is the model.
4. **CPU on the BlackBox** (`node tools/op.mjs dsp`): not measured.
5. A real Better Box measurement (DI through a unit, bypassed and engaged) would say what that pedal actually adds; v3 is a design from the datasheet, the patents and reviews, not a copy.
