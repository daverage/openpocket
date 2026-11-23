# OpenPocket — sounds and controls (v37)

![OpenPocket screens](ui-v23.png)

## The chain (12 blocks, 79 models)
Default order: `GT → CMP → PIT → SFX → SYN → DRV → AMP → CAB → EQ → MOD → DLY → REV`

Since v29 every preset has its own block order: **double-click PUSH** on the chain to grab the selected block, turn
the knob to move it, PUSH to drop it (HOME puts it back). Delay before the amp, wah after the drive, octaver after
the fuzz... The order is saved with the preset. DRV and AMP share one oversampled section when AMP comes right after
DRV; elsewhere each runs its own.

Each block has a **type** (the model), an **ON/OFF** state and up to **5 settings (0–100)**. Where a bare number
would mean nothing, the screen shows the real value: interval (`+5TH`), semitones (`-7`), time in ms (`510`),
playback heads (`1+2`), EQ dB (`+6`), bits (`8`), `SLOW`/`FAST` for the rotary.

The names refer to the famous units the sounds are inspired by. Amps, drives and the V30 cabinet are fitted to
captures of the real units (v34); the effects follow published circuit analyses (ElectroSmash, Electric Druid,
R.G. Keen, Boss/Roland/EHX manuals). They are digital models, not copies. The product names are trademarks of their
owners, used only to describe the sound.

### AMP — NAM: neural amp models (v37; MODEL, GAIN, BASS, MID, TREBLE)
The ninth AMP type plays Neural Amp Modeler A2 networks: 12 factory models (TWIN, AC15, SUPER 59, SHIRL CL, JCM900,
SHIRLEY, JCM2000, MARK IV, JVM OD2, PRO MOD, 5150, 6505 RED) and 16 user slots filled from the web editor with A2
captures. MODEL picks the network, GAIN sets the level into it (±12 dB), BASS / MID / TREBLE are an EQ after it. The
models have no cabinet: keep a CAB after the AMP. All the factory presets use NAM. Details: [NAM.md](NAM.md).

### AMP — 8 amps, fitted to captures of the real ones (GAIN, BASS, MID, TREBLE, PRESENCE)
Since v34 there are fewer amps, and each one is fitted by computer to a capture of the real amp (method and
references: [FITTED-MODELS.md](FITTED-MODELS.md)). With the knobs at noon it sounds like the capture.

| Type | Style | Reference |
|---|---|---|
| TWIN | American clean | Fender Twin Reverb |
| AC15 TB | chime | Vox AC15 Top Boost |
| JCM900 | British crunch | Marshall JCM900 |
| JCM2000 | British lead | Marshall JCM2000 DSL |
| MARK IV | American lead | Mesa Boogie Mark IV |
| SHIRLEY | boutique crunch | Friedman Dirty Shirley |
| PRO MOD | modern rock | Splawn Pro Mod |
| 5150 | high gain | Peavey 5150 |

Each amp = 1 to 4 preamp stages (gain, asymmetry, RC filters per stage) oversampled ×4, input filter (tightness),
a "bright" cap that fades as the gain goes up, a tone stack, presence, resonance, power-stage saturation and supply
sag (asymmetric triode curve, bias shift that follows your playing, sag that reaches the preamp too). All 8 amps are
level-matched (±0.1 dB).

### CAB — 7 cabinets (BODY, BRIGHT, MIC) and USER IR (v38: IR, LOW CUT, HIGH CUT)
1x12 US OPEN (Jensen C12N) · 2x12 BLUE (Alnico Blue) · 4x12 GREEN (Greenback) · 4x12 T75 · 4x12 V30 (Vintage 30,
fitted to measurements since v34) · 1x12 CREAM · DI (NO CAB) · USER IR.
**USER IR** (v38) plays a cabinet impulse response loaded from the web editor (8 slots, a `.wav` file each): its first
23 ms (1024 samples at 44.1 kHz), convolved exactly (partitioned FFT), at the same loudness as the other cabinets. IR
picks the slot (its name shows on the PLAY page), LOW CUT 20..400 Hz, HIGH CUT 20 kHz..2 kHz.
Speaker resonance, presence dips and peaks taken from the makers' curves (the V30 from 8 measured amp / amp + cab
pairs). **MIC** moves the mic from the edge (dark) to the centre (bright).
Each cabinet also has a detail layer: the cone's breakup resonances between 1.2 and 6 kHz, the comb filtering of an
off-axis mic (MIC 0 = cone edge: dip around 6 kHz; MIC 100 = centre: out of the audio band) and the rear wave of
the open-back cabinets (1x12 US, 2x12 BLUE), computed with resonators.

