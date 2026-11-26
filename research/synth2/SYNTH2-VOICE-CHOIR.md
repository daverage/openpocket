# Synth2: the original FM-1 synth's VOICE engine as the choir

Status: **integrated as the CHOIR type (id 33, test build 916): host tested, built for the target, not flashed, not listened to by anyone, pedal CPU not measured.** Reproduce with `sh tools/research/voice_eval/run.sh` (the engine file is an unmodified copy with its provenance in `tools/research/voice_eval/README.md`). Listening files: `build/choir-voice/` (76 files, not committed).

## Why this engine

The user asked to check that the engines are used as in the original FM-1 synth (docs/research/synth2/SYNTH2-HOST-AUDIT.md). That synth has an engine for sung vowels that `Felucca-Engines` does not contain: `eng_formant.c`, "VOICE: formant oscillator, sung vowels from the keyboard", after Klatt (1980): a Rosenberg glottal source into four resonators in cascade (F1 > F2 > F3 > F4), vowels A E I O U from Hillenbrand et al. (1995), a TALK morph between two vowels, BUZZ (the glottis' open quotient), BREATH noise, Q (the bandwidths) and a RAND vowel. Its preset `CHOIR AAH` is `VOWEL 0 (A), VOWL2 0, TALK 0, SHIFT 0, BUZZ 40, BREATH 22, Q 60, RAND 0`, `A 85 D 90 S 115 R 95` (attack 476 ms, release 980 ms by Felucca's tables), sends chorus 70 and reverb 90. This is the engine the TRIO evaluation lacked (TRIO has one filter, so one resonance).
Licence: GPL-3.0-only (`guysoft/blackbox-fm1`, commit `312f1035`, copyright Leo Kuroshita / Hugelton Instruments), the same as OpenPocket.

## Host results (steady plucks through the tracker and the adapter, Felucca's own envelope)

| Question | Result |
|---|---|
| Does it run in the adapter? | Yes, first try: it needs `blep`, `soft_knee`, `voice_amp`, `noise32` (all in the vendored `dsp.c`) and the `ENV_LIN` table for TALK (generated from Felucca's `gen_tables.py` formula, 128 numbers). `FELUCCA_CPU2 0`. |
| Formants | **Pitch-independent.** On 110, 165, 220 and 330 Hz notes the strongest harmonic is at 660 Hz in every case (F1), the second peak at 1100-1320 Hz (F2) and, where the harmonics are dense enough, a third near 2640 Hz (F3): the /a/ pattern, fixed while the pitch moves. (LPC is unreliable on a sparse harmonic spectrum, so the harmonic peaks are the evidence.) |
| Level across the guitar's range | **Steady: -16.0 to -16.8 dBFS from 110 to 330 Hz** (TRIO: 13.5 dB of drift). |
| CPU, host (ns per 64-frame call with the tracker; noisy at this size) | 1 singer 1 967; ENSEMBLE with 3 singers 2 731, 4 singers 3 265, 5 singers 3 606 (INSTRUMENT STRINGS 2 398; ENSEMBLE ORGAN OPEN 3 573): each singer is about as costly as an organ voice on the host. **The target is the risk:** the engine does four 64-bit multiply-accumulates per sample (the resonators) and several per block; the AC791N's cost for that is not known. |
| Stacked singers | ENSEMBLE voicings work with it unchanged: each voice is a singer with its own drift (the pitch wanders, the formants stay: what a choir does). 3 singers (UNISON) gated RMS -13.7 dBFS and a -1.8 dBFS peak on the lead loop: it needs a trim of about -4 dB. |
| Vowels | VOWL 0 A, 32 E, 64 I, 95 O, 127 U (position along the five vowels); TONE shifts all formants +-5 semitones (the singer's size). |

## What is not known

Whether it sounds like a choir. The objective evidence is the right one (real, fixed formants, a glottal source, breath, steady level across the range), and it is the engine the original synth uses for exactly this sound; whether the result is convincing, and whether the 476 ms attack suits playing, is for the ear. It is mono (one note from the tracker), as all of this is.

## Integrated (build 916)

A new SYNTH type CHOIR (id 33) is the cleanest: PATCH = the vowel (AH, EH, EE, OH, OO and mixes of them: VOWL / VOWL2 / TALK pairs), VOICING = the singers (the ENSEMBLE stacks, reusing `PX_STACKS`), SPREAD, TONE (the formant shift), LEVEL; vendored `eng_formant.c` and `fe_env_tables.h` under `third_party/` with the licence notice (and a NOTICE.md line). Before that: a target build to measure the cost of the 64-bit arithmetic (a float or 32-bit resonator rewrite if it is too dear), and the listening test.

Done: `third_party/felucca-host/` (the unmodified engine, `fe_env_tables.h`, `gen_env_tables.py`, `PINNED.md`, a NOTICE.md line); `PX_VOWELS` (ten vowel patches: AH, EH, EE, OH, OO, OO>AH, AH>OO, AH>EE, MIXED, BREATHY; the vowels' positions are Felucca's A 0, E 32, I 64, O 95, U 127; ATK 72 / REL 92 instead of Felucca's 85 / 95 (190 ms / 800 ms against 476 ms / 980 ms), a delayed vibrato of 10 cents); the adapter's CHOIR type reuses the ENSEMBLE stacks (each voice a singer with its own drift and vibrato) and TONE shifts the formants +-5 semitones; trims set so the vowels sit near -18 dBFS gated RMS on the loops (BREATHY about 2 dB lower). Existing types: 150 renders bit-identical to the previous commit; protected effects identical.
Tests (`choir_suite`): the names; AH's strongest harmonic is in the F1 region (520-820 Hz) at 110, 165 and 220 Hz and the level is within 3 dB across them; EE has more than 10 dB more energy above 2 kHz (relative to 250-1000 Hz) than OO; OO>AH opens and AH>OO closes over each note (the spectrum's centre of mass moves 60 Hz or more); 600 vowel x singers x spread x tone combinations finite and bounded (peak 0.81); silence in, silence out; deterministic and call-size independent; a hard mute stops all five singers within 400 ms.
Target (`make package-synth2`): openpocket.bin 228 748 B (+5 KB: the engine runs from flash), RAM code 62 712 B unchanged (23 296 B free), low RAM unchanged. **Pedal CPU is unmeasured**: the engine does 64-bit multiply-accumulates in its four resonators; read the DSP meter with CHOIR OPEN (five singers), and tell me if it is too dear (a float or 32-bit resonator rewrite is the fallback).
