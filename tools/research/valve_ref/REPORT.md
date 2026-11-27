# 12AX7 stage: DSP vs SPICE validation

Branch `dev`. **No firmware or engine file was changed.** Everything is in `tools/research/valve_ref/`.

## Answer to the key question

**Yes.** The lookup-table triode reproduces the electrical response of the circuit it claims to model (the Dempwolf & Zölzer equations, 250 V, 100k, 1.5k ∥ 2.2 µF, 33k). Step 1, below, closed the one open discrepancy.

### Step 1: unloaded SPICE, fed from the DSP's own oversampled grid voltage

SPICE here has a practically infinite output load (`RL = 1e9`, `Cc = 22 pF`, so τ is still 22 ms and the plate load is the 100k alone, as in the DSP's load line). Its input is the DSP's own 88.2 kHz grid voltage, so the up-sampler is excluded and the samples line up one for one. `step1_run.py` runs it and `step1_analyze.py` measures it, using the unloaded run, the DSP, and the earlier 1 MΩ-loaded run for comparison (`results/step1_results.csv`).

| Measurement | DSP | SPICE unloaded | SPICE 1 MΩ loaded (earlier) |
|---|---|---|---|
| Plate-current gain, 220 Hz, 0.05 | +0.011 / +0.012 dB vs SPICE (0, +12 dB) | reference | +0.47 / +0.45 dB |
| Plate-voltage gain (V/V) | 35.79 / 35.67 dB | 35.78 / 35.65 dB | 35.42 / 35.28 dB |
| H2 (0, +12 dB) | −42.69 / −29.92 dBc | −42.72 / −29.95 dBc | −41.74 / −28.88 dBc |
| H3 (0, +12 dB) | −71.5 / −45.9 dBc | −72.0 / −46.0 dBc | −70.5 / −44.8 dBc |
| THD (0, +12 dB) | 0.734 / 3.231 % | 0.732 / 3.221 % | 0.819 / 3.645 % |
| IMD 440 + 660, 0.02 (0, +12 dB) | 0.299 / 1.215 % | 0.308 / 1.213 % | 0.340 / 1.361 % |
| Bias shift under a 0.3-unit burst (+12, +24 dB) | 0.107 / 0.475 V | 0.107 / 0.475 V | 0.136 / 0.520 V |
| Bias recovery time constant (+12, +24 dB) | 1.66 / 1.71 ms | 1.66 / 1.70 ms | 1.74 / (fit failed) ms |
| Residual bias left by the probe tone (+12, +24 dB) | 0.44 / 7.2 mV | 0.44 / 7.2 mV | not meaningful |

**The 0.5 dB discrepancy is gone.** Against the unloaded circuit the DSP's plate-current gain differs by 0.01 dB, and every distortion, intermodulation and bias figure agrees within about 3 % (H3 at 0 dB is about 0.4 dB apart, at −72 dBc). This also settles the plate-current versus plate-voltage question: loading **raises** plate-current gain (+0.47 dB measured, +0.52 dB predicted) and **lowers** plate-voltage gain (−0.36 dB), exactly as expected for a lower AC plate resistance. The earlier "DSP 3–14 % lower" distortion figures were the load, not the DSP.

**Recovery time, repaired.** The old metric thresholded a window average against the end of the record. The new one fits `vk(t) = c + D·e^(−t/τ)` plus the 220 Hz ripple harmonics over 2 to 200 ms after the burst (fit error below 0.01 mV). The recovery time constant is 1.7 ms (50 % in 1.15 ms, 90 % in 3.8 ms), about half of Rk·Ck = 3.3 ms. This is probably because the tube is still conducting during recovery, so the capacitor also sees the tube's 1/gm in parallel with Rk. I did not test that explanation. DSP and SPICE agree to 0.3 %.

**Freeze the lookup-table algorithm.** Nothing here supports any change to `px_dsp.c`, `gen_triode.py` or the tables.

### Earlier full-matrix results (1 MΩ-loaded SPICE, band-limited input)

These were produced before the load was found, so the DSP-lower-than-SPICE gaps below are the load. The unloaded Step 1 table above supersedes them for gain, THD, IMD and bias; they remain valid for the compression point, aliasing and the Koren comparison.

| Quantity | DSP vs SPICE Dempwolf (loaded) | Verdict |
|---|---|---|
| Idle point (Vk, Vp, Ia) | 1.2325 V / 167.84 V / 0.8216 mA on both, to 4 digits | exact |
| 1 dB compression point | 1.58 V at the grid, identical; 1.50 V at +24 dB, identical | exact |
| Compression depth at 3 V | DSP −4.94 dB, SPICE −5.10 dB | 0.16 dB (load) |
| Cathode bias shift (0, 6, 12, 16, 24 dB) | DSP 0.13 / 0.20 / 0.28 / 0.33 / 0.52 V, SPICE 0.13 / 0.22 / 0.30 / 0.36 / 0.56 V | load; matches unloaded in Step 1 |
| THD at 220 Hz, 0.05 units | DSP 0.73 / 1.50 / 3.23 / 5.22 / 23.0 %, SPICE 0.82 / 1.67 / 3.65 / 6.06 / 23.8 % | load; matches unloaded in Step 1 |
| Aliasing (non-harmonic energy, 220 Hz to 1 kHz sines) | DSP −104 to −118 dBc | negligible |

## Step 2: what the surrounding circuit adds (Dempwolf equations in every variant)

`step2.py` builds the variants, `step2_analyze.py` measures them (`results/step2_ac.csv`, `results/step2_results.csv`, `figs/step2_*.png`). The tube equations, operating point and 33k / 100k / 1.5k ∥ 2.2 µF values are unchanged from the validated baseline. The input is the same band-limited sample stream as before, now applied as a source EMF. Frequency response comes from SPICE's small-signal AC analysis (no noise floor). Distortion and dynamics come from transient runs at 0, +6 and +12 dB.

| Variant | Adds |
|---|---|
| A | the validated unloaded stage, ideal voltage source (control) |
| B | grid-plate 1.7 pF, grid-cathode 1.6 pF, plate-cathode 0.46 pF (typical 12AX7 data-sheet values) |
| C | B + 1 MΩ grid leak, and a source impedance of 0 Ω (C0), 10 kΩ (C10k) or a passive single-coil pickup with cable (Cpu: 6 kΩ, 2.5 H, 500 pF) |
| D | C + 22 nF input coupling capacitor, and 22 nF output coupling into a 1 MΩ external load (D0, D10k, Dpu) |

### Frequency response (plate, relative to A)

| Circuit | 1 kHz | 10 kHz | 20 kHz | Phase at 10 kHz | −3 dB point |
|---|---|---|---|---|---|
| B (parasitics) | 0.00 dB | −0.24 dB | −0.91 dB | −13.7° | 41 kHz |
| C10k (10 kΩ source) | −0.08 dB | −0.49 dB | −1.52 dB | −17.5° | 33 kHz |
| Cpu (pickup, with parasitics) | +0.5 dB | −14.3 dB | −27.9 dB | n/a (resonant) | 6.3 kHz, +14.6 dB peak at 4.0 kHz |
| Cpu without valve capacitances | +0.4 dB | −11.9 dB | −25.4 dB | n/a (resonant) | 7.1 kHz, +15.8 dB peak at 4.5 kHz |

- **The valve's own parasitics are minor.** The Miller effect costs 0.9 dB at 20 kHz and nothing audible below 5 kHz.
- **The source impedance dominates.** A 10 kΩ source costs 1.5 dB at 20 kHz. A passive pickup produces a resonant peak of about +15 dB near 4 kHz followed by a steep roll-off, and that is **independent of the valve**: the pickup, cable and grid leak give the same shape with no valve capacitances at all. The valve's Miller capacitance (about 100 pF at the grid) moves that peak from 4.5 kHz to 4.0 kHz and trims it by 1.1 dB. The pickup model has no eddy-current losses, so its peak is probably exaggerated.
- That input-side behaviour happened before the Blackbox's ADC, so no software valve stage downstream can recreate it. It only matters if you choose to emulate a pickup/input-network.

### Nonlinearity at 220 Hz and IMD (0, +6, +12 dB; deltas against A)

| Circuit | Gain vs A | THD vs A | IMD vs A |
|---|---|---|---|
| B, C0 (parasitics, grid leak) | +0.01 dB | +0.2 % | 0 % |
| C10k | −0.08 dB | −1 % | −1 % |
| Cpu | −0.02 dB | 0 % | +1 to +2 % |
| D0, D10k, Dpu | +0.27 to +0.38 dB | +16 to +19 % | +15 to +19 % |

The 220 Hz and 440 + 660 Hz distortion is **unchanged by capacitances or source impedance**. All the change comes with variant D. Its output load is 1 MΩ, the same load that produced +0.47 dB and +12 % THD in Step 1. Variant D also adds an input capacitor, but I did not run the two separately, so I can't apportion the change; the size and sign match the load.

### Loud-burst bias behaviour (+12 dB, 0.3-unit burst at 220 Hz: peaks around 4.8 V at the grid)

| Circuit | Cathode shift during burst | Recovery |
|---|---|---|
| A, B, C0, C10k, Cpu | +0.09 to +0.11 V (up) | 1.66 ms time constant, identical |
| D0, D10k, Dpu | **−0.37 to −0.39 V (down)** | about 25 ms |

The input coupling capacitor reverses the bias shift. Grid conduction on the positive peaks charges the 22 nF capacitor (grid DC about −3.1 V during the burst, against −0.8 V without it), and the capacitor then discharges through the 1 MΩ leak over about 22 ms. This is real circuit behaviour (blocking distortion) and it is absent from the DSP and from variants A–C. It needs positive grid excursions, so it should not appear at normal clean levels. The single-exponential fit is poor for D (15–18 mV residual), so treat the 25 ms as approximate. `figs/step2_bias_burst.png` shows it.

### Hard pick transient (1.5 kHz, 10 ms burst, +12 dB)

Peak amplitude changes by at most 0.3 dB for every variant. A 10 kΩ source delays the current by 1 µs, and the pickup by 32 to 50 µs. The pickup raises the grid peak from 6.4 V to 10.1 V (resonant ringing), which is an input-network effect.

### What Step 2 means for the DSP

1. **The valve's own parasitic capacitances are not worth modelling.** The loss is under 1 dB at 20 kHz and the distortion is untouched.
2. **Loading and coupling are the circuit choices that change level and distortion by tenths of a dB and roughly 15 %**, and they are choices about the next stage, not valve physics. No correction filter is proposed.
3. **A candidate for a real difference: grid-conduction bias shift** from an input coupling capacitor, but only on hard peaks. Whether it is audible at the intended clean-to-lightly-driven range is a listening question for Step 3.
4. **The largest spectral effect is the pickup interacting with the input network**, outside the valve and outside the Blackbox's reach.

### Limits of Step 2

- Dempwolf only. The independent Koren repeat is not done.
- The pickup is a simple lumped model (no eddy losses, no tone or volume pot), and capacitances are typical data-sheet values with no stray wiring. Treat the pickup numbers as an upper bound on the effect.
- Distortion was measured at 220 Hz and 440 + 660 Hz only, not at high frequencies where capacitances matter most.
- Variant D bundles input coupling and an output load, so their effects are not separated. The bias fit for D is poor.
- The hard-pick transient and the burst were run at +12 dB only.

## Dempwolf vs Koren (model difference, not a DSP fault)

An independent Koren 12AX7 model gives a different valve, as expected for a different fit:
- Idle point: Vk 1.198 V, Vp 170.1 V, Ia 0.799 mA, against 1.232 V, 167.8 V, 0.822 mA for Dempwolf.
- Small-signal gain about 0.6 dB lower than Dempwolf, under the same load.
- THD 30–40 % higher at low drive (5.07 % against 3.67 % at 0 dB, 0.2 units), converging above +12 dB.
- IMD 40–60 % higher at low and mid drive (SMPTE 0.02: 0.81 % against 0.54 % at 0 dB).
- Bias shift 9–14 % smaller than Dempwolf above 0 dB.

The DSP is therefore closer to Dempwolf than to Koren, as it should be. **I can't say which model is closer to a real valve.** That needs a measured tube, not another model.

## Ranked discrepancies

| # | Finding | Electrical importance | Perceptual importance | Status |
|---|---|---|---|---|
| 1 | 0.5 dB gain offset DSP vs SPICE | was 3–14 % on distortion ratios | none | **closed**: it was the 1 MΩ load in my netlist; unloaded agrees to 0.01 dB |
| 2 | DSP bias shift 0–9 % smaller | was small | none | **closed**: same cause |
| 3 | Dempwolf vs Koren: THD +30–40 %, IMD +40–60 % at low drive | model choice | possibly audible as a little more low-level grit | not a bug |
| 4 | Hard-driven 0.2 units at +24 dB: DSP THD 36.7 % vs 36.9 % | none | none | agree |

**Recommended code changes: none.** Nothing here supports altering `px_dsp.c`, `gen_triode.py` or the tables. If you want the model to be more or less "grippy" at low drive, the lever is the choice of tube fit (Koren-like vs Dempwolf), and it should be decided by listening and by measuring a real 12AX7.

## What is not established (read before relying on this)

- **Unloaded re-run is limited to Step 1's signals.** 220 Hz sine, 440 + 660 Hz and the bias burst, at two drive levels each. The compression sweep, frequency response and guitar clips were not repeated unloaded.
- **SPICE noise floor.** At small signal (0.01–0.05 units at 0 to +12 dB) SPICE's own numerical noise is about −84 dBc, so SPICE can't resolve distortion below that. This is why the 0 dB small-signal chirp is meaningless. The +24 dB chirp is usable. Frequency-response numbers above about 10 kHz are also unreliable, because the SPICE input is a band-limited upsample that droops near Nyquist.
- **Bias recovery time.** `results/bias_recovery.csv` now holds the repaired fit for all five drive levels. The original broken values are kept as `results/bias_recovery_original_BROKEN_metric.csv`. In the loaded SPICE rows with `fit_ok = False`, the fit residual is high (SPICE noise) and the time constant should not be trusted.
- **Koren grid current.** Gg = 6.2e-4 A/V^1.5 and Vct = 0.2 V are my assumptions, not part of Koren's 12AX7 set. They only matter above about +16 dB.
- **Processed-output residuals** (−25 to −28 dB vs Dempwolf) in the full matrix include the 0.5 dB load offset and the half-band up-sampler's phase, so they overstate the valve error. The DSP-fed Step 1 runs show the valve stage itself agrees much more closely.
- **Parasitic capacitances.** Koren + Miller capacitance was run for 440 and 1000 Hz sines and the chirp only. The DSP has no equivalent, and I did not quantify its effect.
- **OBSOLETE: blind-listening audio uses the loaded netlist** (see `audio/README_OBSOLETE.md`; the WAVs are not in Git). The SPICE versions are about 0.5 dB quieter in plate current and differ slightly in distortion. Regenerate with an unloaded netlist before using it for a listening judgement.
- **Figures** in `figs/` overlay the DSP on the earlier 1 MΩ-loaded SPICE runs, so small gain offsets in them are the load (Step 1).
- **Not covered:** the knee hack (20 V plate-to-cathode) was only exercised at hard drive. Its accuracy at that point agrees with SPICE (see the +24 dB rows) but I didn't isolate it.

## Reproducing

```
cd tools/research/valve_ref
cc -O2 -std=gnu11 -w -fno-builtin -ffp-contract=off -I../../../engine valve_host.c ../../../firmware/px_math.c -lm -o build_valve_host
python3 signals.py                 # test inputs + manifest
python3 run_all.py -j 8 [name ...] # DSP + SPICE (ngspice 47; set OMP_NUM_THREADS=1, one process per core)
python3 analyze.py                 # results/*.csv
OMP_NUM_THREADS=1 python3 step1_run.py   # the six short unloaded, DSP-fed runs -> data/spice_hiZ
python3 step1_analyze.py                 # results/step1_results.csv
python3 step2.py ac; python3 step2.py run   # step 2 circuits -> data/step2
python3 step2_analyze.py                 # results/step2_*.csv, figs/step2_*.png
python3 make_figs_audio.py         # figs/*.png, audio/named, audio/blind + blind_key.json
```

`valve_host` `#include`s `engine/px_dsp.c`, so it runs the same `triode_block` as the firmware. Its state-exporting copy matches `triode_block` within float rounding (1e-7 relative to the peak).

## Files

- `models/dempwolf.lib`, `models/koren.lib`: SPICE valve models. `gen_spice.py` writes the circuit netlist.
- `results/*.csv`: DC point, sines (harmonics H2–H8, THD, THD+N, non-harmonic floor), IMD, compression, bias, frequency response, residuals.
- `figs/*.png`: overlays of DSP, Dempwolf, Koren.
- `audio/named/`, `audio/blind/` (+ `blind_key.json`): four guitar clips at five drive levels.
- Raw runs (3 GB) are in `data/` and are git-ignored.

## Circuit differences, documented

| | DSP | SPICE |
|---|---|---|
| Output load | none (infinite) | 1 MΩ, 21 nF in the full matrix (the cause of the 0.5 dB offset); 1 GΩ, 22 pF in Step 1 |
| Output coupling | one-pole DC removal, 22 ms | real coupling capacitor |
| Plate dynamics | none (static current) | none (Dempwolf netlist) / 0.9–2.4 pF (Koren + capacitance variant) |
| Cathode capacitor | explicit Euler, current held over each 2× sample | trapezoidal, adaptive step |
| Input | 44.1 kHz through the engine's half-band up-sampler | band-limited ×4 upsample |
| Plate below 20 V | hand-made knee roll-off | model equations only |
