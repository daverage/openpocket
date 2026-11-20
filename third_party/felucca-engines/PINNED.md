# Felucca-Engines (vendored)

Upstream: https://github.com/hugelton/Felucca-Engines, commit `d0260074ce100a47675814b7f38d61f11176c926` (2026-10-28). MIT licence (`LICENSE`),
Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. Source files are unmodified copies; their SPDX headers are kept.

Used: `src/felucca_engine.h`, `src/dsp.c`, `src/eng_analog.c`, `src/eng_lofi.c`, `src/eng_wheel.c` (added for Synth2 stage B; an unmodified copy at the same pinned commit; upstream's golden.txt has no WHEEL lines, so it is checked by OpenPocket's own tests only) (more engines are added only when a stage needs them).
`fe_tables.h` is the output of `tools/gen_engine_tables.py` (44.1 kHz, the default; byte-identical to upstream's build/fe_tables.h).
`tests/golden.txt` is upstream's list of 122 render hashes; `tests/felucca_ref.sh` renders the vendored engines through a copy of upstream's standalone host
(`tests/fe_host_ref.c`, reduced to the engines present) and compares the matching lines. Not imported: the other engines, Felucca's host, sequencer, UI.
OpenPocket's own host adapter is `engine/px_synth_adapter.h`.
