# Analogue pedal emulations (SFX types)

Four SFX types model analogue pedals as **circuits and structures**, not as fitted waveforms. They sit after ENHANCE in the SFX list (ids unchanged, each excludes the other SFX types):

| Type | Controls | What it is | Confidence |
|---|---|---|---|
| **ENHANCE** (Better Box) | BETTER, LOUDER | a compander gain cell: the gain follows a fast rectified copy of the signal (no threshold, works at every level); harmonics without clipping | the mechanism is a hypothesis from the maker's words; every constant is a guess. No real unit was measured |
| **RANGEMASTER** | VOLUME, PICKUP, BODY | the Dallas Rangemaster solved as a circuit: germanium Ebers-Moll stage, R1 470k, R2 68k, R3 3.9k, C1 5n, C3 47u, RV 10k, 9 V, and the guitar's pickup as the source (series R and L) | high for the circuit (reproduces ElectroSmash's numbers); the transistor is a typical part (Is, bf, br are choices) |
| **EP BOOST** | GAIN, VOICE | an EP-3-style JFET stage (2N5484 numbers measured by Aion FX, 18 V, drain 22k, source 3.3k bypassed by 22 nF..470 nF) | **low**: the Xotic EP Booster's schematic is not published (others call it "inspired by" the EP-3); topology and values are a family guess |
| **MAXIMIZER** (BBE) | LO CONTOUR, PROCESS | the BBE process as a structure: three bands, the low and mid bands delayed against the high band, band gains | structure from the published description; crossovers, delays and gains fitted to the real BBE 882i's measured response (0.76 dB RMS); the split of the two knobs is an assumption (only LO = PROCESS was measured) |

## Why circuits
A boost pedal does different things for different pickups and playing levels. The Rangemaster's 10-12 kOhm input makes the guitar's inductance part of the circuit; the same stage gives a mid-focused boost from a single coil and less treble from a humbucker, and distorts at different levels. So the source (PICKUP: series resistance 6k..9k, inductance 2.5..5.5 H) is modelled, and the stage is solved per sample (trapezoidal capacitors and inductor, a 2x2 Newton for the transistor with SPICE-style junction limiting). **Level**: one engine unit is 2 V of pickup EMF (engine 0.5 is converter full scale), and the engine output is 0.048 per collector volt (Rangemaster); both are choices (`PXP_EMF_PER_UNIT`, `PXP_OUT_PER_VOLT`). What a real guitar's EMF is differs by guitar and pick: the engine's guitar level (peaks about 0.14) is itself uncertain.

## How it was checked
* `tools/research/pedals/rm_circuit.py` is the full nodal simulator (7 nodes, Newton per step). It reproduces ElectroSmash's bias (Vbe 0.30 V, 0.2 mA, collector 7.1 V), the 37 dB gain at full volume and the 2.6 kHz input corner. `rm_reduced.py` (the form the engine runs) agrees with it to 0.1-0.2 dB and 2-4% of peak at drive; the C core matches it to the digit. `tests/engine/pedals_test.c` takes its expected numbers from the full simulator, not from the C code.
* The NAM captures of real units (a Dallas Rangemaster, a BBE 882i, a Xotic EP Booster) were used **offline only** as measurements of the hardware (`tools/research/pedals/common.py`, `bbe_measure.py`): they showed the real chain's latency and polarity, agreed in kind with the circuit, and gave the BBE's magnitude response. Nothing NAM runs on the pedal and no model was fitted to a waveform. The recovered A2 reference runner is `nam_a2.py`.
* Aliasing: the circuit runs at the audio rate (`PXP_OS 1`). Measured on a tone exactly periodic in the analysis window: the energy off the harmonic bins is -48 dB re the fundamental hard-driven at 1.5 kHz (-57 dB at 2x), -87 dB (-97) at normal levels; the harmonics are the same. `PXP_OS 2` adds a 39-tap half-band pair (0.43 ms latency, twice the cost).

## Cost (host, relative)
Rangemaster about 0.9 x the POLY FUZZ preset's cost (which contains the 70%-of-a-core synth type on the pedal): treat it as expensive until measured with `node tools/op.mjs dsp`. EP BOOST (one unknown, no exp), MAXIMIZER (eight biquads, two short delays) and ENHANCE are cheap.

