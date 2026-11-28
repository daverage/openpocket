The installer's packages land here when you run `build.bat` (or `firmware/build.sh`):
`PocketX_9NN.fwsc`, `BlackBox_9NN.fwsc` and `manifest.json`. They are published with the release and on GitHub Pages
by `PUBLISH-GITHUB.bat`, not kept in the repository.

For build 997, the files are `PocketX_997.fwsc` for Donner Pocket X and `BlackBox_997.fwsc` for M-VAVE BlackBox.
Run `make package` and `make serve` to use the browser installer. It reads `manifest.json` and selects the package for
the pedal connected over USB. [Browser installation guide](../BUILDING.md#installing-from-the-browser)

A test build (for example `node tools/op.mjs package-test`) goes to a subfolder, `midi-test/`, with its own `manifest.json`. The
installer page offers it only through `index.html?channel=midi-test`, with a banner; the normal page and the files above are never
changed by it.
