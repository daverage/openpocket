# OpenPocket v95 (build 995)

Build 995 brings the device-tested noise suppression and independent patch volume into the published v94 firmware, fixes the editor slider fill for Patch volume and Valve controls, and adds CROSS OSC as an experimental SFX.

## Noise suppression and patch balance

- The GATE block now offers **NOISE GATE** and **NOISE SUPPRESSOR**. The suppressor combines threshold and release with mains HUM reduction and per-preset HUSH.
- The old EQ HUM FILTER and global Smart Gate Hush are migrated into the new suppressor settings when presets are loaded. Existing preset data and global settings are preserved by the store migration.
- **Patch volume** is now independent of the gate and other effects. It ranges from −15 to +15 dB in 0.3 dB steps, and can be edited on the pedal or in the browser editor.

## Experimental SFX

**CROSS OSC** tracks the input note using the existing method and shapes a synthesized voice. Its WAVE control offers square, pulse, triangle, saw, brass, string and flute modes. This is an experiment; it does not replace the existing synth instruments or trackers.

The editor now paints the orange slider fill from the current value when Patch volume and Valve controls are rendered.

## Preserved features

Build 995 retains v94's stronger SIGNATURE settings and the shared Fuzz Face / Rangemaster exponential lookup and RAM placement optimizations. Valve, boosts, Fuzz Face, global settings, and synth tracking remain available.

Install from the [web installer](https://daverage.github.io/openpocket/) and select **v95 · build 995**. Packages are `PocketX_995.fwsc` and `BlackBox_995.fwsc`.
