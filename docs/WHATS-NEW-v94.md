# OpenPocket v94 (build 994)

Build 994 makes SIGNATURE's FOCUS, RICHNESS and PUNCH controls more pronounced on Donner Pocket X and M-VAVE BlackBox. It retains SIGNATURE from [v93](WHATS-NEW-v93.md), the boost performance improvements, and independent Valve output Level.

## Wider SIGNATURE ranges

- **FOCUS** now reaches a −8 dB low shelf at 230 Hz and a +4.8 dB high shelf at 2.8 kHz, up from −5 / +2.4 dB. The shelf frequencies stay the same.
- **RICHNESS** adds three times the second-harmonic coloration. It keeps the same bounded shape, filtering and input-level dependence; quiet notes remain cleaner than strong notes.
- **PUNCH** has three times the gain coefficient and a 70 ms slow envelope instead of 45 ms. Picks receive a stronger, longer lift, returning to unity on sustain.
- **BODY** and **LEVEL** retain their ranges. BODY 50 is neutral; BODY 0 cuts the low mids by 4 dB. LEVEL 50 is 0 dB.

The default control values remain BODY 60, FOCUS 55, RICHNESS 25, PUNCH 30 and LEVEL 50 (0 dB). Existing presets retain their values and will sound stronger after updating; lower FOCUS, RICHNESS or PUNCH if needed. No preset format change is required.

## Measured response

These software measurements use the firmware's DSP and math library at 44.1 kHz, with a 440 Hz sine at 0.12 peak (−18.4 dBFS). All other controls are 0, and LEVEL is 50. BODY 0 therefore includes its low-mid cut. Punch is measured after a sudden note onset, relative to PUNCH 0.

| Control setting | 0 | 25 | 50 | 100 |
| --- | ---: | ---: | ---: | ---: |
| RICHNESS: second harmonic / fundamental | ≈0% | 1.85% | 3.70% | 7.40% |
| PUNCH: peak extra gain | 0 dB | +1.49 dB | +2.75 dB | +4.84 dB |

Pedal DSP load and listening tests still need hardware measurement. This change adjusts coefficients and retains the existing per-sample processing structure.

## Install

Open the [web installer](https://daverage.github.io/openpocket/) in desktop Chrome or Edge and check **v94 · build 994** under Available firmware. Back up your presets with the editor, connect the powered pedal over USB and click **Install**. Keep power and USB connected until the installer verifies **PocketX_994** or **BlackBox_994** after restart.

Packages are `PocketX_994.fwsc` for Donner Pocket X and `BlackBox_994.fwsc` for M-VAVE BlackBox.

[Build and installation guide](BUILDING.md) · [SIGNATURE model guide](PEDALS.md#signature-bst) · [Preset editor](https://daverage.github.io/openpocket/editor.html)
