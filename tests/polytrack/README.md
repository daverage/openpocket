# PolyTrack X host prototype (research, no firmware)

Host-only Python (numpy, scipy, matplotlib). Nothing here touches `firmware/` or `engine/`.

```
cd tests/polytrack
python3 -W ignore bench.py tune     # sweeps small grids on TUNING split (guitars A,B, seeds 0-2) -> protocol.json
python3 -W ignore bench.py test     # frozen protocol on HELD-OUT split (guitars C,D, seeds 100-102) -> results.json
python3 -W ignore quick.py pt,klap mono,poly3          # fast look while developing (tuning split only)
python3 -W ignore realdi.py "../../My Audio/scale + duo + power chords di.wav" ../../build/polytrack   # piano roll + event-driven synth WAV
```

* `synth.py` synthetic guitar DI with exact ground truth (4 instrument parameter sets, 15 scenarios)
* `detectors.py` `HS` harmonic summation, `Klap` iterative cancellation, `NNLS` plain joint sparse fit, `PT` candidate, `YIN` mono
* `metrics.py` framewise P/R/F1, octave and harmonic false positives, onset latency, retriggers, cents error
* `protocol.json` parameters + acceptance criteria, written by `tune` before `test` is ever run
* `results.json`, `test_run.log` the recorded held-out run

Results and caveats: `docs/POLYTRACK-X-BENCHMARKS.md`.
