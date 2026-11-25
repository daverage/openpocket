# OpenPocket legacy cleanup and preamps

Baseline is the clean v89 release at `f18b716`. The dedicated branch carries a cleanup commit and a preamp optimisation commit. Build 992 is a trial package built with the pinned JieLi image. It uses main's ENHANCE voicing. The separate 990/991 ENHANCE trial builds have their own voicings.

## A. Legacy cleanup and compatibility

| Candidate | Classification and change | Compatibility and measured impact |
| --- | --- | --- |
| `engine/px_dsp.c` and `px_dsp.h`: `clip_run`, `drive_pre`, `drive_block`, `drv_v` and `px_drive_state` | Removed the retired drive algorithms, pre/post routes and state. Kept the POLY OCT FUZZ voicing and its oversampling with a direct path. | No preset or type changes. Drive state 256 → 100 bytes in host layout. Steering and output matched baseline in the cleanup-only renders. |
| `engine/px_dsp.c`: `bst_reset` and `px_engine` | Shared mutually exclusive BOOST model storage and initialize only the selected model. Acoustic DRIVE and the fixed input Valve keep independent state. | Existing BOOST reset and knob semantics retained. BOOST storage 2,700 → 1,052 bytes in host layout. This and drive specialization freed 1,808 bytes in the actual DSP pool including alignment. |
| `px_type_canonical`, `px_block_canonical` and retired `PN_POLY` | Removed two identity/no-op hooks, their callers and an unused parameter-label table. | The compiler already eliminated their internal work. No separately measurable deployed storage saving. |
| `firmware/px_editor.h`: retired `ED_NAM_*` and `ED_IR_*` | Removed unused firmware symbols and gave the following commands explicit original numbers. | Old command values remain reserved. Request/reply framing and the browser editor's imports are retained. No separately measurable binary saving. |
| `firmware/build.sh`: obsolete ENHANCE diagnostic example | Removed the stale flag example. Kept the test-build override and supported oversampling options. | Comment-only change. |
| Retired SYNTH and SFX catalogue slots, `px_preset_canonical`, `px_sfx_to_bst` | Compatibility-critical. Preserved reserved IDs, safe bypass and migration of moved SFX pedals when BOOST is still empty. | Catalogue, MIDI model slices and saved slots keep their current meaning. |
| `st_migrate6`, `st_legacy_valve` and Valve conversion | Compatibility-critical. Kept old preset, switch and gesture conversion and v89's Valve Drive and Level. | Existing direct upgrade and browser import routes remain available. The 112-byte flash slot and atomic dual-copy store are retained. |
| Amp/cab/NAM/IR DSP, diagnostics and linker allocations | Legacy amp/cab and NAM/IR engines are absent from the v89 live graph. Acoustic DRIVE, POLY OCT FUZZ, delay/reverb and octave/synth banks are active users of their memory. The release has zero `.bss_hi` payload and zero `.cram` payload at baseline. | Retained current diagnostics and optional builds. Empty linker selectors have no deployed cost. |

The 5,824-byte `st_slot` cache uses ordinary RAM, where the baseline has 45,180 bytes free. A compact format would save at most 9 bytes × 52 slots (468 bytes) by dropping the reserved bytes between Valve Level and the footswitch field. It would require reconstructing complete slots for CRC, write and readback, reducing the existing contiguous sector write or adding a staging buffer. The current cache also keeps preset selection and editor reads independent of flash availability. The constrained regions are the RAM-code window and DSP pool. The cache and all flash write paths were kept.

## B. Preamp review

| Model | Evidence and change | Result and limits |
| --- | --- | --- |
| Rangemaster | The existing coupled BJT Newton model uses the v89 junction lookup. Corrected its optional 2× path to use `PXP_EMF_PER_UNIT` instead of `PXP_FF_IN`. Pruned unused members and compile the half-band state only for 2×. | Default 1× is bit-identical on all three DI clips, with identical 440 Hz gain and THD. 2× passes pedal tests and is independent of the Fuzz Face input-scale option. The full-circuit reference remains the basis for the model. Its default voicing and solver arithmetic are retained. |
| EP Boost | Cached the fixed companion reciprocals and drain resistance when coefficients change. Retained the bracketed JFET Newton solve, source model, drive/headroom and smoothing. | Eight divisions per sample associated with fixed network constants became multiplies. Host 35.7 → 33.1 ns/sample in the measured isolated test. Most changed DI residual was −91.4 dB relative RMS. The 440 Hz gain difference was below 0.0001 dB and H2–H8 THD differed by 0.001 dB. The topology remains EP-3-inspired. |
| Maximizer | Reviewed the existing two crossover stages, fitted low/mid delays and mix. The intentional phase offsets form part of the measured fit. The zero-position fast branch already skips the filters. | Bit-identical output, retained the existing magnitude fit, group-delay behaviour and makeup curve. |
| Fixed Wah | Cached the peak-dependent power and square-root terms at PEAK changes. Position glides reuse them while retaining the same coefficient values. | Steady DI is bit-identical. An alternating POSITION stress test had identical output checksum and 5.2 → 5.0 ns/sample on the host. No on-pedal coefficient-spike measurement. |
| ENHANCE | Reviewed the v89 detector floors, glide and level behavior. The v89 fast detector's harmonics and intermodulation are coupled. A voicing change would change current sounds. | Kept v89 character and controls. DI and 440 Hz spectra are bit-identical. Builds 990 and 991 remain a separate comparison. |
| Fuzz Face | Wiper/pot glides recalculate only the emitter companion. Pickup, transistor and fixed-network constants remain cached. Kept the v89 exponential lookup, 2× oversampling, iteration cap and existing Newton step limit. | Identical output across 36 stress settings spanning both transistors and FUZZ, BIAS and PICKUP, using a 0.84-peak two-tone. They remained finite. Moving FUZZ measured 143.9 → 142.0 ns/sample on the host with matching checksums. Near-singular pivots remain a theoretical limit; the stress run did not establish a need to change the solver. |
| Acoustic PRE | Clear the paused dynamic-filter state and blend when DEQUACK re-enters the enabled range. The existing attack/release blend fades the new correction in. | Steady DI is bit-identical. A transition regression verifies that the old filter tail cannot reappear. Filter centres and dynamic de-quack curve remain unchanged. |
| 12AX7 Valve | Reviewed the fixed input routing and 2× filtered correction. Retained the Dempwolf equations, tables, cathode integration and independent Drive and Level. | Bit-identical on the three DI clips and the 440 Hz test. The existing SPICE validation applies. |

