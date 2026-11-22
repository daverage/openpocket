# Acoustic trumpet and cello: host prototype results

This is the host listening prototype that [REALISTIC-INSTRUMENT-SYNTHESIS.md](REALISTIC-INSTRUMENT-SYNTHESIS.md) and its [handoff](../archive/REALISTIC-SYNTH-HANDOFF.md) asked for. It runs on the computer only. Nothing here is in the firmware, and every timing below is a host timing. Nobody has listened to the renders yet. The numbers measure tracking, tuning, level and a steady-spectrum proxy, but they can't choose a sound.

```
tools/archive/legacy-synth/acoustic_voice.sh        # C compiler, Python 3 with numpy and scipy; downloads about 160 MB the first time
```

It writes `build/acoustic/out/matched/*.wav`, with every render at -20 dBFS RMS for listening, and the unnormalised renders to `raw/`. The full measurement tables go to `build/acoustic/out/report.md`. Nothing is committed under `build/`.

## What it does

| Part | File | What it is |
| --- | --- | --- |
| Sample bank | `tools/archive/legacy-synth/acoustic_prep.py` | Twelve VSCO 2 CE recordings (CC0) at a pinned commit: trumpet `sus` and cello section `susvib`, 3 roots × 2 dynamics. Each is converted to mono 22.05 kHz with its leading silence trimmed. Its f0 is measured and a crossfaded sustain loop is found (30 ms on the trumpet, 90 ms on the cello section, whose vibrato never repeats exactly). Source URL, SHA-256, measured tuning, loop points and level go to `bank.json`. VSCO names notes an octave below scientific pitch: the measured trumpet roots are F3, D#4 and D5, and the cello roots are C2, B2 and A3. |
| Voices | `tools/archive/legacy-synth/acoustic_voice.c` | **Sample voice**: nearest root, soft or loud layer chosen by pick strength, expression as level (24 dB range) and brightness, a 10 ms crossfade on a repick, and a crossfade into the nearer root's loop when a slur moves more than 6 semitones. **Waveguide voices**: a bowed string and a lip-reed brass model, following STK `Bowed` / `Brass`. The trumpet sounds one octave above the guitar; the cello sounds at guitar pitch. |
| Control | same | A fixture mode (known pitch, gate and expression from a score, no tracking), and a tracked mode: the engine's own `synth_hq_track` plus a note layer (pick detector, lock test, octave guard, pick strength, expression). |
| Baselines | same | The engine's current BRASS and STRINGS (`px_engine`, SYN only) on the same DI. |
| Ground truth | same, `tools/archive/legacy-synth/acoustic_truth.py` | A Karplus-Strong DI of the fixture score: quiet and accented picks, a swell, a slur and bend, fast repicks on a ringing string, a slide, muted stabs. For `assets/loops/lead.wav`, the dataset's own transcription `midi_12.mid`: its onsets are off the grid, and one falls exactly on the loop's start. |
| Spectrum proxy | `tools/archive/legacy-synth/acoustic_compare.py` | 1/3-octave long-term spectrum of each steady synthetic note against the recording at the same pitch and dynamic. |

## Results

**Tracking (the main risk).** On the synthetic DI (E2 to E4), the engine's HQ tracker with the note layer found all 17 picked notes at the right pitch. That includes the softer repicks on a ringing string, once the pick detector listens to the click above 1.5 kHz rather than the overall level. The tracker locked in a median 29.5 ms (worst 89.4 ms, on E4) and the instrument note started in a median 50.2 ms (worst 93.4 ms).

On the real lead phrase it got only **13 of 27** notes right. Every miss was A4 or higher. BASS HQ and PWM HQ feed the tracker through two 150 Hz low-passes. That suits the low strings, but a fundamental above about 400 Hz is 30 dB or more down there.

A second copy of the same tracker on 200 Hz to 1 kHz raises the lead to **23 of 27**, with the synthetic result unchanged. The copy is used while that band clearly carries more energy, except when its estimate is a whole multiple of the low band's, because then it is hearing harmonics. The four misses:

- An E5 that overlaps the previous note in the transcription.
- Two softer high notes that followed as slurs.
- A G3 re-entry 287 ms late.