### DRV — overdrive, distortion and fuzz
| Type | Style | Reference |
|---|---|---|
| TS9 | overdrive (mid hump) | Ibanez TS9 |
| KLON | transparent overdrive | Klon Centaur (clone) |
| M77 OD | boost / hot overdrive | MXR M77 Custom Badass |
| SWEDE HM-2 (LOW, HIGH) | Swedish distortion | Boss HM-2 |
| FUZZ FACE (FUZZ, LEVEL) | two-stage germanium-style fuzz | Dunlop JDF2 |

The TS9, KLON, M77 and HM-2 are fitted to pedal captures at noon. FUZZ FACE uses two biased clipping stages with
input and output coupling filters, inspired by the germanium JDF2. FUZZ sets gain and LEVEL sets the output.
The shared clipping model uses a fixed amount of work per sample in both blocks.
[Dunlop JDF2 manual](https://www.jimdunlop.com/content/manuals/JDF2.pdf) · [Fuzz Face circuit measurements](https://www.gitec-forum-eng.de/wp-content/uploads/2019/02/poteg-10-08-05-5-fuzz-face.pdf) · [Embedded circuit-model research](https://doi.org/10.1007/978-1-4020-9823-9_7)

### SYN — Synth2
Played with a normal guitar: no special pickup, no MIDI.

Synth2 (docs/research/synth2/SYNTH2-PLAN.md) rebuilt this block: POLY OCT FUZZ for chords, and two monophonic synths (ANALOG and ARCADE) that follow the one note you play, with Felucca's synthesis engines (docs/research/synth2/SYNTH2-ANALOG.md, docs/research/synth2/SYNTH2-ARCADE.md). They are one-note, not polyphonic: play single notes.

| Family | Type | Settings | How it works |
|---|---|---|---|
| Mono synth (one note) | ANALOG | PATCH, CUTOFF, RESONANCE, ENVELOPE, LEVEL | PATCH picks one of 10 sounds (CLASSIC, FATSAW, PULSE, PWM, SQ BASS, SUB, MOOG, ACID, SWEEP, SYNC; the bass patches play an octave down); the other four move the patch's filter and envelope around where it was written (50) |
| Mono synth (one note) | ARCADE | PATCH, CHARACTER, SWEEP, DECAY, MIX | chip sounds: PATCH picks ARCADE, CHIP, 8BIT BS, WAVE, STEP, ARP, ZAP, PLUCK, NOISE or MIN ARP; CHARACTER the pulse width or wave table, SWEEP the pitch sweep down, DECAY the stepped decay, MIX guitar against synth (50 both, 100 synth only). The guitar share is the signal that reaches the block, after the blocks before it in the order (a gate or compressor before it works on it); the notes are still followed from the clean guitar |
| Mono synth (one note) | ARCADE SPLIT | LOWER, UPPER, SPLIT, DECAY, MIX | ARCADE with two patches: notes below the SPLIT note (E2 to E5 in semitones; shown as a note name) play the LOWER patch, notes from it up play the UPPER one. The range is decided at each new note, so a note that bends across the split keeps its patch. DECAY and MIX are shared (50 = as written); both patches are the ARCADE list |
| Mono synth (one note) | INSTRUMENT | PATCH, TONE, ATTACK, RELEASE, LEVEL | Sustained instrument colours, one note at a time (a chord does not make a section). PATCH 0-9 SOFTPAD, 10-19 STRINGS, 20-29 BRASS, 30-39 SLOWSTR, 40-49 BRSLEAD (the ANALOG engine), 50-59 ORGAN, 60-69 JAZZORG, 70-79 ROCKORG, 80-89 REEDORG, 90-100 SOFTORG (a tonewheel organ with nine drawbars, key click and a built-in rotary: Felucca WHEEL). TONE is the filter on the strings and brass, and a tilt of the drawbars on the organs (up: upper bars up, sub bars down); ATTACK and RELEASE scale the patch's times (50 = as written); LEVEL as ANALOG (docs/research/synth2/SYNTH2-ENSEMBLE.md) |
| Mono synth, a stack of voices | ENSEMBLE | PATCH, VOICING, SPREAD, TONE, LEVEL | The INSTRUMENT patches (PATCH as there: SOFTPAD, STRINGS, BRASS, SLOWSTR, BRSLEAD, ORGAN, JAZZORG, ROCKORG, REEDORG, SOFTORG) played by several voices from the one note the tracker follows: VOICING 0-9 UNISON (the note and two drifting copies), 10-19 OCT UP, 20-29 OCT DN, 30-39 OCTAVES, 40-49 FIFTH, 50-59 POWER (sub octave, root, fifth), 60-69 MAJOR, 70-79 MINOR, 80-89 MAJ 7, 90-100 OPEN (sub octave, root, fifth, octave, tenth). SPREAD scales the detune and the slow random pitch wander of each voice (0 none, 50 as written, 100 three times). TONE as INSTRUMENT's; LEVEL as the others. **One note, not a chord**: a chord you play gives the one note the tracker picks, with the voicing's intervals above and below it (docs/research/synth2/SYNTH2-ENSEMBLE-VOICES.md) |
| Mono synth, sung vowels | CHOIR | VOWEL, SINGERS, SPREAD, TONE, LEVEL | The original FM-1 synth's VOICE engine (a Klatt formant synthesizer: a glottal source through formant resonators, with breath), one note from the tracker sung by several singers. VOWEL 0-9 AH, 10-19 EH, 20-29 EE, 30-39 OH, 40-49 OO, 50-59 OO>AH, 60-69 AH>OO, 70-79 AH>EE (the mouth moves from one vowel to the other over each note, 330 ms), 80-89 MIXED (every note and singer a random vowel), 90-100 BREATHY (a whisper). SINGERS are the ENSEMBLE voicings (2 to 5 singers; UNISON 3), SPREAD scales each singer's drift and detune, TONE shifts all the formants +-5 semitones (the size of the singer), LEVEL as the others. Attack 190 ms, release 800 ms (Felucca's CHOIR AAH: 476 ms and 980 ms). One note, not a chord (docs/research/synth2/SYNTH2-VOICE-CHOIR.md) |
| Octave fuzz (chords OK) | POLY OCT FUZZ | DOWN, UP, FUZZ, TONE, LEVEL | mixes dry guitar and both HQ octave voices before the complete Drive Fuzz Face. With both octaves at zero and TONE and LEVEL at noon, it matches Drive Fuzz Face at the same FUZZ and noon LEVEL. TONE adjusts treble around a flat noon setting. LEVEL (0–100, v66) sets the fuzz output: 0 is silent, noon is 0 dB, 100 is +6 dB. |

**Retired types.** The 27 other SYNTH types of v29–v73 (organs, strings, choirs, MONO / PWM / BASS synths, 12-STRING, ACOUSTIC, SITAR, BARITONE, brass, trumpet, cello, STRING MACHINE, the Poly and Duo voices, the ensembles) are gone. Their numbers are never reused: a preset saved with one of them loads with the SYNTH block switched off and set to the first live type (its five settings are kept), with no message (v75: before it showed RETIRED TYPE). The same goes for the SFX types that moved to BST. The pedal and the editor skip retired types when you step through the list; MIDI model slices count only the live types.

The old implementations are on the `synth` branch.

POLY OCTAVE HQ's **RANGE** dial limits the **OCT −1** voice. The default `FULL` keeps all bands.
Turn up from `FULL` to select a cutoff from B1 to E5. Try E3 for the low E string through fret 12,
or A3 for the A string through fret 12. The filter tapers around the cutoff and follows pitch across all strings.
Dry and OCT +1 keep their full range.

### BST — PRE / BOOST (the tenth block)

RANGEMASTER, EP BOOST, MAXIMIZER, FIXED WAH (the Vox V847 tone curve with the pedal parked; POSITION, PEAK, LEVEL), ACOUSTIC PRE (for a piezo pickup into a PA or a recorder: LOW CUT 20 to 200 Hz, DEQUACK, BODY, a feedback NOTCH 80 Hz to 4 kHz, LEVEL -12 to +12 dB), FUZZ FACE (the two-transistor circuit solved sample by sample, with FUZZ, VOLUME, PICKUP, BIAS and a germanium / silicon TRANSISTOR switch: it cleans up with the guitar's volume), and ENHANCE (moved here from SFX: BETTER adds second-harmonic warmth in parallel with the untouched signal, LOUDER is a clean 0 to +12 dB boost; [ENHANCE.md](ENHANCE.md)). Details in [PEDALS.md](PEDALS.md). It is reached with the FX button (GT, BST, CMP, SFX, EQ in the chain order), can be assigned to a footswitch in STOMP mode like any block, and is CC 29 (on/off), 61 (model), 97 and 102..105 (settings), toggle CC 17 in [MIDI.md](MIDI.md).

### Effects

SMART GATE uses a filtered detector and learns the background while closed. Lower SENS to pick up quieter attacks. HOLD keeps it open through short gaps, RELEASE sets the closing fade, and REDUCTION sets how far the level dips. Start with a brief pause so the detector can hear the idle signal. A sudden noise jump can still open it.

**The SMART GATE's controls, as the code reads them** (`smart_gate_sample` in `engine/px_dsp.c`):

| Control | What it does |
|---|---|
| SENS | The quietest level that counts as playing, **a floor under the learned noise**: -62 dBFS at 0, -28 dBFS at 100. Lower it to catch quieter picking, raise it when noise or hum opens the gate. (The name is the other way round to a threshold: higher is harder to open.) The gate also learns the idle noise of each of its two bands (below and above about 1 kHz) while closed and opens only above three times that floor, so it follows a noisy or a quiet pickup by itself. |
| RELEASE | How fast it closes once it decides the note is over: 5 ms at 0 to 500 ms at 100. |
| PRESET LVL | Not a gate control: the whole preset's output level, 0.3 dB a step, 50 = 0 dB (it lives here because the gate is always the first block). |
| HOLD | How long it stays open after the last attack or sustain: 25 ms at 0 to 150 ms at 100. Raise it for fast playing, staccato and the gaps between notes. |
| REDUCTION | How far it closes: -20 dB at 0 to -80 dB at 100. Less is less obvious; more is dead silence between notes. |
| HUSH (Global panel) | A low-pass that closes with the level, so the hiss of a dying note goes before the note does. |

It opens on a pick attack (energy jumping over the slow level), stays open while sustained energy is above the learned floor, and does not cut a note's decay. A sidechain high-pass at 88 Hz keeps hum from holding it open. It cannot remove noise that sounds while a note rings: that is what the HUM FILTER and HUSH are for.
| Block | Types |
|---|---|
| GT | NOISE GATE (THRESH, RELEASE), SMART GATE (SENS, RELEASE, HOLD, REDUCTION) |
| CMP | SQUASH (Dyna Comp), STUDIO (parallel, MIX), OPTO LA-2 (optical, slow release), SLOW SWELL (violin-style attack) |
| PIT | OCTAVER (analog: SUB -1, SUB -2, UP +1), POLY OCTAVE (−1 and +1 octave), POLY OCTAVE HQ (same controls, phase-scaled octave voices), WHAMMY (±24 semitones, GLIDE), HARMONIZER (fixed interval −OCT…+OCT), DETUNE (doubling ±cents), POLY SHIFTER (v75: a phase-vocoder shifter for chords: SHIFT −12..+12 semitones, FINE ±50 cents, MIX, ATTACK; 17 ms of latency, dry and wet kept together; see [POLY-SHIFTER.md](POLY-SHIFTER.md)) |
| SFX | ENVELOPE WAH (Mu-Tron), AUTO WAH (LFO, or fixed wah with DEPTH 0), RING MOD, BITCRUSHER, SLICER (8 rhythm patterns), LO-FI RADIO, ACOUSTIC DRIVE (a drive for a piezo: it takes the lows and fizz off before a soft clipper and blends the dry guitar back; DRIVE, BLEND, PIEZO, TONE, LEVEL), SQUARE SYNTH (square-wave synth + sub, opening filter), SQUARE SYNTH HQ (monophonic pitch-tracked square + sub with the same controls) (ENHANCE, RANGEMASTER, EP BOOST and MAXIMIZER moved to the BST block in v75: a stored preset is converted) |
| EQ | 5 BAND, GRAPHIC 5 (the Mesa "V"), PARAMETRIC, HUM FILTER (v75: notches at the mains and its harmonics for 50 Hz countries (Europe, Asia, Africa, Australia) and 60 Hz countries (the Americas, Korea, Taiwan, parts of Japan and the Gulf); MAINS = 50 HZ, AUTO (it listens for which hum is in the signal and follows a change, in about a second) or 60 HZ; DEPTH 3..40 dB, WIDTH 1..20 Hz, HARMONICS 1..5. At the default width a low E (82 Hz) and an A (110 Hz) pass untouched; a 60 Hz hum's 2nd harmonic (120 Hz) sits next to the B string's lowest note (123 Hz), so keep WIDTH low when playing there) |
| MOD | CE CHORUS, DIMENSION (4 modes), FLANGER, PHASE 90, SMALL STONE, UNI-VIBE, OPTO TREMOLO, BIAS TREMOLO, HARMONIC TREM, ROTARY (Leslie, real SLOW↔FAST ramp), VIBRATO |
| DLY | DIGITAL, ANALOG BBD, MEMORY MAN (modulated repeats), TAPE EP-3 (wow & flutter), SPACE ECHO (3 heads, 7 combinations), REVERSE, LO-FI, DUAL TAP (quarter + dotted eighth) — TIME 20–2000 ms (REVERSE 250–1000 ms) |
| REV | ROOM, HALL, CATHEDRAL, PLATE, SPRING (DRIP), SHIMMER (octave in the tail), GATED (80s), AMBIENT (up to 20 s) |

The analog/tape delays (ANALOG, MEMORY MAN, TAPE, SPACE ECHO, LO-FI) **self-oscillate above FDBK ≈ 90**, like the
real ones: lower FDBK to stop it.

**Delay TIME (v35)**: 0–80 = 20–1000 ms in steps of 12.25 ms, 80–100 = 1–2 s in steps of 50 ms; the screen shows
the ms. Presets saved before v35 keep their delay time (the knob value moves). The repeats are read with cubic
interpolation (no dulling when the time glides or the tape wobbles), like the chorus, flanger and vibrato.

**Reverbs (v35)**: the halls, rooms, spring, shimmer, gated and ambient are an 8-line feedback delay network with
early reflections, Dattorro's input diffusers, slowly moving lines (no metallic ringing in the tail) and a decay
time set separately for the lows and for 4 kHz (TONE); PLATE is Dattorro's plate (the classic "Effect Design"
plate). DECAY sets the real decay time (RT60) of the lows: ROOM 0.3–1.2 s, HALL 1.2–4 s, CATHEDRAL 3–10 s,
PLATE 0.6–5 s, SPRING 1.5–4 s, SHIMMER 2–8 s, AMBIENT 6–20 s. They run at 22 kHz in 16 bits (like the classic
studio units), in a quarter of v34's memory.

### Overload (v75)

The pedal watches how much of each audio block's time the effects take. When it stays over 85 % for four blocks in a row it **switches off the heaviest block** (a short fade, nothing in the preset changes), then waits a moment and, if it is still over, the next heaviest, up to three. Only when that is not enough, or there is nothing left to switch off, does it do what it always did: pass the dry guitar through (BYPASS). The screen reads **SHED 71%** instead of DSP while a block is off for the load, and the web editor hatches its tile, says so, and offers **Bring back**. A block comes back by itself when you switch it or change its type, and every preset change starts clean. The gate is never switched off. The web editor also warns before you switch on a block or pick a type that its estimate says will not fit (the estimate is `docs/pxcost.js`, from host timings scaled to one pedal reading; the real shares are PING's, shown in the editor's load popover).

## Factory presets (v75): 52 sounds, all at the same loudness
A new bank (the old one was built round the NAM models and drives of v37, which v70 removed). Every preset plays as loud as the guitar alone at noon: its **PRESET LVL** (the GT page's 3rd line,
-15..+15 dB) is set so that the mean loudness over the five bundled guitar loops (ITU-R BS.1770) is within 0.15 dB of the dry guitar (`tools/research/preset_levels.py`; `tests/engine/factory_test.c` keeps it
within 0.5 dB and the peaks under -3 dBFS). The estimated processor share of every preset is under 55 % (the editor's `docs/pxcost.js`).

| Group | Slots |
|---|---|
| Clean and boost | 01 CLEAN HALL, 02 STUDIO CLEAN, 03 TREBLE BOOST (RANGEMASTER), 04 RM FULL BODY, 05 EP PUSH, 06 EP FAT, 07 BBE MAX (MAXIMIZER), 08 WAH PARKED LO, 09 WAH PARKED HI (FIXED WAH), 10 ENHANCED |
| Pitch | 11 POLY OCTAVES, 12 LOWEST OCT, 13 WHAMMY +12, 14 HARMONY +3RD, 15 HARMONY +5TH, 16 DETUNE, 17 POLY SHIFT +7, 18 SHIFT -12 PAD (POLY SHIFTER) |
| Synths | 19 SQUARE LEAD, 20 SQ HQ BASS, 21 POLY FUZZ, 22 MOOG BASS, 23 CHIP LEAD, 24 STRING PAD, 25 JAZZ ORGAN, 26 STRING ENSEMB, 27 CHOIR AAH, 28 BRASS + SQ HQ, 29 ARCADE SPLIT, 30 ANALOG LEAD |
| Modulation, delay, reverb | 31 CE CHORUS, 32 DIMENSION, 33 JET FLANGER, 34 PHASE 90, 35 UNI-VIBE, 36 OPTO TREM, 37 ROTARY, 38 SPRING SURF, 39 SHIMMER PAD, 40 AMBIENT WASH, 41 TAPE ECHO, 42 SPACE ECHO, 43 REVERSE |
| Special effects and utility | 44 ENV WAH, 45 LFO WAH, 46 SLICER, 47 RING MOD, 48 LO-FI RADIO, 49 BIT CRUSH, 50 SLOW SWELL, 51 NOISY ROOM (smart gate, HUM FILTER, studio compressor, room) |
| Blank | 52 INIT: the gate only, LEVEL 0 dB, to build your own |

Presets you have saved are not touched: this is only what an unsaved slot plays and what **restore** gives back. The settings are a first set by ear-less measurement (the loudness and the processor share are
measured; the voicing is not yet judged on a pedal).

**Levels (v36)**: every block type, with its knobs at noon, keeps the loudness when switched on (`engine/px_trim.h`); each preset has its own **PRESET LVL** to balance your sounds. The output ends in a soft
limiter (transparent up to -2 dBFS) instead of a hard clip.

## Controls
| Control | Action |
|---|---|
| KNOB (PARA) | PLAY: pick the block · EDIT page: pick the line, in the order drawn (ON at the top, the model, the settings) · editing: change the value (turn fast = steps of 5; a list such as NAM MODEL or USER IR moves one filled entry per click) |
| PUSH (knob) | PLAY → open the block's EDIT page at once · model/setting line → edit · ON/OFF → toggle · editing → confirm |
| PUSH **double-click** (PLAY) | **MOVE** the selected block: knob = move it, PUSH = drop, HOME = put it back (the block's page shows for a moment between the clicks) |
| PUSH **long press (0.8 s)** | **SAVE**: store the current sound (see below) |
| HOME | Back to PLAY. On PLAY, opens the preset list. Hold for 0.7 s to open the tuner. Release and hold again for **GLOBAL**. |
| Right / left footswitch | next / previous preset (on a block's EDIT page it also goes back to PLAY, where the preset shows). Coming back to a preset you edited without saving gives the edit back (one edit is kept) |
| **Both footswitches** | **tuner** (the first one changes the preset at once, as always; the second within about 0.15 s changes it back and opens the tuner). Both again leave it |
| **Hold CAB** (0.8 s) | Switches between **STOMP** and **PATCH**. In PATCH, the footswitches change presets. In STOMP, they toggle the assigned effect, Valve or bypass. The PLAY footer shows the assignments, for example `STOMP   L: VLV   R: BYP`. Tap CAB to open OCTAVE. |
| Footswitch held + a block button (STOMP) | Assigns that footswitch. MOD, AMP, CAB, DLY and REV directly assign MOD, SYNTH, OCTAVE, DELAY and REVERB. FX cycles through Valve, all ten blocks, BYPASS and unassigned. Save the preset to keep its assignments. |
| BYPASS on a footswitch (STOMP) | the dry guitar, every block off (DSP off); the footer turns red (`BYPASSED`). Press again to come back. Leaving STOMP mode ends it. STOMP mode is kept in the flash with the next save |
| AMP, CAB, DLY, REV, MOD | jump to the block; press again = on/off |
| FX | Cycles through GATE, PRE / BOOST, COMP, SPECIAL FX and EQ in chain order, then **VALVE INPUT**. Hold for 0.8 s for MIDI and USB audio settings. |
| MASTER | output volume |

On **GLOBAL**, turn PARA to select **Output EQ**, **Input High-Pass**, **Smart Gate Hush**, **Limiter**, **Tails**,
**Tuner Lights**, or **Button Lights**. Press PARA to edit, turn it to adjust, and press it again to confirm. The effects
apply live across presets. Output EQ opens a page for all five bands. Press HOME to go back from EQ, then press HOME on
the main Global page to save and go back to PLAY. Open **Diagnostics** from the bottom of the Global list.

On **VALVE INPUT**, turn PARA to adjust Drive (0 to +24 dB), and press it to toggle Valve. The stage sits ahead of the
effects chain and can run with COMP.

Current source builds add **Level**. Tap FX once more to select it, then turn PARA to adjust the valve output from −12
to +12 dB in 0.5 dB steps. Level changes the gain after the valve stage. Set it to 0 dB for unity or raise it for a boost
at low Drive. Press PARA from either setting to toggle Valve, or hold PARA to save the preset. Presets saved before
Level was added load at 0 dB. In the browser editor, open **Sound → Valve**. The Level slider appears on supported firmware.

**Screen (v36)**: PLAY shows the preset number and name large, the amp and cab, the chain in colour (on = filled,
off = dark) and the selected block; EDIT shows the block on its colour, the model, and up to 5 settings with value and
bar; the EDIT band also shows the preset number. `DSP xx%` (top right on PLAY, bottom right on EDIT) = DSP load (amber
above 80 %; red with `!` = a block ran late; which cores ran is on the diagnostics page). **Overload guard**: the
effects are bypassed until the next change, a red bar says `OVERLOAD: SOUND BYPASSED` (on EDIT the whole footer turns
red) and the footer says `TURN A BLOCK OFF` (with `PEAK xxx%` on PLAY). While a NAM model loads, the MODEL row of the
EDIT page reads `LOADING` in amber (on PLAY the corner does, and the amp name goes grey). Labels of blocks that are off
are dimmer than those of blocks that are on. `EDITED`: the sound differs from its saved slot.
Top centre of PLAY: the **battery** (v36), from the chip's own VBAT/4 channel (green, amber below 30 %, red below 15 %;
`USB` when the 5 V supply is there); the diagnostics page shows the same reading in volts. On PLAY, the line under the
tiles names a NAM amp's model and a user IR's name.

## MIDI over USB-C (test build v37; [MIDI.md](MIDI.md))
**Hold FX** for 0.8 s: the MIDI page (CHANNEL OMNI or 1 to 16, SEND, CLOCK > DELAY, USB AUDIO: [USB-AUDIO.md](USB-AUDIO.md)). Program Change 0 to 51 loads slot 1 to 52;
Control Change sets the sound; the pedal sends Program Change and Control Change for what you do on it.

| Block | On / off | Model | Setting 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| GATE | 20 | 52 | 33 | 34 | 35 | 36 | 37 |
| COMP | 21 | 53 | 39 | 40 | 41 | 42 | 43 |
| PITCH | 22 | 54 | 44 | 45 | 46 | 47 | 48 |
| SFX | 23 | 55 | 49 | 50 | 51 | 70 | 71 |
| DRIVE | 24 | 56 | 72 | 73 | 74 | 75 | 76 |
| AMP | 25 | 57 | 77 | 78 | 79 | 80 | 81 |
| CAB | 26 | 58 | 82 | 83 | 84 | 85 | 86 |
| EQ | 27 | 59 | 87 | 88 | 89 | 90 | 91 |
| MOD | 28 | 60 | 92 | 93 | 94 | 95 | 96 |
| DELAY | 29 | 61 | 97 | 102 | 103 | 104 | 105 |
| REVERB | 30 | 62 | 106 | 107 | 108 | 109 | 110 |
| SYN | 31 | 63 | 111 | 112 | 113 | 114 | 115 |

On / off: 64 and up is on. Settings 0 to 127 are 0 to 100. Model: the whole 0 to 127 range is divided between the block's models.
CC 7 is the MASTER volume. Details, the clock sync and the limits: [MIDI.md](MIDI.md).

## Saving your sounds (v25)
- `EDITED` at the top of the PLAY screen means the sound has been edited since the preset was loaded; the footer then reads `HOLD: SAVE`.
- **Long press on PUSH**: SAVE page. The knob picks the slot (1 to 52; the current preset is offered first; the
  screen shows the name of the sound that will be replaced), PUSH saves, HOME cancels.
- At the very end of the list: **RESTORE FACTORY** puts the current slot back to its factory preset (the footer reads
  `PUSH: RESTORE`; on a slot that already holds its factory sound, PUSH does nothing).
- A sound saved to another slot keeps its name: rename it in the web editor.
- The pedal starts on the slot saved last.
- Sounds are kept in flash (0xF0000–0xF4000, two copies): a power cut during a save leaves the previous version,
  never a damaged slot. They **survive OpenPocket updates** (the update loader only writes the app area). The sound
  is muted for about 0.1 s while saving. The block order is saved too. Sounds saved with v25–v28 are converted at
  the first start of v29 (the SYN block off, the default order).

## Tuner
Both footswitches, or a long press on HOME. The output is muted while the tuner is open (v36). The note, how far off it
is in cents, and an amber arrow on the side of the note that points toward in tune. Any switch leaves it.

v75: it reads from **25 Hz to 1.5 kHz** (a 5-string bass's low B is 31 Hz; it used to stop at 61 Hz) and is steadied: the median of the last five readings, a note change shown at once,
the in-tune state from 4 cents until 7 so it does not flicker at the edge. The reading is YIN (de Cheveigne and Kawahara) on the input low-passed to 11 kHz, then the fundamental's own frequency from how far its
phase turns between two windows, so a string's stretched upper partials do not pull it sharp (they did, by about 2 cents). On synthetic strings it is within 1.5 cents from B0 to E6 (tests/tunersim.c; not yet heard on a real
guitar). **The knob sets the reference A** (415 to 466 Hz, one hertz a detent; the editor's Global panel too; kept with the next save). Tuner lights: see "Global settings".

## Global settings

These settings apply across presets. On v81, open **GLOBAL** from the tuner with another HOME hold and press HOME from the main list to save and exit. In the web editor, open **Global** in the header. Changes apply live and the editor saves them automatically.

| Setting | What it does |
|---|---|
| Input high-pass | off, or 20 to 140 Hz: cuts a steady low rumble before every effect (25 Hz and below for bass, about 80 Hz for a guitar on a loud stage) |
| Output EQ | five bands, 80 Hz shelf, 250 Hz, 1 kHz, 3.5 kHz, 9 kHz shelf, each -12 to +12 dB |
| Limiter | off, or a ceiling from -0.5 to -12 dB on the output |
| Tails | a delay (3 s) or reverb (6 s) that is switched off, or left by a preset change, plays out with its input muted and its own settings instead of stopping |
| Smart gate hush | 0 to 100 %: the SMART GATE's dynamic hiss filter, a low-pass that closes as the note dies away; only with the SMART GATE |
| Tuner reference A | 415 to 466 Hz (also the knob on the tuner page) |
| Tuner lights | BLINK (HOME blinks, as before), NEEDLE (one of the seven button lights moves from HOME to CAB with the cents; the three in the middle are steady when in tune), STROBE (one light chases toward the side the note is out to, faster the further out) |
| Button lights | turn the lights off while you play. The tuner still uses its chosen lights mode. The editor labels this setting **Lights off**. |

The defaults change nothing in the sound except TAILS, which is on. Older stores read as the defaults.

## Footswitch gestures (v75)
Each footswitch has a **double tap** (STOMP mode only: the two toggles cancel, then the action runs) and a **hold** (0.7 s: the press's own step is taken back, then the action runs; PATCH mode too).
Actions include toggling a block or Valve, BYPASS, the tuner, the next or previous preset, STOMP / PATCH mode, and unassigned. In the web editor, expand **Sound → Footswitches** to set them. Save the preset to keep them.

## Stage view (v75)
The web editor's **Stage** button: a big second display for the pedalboard (put a phone or laptop beside it). The preset, the blocks that are on (tap one to switch it), the two footswitches with their assignment and
gestures (tap one as on the pedal), previous / next preset, a **tuner** (the pedal's own: it opens its tuner page, the output is muted, the editor draws the note and the needle), a screen lock (tap to lock, hold the padlock for
a second to unlock), and three looks (Color, AMOLED, Mono). The screen stays on where the browser allows it.

## Diagnostics

Open **GLOBAL** by holding HOME for the tuner, releasing it, and holding it again. Select **Diagnostics** at the bottom
of the list and press PARA. Turn PARA to switch diagnostic pages. Audio plays while they are open. Green indicates normal
operation, amber indicates DSP load above 80%, and red indicates a late block or bypassed effects.

- **Page 1, the DSP:** the load as a bar with the safety bypass's limit marked (`GUARD 93%` with both cores, 85 % on one)
  and the peak since the preset was set; which cores ran (`C1` the second core, `C0` the fallback on the main core, `2C`
  both) and `PK`, the highest block load since boot; a bar for each core while the NAM network is split between them;
  `LATE` (blocks that ran late), `CLIP` (blocks at the soft clipper) and `BYPASS`; the four costliest blocks as bars, the
  heaviest in amber, and `CUT n` once the delay or the reverb had to cut a wet block short.
- **Page 2, input and power:** a meter for each input channel (`L -62 DB`, dB below full scale; with nothing plugged in,
  the pedal's own noise floor), with the last 3 dB marked as the clip zone; a marker that lights for 2 s when the output
  reaches the soft clipper, with the count; the battery as a cell with its volts and percent (`USB` on the 5 V supply);
  the raw battery readings (`C9`, `CH`).

## Audio input (v26–v28)
Like the official firmware: input and output in 24 bits (v26), and the guitar read from the left channel only (v28).
The right input channel only carries constant noise (−77 dB); before v28 it was mixed into the sound, and the
high-gain amps turned it into a constant hiss, even with nothing plugged in.

## Checks run on the PC (with the pedal's own math library, `firmware/px_math.c`)
- 2,443 configurations (every block × type × extreme settings, changed while playing; signal, silence, very loud
  signal): 0 NaN.
- 231 long runs (3 s plucked string + 8 s of silence, every model at full settings): nothing runs away; only the
  delays at FDBK 100 keep repeating (by design).
- Pitch shifters: 100 % of the energy on the target note (440 Hz, 110 Hz, −7 semitones) thanks to phase alignment
  at every crossfade (SOLA).
- Digital silence: no more denormal numbers (silence used to cost 25× more CPU on the PC).
- Memory: engine 247 KB in the 336 KB POOL area (v35: delay and reverb in 16 bits; v34: 332 KB).
- Preset saving (`tests/test_store.c`): power cut simulated at 1,286 points of a save → old version kept 578 times,
  new one 708 times, never a damaged slot; no write outside the preset area.
- CPU: v25 loaded the pedal too much (DOOM SLUDGE in BYP at 89 % on v26). v27 computes the cabinets and the amp
  differently (same sound, 0.5 dB on average): about −16 % on DOOM SLUDGE, −19 % on GROOVE METAL, i.e. +16 to +22 %
  over v24 (BYP guard at 85 %).

## USB update (v24)
Over USB the pedal shows up as a MIDI device named "OpenPocket" (and, with the USB AUDIO setting on, a sound card: [USB-AUDIO.md](USB-AUDIO.md)). The installer page (`docs/index.html`, published
on GitHub Pages) recognises it, installs a new version or takes the pedal back to the official firmware. During an
update the screen shows `UPDATE` and the current step; if it fails, `FAILED -N`, then the pedal restarts on the
previous version (nothing is written before the pedal has checked the package).
