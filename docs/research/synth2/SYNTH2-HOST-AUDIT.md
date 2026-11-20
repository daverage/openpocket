# Synth2: are the Felucca engines used as the original host uses them? (audit, test build 915)

Asked by the user: check that the engines are driven the way the original FM-1 synth drives them. References read: `guysoft/blackbox-fm1` (branch `blackbox-fm1`: `firmware/src/voice.c`, `fx.c`, `engines.c`, `eng_*.c`, `INTERNALS.md`) and upstream `hugelton/Felucca` (`tools/gen_tables.py`). The Felucca host and its own engines are GPL-3.0-only (as OpenPocket); `Felucca-Engines` (analog, lofi, noise, phase, trio, wheel) is MIT.

## What the original host does that `engine/px_synth_adapter.h` did not

| Item | Original (`voice.c`, `gen_tables.py`) | Builds 907-914 | Status |
|---|---|---|---|
| **Envelope** | ATK, DEC, REL are 0-127 values for a time `T = 1 ms x 10000^(v/127)` (1 ms .. 10 s). The attack is **linear**, full level in T. Decay (to SUS) and release are exponential, **99 % of the way after T** (time constant T / 4.6) | my own one-pole curves, time constant `1 + (v/127)^2 x 1500..3000 ms`: **4-15 times slower** (STRINGS attack 160 ms against about 820 ms, release 680 ms against about 7 s; BRASS attack 13 ms against about 200 ms, release 26 ms against about 1.8 s) | **Fixed in build 915** (`PX_S2_ENV_ORIG`; 0 restores the old curves: with it off, 150 renders are bit-identical to build 914, so the envelope is the only change) |
| FX sends | each preset carries DIST / CHORUS / DELAY / REVERB send levels (`FX(0, 60, 20, 70)` on STRINGS and SOFT PAD); the chorus is one swept 9-17 ms delay (rate 40, depth 60 at power-on), the reverb HALL | not applied (the SYNTH block has no sends; OpenPocket's MOD, DELAY and REVERB blocks come after it and are the user's) | **open**: the presets were written to be heard through the chorus and reverb |
| Filter envelope | `cutoff = (env x ED_FLT) >> 7` in 1/256 cut steps (= the preset's `fenv`); `vel > 110` adds `env x 24 >> 7` | same, except the accent | matches |
| Pitch | glide between legato notes; LFO and ENV to pitch through the matrix | continuous tracked pitch; vibrato added in fix pass 6 for patches that ask for it | different by design (the pitch is the guitar's) |
| Amplitude | `env x vel x 258` Q15, vel 1-127 | pick strength 0.35-1 | different by design (a guitar's dynamics) |
| UNISON | up to 8 voices, DETUNE spreading them up to about +-40 cents, random start phases, 36 % level each | ENSEMBLE: up to 5 (9 now) voices, fixed intervals, fixed detune plus a slow random drift per voice | same idea, richer here |
| Retrigger | keeps the phases and the engine's filter state (`engine_t.keep`) | same | matches |
| Engine hooks | `.block` once per block and part, `.amp` (LOFI's stepped envelope), `.keep` | all honoured (`.block` added for WHEEL in stage B) | matches |
| Engine-private state | WHEEL's `drw_t`, `drw_v` | zeroed on a reset | matches |

The engines themselves read nothing else from the host: ANALOG, LOFI and WHEEL use only `track.p[]`, the voice and `vmod_t` (LOFI's vibrato is its own phase, ANALOG's shape modulation arrives through `vmod_t.shape`).

## My patches against the upstream presets

* **Exactly upstream:** SQ BASS (SQR BASS), SUB (SUB BASS), ACID, ARCADE (PULSE LD), WAVE (WAVE LEAD), STEP (STEP LEAD), ARP (ARP 8BIT).
* **Upstream with my changes:** CLASSIC (SAW LEAD: DTN 12 -> 10, MIX 64 -> 16), SOFTPAD, STRINGS, BRASS (MIX 64 -> 16; the second-oscillator level, lowered in fix passes 2 and 4 against the beating), the five organs (ATK 0 -> 8: 1 ms -> 2 ms), CHIP, 8BIT BS, PLUCK, MIN ARP.
* **My own designs:** FATSAW, PULSE, PWM, MOOG, SWEEP, SYNC, ZAP, NOISE, SLOWSTR, BRSLEAD, REEDORG.
* **Upstream presets not used:** PWM STR (the string-machine sound), WIND, SINE KEY, RAVE, PLUCK, GOSPEL.
* The engines the original FM-1 has that are not in `Felucca-Engines`: **VOICE** (`eng_formant.c`: a Klatt cascade formant synthesizer, a Rosenberg glottal source, sung vowels A E I O U, BUZZ, BREATH, a TALK vowel morph), **PHYS** (`eng_phys.c`: MODAL with BOW > 0 "bowed / blown", STRNG a plucked string, SYMP sympathetic strings, MEMB), FM6, GRAIN, SAMPLE. GPL-3.0-only: usable here. Not evaluated yet.

## Consequences for earlier results (to be corrected)

* Every envelope time quoted or measured in this project for ANALOG, ARCADE, INSTRUMENT and ENSEMBLE used the wrong curves: the attack and release feel, the "release rings on" after a mute, the INSTRUMENT ATTACK / RELEASE ranges, and the stuck-note behaviour (fix passes 2-3 shortened the release after a damp to compensate). The TRIO evaluation's "450 ms attack, 1.3 s release" was the old curves: CHIP CHOIR is really 160 ms and 480 ms. Its formant finding (one resonance) is unchanged.
* The dark tone of STRINGS and BRASS (centroid 1.3-1.7 times the fundamental) is largely the preset as written plus my lowered second oscillator; the original also runs them through chorus 60 / 20 and reverb 70 / 40.
