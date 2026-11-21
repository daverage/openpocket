# FastTrack ground truth: what the existing labels actually say, and what must be recorded

Branch `synth3-fasttrack`. Host only. **No pitch estimator, onset detector or controller was changed.** Controller v0/v1 and all earlier results stay frozen. Nothing pushed.
Files: `tests/fasttrack/ground_truth_manifest.csv` (20 rows), `gt_format.py` (annotation format v1), `validate_annotations.py`, `convert_idmt.py`, `ft_eval_events.py` (scorer), `flux_reclass.py`, `labels_import.py` (Audacity/Reaper labels <-> events, onset candidate drafts), `make_recording_plan.py` -> `recording_plan.csv`, protocol in `docs/FASTTRACK-RECORDING-PROTOCOL.md`.

## 1. Answer first: what can proceed on existing data, and what needs new recordings
| question | existing data | verdict |
|---|---|---|
| pick-note recall and latency, isolated and up to 5 notes/s | Guitar-TECHS scales/single notes, IDMT licks | **can proceed** (timing +-25 ms: compare variants, not absolutes) |
| palm-mute picks | IDMT `MU` notes (868 scored, 788 isolated), Guitar-TECHS palm-mute takes (incomplete annotation) | **can proceed** |
| bend / vibrato / slide must not retrigger | IDMT BE/VI/SL (198 notes, confidence 0.4 in the converter: end pitch unknown) | **partly**: retrigger counts yes, pitch-track accuracy needs end pitches |
| dead / muted notes must not sound | IDMT `DN` (45 notes) | **weak**: 45 events, one player |
| onset-only false-onset rates on whole pieces | IDMT dataset 4 (direct): 236 pieces, no pitch | **can proceed** for onsets only |
| pitch accuracy and early pitch | Guitar-TECHS, IDMT (note-level) | **can proceed** |
| **picked vs legato** (hammer-on, pull-off) | **none**: Guitar-TECHS/IDMT have no such class; AG-PT-set records hammer-on but did not label it, and is acoustic | **needs new recordings** |
| **hard mute then a new pick at 20/40/80 ms** | none | **needs new recordings** |
| **fast runs at 12.5 / 16 / 20+ notes/s, labelled** | none: IDMT licks max 5.1/s; Guitar-TECHS fast windows reach ~21/s only as short chord-roll/mixed groups | **needs new recordings** |
| alternating loud/soft picks, soft after loud | none labelled by dynamics | **needs new recordings** (IDMT/AG-PT have intensity only per take, not per event) |
| incidental noise (fret, string contact, mute thump with no note) | AG-PT percussive hits (acoustic, not downloaded); Guitar-TECHS unannotated | **needs new recordings** for electric DI |
| the user's own playing (bleep3 di, fast play di) | audio exists, **no labels** | **label first**: cheapest labelled fast data (see protocol, section annotation) |

## 2. Dataset audit (full detail in the manifest CSV)
| set | signal | labels | what it distinguishes | timing accuracy |
|---|---|---|---|---|
| Guitar-TECHS P1-P3 (CC BY 4.0) | electric DI | hexaphonic MIDI per note | note pitch and onset; technique only by take name | per-take median offset to the audio flux -7..+22 ms; **pitch dependent: +20 ms for notes in the 3rd octave, +11 / +6 / +5 ms higher up**; p10-p90 within a take about 17 ms |
| IDMT-SMT-Guitar V2 (CC BY-NC-ND 4.0) | electric DI line-in (dataset 4 `acoustic_mic`/`acoustic_pickup`: **mic / acoustic, no onset labels, excluded**) | XML note events: pitch, string, fret, PK/FS/MU, NO/BE/SL/VI/HA/DN/ST/FL/TR | picked vs muted vs dead note, bend, slide, vibrato, harmonic; **no hammer-on, pull-off, hard mute** | -22..+32 ms against the audio energy rise (median +20, 999 notes); several recordings (AR_Lick4_*) have 25-50 % of onsets with no rise nearby |
| AG-PT-set (CC BY 4.0), **audio not downloaded** (6.7 GB acoustic; labels, README and metadata read remotely) | acoustic guitar, built-in pickups, 48 kHz | millisecond onsets for 8 techniques, 32592 notes: pick over soundhole 7657, **palm mute 7588**, pick near bridge 7227, tom 2167, harmonics 2092, snare-A 1968, kick 1950, snare-B 1943 | pluck vs percussive/muted-string hits; **hammer-on, bending, staccato and vibrato are recorded but unlabelled** | best of all (ms level) |
| owner recordings (`My Audio`) | electric DI | none | -- | -- |
Existing licences preserved: audio is never committed; IDMT audio is referenced by symlink only; attribution is in `NOTICE.md`.

