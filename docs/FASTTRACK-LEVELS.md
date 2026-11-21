# FastTrack: input level and normalisation review

Question: do the datasets, the harness and the trackers treat signal level consistently? Short answer: **audio is used as recorded everywhere (no hidden normalisation), but the trackers use absolute thresholds, levels differ by up to 18 dB between datasets, and false-note rates depend on level.** Attack recall does not.

## 1. What the code does to level
- `tools/research/prep_sets.py` (Guitar-TECHS, bass sets): the line `x / max|x| * min(1, max|x|)` is a no-op unless the peak exceeds 1.0, where it normalises to peak 1: **as recorded, clip protection only.**
- The harness (`ft_wav.h`) reads PCM at full scale 1.0, **left channel only** (the pedal's guitar input is the left ADC channel; the right carries noise: HARDWARE-HANDBOOK). Before this clean-up stereo files were averaged; the owner's clips are exactly dual-mono (L-R = 0), so no result changed (regression identical). `FT_GAIN_DB` applies a test gain. Nothing is normalised.
- **The pedal scales its input by 0.5 / 2^23** (`firmware/px_audio.h:358`): the engine sees ADC full scale as 0.5. A file read at full scale 1.0 therefore feeds the tracker 6 dB hotter than the same signal at the pedal's ADC. The real analogue level at the ADC (guitar, input gain) is not documented here and not measured.
- Absolute thresholds in the tracker path: layer gate 0.003 (on at 0.0045, off at 0.0015 in the controller) = -50.5 dBFS; `px_mono.h` mute and hold use `pk > 0.01` (-40 dBFS); fix-pass thresholds were tuned on specific recordings.

## 2. Level census (active frames: 10 ms RMS above -60 dBFS; medians over files)
| set | peak dBFS | active RMS median (p10 / p90) | active frames under the 0.003 gate |
|---|---|---|---|
| Guitar-TECHS scales | -16.4 | -33.2 (-42.5 / -28.5) | 5.6 % |
| Guitar-TECHS music | -7.9 | -27.3 (-43.3 / -21.9) | 4.8 % |
| Guitar-TECHS palm mute | -10.6 | -49.8 (-58.3 / -31.3) | 49.8 % |
| Guitar-TECHS single notes | -17.9 | -45.9 (-56.4 / -35.0) | 31.7 % |
| IDMT licks (24-bit) | -8.1 | -28.9 (-49.5 / -23.4) | 14.4 % |
| **IDMT dataset 4 (direct)** | **-1.2** | **-19.1 (-47.3 / -14.0)** | 9.9 % |
| owner recordings (5 clips) | -13.7 | -36.9 (-49.3 / -30.9) | 17.1 % |
IDMT dataset 4 is 14-18 dB hotter than the owner's clips and Guitar-TECHS scales; Guitar-TECHS scales and the owner's clips are within about 4 dB, so Guitar-TECHS is the closest level proxy for the owner's playing. Palm-mute and decaying single notes spend a third to a half of their active time below the gate.

## 3. Level sensitivity (`tests/fasttrack/level_sweep.sh`, Guitar-TECHS, 9623 picks)
| gain | legacy (FOLLOW off): attack all / fast, spurious /min | controller A: attack all / fast, spurious /min |
|---|---|---|
| -12 dB | 78.0 / 50.3 %, **4.2** | 78.9 / 50.6 %, **4.2** |
| -6 dB | 78.2 / 50.7 %, 5.6 | 79.7 / 51.1 %, 7.0 |
| 0 dB | 77.9 / 51.0 %, 5.2 | 79.5 / 51.2 %, 8.7 |
| +6 dB | 78.0 / 50.8 %, 7.9 | 79.5 / 51.2 %, 12.4 |
| +12 dB | 78.3 / 51.6 %, 7.9 | 79.5 / 51.2 %, 14.8 |
- **Attack recall is level independent** (77.9-78.3 % and 78.9-79.7 %; fast 50.3-51.6 % and 50.6-51.2 %).
- **Spurious attacks scale with level**: legacy 4.2 -> 7.9, controller 4.2 -> 14.8 per minute over 24 dB. Quiet events (the precursors, below the gate in `FASTTRACK-GROUND-TRUTH.md`) cross the gate when the signal is hotter. At -12 dB the controller's spurious rate equals the legacy chain's; the earlier "controller 8.7 vs legacy 5.2" statement is true at the as-recorded level only.
- Pitch delivered drops at -12 dB (91.0 -> 89.5 % legacy, 90.0 % controller): low-level notes lose their start.
- Consequence for the next experiments: any detector built on post-pick level (AUC 0.93) or on the absolute gate is level dependent by construction. Make it relative to a running level or calibrate it to the real ADC level before comparing datasets.

## 4. Recommendations (adopted in the tooling)
1. Keep audio **as recorded** for tests; never peak-normalise per file (the absolute-gate behaviour is part of what is under test). Always state the level of a recording set (this census).
2. Treat the 0.5 ADC scale explicitly: a pedal-faithful test applies `FT_GAIN_DB=-6` to full-scale file material. -6 dB changes attacks by under 0.5 point and moves spurious by +0.4 (legacy) / -1.7 (controller).
3. Add a level sweep to the regression of any new detector (`level_sweep.sh`), and report spurious notes per minute at -12, 0 and +12 dB, not one number.
4. New recordings (protocol): peak about -14 to -12 dBFS on normal playing (the owner's clips are at -13.7), no clipping, no normalisation, record the interface gain.
5. **Level-matched listening:** `tests/fasttrack/level_match.py` sets each file to the same active RMS (default -24 dBFS) with one gain per file. The existing renders differed by 3 dB (legacy -23.0, controller -19.9 active RMS) and the dry guitar by 13 dB (-32.9): matched, they are all -24.0. Use it for any A/B comparison; the earlier renders were not level matched.
6. Open question for the hardware stage: measure the actual peak and active RMS at the pedal ADC (play, read `a_in`), and compare with the table above before deciding which dataset represents the instrument.
