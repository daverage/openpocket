# tests/fasttrack: FastTrack host research harness

Host-only research on the Synth3 note tracker (branch `synth3-fasttrack`). **Nothing here changes `engine/`, `firmware/` or any preset**: engine experiments are applied to a build-time copy of `engine/` (`mk_overlay.py`). Results and conclusions: `docs/FASTTRACK-*.md`. Quick start: `regress.sh` (rebuild everything, check the headline numbers: about 1 minute with 8 cores).

## Data
`build/stage5/sets/<set>/NAME.wav + NAME.csv` (onset s, offset s, midi). `gtechs` (Guitar-TECHS DI) from `tools/research/prep_sets.py gtechs`; `idmt_licks`, `idmt_pieces`, `idmt_d4` are symlinks to the unchanged IDMT audio made by `idmt_inventory.py` (licence CC BY-NC-ND: never commit audio). Levels are used **as recorded**; see `docs/FASTTRACK-LEVELS.md`. A stereo file gives its left channel (the pedal's guitar input).

## Harness (C)
| file | what |
|---|---|
| `ft_wav.h` | shared WAV reader (left channel, `FT_GAIN_DB`) and writer |
| `ft_trace.c` | the real mono tracker (`px_mono.h`) alone; logs each stage (R/H/S/U/N/C/L/X events) + pitch/gate trace |
| `ft_synth.c` | the whole legacy synth path (tracker, wrapper, adapter, Felucca) with suppression hooks; `A` = a synth envelope attack |
| `ft_ctrl.h`, `ft_ctrl_trace.c` | experimental event controller v0/v1 (frozen baseline) + neutral oscillator, oracles `FT_ORACLE_ONSET` / `FT_ORACLE_PITCH` |
| `ft_early.c` | probe: raw tracker evidence around annotated attacks and every flux pick |
| `mk_overlay.py` | builds `build/fasttrack/eng_root/engine` with `FT_HOOKS` (diagnostics), `FT_FOLLOW`, `FT_MUTE_K`, `FT_YIN`, `FT_LEAKY` |
| `build.sh NAME [-D..]`, `run_set.sh VARIANT SET`, `one.sh` | build one variant (`FT_SRC=`, `FT_OVERLAY=1`) and run it over a set (8 parallel) |
| `ablate.sh` | the single-knob ablations of `PX_MONO_*` |
| `regress.sh`, `regress_expected.txt` | rebuild and compare against the recorded headline numbers |
| `level_sweep.sh` | input-level sensitivity (-12..+12 dB) |
| `patch_latency.py` | pluck-to-audible time per synth patch (synthetic pluck through the real chain) |

## Scoring and analysis (Python)
`ft_score.py` (legacy scorer, recall/latency/false counts), `ft_attrib.py` (where each pick is lost; pitch / event / attack), `ft_compare.py` (side-by-side table), `early_analysis.py`, `early_rules.py`, `onset_study.py`, `flux_reclass.py` (pick classification), `idmt_inventory.py`, `idmt_bench.py`.

## Ground truth
`gt_format.py` (annotation format v1), `validate_annotations.py`, `convert_idmt.py`, `ft_eval_events.py` (scores physical actions), `labels_import.py` (Audacity labels, onset candidates), `reaper_session.py` (Reaper review session and import), `draft_annotate.py` + `review_plots.py` (machine-assisted draft, unverified), `gt_manifest.py` -> `ground_truth_manifest.csv`, `make_recording_plan.py` -> `recording_plan.csv`, `gt/own_draft/` (draft annotations of the owner's clips), `level_match.py` (level-matched A/B renders).

## Conventions
- Variant outputs go to `build/fasttrack/out/<set>/<variant>/` (git-ignored, regenerable). Time is ms from the first sample. Scoring uses +-50 cents.
- No harness file may change production behaviour; engine edits belong in `mk_overlay.py`.
- Misses count as infinite latency in percentiles (`p90*`).
