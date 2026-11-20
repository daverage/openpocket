# VOICE (the original FM-1 synth's formant engine) evaluation

`eng_formant.c` is an **unmodified** copy of `firmware/src/eng_formant.c` from `guysoft/blackbox-fm1`, branch `blackbox-fm1`, commit `312f1035504a96ba6a36ab042f917e7c558de763` (2026-11-19), SPDX `GPL-3.0-only`, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments (upstream: `hugelton/Felucca`). OpenPocket is GPL-3.0-only, so the licences match.
It is **not** part of the firmware: `run.sh` builds a scratch copy of `engine/` and `third_party/` under `build/voice_eval/`, adds the engine as INSTRUMENT patch 45 (`patch_scratch.py`), renders steady notes through the tracker and the adapter and analyses them (`analysis.py`, needs numpy).
Results: `docs/research/synth2/SYNTH2-VOICE-CHOIR.md`.
