# Fast synth notes, mute rejection and overtone errors

Keep the current pitch estimator for the first experiment. Make pick detection more sensitive, then require evidence that the pick started a pitched note before committing it to the synth. Give the voice a separate way to articulate a confirmed pick while it sustains. This addresses three different losses in the host replay.

## What changed across versions

The v42 tag has two relevant approaches. Organ and ensemble-style effects transform the guitar waveform, so a wrong fundamental estimate cannot start an oscillator at the wrong pitch. The old MONO SYNTH uses low-pass filtering, hysteretic zero-crossings, and a median of three periods. Its amplitude follows a level gate and the oscillator glides while the gate stays open. A pick can excite its filter envelope inside that gate. This is a useful precedent for articulating a pick without restarting the main amplitude envelope.

I reran the existing [Guitar-TECHS comparison](../../tools/research/tracker_compare/score_f0.py) with the actual v42 tag and current HEAD. On the two palm-mute sets, v42 reached the reference pitch on 62% of notes in each set, against 94% for current HEAD. Median time to a matching gated pitch was 38–43 ms versus 7–10 ms. The scorer looks for a match within 50 cents in a window from 20 ms before to 300 ms after the annotation. It measures pitch trajectory, so it can give credit for a correct pitch during a glide without a fresh attack.

| Tracker revision | Fast DI note-ons | `bleep.wav` note-ons | Large downward pitch changes on `bleep.wav` |
| --- | ---: | ---: | ---: |
| 902 (`2f2c114`), initial confirmation wait | 26 | 61 | 26 |
| 910 (`da4bcd3`), stricter stability and fast damp release | 26 | 45 | 7 |
| 913 (`9d4e936`), dead-pick rejection and harmonic confirmation | 26 | 42 | 3 |
| 917 (`238e498`), first slur rule | 4 | 30 | 2 |
| 918 (`c2f1c3a`), corrected slur rule | 25 | 40 | 3 |
| Current HEAD (`d645615`) | 25 | 40 | 3 |

These are tracker event counts from stereo downmix at 44.1 kHz, using the same DC block as the firmware and the [note-on diagnostic](../../tools/research/note_on_diag.c). Large downward changes are readings below 0.6 times the onset pitch, within the diagnostic's first 150 ms window. These are a proxy for bad starts. Both owner clips lack reference event labels in this comparison. For example, a lower event count on the bleep recording could also reflect missed good notes.

All of these versions except 917 emitted seven events on the 26-pluck, 22 notes/sec synthetic fixture below. Build 917 emitted one. The current adapter also has a separate FOLLOW rule that starts just six of the 25 accepted envelopes on the fast DI. Choir starts all 25, with most vowels taking about 208 ms for a nominal full attack at midpoint controls. That makes short notes less distinct even with good event tracking. [Mono tracker](../../engine/px_mono.h) · [Voice adapter](../../engine/px_synth_adapter.h)

## A pick detector experiment

The synthetic input cycles 110, 123.47, 146.83 and 164.81 Hz through 26 plucks after 250 ms of silence. Each pluck resets phase and uses six sine harmonics with amplitudes 1/h, multiplied by `0.4 * (1 - exp(-1500 * age)) * exp(-30 * age)`, where age is in seconds. Each pluck ends at the next scheduled attack. Samples are clamped to ±1 and written as mono 16-bit PCM at 44.1 kHz. A match is the first valid event within 50 cents of the scheduled pitch in the interval from 3 ms before that pick to 3 ms before the next one.

| Scratch configuration in `pxt_frame` | Correctly matched at 10 / 16 / 22 notes/sec, out of 26 | Raw detections on `bleep.wav` | Emitted events on `bleep.wav` | Large downward changes on `bleep.wav` |
| --- | --- | ---: | ---: | ---: |
| Current `6 + 2.5 * flux_avg`, 20-frame lockout | 26 / 25 / 6 | 81 | 40 | 3 |
| Threshold floor 6 → 3 | 26 / 25 / 23 | 101 | 37 | 4 |
| Running-average multiplier 2.5 → 1.5 | 26 / 26 / 24 | 106 | 38 | 4 |
| Lockout 20 → 8 frames | 26 / 25 / 6 | 87 | 40 | 2 |

