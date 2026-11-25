# OpenPocket v93 (build 993)

Build 993 adds **SIGNATURE** to PRE / BOOST on Donner Pocket X and M-VAVE BlackBox. It also includes the lower-cost Fuzz Face and Rangemaster processing and independent Valve output Level introduced in [v89](WHATS-NEW-v89.md).

## SIGNATURE boost

- **BODY** shapes the low mids around 350 Hz.
- **FOCUS** tightens the bass and adds high-frequency definition.
- **RICHNESS** adds restrained, JFET-inspired second-harmonic coloration.
- **PUNCH** lifts the pick attack and returns to unity gain on sustain.
- **LEVEL** adjusts output from −12 to +12 dB independently of the coloration.

The default voicing is BODY 60, FOCUS 55, RICHNESS 25, PUNCH 30, and LEVEL 0 dB. Select SIGNATURE in PRE / BOOST on the pedal or in the editor. Valve remains independently switchable before PRE / BOOST, with its own Drive and Level controls.

The host-based DSP estimate for SIGNATURE is 5.9%. The under-5% target still needs measurement on BlackBox hardware. See the [model controls and design](PEDALS.md#signature-bst).

## Install

Open the [web installer](https://daverage.github.io/openpocket/) in desktop Chrome or Edge. The normal installer now offers **v93 · build 993**, without a test-channel link. Connect the powered pedal over USB, click **Install**, and keep power and USB connected until the installer verifies **PocketX_993** or **BlackBox_993** after restart.

Packages are `PocketX_993.fwsc` for Donner Pocket X and `BlackBox_993.fwsc` for M-VAVE BlackBox. Back up your presets with the editor before updating.

## Verification

Both published model packages match their manifest sizes and SHA-256 checksums. The firmware builds successfully, and SIGNATURE DSP and editor checks pass. The host suite completes with the same two poly-octave sweep warnings and seven delay-tail flags as v89. Optional browser checks require Playwright; hardware DSP measurements remain pending.

[Build and installation guide](BUILDING.md) · [Pedal controls](SOUNDS-AND-CONTROLS.md) · [Preset editor](https://daverage.github.io/openpocket/editor.html)
