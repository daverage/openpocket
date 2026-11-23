# What's new in OpenPocket v76: boosts and fuzz you can play, acoustic tools, a better tuner

Changes since v73. What is measured on a pedal and what is only measured on the host is stated for each item; the pending hardware checks are at the end.

## A new BST (PRE / BOOST) block

Ten blocks now. BST sits right after the gate and holds the boosts:

- **RANGEMASTER, EP BOOST, MAXIMIZER** moved here from SPECIAL FX; saved presets are converted when read.
- **FIXED WAH** is new: a Vox-style wah parked at one position. Its Q follows published measurements of Vox-style wahs (not a V847 itself).
- **ENHANCE** moved here too (BETTER and a LOUDER 0 to +12 dB boost).
- **FUZZ FACE** is new: the Dallas Arbiter circuit solved sample by sample (two transistors, the feedback resistor, the pickup loading), germanium or silicon. It cleans up when you turn the guitar's volume down, which a clipper cannot do. Controls FUZZ, VOLUME, PICKUP, BIAS, TRANSISTOR. The germanium values were fitted to the published bias, not measured on a unit. On a BlackBox it takes about 30 to 40 % of the DSP time (read with no guitar plugged in, so it may be higher while playing).
- **ACOUSTIC PRE** is new, for a piezo pickup into a PA or a recorder: LOW CUT, DEQUACK (a quack dip, a treble shelf and an extra dip on the pick attack only), BODY, a feedback NOTCH, LEVEL.

BST can sit on a footswitch in STOMP mode and is reached with the FX button.

## SPECIAL FX: ACOUSTIC DRIVE

A drive voiced for a piezo: it takes the bass and fizz off before a soft clipper, runs the clipper at twice the sample rate (aliasing measured at -77 dB, against -38 dB without), and blends the dry guitar back, delay-aligned so the blend does not comb. DRIVE, BLEND, PIEZO, TONE, LEVEL.

The acoustic settings are starting points from the research (docs/research/ACOUSTIC-BOOSTS-RESEARCH.md), not tuned on a real piezo.

## Pitch and tuning

- **POLY SHIFTER** (OCTAVE block): a phase-vocoder shifter for chords. About 26 % of the DSP time on a BlackBox, 17 ms of delay.
- **A new tuner**: 25 Hz to 1.5 kHz (a 5-string bass's low B is 31 Hz), with a phase refinement that removes the small error a string's inharmonic partials cause; the A reference is on the knob. Lights: blink, needle or strobe, or off. Tested on synthetic strings only.
- **HUM FILTER** (EQ block): a true notch at 50 Hz, 60 Hz or AUTO.

## Staying inside the DSP budget

- **Load shedding**: when four or more blocks run over 85 %, the heaviest is switched off, up to three, then BYPASS. The screen says SHED; the editor shows the tile hatched with a Bring back button. Working on a BlackBox.
- **Tails**: a delay or reverb you switch off plays out (3 s and 6 s), if the TAILS setting is on.

## Global settings, gestures, editor

- **Global settings** (outside the presets): input high-pass, output EQ, limiter, TAILS, smart-gate HUSH, tuner lights.
- **Footswitch gestures** in STOMP mode: double tap and hold on each switch, per preset (blocks, BYPASS, tuner, next / previous preset, STOMP / PATCH).
- **Web editor** (protocol 9): a Stage view for a phone or tablet, the Global panel, a Footswitches row.
- **New factory bank**: 52 presets. The "RETIRED TYPE" messages are gone.
- **Levels**: every effect type now sits within about 1 dB of the plain guitar at noon settings. The synths are 6 to 17 dB quieter than before at the same knobs; raise their LEVEL if a pad should sit above the guitar.

## Before you update

- **The first save** on this firmware converts the preset store to format 7, which older firmware cannot read. Export your presets from the editor first.
- Update with the web installer or `node tools/op.mjs flash --dry-run` first; see the [recovery guide](RESCUE.md) if a pedal loops.

## Not yet checked on a pedal

The tuner with a real guitar and bass, the footswitch gestures and the tuner lights by hand, the Global panel by ear, the Stage view on a phone, a preset save (format 7), listening to FIXED WAH, POLY SHIFTER, FUZZ FACE and the two acoustic effects, and the DSP load of ACOUSTIC PRE and DRIVE. Everything above passes the host tests (`make test`).
