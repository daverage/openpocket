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