**Correction to earlier text:** the docs said Guitar-TECHS timing was "+-10 ms". The per-take offset is small on average, but it depends on pitch (low notes are annotated about 15 ms later than high ones) and spreads about 17 ms inside a take; use +-25 ms as the working bound. IDMT is -22..+32 ms.

## 3. Reclassification of the 18579 flux picks (`flux_reclass.py`)
The earlier statement "55 % of flux picks are false" meant **55 % had no annotated onset in [-45, +15] ms**. It is not 55 % false. Ordered rules using annotation context and audio features, plus a visual check (below). Proposed labels, not verified by listening:

| class | n | share | median level rise | median level 20 ms after the pick | weak flag |
|---|---|---|---|---|---|
| genuine, aligned with an annotation | 8296 | 44.7 % | 10.7 | 0.035 | 22 % |
| **precursor before the pick**: flux 15-120 ms before an annotated onset whose primary pick is another flux | 4918 | **26.5 %** | 1.6 | **0.0031** | 70 % |
| **below the gate**: level after the pick under 0.003 (about -50 dBFS), mostly technique takes | 2476 | 13.3 % | 2.1 | 0.0006 | 55 % |
| genuine, annotation offset (nearest flux to its annotated onset) | 1617 | 8.7 % | 4.9 | 0.025 | 6 % |
| in-note fluctuation (vibrato, bend, decay ripple; level not rising) | 418 | 2.2 % | 1.2 | 0.009 | 86 % |
| **unannotated rise: UNCERTAIN** (level rising, no annotation) | 410 | 2.2 % | 4.2 | 0.008 | 36 % |
| note-end transient | 163 | 0.9 % | 1.95 | 0.0075 | 43 % |
| in-note rise: UNCERTAIN | 137 | 0.7 % | 1.6 | 0.011 | 71 % |
| silence noise | 130 | 0.7 % | 1.2 | 0.0055 | 72 % |
| late extra after the pick | 14 | 0.1 % | 1.15 | 0.0023 | 57 % |

Of the 10283 picks without a +-(-45,+15) match: 47.8 % are precursors, 24.1 % are below-gate noise, 15.7 % are genuine picks whose annotation is offset, and only about 12 % are decay/fluctuation/noise-type events or uncertain rises.
What the precursors are: in 61 % of annotated scale notes there are two or more flux picks within +-150 ms, and the extra ones come **before** the main pick (93 % earlier, median 25 ms, 72 % weak-flag). The plots (below) show the previous note's level collapsing to about the gate during a lift or damp, with the weak flux firing on that low-level movement 25-80 ms before the pluck. They are real acoustic events that are not note starts.
Visual check (9 stratified plots, `build/fasttrack/plots/flux_sample.png`, waveform + 2 ms level + all picks + annotated onsets/offsets): 3 of 3 sampled precursors show a level collapse before the pluck; the sampled PalmMute "unannotated rise" is a clear palm-muted pluck with a 7x level rise that the annotation does not list (the palm-mute takes annotate only some chugs: **the annotation is incomplete, the pick is genuine**); the sampled Bendings and PinchHarmonics "unannotated" picks sit at the -64 dBFS noise floor; the sampled note-end and in-note ones are a damp and a vibrato ripple. Nine plots are a sanity check, not a verification; I did not audition any audio.
**The one new strong feature:** the absolute level 20 ms after the pick separates genuine from the non-genuine classes with AUC **0.926** (against 0.85 for level rise and 0.83 for flux strength, the best features of the earlier study): 1.2 % of genuine picks, 48.8 % of precursors and 100 % of below-gate events have a post-pick level under 0.003. It is available 20 ms after the pick, i.e. at the current admission time. This is evidence for the next stage, not a rule; soft genuine picks (1-4 % of genuine) are the cost to measure.
**Do not call the 10283 "false picks": 15.7 % are genuine with an offset annotation and the precursor class needs a decision about what a synth should do with it (they are not note starts, but they are real).**

