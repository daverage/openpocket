# OpenPocket v81 (build 981)

Build 981 adds a per-preset Valve input stage, Valve footswitch assignments, and a Global settings menu on the pedal. It runs on the Donner Pocket X and M-VAVE BlackBox.

## Valve input stage

- Valve models a single 12AX7 stage ahead of the reorderable effects. It can run alongside any COMP model.
- **Drive** adds 0 to +24 dB inside the model. Automatic output compensation keeps the level closer to the bypassed signal. The default is off at +12 dB.
- Saved presets that used **COMP → VALVE** retain their Valve on/off state and Drive. Existing COMP footswitch assignments and gestures on those presets follow Valve to its new location. COMP opens as SQUASH, switched off, so you can choose a compressor when you want one.
- On the pedal, tap **FX** until **VALVE INPUT** appears. Turn **PARA** for Drive and press it to switch Valve on or off. Hold **PARA** to open SAVE and press it again to confirm.

## Footswitches and the editor

- Hold **CAB** to enter **STOMP** mode. Hold either footswitch and tap **FX** until **VLV** appears in its assignment. Save the preset to keep the assignment.
- Expand **Valve** in the editor’s **Sound** tab for on/off and Drive. Expand **Footswitches** to choose press, double-tap and hold actions. Valve is available for all three, and an assigned Valve switch shows its state in **Stage** view.

## Global settings on the pedal

Hold **HOME** for the tuner, release it, then hold it again to open **GLOBAL**. Turn **PARA** to select a row, press to edit, turn to adjust, and press again to confirm.

- **Output EQ** opens all five bands (80 Hz, 250 Hz, 1 kHz, 3.5 kHz and 9 kHz), with ±12 dB in half-dB steps.
- The main list includes **Input High-Pass**, **Smart Gate Hush**, **Limiter**, **Tails**, **Tuner Lights** and **Button Lights**. Smart Gate Hush works when the active preset uses Smart Gate.
- Changes apply live across presets. Press **HOME** from EQ to go back, then press **HOME** from the main list to save and exit. **Diagnostics** is the last item in Global. Adjust **reference A** by turning PARA in the tuner, and hold **FX** for MIDI and USB audio settings.

The web editor’s **Global** panel edits the same settings. The simulator supports the new screens and applies Global audio settings while playing a loop, file or audio input.

## Install or try it

Use the [browser installer](index.html) in desktop Chrome or Edge. Connect your powered pedal with a USB data cable, allow MIDI access, and click **Install** when the page shows **v81 · build 981**. Wait for the pedal to restart and the installer to report **PocketX_981** or **BlackBox_981**. For local builds, use `make package` followed by `make serve`. [Build and installation guide](BUILDING.md)

Run `make sim` to try the controls and audio on your computer. [Simulator guide](SIMULATOR.md)
