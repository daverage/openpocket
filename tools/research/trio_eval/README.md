# TRIO / CHIP CHOIR evaluation (Synth2 stage D)

`eng_trio.c` is an unmodified copy of Felucca-Engines `src/eng_trio.c` at the pinned commit `d0260074` (MIT, SPDX header kept). It is **not** part of the firmware: `run.sh` builds a scratch copy of `engine/` and
`third_party/` under `build/trio_eval/`, puts TRIO in as ENSEMBLE patch 45 (`patch_scratch.py`), renders steady notes through the tracker and the adapter, and analyses them (`analysis.py`, needs numpy).
Results and the verdict: `docs/research/synth2/SYNTH2-CHOIR.md`.