## Levels (owner feedback: no volume control means at least unity)
Measured with `node tests/pedal_levels.mjs OUT && python3 tests/pedal_levels_measure.py OUT` (BS.1770 loudness of five guitar loops through the engine, against the same loop with the block off):
| | before | now |
|---|---|---|
| RANGEMASTER VOLUME 100 | +1.5 dB (worst loop -3.5) | +5.4 dB (worst +0.5); VOLUME 50 about -11 dB (was -19) on a gentler taper |
| EP BOOST GAIN 0 / 50 / 85 / 100 | -34 / -19 / -5 / +1.5 dB (EARLY) | **0 / +5 / +8.5 / +10 dB in every voicing**: a boost never cuts, per-voicing normalised |
| MAXIMIZER (no output control) | -1 to -4 dB, -8.7 dB on a low-mid loop | +0.6 to +1.4 dB on average at every setting (worst loop -4.7 dB at high PROCESS) |
| ENHANCE BETTER 100 | -0.3 dB | -0.3 dB |
The MAXIMIZER's makeup is `pxp_bbe_makeup_db` (a 3 x 3 grid over the knobs with a 1.2 dB margin); the tests use a K-weighted loudness measure so they follow the loops, not plain RMS (the BBE scoops the mids the weighting favours).

## Rangemaster: VOLUME and BODY (owner feedback: unity was near 90; add a body option)
* VOLUME is unity at 50 and +12 dB at 100 (a taper written in dB: +0.24 dB per step above 50, 0.8 dB per step below, silent at 0); measured on the guitar loops: 50 -> +0.1 dB, 70 -> +4.9, 90 -> +9.7, 100 -> +12.1.
* BODY is the popular input-capacitor mod as a real circuit change to C1: 5 nF (the original treble booster, input corner 2.6 kHz) at 0, 100 nF (a full-range boost, corner about 130 Hz) at 100 (log). The loudness is kept (a makeup of 7.2 dB at 50 and 9.6 dB at 100 comes off the output: `pxp_rm_body_db`), so unity stays at VOLUME 50 for every BODY; 100 Hz gains 12 dB or more, the treble hardly moves. It also evens out the loops (the worst loop at VOLUME 100 is +10.9 dB at BODY 100, +7.1 dB at BODY 0).

## Rangemaster EQ curve (small signal, volume max; the stage's own gain in dB)
| source | 100 Hz | 300 Hz | 1 kHz | 2.6 kHz | 5 kHz | 10 kHz |
|---|---|---|---|---|---|---|
| ideal (low impedance) | 7.8 | 17.4 | 27.4 | 33.6 | 35.8 | 36.7 |
| single coil (PICKUP 0) | 7.9 | 17.8 | 30.5 | 27.6 | 20.6 | 13.9 |
| humbucker (PICKUP 100) | 7.9 | 18.1 | 32.4 | 20.9 | 14.4 | 7.9 |
The ideal-source column is the ElectroSmash circuit (first-order input corner 2.6 kHz, 37 dB at the top); a real Rangemaster capture (the NAM one, low-impedance source) rises 0 -> +9.3 dB (1 kHz) -> +19.5 dB (6 kHz), a corner near 3 kHz but with about 10 dB more low end than a first-order high-pass. With a pickup behind it the 12 kOhm input loads the coil's inductance and the highs fall away above 2-3 kHz: a mid-forward boost with little below 300 Hz and a smooth top, which is why the guitar's energy (80-800 Hz) gets far less gain than 1-3 kHz and the loudness dropped before the level fix.

## POLY OCTAVE HQ: the -1 and RANGE
RANGE thins the octave-down out (the bank follows fewer notes): on a single low E the -1 was 7 dB quieter at RANGE 1 than at 100 (a chord 13 dB), and LOWEST OCTAVE HQ is about 9 dB louder than this type anyway. Engaged (RANGE 1..99) the -1 now gets the make-up `(100 / RANGE)^0.196` (`PX_POLY_RANGE_MAKEUP`) and a further +9 dB (`PX_POLY_RANGE_LIFT`, LOWEST's volume; full by RANGE 90, fading out to nothing at 100 so there is no jump): about -27 to -28 dB across RANGE 1..90 on a single low E against -36.4 dB at 100 and LOWEST's -24 to -27 dB (`tests/engine/poly_range_test.c`). A chord at RANGE 1 is -36 dB against -45 before. RANGE 0 and 100 are unchanged.

## Not done / open
* None of this has been **heard or measured on the pedal**. Level matching between the types and the factory presets is not done (their SFX trim is 0).
* EP BOOST's circuit is a guess for the family; a real schematic, or a clean DI A/B of the Xotic, would replace it.
* The BBE's phase behaviour is in the delays, but the real unit's dynamic high-frequency stage (if any) was not found in the captures; LO CONTOUR and PROCESS separately were not measured.
* The Better Box has no capture or schematic: the detector mix, time constants and the 2:1 law are the maker's words turned into a mechanism.
