# FastTrack documents: reading order and what is superseded

Branch `synth3-fasttrack` (base `synth3` a35ca9b). Host-only research on the note tracker feeding the Felucca synths. Harness and commands: `tests/fasttrack/README.md`.
1. `FASTTRACK-BASELINE.md`: production mono tracker, latency stages, single-knob ablations of `PX_MONO_*`.
2. `FASTTRACK-EVENT-AUDIT.md`: the event path from pick to synth envelope, per-pick loss bins, old-rule to proposed-replacement table (design only).
3. `FASTTRACK-EVENT-BENCHMARKS.md`: FOLLOW isolation, three-layer metrics, event controller v0/v1 and its failure (false notes).
4. `FASTTRACK-EARLY-EVIDENCE.md`: oracle onset/pitch experiments (onset limits fast recall, pitch limits wrong notes), early-pitch probe, onset-evidence study.
5. `FASTTRACK-GROUND-TRUTH.md`: dataset audit, reclassified flux picks, annotation format, recording plan, draft annotation of the owner's clips, Reaper review sessions. `FASTTRACK-RECORDING-PROTOCOL.md`.
6. `FASTTRACK-BENCHMARKS.md`: IDMT-SMT-Guitar added to the corpus, mute-threshold / YIN / leaky-vote experiments (negative results).
7. `FASTTRACK-LEVELS.md`: input level and normalisation review.

Corrections that supersede earlier statements: "55 % of flux picks are false" -> they are unmatched to annotated onsets (most are precursors and below-gate noise; GROUND-TRUTH section 3); annotation timing "+-10 ms" -> "+-25 ms, pitch dependent"; "controller makes 67 % more spurious notes" -> true at the as-recorded level only (LEVELS section 3); "wrong initial pitches are upper partials" -> mostly not (EARLY-EVIDENCE section 2, GROUND-TRUTH section 4).
Status: the production tracker is still the best implementation; nothing here is firmware-ready. Open: label the owner's two fast clips (Reaper sessions in `build/fasttrack/labelling/`), then the pitch-aware onset experiment.

## Known pre-existing issue (not from FastTrack)
`make test` stops at `tests/test_audio.mjs`, check "a preset change changes the spectrum" (`harmonics over fundamental: 0.294 0.169`). It fails identically on the untouched base commit a35ca9b in a clean worktree: the check compares factory slot 0 with a fuzz preset, and slot 0 is no longer a clean placeholder since commits 207aacf / ddc5d0c ("the first 12 factory presets show the synths"). Everything before it passes (172 checks: engine, store, editor, flasher, MIDI, pedal UI). The test needs updating by whoever owns the factory presets; the remaining page tests after it did not run because the script aborts. `engine/`, `firmware/` and `third_party/` are byte-identical to the base.
