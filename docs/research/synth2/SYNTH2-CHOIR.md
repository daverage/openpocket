# Synth2 stage D: Felucca TRIO and its CHIP CHOIR

Status: **host-only evaluation. Nothing is in the firmware, nothing was flashed, and nobody has listened to it yet** (the listening files are in `build/ensemble-stageD/`, not committed). Reproduce with `sh tools/research/trio_eval/run.sh` (`tools/research/trio_eval/`: an unmodified copy of `src/eng_trio.c` at the pinned `d0260074`, a scratch-copy patch, a harness, `analysis.py`).

## What it is (source inspection)

TRIO: three oscillators (triangle, saw, pulse, noise, two combined waves) with ring modulation and hard sync into a 12 dB/oct state-variable filter (LP / BP / HP / notch), after the sound chips of early 8-bit computers. The upstream `CHIP CHOIR` preset is:
`WAVE 1 (three pulses), INT2 0, INT3 +12 (osc 3 an octave up), DTN 7 cents (osc 2 up, osc 3 down), MODE 1 (band-pass), CUT 62, RES 95, PW 40; A 70 D 90 S 110 R 85, filter envelope 30.`
So it is a three-pulse pad through a **single** resonant band-pass at a fixed cutoff (not following the note), a 450 ms attack and a long release. It is not a sample-based or formant-modelled choir.

## Host results (steady plucks and real DI, through the tracker and the adapter)

| Question | Result |
|---|---|
| Vocal-like character | **Not as written.** The band-pass centre as written is about 2.3 kHz (the strongest part of a 110 Hz note's spectrum; 2 264 Hz by LPC): a bright, nasal, buzzy resonance. A vowel needs two formants, the first at 300-800 Hz. TRIO has one filter, so every setting gives **one** resonance. TONE moves it: TONE 25 about 1.2 kHz, TONE 8 about 0.5-0.85 kHz, TONE 0 about 220 Hz (the ENSEMBLE TONE here moves CUT by +-40). So it can be steered toward one "ooh" / "ah"-like region, never a two-formant vowel. |
| A second formant from OpenPocket's own EQ block | Tried (parametric EQ mid band at 1 090 Hz, +6 and +12 dB, after the synth): the spectrum stays dominated by the oscillators' structure (strong 220 and 440 Hz harmonics from the octave-up oscillator, a pitch effect, not a formant); it did not give a clean two-formant vowel. Not pursued. |
| Timbre and level across the guitar range | The fixed band-pass is a plus for a formant (it does not move with the note), but **the level rises 13.5 dB from 82 Hz (-28.1 dBFS) to 988 Hz (-14.6 dBFS) at TONE 50** (7.8 dB at TONE 8, peaking mid-range), and above about 660 Hz only one to three harmonics fall under the resonance. It would need a pitch-dependent level compensation. |
| Level on the bundled loops and DI sets | As written -18 to -31 dBFS gated RMS (ANALOG patches are tuned to about -18); the vowel-steered variants are 4-8 dB louder. Trims not set. |
| CPU | Host, ns per 64-frame call with the tracker: TRIO choir 1 982, WHEEL organ 1 962, ENSEMBLE STRINGS 2 006, ANALOG CLASSIC 2 192: the same class. Pedal not measured. |
| Aliasing | Under 0.7 % of the energy off the harmonic series on notes from 82 to 988 Hz (the pulses are polyBLEP-corrected). |
| Sustain, transitions, release | On the scale and chord DI (Guitar-TECHS) and `bleep.wav`: no stuck notes (the synth is below -60 dBFS 300 ms into every guitar silence of 400 ms or more: 3 silences on bleep.wav); the biggest sample-to-sample step at a note start is at most 3.5 times the local RMS (median 0.1-1.4). A hard mute releases fast (the damp release of fix pass 3b). The 450 ms attack and 1.3 s release time constant are slow for picked playing: it is a pad, and it would start late on every note. |
| Compile / state | Built unmodified with the same adapter (new `eng = 2` path); `eng_trio.c` needs nothing the vendored `dsp.c` lacks. |

## Verdict (by the brief's own rule)

**CHIP CHOIR as written should not be called a choir.** The objective evidence is a bright single-resonance three-pulse pad; the brief says to reject it rather than label an ordinary synth pad a choir. Whether a TONE-steered version evokes "ooh" well enough is a matter of ear, and I have not heard it. Three listening versions are in `build/ensemble-stageD/` on the scale, chord and `bleep.wav` DI and the bundled loops: `*_choir_as_written.wav` (TONE 50), `*_choir_vowel_ah.wav` (TONE 25), `*_choir_vowel_oo.wav` (TONE 8).

Options for the user:
1. **Reject.** Nothing is added; ENSEMBLE stays at ten patches; no `ENSEMBLE 2`.
2. **Keep as a pad, not a choir** (call it CHIP PAD / PULSE PAD): it needs `ENSEMBLE 2` for the slot, a level trim, a pitch-dependent level compensation, and the adapter's `eng = 2` path (the scratch patch shows the change: about 8 lines).
3. **A vowel-style pad** from TONE-steered patches, if the "ah" / "oo" files sound good; same work as 2.
4. **A real choir** needs formant synthesis (several parallel band-pass formants driven by a pulse train, or a lightweight sample or wavetable choir): a separate investigation, not started here.

Polyphony: none of this is polyphonic. For the polyphonic choir the user wants, the voice question (is this a good enough voice) is answered here, the tracker question is PolyTrack X.