Time to the right pitch on the lead: median 20 ms.

**Latency.** On the synthetic DI, the tracked voices reach 12 dB below their peak a median **43 to 58 ms** after the pick. Today's BRASS and STRINGS take 9 to 12 ms, because they reshape the string's own sound. This is the main playability cost of a tracked voice. The source doc's optional direct attack, or some dry blend, is the obvious mitigation.

**Tuning.** The bowed waveguide is in tune within 1 cent from C2 to C4. It needed 1.7 samples of loop compensation where STK subtracts 4. The brass waveguide is within 6 cents from E3 to E5 across breath strength, but only after three changes to the STK algorithm:

- **Lip gain:** the peak lip gain is held constant over the range. STK's fixed 0.03 drives the lip resonator's peak up as 1/sin(w). Below about E5 the lip then stays fully open, and the model never speaks.
- **Breath floor:** breath stays above the blowing threshold, so the soft D#4 and D5 notes speak. Dynamics move to the output level.
- **Pitch table:** a drive-to-pitch table is solved at start-up, because the lip pulls the pitch by up to 140 cents.

**Steady spectrum against the recordings** (mean 1/3-octave difference; lower is closer; a proxy only):

| | trumpet (6 notes) | cello (6 notes) |
| --- | --- | --- |
| waveguide | 17.0 dB | 7.4 dB |
| engine today (BRASS / STRINGS) | 26.4 dB | 9.8 dB |
| sample voice | the recording itself at its roots | the recording itself at its roots |

The trumpet waveguide's centroid follows the recording from soft to loud (447 → 1072 Hz at F3; recording 432 → 962 Hz), but its band shape stays 18 to 23 dB away at D#4 and D5.

**Cost and size.**

| | trumpet sample | trumpet waveguide | cello sample | cello waveguide |
| --- | --- | --- | --- | --- |
| host time (ns / sample, -O2) | 8.8 | 25.9 | 15.4 | 17.0 |
| flash | 190,114 B PCM16 | none | 229,250 B PCM16 | none |
| RAM, delay lines at the lowest note (float) | none | 2,152 B (E3) | none | 2,696 B (C2) |

Both banks at 4 bits per sample would be about 104,800 bytes, an estimate made before any block headers or loop checkpoints; no codec is written. The source doc's figure of 264,600 bytes assumed 0.5 s per sample. The cello section needs longer loops (0.3 to 0.45 s) after slower attacks, so the real bank is 419,364 bytes of PCM16.

## Recommendation

- **Trumpet: the sample voice.** It is the only trumpet voice here with a recorded identity. The waveguide speaks and stays in tune only after the three changes above, and it is still far from the recordings in the upper register. At PCM16 its bank would take nearly all of the nominal 209,444 bytes of flash headroom in the source doc. Plan a 4-bit loopable coding (about 48 KB) or shorter loops before porting.
- **Cello: port the bowed waveguide first.** It is exactly in tune and takes bow speed and pressure continuously from the guitar's envelope. It needs no flash and comes closer to the recordings than today's STRINGS. Its 2.7 KB of delay lines must be placed explicitly: the source doc reports 208 bytes left in the DSP pool and 20,040 bytes in main RAM. The VSCO cello is a section, so a solo-cello sample voice would need a different, dry solo recording.
- **Tracker: do not use the HQ tracker's input filters unchanged.** Use a front end that covers the lead register, such as the two-band version here, and keep the note layer: click-based pick detection, a lock on the tracker's own target, and the octave guard.
- **Before any port:** listen to `matched/fixture_*` (instrument alone) and `matched/lead_2band_*` against `matched/lead_engine_*`. Then time the chosen voice on both pedals with the feature running alone.

## Not done

- No listening judgement. Only a person can make one.
- No pedal build or pedal timing.
- No 4-bit sample coding.
- No solo cello recording.
- No double stops. The arpeggio loop lets its notes ring together, so the renders show what a mono voice does with it, and the report has no ground-truth score for it.

The waveguides are written fresh from the published STK algorithms. STK's licence (MIT-style, Perry R. Cook and Gary P. Scavone) requires its copyright and permission notice in copies of substantial portions, so add that notice to `NOTICE.md` if this code goes into the firmware.
