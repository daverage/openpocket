# Analogue pedal emulations (BST: PRE / BOOST, and ENHANCE in SFX)

The boost-style pedals live in their own block, **BST (PRE / BOOST)**, so they can run together with an SFX type (a boost into a wah) and be placed anywhere in the chain (by default right after the gate). RANGEMASTER, EP BOOST and MAXIMIZER were SFX types 10..12 in test builds 929..931; those ids are tombstones now and a stored preset that holds one is moved to BST when it is read (store format 7, the editor, a preset file). ENHANCE (Better Box) moved there too (SFX type 9). The rest of this page says "SFX" where the pedal is a BST type now. The whole effect set is reviewed in [EFFECTS-REVIEW.md](EFFECTS-REVIEW.md).

Four types model analogue pedals as **circuits and structures**, not as fitted waveforms. They sit after ENHANCE in the SFX list (ids unchanged, each excludes the other SFX types):

| Type | Controls | What it is | Confidence |
|---|---|---|---|
| **ENHANCE** (Better Box) | BETTER, LOUDER | a compander gain cell: the gain follows a fast rectified copy of the signal (no threshold, works at every level); harmonics without clipping | the mechanism is a hypothesis from the maker's words; every constant is a guess. No real unit was measured |
| **RANGEMASTER** | VOLUME, PICKUP, BODY | the Dallas Rangemaster solved as a circuit: germanium Ebers-Moll stage, R1 470k, R2 68k, R3 3.9k, C1 5n, C3 47u, RV 10k, 9 V, and the guitar's pickup as the source (series R and L) | high for the circuit (reproduces ElectroSmash's numbers); the transistor is a typical part (Is, bf, br are choices) |
| **EP BOOST** | GAIN, VOICE | an EP-3-style JFET stage (2N5484 numbers measured by Aion FX, 18 V, drain 22k, source 3.3k bypassed by 22 nF..470 nF) | based on the EP-3 circuit family. Aion FX has since published a [trace of the Xotic EP Booster V2](https://aionfx.com/news/tracing-journal-xotic-ep-booster-v2/) |
| **FIXED WAH** | POSITION, PEAK, LEVEL | the Vox V847 wah with the pedal parked: one peaking filter swept 450 Hz (POSITION 0, heel) to 1.6 kHz (100, toe), about 750 Hz at mid travel, up to 18 dB, with the low end cut below it. PEAK is the height (8..18 dB) and sharpness of the peak (the V847's R7); the sharpness follows the position as in a real wah (Q about 1/frequency: -3 dB Q 2.4..3.3 at the toe, 8.5..11.7 at the heel), LEVEL 0..+12 dB over the guitar's own loudness. POSITION shows the peak's frequency in Hz | the frequency range, the mid point and the 18 dB figure are from the ElectroSmash analysis of the V847; the Q law comes from measurements of Vox-style wahs on the music-electronics-forum thread "a Vox wah project: some circuit analysis and measuring results" (-3 dB Q about 2.3..3.3 at the treble end, 8..11 at the bass end, Q = Rp*sqrt(C/L)), and ElectroSmash gives no Q; the position law and the peak height range (8..18 dB) are fits to the ElectroSmash figures. **None of it measured on a unit**: check it against a recorded sweep |
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

## FUZZ FACE (BST, v75): the circuit solved, not a clipper

The first Dallas Arbiter Fuzz Face, two transistors with the Q2 emitter feeding back to Q1's base through 100k. Topology and values follow the ElectroSmash analysis (their text; their schematic image is not used). `pxp_ff_*` in `engine/px_pedals.h` solves the four node voltages (Q1 base, Q1 collector = Q2 base, Q2 emitter, Q2 collector) per sample with Newton's method: Ebers-Moll transistors, the 2.2 uF input and 20 uF wiper capacitors as trapezoid companions, and the pickup as a source with series resistance and inductance (as in RANGEMASTER), so the pedal loads the guitar and **cleans up when the guitar's volume comes down**, which a clipper cannot do. Two times oversampling (half-band in and out); a 3x3 solve while Q2's collector is above its base (almost always), the full 4x4 only when it saturates; 1.1 to 1.5 iterations per circuit step.

Controls: FUZZ (the 1k emitter pot: more fuzz, less AC degeneration), VOLUME, PICKUP (source impedance: single coil, P90, humbucker), BIAS (the Q2 collector load, the pedal's bias trim: low starves it, the gated spitting sound; high is open and cleaner), TRANSISTOR (germanium up to 50, silicon above).

The germanium values (Is 3.1e-7, beta 70 and 120) were fitted to land on the analysis's bias (Q2 collector 4.5 V, Q1 collector about 0.7 V at BIAS 50): **not measured on a unit**; silicon is a BC108-like pair. Host: about 4.8 ms/s of audio (RANGEMASTER 1.9, which is 12 % on the pedal), so expect roughly 25 to 35 % of a core; the cost table says 66 % (the scaled host figure, an overestimate). Not read on a pedal yet. Level: FUZZ 25..50 within 0 to +1.5 dB of the guitar, FUZZ 100 about +4 dB (`tools/research/ff_levels.c`). Tests: `tests/engine/fuzzface_test.c`.

## FUZZ FACE after the first listening (v76)

Heard on the pedal: FUZZ did nothing below about 80 %, and the guitar's volume did not clean it up. Two causes, both fixed. (1) The FUZZ pot was linear in resistance, but the gain follows 1 / (emitter resistance), so everything happened in the last fifth: the pot now has a law (the resistance falls as (1 - FUZZ)^3, a 1k audio pot; the total stays 1k so the bias is unchanged). (2) The circuit was fed 2 V of EMF per engine unit, so a normal guitar level drove it into hard clipping at every FUZZ: now 0.25 V per unit. Measured with a 196 Hz sine (THD = harmonics 2..5 against the fundamental): at a guitar's level (0.125) FUZZ 0 / 25 / 50 / 75 give -63 / -17 / -9 / -8 dB, and at FUZZ 60 the same note is -8.6 dB at full level, -15 dB 12 dB lower and clean (-45 dB) at 24 dB lower. The make-up gain now follows FUZZ so the loudness stays within about 6 dB across the travel. The cleanup is still more abrupt than a real pedal's (a knee within about 12 dB); loading of the pickup by the guitar's own volume pot is not modelled.

## Fuzz Face and Rangemaster in v89

Both effects use a shared lookup table for the transistor junction curves. The circuit solvers still update pickup and
capacitor states from the incoming signal. Fuzz Face keeps 2× oversampling and its germanium and silicon settings.
Rangemaster keeps its BODY and PICKUP controls. See the [v89 hardware measurements](WHATS-NEW-v89.md#boost-dsp-load).

## ACOUSTIC PRE (BST) and ACOUSTIC DRIVE (SFX), v76

For a piezo pickup into a PA or a recorder; the research and its sources are in [research/ACOUSTIC-BOOSTS-RESEARCH.md](research/ACOUSTIC-BOOSTS-RESEARCH.md). They sit in different blocks so they can be used together. Starting points were set from that research and a synthetic test, **not tuned on a real piezo**: set them by ear.

**ACOUSTIC PRE** (seven biquads, no solver): LOW CUT 20 to 200 Hz (second-order high-pass); DEQUACK = a dip at 2.6 kHz (up to -6 dB), a high shelf at 6.5 kHz (up to -5 dB) and a further dip at 2.9 kHz that is blended in only on the pick attack (a fast level over a slow one; at DEQUACK 100 the attack at 2.9 kHz is about 0.5 of its sustained level, and a held note is not touched); BODY (below 50 a bell at 300 Hz cuts the box up to -8 dB; above 50 a low shelf at 160 Hz gives up to +5 dB and a high shelf at 8 kHz up to +3 dB); NOTCH (0 = off, else 80 Hz to 4 kHz, Q 8, 24 dB deep, for feedback); LEVEL -12 to +12 dB (unity at 50 with +0.8 dB of make-up for the default dips). It has no limiter of its own: the global limiter is after it.

**ACOUSTIC DRIVE**: before the clipper a high-pass (90 to 260 Hz with PIEZO), a one-pole low-pass (9 to 3.8 kHz) and a quack dip; a soft, slightly asymmetric tanh clipper (gain 1 to 61; it was 1 to 15 and did not overdrive at the first listening; DRIVE 60 on a guitar's level now measures THD -14 dB, DRIVE 100 about -9 dB) run at twice the rate with half-band filters; after it a DC block and the TONE low-pass (2.5 to 10 kHz); then the dry guitar is blended back, **delayed by the 18.5 samples the half-band filters take** and the low-passes kept one-pole, so the blend does not comb (at BLEND 50 and DRIVE 0 the response is within -2 to +0.6 dB from 200 Hz to 6 kHz; with a plain, undelayed dry it had a 10 dB hole above 5 kHz). Measured aliasing of a hard-driven 5 kHz sine: -77 dB with the 2x oversampling, -38 dB without, so the 2x stays. Trim -0.4 dB at noon (`engine/px_trim.h`).

Host cost 0.45 ms/s (PRE) and 0.56 ms/s (DRIVE) against RANGEMASTER's 1.9 (12 % on the pedal), so a few per cent each is expected; **not read on a pedal yet** (the cost table's 6 to 8 % is the scaled estimate). Tests: `tests/engine/acoustic_test.c`.

## SIGNATURE (BST)

SIGNATURE is a selectable preamp voicing for electric guitar. BODY shapes the low mids. FOCUS tightens the bass before coloration and adds a gentle high shelf at the end. RICHNESS adds an even-order component. PUNCH follows the difference between fast and slow envelopes to lift the pick attack. LEVEL applies independent output gain from −12 to +12 dB. The default voicing is BODY 60, FOCUS 55, RICHNESS 25, PUNCH 30 and LEVEL 0 dB. For always-on use, enable PRE / BOOST in the preset. Valve can be enabled independently and runs ahead of PRE / BOOST.

The color stage uses an AC-coupled, bandwidth-limited square-law residual inspired by the common-source MMBF5457 in the traced Xotic EP Booster V2. Its 4.7 kΩ source resistor and gain-dependent bypass establish the circuit context. The residual's strength and bandwidth are prototype voicing choices. A circuit-level fit would need to account for the source bypass setting, device bias and input calibration. [Aion FX circuit trace](https://aionfx.com/app/files/schematics/xotic-ep-booster-trace-schematic.pdf)

Three biquads handle the tonal shaping, with a broad low-mid bell and shelves for bass and definition. The Maximizer influence is spectral. The target is under 5% of the BlackBox's audio processing budget, pending pedal measurement.

## Not done / open
* None of this has been **heard or measured on the pedal**. Level matching between the types and the factory presets is not done (their SFX trim is 0).
* EP BOOST uses the EP-3 circuit family. The Xotic V2 trace and a level-matched DI comparison could inform a future model.
* The BBE's phase behaviour is in the delays, but the real unit's dynamic high-frequency stage (if any) was not found in the captures; LO CONTOUR and PROCESS separately were not measured.
* The Better Box has no capture or schematic: the detector mix, time constants and the 2:1 law are the maker's words turned into a mechanism.
