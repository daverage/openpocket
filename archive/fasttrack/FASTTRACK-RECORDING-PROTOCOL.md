# FastTrack recording and annotation protocol (short)

Purpose: labelled DI recordings of the playing actions the synth must distinguish. Exercises, intended notes, rates and expected synth events: `tests/archive/fasttrack/recording_plan.csv`. Format: `tests/archive/fasttrack/gt_format.py`.

## Capture
- Standard mono electric-guitar pickup into the audio interface line/instrument input. **Dry DI**: no compressor, gate, distortion, pitch or any plug-in; no amp simulation in the recorded signal (monitor however you like).
- 44.1 kHz, mono, 24-bit WAV. Peak about -12 dBFS on normal playing (never clipping); keep the noise floor honest, no noise reduction.
- Metronome or click in headphones only, never in the file. The player will not play on the grid: that is why timing is annotated afterwards.
- One file per exercise take; name `E04a_take1_<guitar>_<yyyymmdd>.wav`. Note guitar, pickup position, strings, player, date in `recordings.csv`.
- Splits: take 1-2 are `dev`; take 3 is `holdout`, recorded on another day or another guitar where possible. E11 free playing is `holdout` only. **Holdout recordings are not opened during development** (no listening, no plotting) until a stage is declared final.
- Hard mutes and noise (E07, E08): record both loud and soft versions; for E08 include events with no note intended (fret-hand placement without a pick, string contact, pick scrape, palm slap on muted strings).
- Legato (E04): hammer-ons and pull-offs with **no pick**, one picked note then the legato note; semitone and whole tone; low string (E2) and high string (E4); slow isolated and trills at 6, 8, 10, 12.5 notes/s.
- Fast runs (E01/E02): at 8, 12.5, 16, 20, 24 notes/s. Say in the file notes which rates you could not hold; do not fake them.

## Annotation (after recording)
1. `python3 tests/archive/fasttrack/labels_import.py draft FILE.wav cand.txt` writes onset candidates (a plain energy-rise detector, not the tracker). Open the WAV and `cand.txt` as a label track in Audacity or Reaper.
2. **Zoom in and place every label at the first waveform rise of the actual event.** Add missed events; delete false candidates; do not accept the grid or the candidates unseen. A candidate-only annotation inherits the detector's blind spots.
3. Label text `ACTION[ pitch_midi[ confidence]]`, for example `PICK 57 0.9`, `HAMMER_ON 59`, `PULL_OFF 57`, `PALM_MUTE_PICK 40`, `HARD_MUTE`, `NOISE 0.4`, `LIFT`. Use confidence below 0.5 for anything you are unsure of (timing or class); those events are scored separately.
4. Export labels (tab separated), then `python3 tests/archive/fasttrack/labels_import.py import RECORDING labels.txt events.csv --basis waveform --append`.
5. `python3 tests/archive/fasttrack/validate_annotations.py DIR` must report 0 errors. Read the warnings: an onset with no energy rise nearby usually means a mislabelled time.
6. A second annotator on 10 % of the events (any file) gives the inter-annotator timing spread; report it with the results.
7. Expected synth events are the plan's targets for evaluation (PICK -> NOTE_ON, REPICK -> REPICK, HAMMER_ON/PULL_OFF -> LEGATO_CHANGE, SLIDE/BEND/VIBRATO -> PITCH_UPDATE, HARD_MUTE -> DAMP, NOISE -> NONE). They are not rules for the detector.

## Cheapest first
Label the existing `My Audio/bleep3 di.wav` and `fast play di.wav` (51 and 31 candidates) before recording anything.