## 4. The +-1 semitone early-pitch errors
242 isolated single-note groups whose first confirmed pitch (3 agreeing frames and a repeating waveform) was +-1 semitone from the annotation. Their pitch 100-240 ms later: **62 % settle to the annotated pitch (an early transient / pre-settle error of the estimator), 3 % stay at the admitted pitch (bend, intonation, annotation or bias; mean offset -18 cents), 7 % move elsewhere, 28 % cannot be judged (note ends before 240 ms).** So the semitone errors are real early estimator errors in the first about 12 ms, not annotation artefacts or bends; the cause of the pre-settle is not yet known (the first -1 outnumbered +1 by 138 to 39 in the earlier count).

## 5. Annotation format v1, validator, scorer
`gt_format.py` defines the physical actions (PICK, REPICK, HAMMER_ON, PULL_OFF, SLIDE, BEND, VIBRATO, PALM_MUTE_PICK, HARD_MUTE, NOISE, LIFT), the expected synth event for each (NOTE_ON, REPICK, LEGATO_CHANGE, PITCH_UPDATE, DAMP, NOTE_OFF, NONE; defaults enforced by the validator unless a note explains the exception), a per-event confidence, and the timing basis (`intended` is only allowed with confidence <= 0.3: the metronome time is never truth). `recordings.csv` carries signal (DI only for the development claims), processing (`none`), the dev/holdout/calibration split and whether the annotation is complete.
`validate_annotations.py DIR` checks schema, enums, ordering, duplicates, file bounds, DI requirements, hammer-on without a ringing note, and **reports onsets with no audio energy rise within +-40 ms**. Run on the converted IDMT licks (194 recordings, 3725 events) it found **one exact duplicate annotation in the source XML** and 59 warnings (BENDs without end pitch; recordings with 25-50 % of onsets showing no energy rise).
`ft_eval_events.py GT OUT` scores pick detection (strum members collapsed to one event, any chord pitch counts), legato detection vs "treated as a new note", mute detection, noise false notes, bend/slide/vibrato retriggers, unlabelled attacks per minute, and stratifies picks by the actual inter-onset interval (<40, 40-60, 60-80, 80-125, 125-200, >200 ms); misses are infinite in the percentiles; confidence < 0.5 is reported separately.

First scores with it (IDMT licks, development data, annotation timing +-25 ms so absolute latencies, about 60 ms, are not comparable to Guitar-TECHS):
| | legacy CLASSIC (FOLLOW) | legacy, FOLLOW off | controller A (frozen) |
|---|---|---|---|
| PICK found (n=1744; isolated >200 ms: 1654) | 56.5 % | 73.3 % | 75.5 % |
| PALM_MUTE_PICK found (n=868) | 47.8 % | 80.2 % | 89.2 % |
| NOISE (dead note) produced a note (n=45) | 55.6 % | 55.6 % | **97.8 %** |
| unlabelled attacks per minute (19 min, complete annotation) | 4.8 | 13.3 | 30.4 |
| picks with the previous pick 40-80 ms earlier (n=42) | 0 % found | 0 % | 0-7 % |
The 42 fast picks show IDMT cannot test the fast stratum. The dead-note result is the first number on incidental noise: the controller with no veto plays 98 % of dead notes; the legacy chain with FOLLOW plays 56 %.

## 6. Recording and annotation protocol, expected behaviour
`docs/FASTTRACK-RECORDING-PROTOCOL.md` and `recording_plan.csv` (27 exercises): fast alternate picking at 8, 12.5, 16, 20, 24 notes/s; strong/weak alternation; soft after loud at 80/150/300 ms; hammer-ons, pull-offs, trills at 6-12.5/s; slides; bends and vibrato; palm-mute chugs then an open note; hard mutes then a pick at about 20/40/80 ms; mute and noise events with no note; string skipping and overlap; repick while ringing; free-play holdouts. Each exercise states the intended notes and technique and the synth event a correct system should give; annotation of the real acoustic timing follows the recording.

## 7. Uncertainty and limits
- The class labels in section 3 are rule-based proposals checked on 9 plots, not on audio. Precursors are described, not yet proven to be damp/lift events in every case.
- The IDMT converter treats MU as a palm-muted pick and DN as a non-note; both are my reading of the dataset description.
- AG-PT-set audio was not downloaded; nothing here scores against it.
- Guitar-TECHS and IDMT remain development data; nothing is reserved yet. The protocol reserves holdout takes.

## 8. What I would do next (evidence-based, not started)
1. **Label `bleep3 di.wav` and `fast play di.wav`** with the v1 format (`labels_import.py draft` gives 51 and 31 onset candidates to correct; plain energy-rise detector, not the tracker). They are the user's real fast material and cost no recording. A labelled version of bleep3 would also test the swallowed-note hypothesis directly.
2. **Record the protocol's E04, E07, E08 and E01 sets** (legato, hard mutes, noise, fast alternate picking). Without them the picked/legato, mute-recovery and 12.5-24 notes/s questions cannot be scored.
3. Then the pitch-aware onset study, with the post-pick-level feature (AUC 0.93) and the precursor class as the first things to test; then the full-rate YIN/MPM test on the semitone pre-settle errors.