At 22 notes/sec the stock detector emits seven candidates, and six become time- and pitch-matched note-ons. Lowering the multiplier yields 25 candidates and 24 matches. Median and 90th-percentile event delay for those 24 matches are 30 and 36 ms. The changes above were compiled one at a time in scratch copies. The stock confirmation and rejection checks stayed active. The multiplier change left the fast DI at 25 emitted events, despite raising the raw count from 29 to 36. Globally shortening the pitch-stability waits in another scratch run left the 22 notes/sec case at seven events and increased large downward changes on the bleep replay from 3 to 30. [Current flux threshold](../../engine/px_track.h#L212) · [Pitch stability and confirmation rules](../../engine/px_mono.h#L22)

## How other systems approach it

- **Look for fresh energy while discounting vibrato.** Böck and Widmer's causal SuperFlux compares against a maximum over nearby previous spectral bins, so a moving harmonic is less likely to look like a fresh attack. The current ERB bank already has band energies. Compare a neighboring-band version against same-band flux on bends and fast repetitions. [SuperFlux paper](https://phenicx.upf.edu/system/files/publications/Boeck_DAFx-13.pdf)
- **Use periodicity to decide whether a transient is a note.** Derrien reported fewer ghost notes on real guitar when segmentation used a steadier periodicity measure at similar quantized pitch accuracy. pYIN supplies a complementary precedent for keeping multiple pitch choices through temporal scoring. The pedal can check a small number of candidates causally, using the existing band-bank candidates and waveform history. Neither result establishes performance on OpenPocket's DI. [Derrien guitar study](https://www.dafx14.fau.de/papers/dafx14_olivier_derrien_a_very_low_latency_pitch_.pdf) · [pYIN paper](https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2014/MauchDixon-PYIN-ICASSP2014.pdf)
- **Exploit the pickup or the signal path when possible.** Roland's divided pickup gives its tracker independent string signals and a low-velocity rejection setting. A single mixed DI gives us less information when two strings overlap. BOSS also describes guitar synth patches that remodel the input waveform, which is closer to v42's organ-style effects and useful when the sound can follow guitar articulation directly. [Roland GR-55 manual](https://static.roland.com/assets/media/pdf/GR-55_OM.pdf) · [BOSS guitar synth guide](https://articles.boss.info/the-complete-guide-to-guitar-synthesizers/)

## First firmware experiment

1. **Detect a candidate.** Offer the lower flux threshold to Synth2 in a host A/B, with the stock detector as baseline. Compare straightforward threshold relaxation with neighboring-band flux. Keep a short event timestamp and peak measurement. Count candidates even when another candidate is waiting, so a genuine new pick can replace stale evidence. Keep this experiment scoped to Synth2 because the detector also serves other effects.
2. **Confirm a pick and its pitch.** Compare short-window periodicity and fresh harmonic energy for the leading pitch, the currently sounding pitch, and octave alternatives. Multiple harmonics of a *new* fundamental rising together are stronger evidence than one old overtone taking over a decaying spectrum. A repeated pick at the same pitch still qualifies when pitched energy rises again after the transient. Use quick confirmation for clear evidence and retain the current longer confirmation for ambiguous starts until the replay demonstrates a safer policy. At low E, one period takes about 12 ms, so measure latency by register.
3. **Recognize damping independently.** On an observed level and periodicity collapse, release at the last trusted pitch and discard unconfirmed candidates associated with the collapse. A soft new pick during that release can qualify on its own fresh periodic evidence. Keep the current mute/dead checks in the baseline and evaluate replacements individually. Score short pitched palm mutes separately from unpitched stops. A single pickup can produce an incidental sound that is genuinely pitched and indistinguishable from an intentional short note at first.
4. **Render the accepted event according to the patch.** Keep oscillator and filter state continuous for leads and sustained organs. Try a short filter or amplitude accent on a confirmed pick, without resetting the main amplitude envelope or striking organ percussion on every transition. Preserve the deliberate envelope attacks for percussion and compare Choir's current attack against a quicker articulation at equal volume. Bends and slides with continuous periodicity can still move pitch without a pick accent.

Use the owner's DI and bleep clips for listening once the intended picks and mutes are marked. Use [Guitar-TECHS](https://zenodo.org/records/14963133) and generated cases for repeatable checks, including equal and alternating pick strengths, repicks at the same pitch, low-E runs, string crossings, intentional octave changes, overtones, bends and dead stops. Report pitch-correct event recall, false starts on annotated stops, first-pitch errors and median/p90 onset latency by register. Compare output on a fast chip patch, a FOLLOW organ and Choir at matched levels. A candidate is ready for a pedal A/B when it improves fast-pick recall without increasing annotated mute false starts or wrong first pitches. Pedal timing and worst-case DSP load still need measurement.
