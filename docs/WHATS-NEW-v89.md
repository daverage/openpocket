# OpenPocket v89 (build 989)

Build 989 reduces Fuzz Face DSP load, shares the transistor lookup table with Rangemaster, and adds independent Valve output Level to the public firmware. It supports Donner Pocket X and M-VAVE BlackBox.

## Boost DSP load

Fuzz Face and Rangemaster look up the transistor junction curves while their circuit solvers continue to update pickup and capacitor states from the input. Fuzz Face retains 2× oversampling, germanium and silicon settings, bias, and guitar-volume cleanup. Rangemaster retains PICKUP and BODY.

The table compares median total DSP load in the pre-release tests on one BlackBox running on one core. Settings were matched within each comparison. The readings used live input without a fixed playback signal.

| Setup | Before | With lookup table |
| --- | ---: | ---: |
| Fuzz Face alone, high-FUZZ silicon patch | 49% | 39% |
| Same Fuzz Face patch with Valve | 56% | 49% |
| Rangemaster alone, all controls at 50 | 14% | 13% |

The isolated Rangemaster block profiler read 11% with both builds, so its measured gain on the pedal was small. All effects off read 2%.

## Valve output Level

Set **Level** from −12 to +12 dB in 0.5 dB steps while keeping the same Drive. Tap **FX** from the Valve Drive page, then turn **PARA** to set Level. On a connected pedal, the preset editor shows the Level control under **Sound → Valve**. Older presets load at 0 dB Level.

## Install

Open the [web installer](https://daverage.github.io/openpocket/) in desktop Chrome or Edge. Connect the powered pedal over USB and check that **Available firmware** shows **v89 · build 989**. Keep USB and power connected until the installer verifies the restart. The package names are `PocketX_989.fwsc` for Donner Pocket X and `BlackBox_989.fwsc` for M-VAVE BlackBox.

[Build and installation guide](BUILDING.md) · [Pedal controls](SOUNDS-AND-CONTROLS.md) · [Preset editor](https://daverage.github.io/openpocket/editor.html)