## 9. Draft annotation of `bleep3 di.wav` and `fast play di.wav` (machine-assisted, UNVERIFIED)
Done by `draft_annotate.py` + `review_plots.py`; files in `tests/fasttrack/gt/own_draft/` (annotations only, no audio; the recordings point at local mono 24-bit copies of the left channel in `build/fasttrack/labelling/`).
**What it is:** an annotator that cannot hear. Onsets: an energy-rise detector independent of the FastTrack tracker, refined to the first waveform rise. Pitch: librosa pYIN. Class: transparent rules on the signal (pluck burst in 2-8 kHz, level collapse, pitch jump with no pluck burst, decay speed; rules in the script header). I reviewed 6 zoomed waveform+spectrogram strips (3 of them in full for both clips' first 4.5 s): picks sit at sharp rises, damps at level collapses, and the possible hammer-ons/pull-offs at bumps with no pluck burst; I did not verify every event, did not hear anything, and a NOISE rule that fired on pitch-estimator failures was changed after review. All confidences are capped at 0.7; `complete_annotation=0`. **A person must confirm or correct these before they count as ground truth.**
Content: bleep3 di 60 events (PICK 24, REPICK 10, HARD_MUTE 15, PULL_OFF 4, HAMMER_ON 1, PALM_MUTE_PICK 2, NOISE 4), fast play di 36 (PICK 15, HAMMER_ON 5, PULL_OFF 5, HARD_MUTE 8, REPICK 1, NOISE 2). The validator passes (0 errors, 4 warnings).
Diagnostic scores against the draft (events with confidence >= 0.5 only; n = 44 pick-like, 6 hammer-ons, 9 pull-offs, 23 damps: far too few for any conclusion, and the labels are unverified):
| | legacy CLASSIC (FOLLOW) | legacy, FOLLOW off | controller A |
|---|---|---|---|
| PICK found / REPICK found | 33 % / 9 % | 61 % / 45 % | 73 % / 45 % |
| wrong-pitch attacks on picks | 6 % | 15 % | 24 % |
| HAMMER_ON treated as a new note / legato event | 17 % / 0 % | 100 % / 0 % | 100 % / 0 % |
| PULL_OFF treated as a new note / legato event | 22 % / 33 % | 78 % / 33 % | 100 % / 22 % |
| HARD_MUTE: damp event | 48 % | 48 % | 52 % |
Reading, with the caveat above: on the user's own fast playing the production chain with FOLLOW makes an envelope attack for about a third of the picks, which fits the "misses notes" report; the controller finds most picks but plays 24 % at a wrong pitch; none of the variants has a working hammer-on/pull-off path, and about half the damps release nothing. These are hints for what to label properly, not results.

## 10. Reaper review sessions for the two draft annotations
`tests/fasttrack/reaper_session.py make|import`. Sessions (local, `build/fasttrack/labelling/review_bleep3_di.RPP`, `review_fast_play_di.RPP`) contain the **original stereo audio** (referenced by path), one project marker per proposed event coloured by class (a leading `?` = uncertain: confidence < 0.5 or any legato / hard-mute / noise proposal: 30 of 60 on bleep3, 20 of 36 on fast play), grey `CAND?` markers where a more sensitive detector (5 dB rise) found an onset the draft did not keep (25 and 1), a track **UNCERTAIN: listen** with an empty item at every uncertain proposal, and a track **GAPS: any missed pick?** with an item over each stretch of 120 ms or more where sound continues between proposals (28 and 26), to check for notes no marker covers. Review: move a marker to the first waveform rise, rename it to the right action/pitch (`PICK 45`, `HAMMER_ON 47`, `HARD_MUTE`...), delete false ones, add markers for missed notes. `import SESSION.RPP RECORDING out.csv` reads the saved project back: markers you moved or renamed count as reviewed (confidence 0.9, basis `listening`); markers unchanged from the proposal, or still starting with `?`, stay unreviewed (`--accept-unchanged` accepts the unchanged non-`?` ones; the `?` ones must be touched). The generated sessions were not opened in Reaper here (the format follows your own project files); if one does not open, use the label text files in the same folder instead.
