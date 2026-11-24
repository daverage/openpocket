# OpenPocket Synth2

OpenPocket Synth2 is firmware for the **Donner Pocket X** and **M-VAVE BlackBox** focused on **guitar synths,
octavers, and boosts**. It covers single-note synth and keyboard sounds, chord-friendly octave and pitch effects,
and a dedicated preamp and boost block.

Synth2 removes the original **DRIVE**, **AMP**, and **CAB** blocks, including **NAM amp models** and **user cabinet IRs**.
It also retires the older synth catalogue, the earlier delay-line poly octaver, and the **Applause** effect from a
Synth2 test build. Gate, compression, EQ, modulation, delay, and reverb are still available, alongside selected
special effects and dedicated sounds such as **Fuzz Face** and **Acoustic Drive**.

[Web installer](https://daverage.github.io/openpocket/) · [What’s new in v89](docs/WHATS-NEW-v89.md) · [Pedal controls](docs/SOUNDS-AND-CONTROLS.md)

## Sounds and effects

This is the feature set in **v89 (build 989)** compared with upstream OpenPocket v42. [Upstream v42](https://github.com/isod89/openpocket/tree/995c84b5df53aace5d83478e66e85b37d0fa4372) · [v76 release notes](docs/WHATS-NEW-v76.md)

### Guitar synths

The SYNTH block follows single notes from the clean guitar input for its tracked voices. The shared mono tracker
confirms note starts and rejects brief harmonic jumps and mute thumps. On a hard mute it holds the last pitch
through the drop in level and releases quickly. Pitch bends, slides and vibrato follow the played note.
ENSEMBLE adds intervals and detuned copies around that one tracked note. POLY OCT FUZZ accepts guitar chords.

| Type | Sounds and controls |
| --- | --- |
| **ANALOG** | Ten Felucca ANALOG patches covering saw, pulse, PWM, octave-down bass, sweep and sync sounds. PATCH, CUTOFF, RESONANCE, ENVELOPE and LEVEL. |
| **ARCADE** | Ten Felucca LOFI patches covering chip pulses, bass, wavetables, arpeggios, sweeps, plucks and noise. PATCH, CHARACTER, SWEEP, DECAY and MIX. MIX blends the synth with the guitar signal arriving at the block. |
| **ARCADE SPLIT** | Two ARCADE patches split at a selectable note from E2 to E5. Each note keeps its assigned patch through bends. LOWER, UPPER, SPLIT, DECAY and MIX. |
| **INSTRUMENT** | Five Felucca ANALOG pads, strings and brass sounds, plus five Felucca WHEEL tonewheel organ patches with drawbars, key click and rotary. PATCH, TONE, ATTACK, RELEASE and LEVEL. |
| **ENSEMBLE** | The INSTRUMENT patches stacked in ten voicings with up to five voices. Choose unison, octaves, fifths or chord voicings and adjust detune and pitch drift. PATCH, VOICING, SPREAD, TONE and LEVEL. |
| **CHOIR** | Tracked vocal formants with selectable vowels and singer stacks. VOWEL, SINGERS, SPREAD, TONE and LEVEL. |
| **POLY OCT FUZZ** | Mixes guitar with octave-down and octave-up voices before a two-stage germanium-style Fuzz Face model. DOWN, UP, FUZZ, TONE and LEVEL. |

### Octave and pitch

- **POLY OCTAVE HQ** supplies polyphonic octave-up and octave-down voices, with an independent two-octave-down
  level. RANGE tapers the one-octave-down voice above a selected note from B1 to E5. FULL keeps the full range.
- **LOWEST OCTAVE HQ** takes its octave-down voice from the lowest sounding band. It adds RANGE and make-up gain
  for low-note reinforcement under chords.
- **POLY SHIFTER** transposes chords up to an octave in either direction. It has fine tuning, dry/wet mix, and attack controls. [Poly Shifter guide](docs/POLY-SHIFTER.md)
- **OCTAVER** provides one- and two-octave-down voices and an octave-up voice. **WHAMMY**, **HARMONIZER**, and
  **DETUNE** cover pitch sweeps, fixed intervals, and doubling.

### Preamp, boosts, and effects

| PRE / BOOST model | Sound |
| --- | --- |
| **RANGEMASTER** | Treble booster with pickup and body controls. |
| **EP BOOST** | Preamp boost with selectable voicing. |
| **MAXIMIZER** | Low Contour and Process controls for tone shaping. |
| **FIXED WAH** | Parked wah with adjustable position, peak, and output boost. |
| **ENHANCE** | Harmonic enhancement and compression with up to +12 dB of clean boost. |
| **FUZZ FACE** | Germanium or silicon Fuzz Face circuit model with pickup loading and bias controls. |
| **ACOUSTIC PRE** | Piezo preamp with low cut, dequack, body, feedback notch, and level. |

- **Valve** is a separate 12AX7 input stage that runs before the reorderable effects and can run alongside COMP. Drive
  ranges from 0 to +24 dB with automatic output compensation. Independent output **Level** ranges
  from −12 to +12 dB in 0.5 dB steps. Presets save both controls. Valve defaults to off, with +12 dB Drive and 0 dB Level.
- **SMART GATE** learns the background level while closed. The global **Smart Gate Hush** setting controls its dynamic
  hiss filter. **HUM FILTER** in EQ notches mains hum and its harmonics at 50 Hz or 60 Hz, with an Auto setting.
- **SPECIAL FX** includes **SQUARE SYNTH HQ**, a tracked square wave and sub voice, and **ACOUSTIC DRIVE**, a piezo-voiced
  drive with a dry blend. Envelope wah, auto wah, ring mod, bitcrushing, slicing, lo-fi radio, and the original square
  synth are also available. [Full effects catalogue](docs/SOUNDS-AND-CONTROLS.md) · [Boost and pedal models](docs/PEDALS.md)

Build 989 uses a shared lookup table for the Fuzz Face and Rangemaster transistor curves. On a one-core BlackBox,
median total DSP load fell from 49% to 39% with Fuzz Face alone in a live comparison.
[Performance details](docs/WHATS-NEW-v89.md#boost-dsp-load)

### Signal chain, controls and presets

- **Ten reorderable blocks** run in the order stored with each preset. The default is GATE → PRE / BOOST (BST) → COMP →
  OCTAVE → SYNTH → SPECIAL FX → EQ → MOD → DELAY → REVERB.
- **Pedal layout** shows the SYNTH and OCTAVE types under the preset number. The physical AMP and CAB buttons
  open SYNTH and OCTAVE, respectively. FX cycles through the utility blocks and then opens VALVE INPUT.
- **STOMP mode** gives each footswitch a per-preset assignment. Hold CAB to switch between STOMP and PATCH.
  In STOMP, hold a footswitch and press a block button to assign it directly, or tap FX to cycle through Valve, all ten
  blocks, global bypass, and unassigned. The editor sets per-preset double-tap and hold actions. Hold PARA to open SAVE,
  then press PARA to confirm.
- **Tuner** covers 25 Hz to 1.5 kHz with adjustable reference A and blink, needle, or strobe lights. **Diagnostics**
  shows DSP load and system readings. The load safeguard sheds expensive blocks when the pedal exceeds its DSP
  budget. The pedal shows SHED and the editor can bring those blocks back.
- **52 factory presets** cover synth, pitch, boost, acoustic, and conventional effects. Updating from the original
  multi-effects preset format retains global settings and starts with the Synth2 factory bank. A retired SYNTH type
  loads switched off with its parameter values retained. Older **COMP → VALVE** presets carry on/off, Drive, and
  Valve footswitch assignments forward. Presets saved before Level was added load at 0 dB Level.
  Earlier boost presets can migrate their SPECIAL FX settings to PRE / BOOST.
  Export your presets before changing firmware. Older firmware cannot read the preset store format written by v75 and later.
- **USB MIDI and USB audio** carry over from upstream. USB MIDI handles preset selection, block controls and delay
  clock. Enable USB AUDIO to record and play back stereo 16-bit audio at 44.1 kHz. See [MIDI](docs/MIDI.md) and
  [USB audio](docs/USB-AUDIO.md).

## Install

1. Open the [web installer](https://daverage.github.io/openpocket/) in desktop Chrome or Edge.
2. Power on the pedal and connect it with a USB data cable. Click **Connect**, allow MIDI access, and confirm the model
   and **v89 · build 989** under **Available firmware**.
3. Click **Install** and leave the pedal powered and connected until the installer verifies the firmware after restart.

Build 989 packages are `PocketX_989.fwsc` for the Donner Pocket X and `BlackBox_989.fwsc` for the M-VAVE BlackBox.
The installer can also restore the manufacturer’s official `.fwsc`. For a local build, run `make package` and `make serve`,
then open the localhost address it prints.
[Installation guide](docs/BUILDING.md#installing-from-the-browser) · [Recovery guide](docs/RESCUE.md)

## Preset editor

Open the editor from the installer or directly in Chrome or Edge and connect the pedal
over USB. The editor uses the ten-block layout and the pedal's own model catalogue, and has a Stage view and a Global settings panel.
[Open the preset editor](https://daverage.github.io/openpocket/editor.html)

- **Presets** browses, loads, renames, copies and moves the 52 slots. Export or import JSON to back up and restore
  presets.
- **Sound** edits the active blocks, models, settings and chain order. It shows DSP load and peaks and lets you
  compare the current sound with the saved preset. Expand **Valve** for on/off, Drive and Level on supported firmware.
  Expand **Footswitches** for Stomp/Patch mode and the current preset's press, double-tap and hold assignments.
- **MIDI** shows the current preset's Control Change mappings and lets you learn controls from a MIDI controller
  connected to the computer.
- **Stage** gives you a larger view of the current preset, effect and footswitch states, and the tuner.
- **Global** adjusts settings across presets. Changes apply live and save automatically in the editor. **Tails** lets
  delay and reverb play out when switched off or when changing presets.

Edits take effect live and changes on the pedal appear in the editor. See the
[editor protocol](docs/EDITOR-PROTOCOL.md).

### Editing from the pedal

- **Valve**. Tap FX until **VALVE INPUT** appears. Turn PARA to adjust Drive (0 to +24 dB).
  Tap FX once more and turn PARA to adjust output Level (−12 to +12 dB, 0 dB for unity).
  Press PARA to toggle Valve.
  Hold PARA to open SAVE, select the destination if needed, and press PARA to confirm. Both controls are also in the editor's Valve panel.
- **Footswitches**. From the home screen, hold CAB to enter STOMP. Hold either footswitch and tap FX until its assignment shows **VLV**,
  or select another assignment the same way. Tap the footswitch to toggle its assignment while playing. Use the editor
  to change the double-tap and hold actions.
- **Global settings**. Hold HOME to open the tuner, release it, then hold HOME again for **GLOBAL**. Use PARA to select,
  edit and confirm a setting. Adjust the five-band output EQ, input high-pass, Smart Gate Hush, limiter, tails, tuner
  lights and button lights. HOME goes back from EQ and saves when leaving the main Global page. Diagnostics is the last item.
  Turn PARA in the tuner to adjust reference A. Hold FX for MIDI and USB audio settings. The web editor’s **Global** panel edits the same settings.

## Build

Use Docker and Node 18+ on macOS, Linux or Windows. Start with `make doctor` and use
`make package-synth2` for the separate Synth2 test channel. The regular `make package` target builds the
checkout into the default installer channel. See the [build guide](docs/BUILDING.md) for the JieLi toolchain,
native builds, Windows and recovery images.

## Test (no pedal needed)

`tests/run_tests.sh [PocketX_016.fwsc]` checks, on the PC:

- the preset store on a simulated flash, with a power cut at every step of a save;
- the sound engine with the pedal's own math library: every block × model × extreme setting, no NaN, nothing runs away;
- the package builder: it rebuilds the official PocketX 016 app area **byte for byte** (with the official file);
- the installer against a **simulated pedal** that runs the pedal's real update code (`ota.c`, then the loader's
  `ldr_core.c`) on a simulated flash: update, install over the official firmware, wrong model refused, damaged package
  refused before anything is erased, way back to the official firmware (its loader staged and checked);
- the page itself in headless Chromium (Playwright), clicking Connect and Install.
- the preset editor against a simulated pedal that runs the pedal's real editor code (`px_editor.h`) and preset store
  on a simulated flash: every command, pushes both ways, a power cycle; then the editor page in Chromium (every
  section, including settings, drag and drop, save and undo, preset moves, import and export, learned MIDI
  controls, reconnecting, and narrow screens).

To play with the simulated pedal yourself (screen, footswitches, knob, the installer and editor pages, and its sound,
with bundled guitar loops, a file or an input of your computer), run `make sim` and open `http://127.0.0.1:8766/`; see [`docs/SIMULATOR.md`](docs/SIMULATOR.md).

## Layout

| | |
| --- | --- |
| `engine/` | the sound engine (`px_dsp.c`), portable C, float |
| `firmware/` | the pedal app: screen, controls, audio (I2S), USB, UI (`ui.h`), board support (`bsp/`, felucca-bsp) |
| `loader/` | the update loader (runs from RAM, writes the app area) |
| `docs/` | GitHub Pages installer and editor, firmware packages, user guides, and release notes |
| `tools/` | build, packaging, flashing, recovery, and editor support tools |
| `tools/sim/` | simulator pages, WebAssembly audio engine, and WAV renderer |
| `tests/` | host verification runner and pedal fixtures, with DSP tests in `tests/engine/` and Playwright tests in `tests/browser/` |
| `flash/` | SD-card recovery with fm1buddy (ESP32-S3) |
| `firmware/ble/`, `third_party/jieli-ble/` | the optional Bluetooth MIDI test image (`make ble`, [docs/BLE-TEST.md](docs/BLE-TEST.md)) and JieLi's Bluetooth controller it links |

## Licence

GPL-3.0-only. Board support and loader: MPL-2.0 (felucca-bsp). JieLi SDK files: Apache-2.0. Inter: OFL 1.1.
See [NOTICE.md](NOTICE.md) for Felucca and other third-party components.
Product names are trademarks of their owners, used only to describe the sound.
OpenPocket is independent of Donner and M-VAVE. Install at your own risk.

