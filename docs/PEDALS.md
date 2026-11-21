# Analogue pedal emulations (SFX types)

Four SFX types model analogue pedals as **circuits and structures**, not as fitted waveforms. They sit after ENHANCE in the SFX list (ids unchanged, each excludes the other SFX types):

| Type | Controls | What it is | Confidence |
|---|---|---|---|
| **ENHANCE** (Better Box) | BETTER, LOUDER | a compander gain cell: the gain follows a fast rectified copy of the signal (no threshold, works at every level); harmonics without clipping | the mechanism is a hypothesis from the maker's words; every constant is a guess. No real unit was measured |
| **RANGEMASTER** | VOLUME, PICKUP | the Dallas Rangemaster solved as a circuit: germanium Ebers-Moll stage, R1 470k, R2 68k, R3 3.9k, C1 5n, C3 47u, RV 10k, 9 V, and the guitar's pickup as the source (series R and L) | high for the circuit (reproduces ElectroSmash's numbers); the transistor is a typical part (Is, bf, br are choices) |
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

## Not done / open
* None of this has been **heard or measured on the pedal**. Level matching between the types and the factory presets is not done (their SFX trim is 0).
* EP BOOST's circuit is a guess for the family; a real schematic, or a clean DI A/B of the Xotic, would replace it.
* The BBE's phase behaviour is in the delays, but the real unit's dynamic high-frequency stage (if any) was not found in the captures; LO CONTOUR and PROCESS separately were not measured.
* The Better Box has no capture or schematic: the detector mix, time constants and the 2:1 law are the maker's words turned into a mechanism.
