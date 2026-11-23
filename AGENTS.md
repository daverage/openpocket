# OpenPocket

<!-- codebase-memory-mcp:start -->
For structural codebase exploration, use the installed `codebase-memory` skill.
<!-- codebase-memory-mcp:end -->

OpenPocket is firmware for the Donner Pocket X and M-VAVE BlackBox. See the [README layout](README.md#layout) for the firmware, DSP engine, update loader, web installer, and host tools.

## Build and verify

- Follow [docs/BUILDING.md](docs/BUILDING.md) for toolchain setup and packaging. `make doctor` checks prerequisites, and `make package` builds the firmware and installer packages.
- Run `make test` for host verification. The suite covers DSP, preset storage, and the installer and editor against a simulated pedal. See the [README test section](README.md#test-no-pedal-needed) for the optional factory firmware input.
- Use `make sim` to exercise the pedal UI, web installer, preset editor, or audio interactively without hardware. See [docs/SIMULATOR.md](docs/SIMULATOR.md).

## Bluetooth

The release has no radio. The Bluetooth test image is unstable at v76 and its cause is not found: read [docs/research/BLUETOOTH-RADIO-FINDINGS.md](docs/research/BLUETOOTH-RADIO-FINDINGS.md) (experiments, what is ruled out, next tests) and the Bluetooth section of [CLAUDE.md](CLAUDE.md) before changing `firmware/ble/` or flashing a pedal. The browser side is `docs/pxble.js`, tested by `tests/test_pxble.mjs`. Never flash a pedal without the user's explicit word for that image; dry-run first, and restore the normal firmware afterwards.
