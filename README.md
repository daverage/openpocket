# OpenPocket Synth2

OpenPocket Synth2 is guitar-synth and multi-effects firmware for the **Donner Pocket X** and
**M-VAVE BlackBox**. It turns a standard guitar signal into synth voices, keyboard sounds and octave effects.
It runs on the pedals' JieLi AC791N chip and uses their built-in controls, USB MIDI and browser-based preset editor.

## Build 915 compared with upstream v42

This comparison covers the Synth2 build 915 packages for both pedals and uses
[OpenPocket v42](https://github.com/isod89/openpocket/tree/995c84b5df53aace5d83478e66e85b37d0fa4372)
as the baseline.

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
| **POLY OCT FUZZ** | Mixes guitar with octave-down and octave-up voices before a two-stage germanium-style Fuzz Face model. DOWN, UP, FUZZ, TONE and LEVEL. |

### Octave, gate and special effects

- **POLY OCTAVE HQ** supplies polyphonic octave-up and octave-down voices, with an independent two-octave-down
  level. RANGE tapers the one-octave-down voice above a selected note from B1 to E5. FULL keeps the full range.
- **LOWEST OCTAVE HQ** takes its octave-down voice from the lowest sounding band. It adds RANGE and make-up gain
  for low-note reinforcement under chords.
- **SMART GATE** learns the background level while closed using a filtered detector. Adjust sensitivity, hold time,
  release and reduction.
- **SQUARE SYNTH HQ** in SPECIAL FX adds a pitch-tracked square wave and sub voice with SWEEP, RESO, SUB and MIX.

### Signal chain, controls and presets

- **Ten reorderable blocks** run in the order stored with each preset. The default is GATE → BST (PRE / BOOST) → COMP →
  OCTAVE → SYNTH → SPECIAL FX → EQ → MOD → DELAY → REVERB. The synth firmware drops DRIVE, AMP, CAB, NAM and user cabinet
  IRs, along with their editor controls. POLY OCT FUZZ retains the Fuzz Face sound inside SYNTH.
- **Pedal layout** shows the SYNTH and OCTAVE types under the preset number. The physical AMP and CAB buttons
  open SYNTH and OCTAVE, respectively. FX cycles through the utility blocks.
- **STOMP mode** gives each footswitch a per-preset assignment. Hold CAB to switch between STOMP and PATCH.
  In STOMP, hold a footswitch and press a block button to assign it directly, or press FX to cycle through all ten
  blocks, global bypass and unassigned. Hold PUSH to save the assignments. Block lights follow bypass state.
- **Preset compatibility** uses 52 slots in the synth bank. Updating from the original multi-effects preset format
  retains global settings and starts with the synth factory slots. Back up your presets before switching firmware.
  Existing synth-format presets with a retired SYNTH type keep their stored type and settings and load that block
  bypassed. The pedal and editor skip retired types, and MIDI model selection skips them. The first save on v75 or later
  converts the preset store to format 7, which older firmware cannot read: back up your presets first.
- **USB MIDI and USB audio** carry over from upstream. USB MIDI handles preset selection, block controls and delay
  clock. Enable USB AUDIO to record and play back stereo 16-bit audio at 44.1 kHz. See [MIDI](docs/MIDI.md) and
  [USB audio](docs/USB-AUDIO.md).

## Install

Build 976 packages are in `docs/firmware/` (built by `make package`). Use `PocketX_976.fwsc` for the Donner Pocket X or
`BlackBox_976.fwsc` for the M-VAVE BlackBox. The [hardware test guide](docs/SYNTH2-HARDWARE-TEST.md)
describes the package and dry-run workflow using build 901 as its example. Select the 976 file for your pedal.

Run `make serve` to open the local installer and editor in Chrome or Edge. The installer also accepts the
manufacturer's official `.fwsc` for restoring stock firmware. See the [recovery guide](docs/RESCUE.md)
if the pedal restarts in a loop.

## Preset editor

Open [the preset editor](docs/editor.html) from the local installer in Chrome or Edge and connect the pedal
over USB. The editor uses the ten-block layout and the pedal's own model catalogue, and has a Stage view and a Global settings panel.

- **Presets** browses, loads, renames, copies and moves the 52 slots. Export or import JSON to back up and restore
  presets.
- **Sound** edits the active blocks, models, settings and chain order. It shows DSP load and peaks and lets you
  compare the current sound with the saved preset.
- **MIDI** shows the current preset's Control Change mappings and lets you learn controls from a MIDI controller
  connected to the computer.

Edits take effect live and changes on the pedal appear in the editor. See the
[editor protocol](docs/EDITOR-PROTOCOL.md).

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

