# Synth tracking experiment

Can lead, Choir, and Ensemble feel more responsive with fewer note-event rules? This host experiment uses the production engine and oscillator, formant, organ and ensemble renderers. It substitutes one call and the pick accent in a temporary copy of the synth adapter.

```sh
python3 tests/experiments/synth_tracking/run.py "My Audio/fast play di.wav" "My Audio/bleep3 di.wav" \
  --truth-dir build/stage5/sets/gtechs
```

Run with no arguments to use just the synthetic plucks. The optional inputs are local 44.1 kHz guitar DI WAVs and the optional truth directory contains prepared Guitar-TECHS scales and palm mutes. The script uses a C compiler, NumPy and SoundFile.

Open `build/synth-tracking-prototype/index.html` to hear the dry inputs and level-matched renders. Each render uses one gain for the entire clip, so the dynamics remain audible. The generated `plucks.csv` reports the original output level, time to half the local peak, time to the correct tracker pitch (within 50 cents), and wrapper note-on counts. The first low E also reports the time after the cut until its output stays 30 dB below its peak. Output can start before the tracker reaches the correct pitch. During a connected pluck the half-peak time can include the preceding note.

## Compared paths

| Variant | Pitch and note events | Dynamics |
|---|---|---|
| Current | Production mono wrapper | Production expression and pick accent |
| Direct envelope | Production mono wrapper | Rectified guitar envelope with 2 ms attack and 20 ms release drives the existing dynamics law |
| Gate and pitch | Square HQ's default note layer and pitch. Gate transitions start and release the voice. | Same direct envelope |
| No harmonic hold | Production mono wrapper with the 35 ms harmonic-ratio hold disabled | Production expression and pick accent |

All four use the patch's existing envelope, glide and voice stack. The gate variant leaves those voices running across pitches while its gate stays open, so Choir and Strings also get a continuous articulation to compare with their usual per-note behavior.

The Guitar-TECHS comparison uses the pitch and gate entering the adapter. It counts correct and incorrect gated frames as shares of the annotated note duration. The first-frame count includes the preceding note carrying across a MIDI boundary. Arrival percentiles cover changed pitches the tracker reached during the annotated note. These figures measure pitch behavior. Use the renders to assess accents, vowel changes, and whether a patch feels like it is following the pick.

## First results

| Patch | Low E to half level, current / gate and pitch | Low E tail to -30 dB after cut, current / gate and pitch |
|---|---:|---:|
| Analog Classic | 48 / 12 ms | 50 / 125 ms |
| Choir AH | 68 / 14 ms | 166 / 309 ms |
| Ensemble Strings | 71 / 28 ms | 145 / 304 ms |
| Ensemble Organ | 50 / 12 ms | 45 / 130 ms |

Gate and pitch rendered all three 18 ms plucks in every patch. The short low E never reached the correct tracker pitch, and the short E3 began sounding before its pitch settled. Direct envelope alone left pitch arrival unchanged and shortened the low E tail to 106 ms on Choir and 99 ms on Strings.

On 24 Guitar-TECHS scales, gate and pitch dropped median pitch arrival from 19.8 to 1.9 ms and raised correct-pitch time from 94.6% to 95.5%. On two palm-mute takes it arrived at 2.7 ms versus 17.1 ms, with wrong gated pitch rising from 0.9% to 3.0% of annotated note time.

On the connected A2 to A3 pluck, the current wrapper took 74 ms to send the correct pitch to the adapter. Disabling the harmonic hold reduced it to 37 ms. Gate and pitch took 52 ms and kept the voice active across the transition.

The lead and organ patches are candidates for continuous articulation. The wrong low-E start and the long tails identify two behaviors to keep testing before applying it to Choir and Strings. On-pedal response and player preference still need a listening pass.

## Pedal build v82 Synth Tracking

`node tools/op.mjs package-synth-tracking` builds test packages in `docs/firmware/synth-tracking/`. The test macro selects Square HQ-style pitch and gate with hard-mute detection for ANALOG follow patches and WHEEL organs. CHOIR and the other ENSEMBLE patches use the direct envelope with their existing pitch, onset and release handling. The envelope is converted to expression once per synth control block.

For a dry comparison, bypass MOD, DELAY and REVERB in a working preset. Use the factory names below or select the matching effect in an existing preset. Repeat the same short phrase with each patch.

| Patch | Play | Listen for |
|---|---|---|
| SQUARE LEAD, then ANALOG LEAD | Fast alternate picking on one note, then a run from low E up the neck | Lost or merged picks, wrong starting pitch, lag against Square |
| ANALOG LEAD | Alternate low E and the E an octave up, then bend, slide and pull off | Hanging on the first pitch, octave bleeps, audible steps during the glide |
| JAZZ ORGAN | Hard mute a note. Re-pick softly right away, then repeat with palm-muted eighths. | Ghost notes after the mute, a lost soft re-pick, delayed cutoff |
| CHOIR AAH | Alternate soft and hard notes, then try OO>AH and repeat the same phrase | Pick dynamics, clicks, and whether the vowel restarts sound musical |
| STRING ENSEMB | Hold a note, cut it dead, then play a short separated pair and a legato pair | Swell and tail length, missing second notes, big volume dips |

## Pedal follow-up v83

On v82, SQUARE LEAD tracked more reliably than ANALOG LEAD, which missed notes and jumped octaves. JAZZ ORGAN missed notes and gave ghost blips on hard attacks, with a high hiss. STRING ENSEMB jumped pitch in fast runs and became unstable with two guitar notes. CHOIR AAH and OO>AH played well.

v83 keeps v82's envelope follower for CHOIR. ANALOG, Organ and Strings return to the pre-v82 pitch, onset and dynamics paths. The original SQUARE LEAD remains the comparison. The Strings voice stack still follows one guitar pitch. Test clean single-note runs first, then add note overlap to hear when it loses the intended pitch.

`node tools/op.mjs package-synth-tracking` now builds v83. The v82 source and build command are at commit `68327cf`.

## Original SFX Square comparison

```sh
python3 tests/experiments/synth_tracking/run.py --square-edges "My Audio/fast play di.wav" "My Audio/bleep3 di.wav"
```

Open `build/synth-square-edges/index.html` to compare Analog Lead, Instrument
Jazz Organ and Ensemble Strings with their stock tracking, the experimental
edge tracker and the original SFX Square as a reference. All are rendered
without the factory presets' downstream effects.

The experiment uses the original Square's 700 Hz low-pass, envelope and threshold
to find rising edges. It measures the period between those edges to drive each
patch's existing oscillator voices. A 25 ms gap closes the gate. The filter and
crossings match the Square method, but the oscillator voices need two rising
edges before they can sound. A short low E can end before the second edge.
This experiment runs on the host and does not change the pedal firmware.