## C. Linker memory comparison

All figures are bytes from clean JieLi builds. Input-section totals omit padding. The linker places code and read-only data in the combined XIP `.text` output. `.pool.tail` is a subset of `.pool`, and `.ram_text` also occupies deployed flash.

| Area | v89 | Cleanup only | Final 992 | Final headroom / notes |
| --- | ---: | ---: | ---: | --- |
| Combined XIP `.text` | 200,440 | 200,472 | 200,596 | Shares flash with load images |
| XIP input `.text*` | 112,162 | 112,194 | 112,290 | Subset of combined row |
| XIP input `.rodata*` | 88,268 | 88,268 | 88,298 | Subset of combined row |
| `.ram_text` | 76,920 | 75,448 | 75,172 | 10,832 free in RAM-code window, including alignment and `.bss_hi` |
| `.data` | 220 | 220 | 220 | Shares 96 KiB general RAM with `.bss` |
| `.bss` | 52,900 | 52,900 | 52,900 | 45,180 general RAM bytes free with `.data` and alignment |
| `.bss_lo` | 13,476 | 13,476 | 13,476 | 11,100 free |
| `.bss_hi` | 0 | 0 | 4 | Alignment, included in RAM-code headroom above |
| `.pool` with tail | 187,272 | 185,464 | 185,464 | 72,584 free |
| `.pool.tail` | 1,024 | 1,024 | 1,024 | Shared DSP oversampling scratch |
| `.cram` | 0 | 0 | 0 | 28,672 free |
| `.noinit` | 448 | 448 | 448 | 15,248 free in its linker region |
| App binary | 277,588 | 276,148 | 275,996 | 305,568 below the linker XIP limit |

Cleanup saved 1,440 deployed flash bytes and 1,808 DSP-pool bytes. The preamp pass saved another 152 deployed bytes. Final app flash savings are **1,592 bytes**. RAM-code occupancy decreased by 1,748 bytes before alignment. The fixed update package layout keeps the .fwsc download size unchanged. The linker stack reservations remain 24,320 bytes main and 7,936 bytes IRQ. CPU1's 3,072-byte stack reservation remains in `.bss`.

## D. Validation

- Baseline binary, ELF and map are saved locally in `build/cleanup-baseline/`. Intermediate and final maps and logs are in `build/cleanup-only/` and `build/cleanup-final/`.
- The cleanup-only engine matched the clean current main in all 872 equivalence renders. The final isolated hashes differ only for EP Boost's float rounding. In the three controlled 44.1 kHz guitar DIs, the other seven isolated models remained bit-identical. The tests use the pedal's own math functions, no fused multiply-add, and a 0.15 input scale. Host timings are CPU measurements on this machine.
- The PRE / BOOST and Valve tests passed, including the separate Rangemaster 2× build, Fuzz Face extreme settings and Acoustic PRE re-enable. Factory presets, the store's atomic-save/power-cut tests, MIDI, footswitch and editor behavior are checked by the host suite.
- The broad suite still reports the baseline's two poly-octave sweep findings, seven long-run flags and the historical `2f3519b` parity difference. The compatibility and simulator portions were run separately after the historical check. The separately run continuation passed through MIDI, footswitch, store, installer, simulator and editor checks. The optional official rollback package and Playwright page tests were unavailable. Both actual 992 packages passed manifest/CRC verification and simulated installation.

## E. Remaining opportunities

- Drive and capture the BlackBox with a repeatable analog guitar signal while recording peak block time. That would establish Fuzz Face worst-case load and distinguish knob-update spikes from audio-rate work.
- If future development pressure moves to general RAM, measure compact-cache save latency and power-cut behavior before trading the current single-write first sector for smaller cache rows.
