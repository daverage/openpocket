# Noise Suppressor and patch volume

Implementation and host validation, 26 November 2026. This is a test firmware change; no physical pedal was flashed.

## Interface and routing

GATE contains NOISE GATE (THRESH, RELEASE) and NOISE SUPPRESSOR (THRESH, RELEASE, HUM, HUSH). The original Noise Gate sample algorithm is retained. The previous Smart Gate DSP and its state are removed from the product engine. Standalone EQ HUM FILTER and global HUSH are removed.

Patch volume is `px_preset_t.patch_volume`, independent of effects: 0..100 maps to −15..+15 dB in 0.3 dB steps, with 50 at unity. Existing output gain placement, gain glide and MASTER behavior remain. Hardware FX visits PATCH SETTINGS after Valve Level; FX from the patch list opens it directly. The browser provides Patch settings and shows volume in patch list summaries. MIDI CC 3 controls patch volume; CC 7 remains MASTER. The editor's SET action 11/parameter 0 is a setting, not a footswitch assignment.

```
Input DC removal / optional global HPF → input HUM → clean synth tracking
                                                   └→ suppressor detector
                                          → Valve → ordered effect chain
                                                     └→ GATE gain/HUSH stage
                                          → patch volume → existing output path
```

The detector runs on the hum-filtered input before Valve, boosts, compression, synth replacement and modulation. Only per-sample control values, not audio, are retained until the reorderable gain stage. Existing mono synth tracking keeps its clean input. No audio lookahead, added audio delay, FFT, pitch detection, heap allocation or extra audio buffering is introduced by suppression.

Factory block orders remain unchanged. Keep GATE before pitch processors, delay and reverb. When pitch processing precedes GATE, hold increases by 100 ms to cover delayed wet output; this is a conservative tail allowance, not sample-exact wet/dry alignment. Moving GATE after delay/reverb intentionally attenuates their tails, including spillover. The existing spillover engine is unchanged.

## Broadband detector

- Peak follower: instantaneous capture, 18 ms decay. No sidechain high-pass that discards low guitar fundamentals.
- Sustain follower: fast rise, 80 ms decay, driven by the peak follower. This bridges low-frequency waveform valleys.
- THRESH: approximately −80..−28 dBFS plus a conservative learned floor. Zero opens broadband attenuation while HUM/HUSH remain available.
- Noise learning: upward updates only while closed and below half the configured threshold, with an 8 s time constant. Falling estimates use 150 ms. Musical sustain freezes upward learning.
- Hysteresis: open above threshold; sustain above 45% of threshold. Automatic hold is 35 ms, plus the pitch allowance when needed.
- Closing expansion: bounded eighth-power amplitude curve below threshold, limited to 0.0001 (−80 dB). Gain follows continuously; attack is 150 µs and release is 30..1000 ms. New attacks resume from the current gain.
- State remains fixed-size: suppressor state is 96 bytes. Two 64-sample control arrays add 512 bytes of temporary stack storage, without delaying audio.

A threshold still needs to suit the pickup and noise. Wanted notes below the noise floor cannot reliably be distinguished by this design. There is no program-dependent release or classification system.

## HUM

The existing notch utility and six Goertzel resonators are reused, separate from EQ. HUM values are OFF / 50 Hz / 60 Hz / AUTO. Fixed processing uses the mains fundamental and first two harmonics, each with a 2 Hz width and 25 dB center attenuation.

AUTO compares all three harmonics in each family over half-second observation windows, requiring two consistent votes. Strong agreement from every harmonic prevents a single guitar partial from deciding the mains. During an open note, a change from an established frequency additionally requires a 16:1 family contrast at every harmonic. Ambiguous inputs retain the selection. There is no guarantee of correct identification under arbitrary musical interference; fixed 50/60 Hz modes remain available.

Two small filter banks crossfade over 20 ms when frequency or enable state changes, avoiding live coefficient replacement. OFF stops filtering and mains detection once the disable transition completes. A note exactly at a notch frequency is attenuated too: no claim of noise rejection should hide that unavoidable overlap. The frequency tests check wanted low E, A and midrange tones independently of hum rejection.

## HUSH

HUSH uses a fixed complementary split near 3 kHz: low = LP(x), high = x − low. Output is `x − (1 − high_gain) × high`, so unity high gain reconstructs the input exactly. There is no moving cutoff.

A separate clean-input HF envelope and a conservatively learned HF floor control high-band gain. Learning freezes during detected notes; with broadband THRESH disabled it permits only quiet, HF-rich observations with peak-to-mean input ratio over 1.85; a simple HF sine is excluded from upward learning. Pick onsets bypass HF reduction. HUSH scales maximum high-band reduction conservatively to about 16.5 dB, with continuous gain smoothing. Turning HUSH off restores unity smoothly, then bypasses processing. The algorithm cannot remove hiss under strong wanted upper harmonics without affecting those harmonics; it favors preserving them.

## Presets and flash

Factory levels move directly into the patch field. NOISY ROOM uses the integrated AUTO hum suppressor. The bundled JSON bank gains patch-level volume.

Wire protocol 12 sends 98 bytes; historical wire preset layouts are rejected. JSON carries `patchVolume`. Flash format 8 keeps the 112-byte slot, base layout, saved mark, gestures, Valve extension and assignment offsets; volume is stored at byte 102.

This transition build has one bounded format-7 update at boot. It moves saved volume out of GATE, maps global HUSH into existing suppressor patches, and moves active standalone HUM into the suppressor, preserving block order and remapping a HUM footswitch/gesture from EQ to GATE. Other assignments remain unchanged. Nominal threshold and release are translated to the new control scales. A hum-only patch gets THRESH 0. The current notch design replaces old variable width/depth/harmonics settings, so exact historical hum-filter responses are not retained. Separate hum and gate switching is consolidated in the new block; an old HUM footswitch assignment follows the integrated suppressor, so it now switches the combined processing.

The update commits through the existing dual-copy, CRC16, header-last, read-back-verified store. A failed write leaves the original flash copy intact and the update can retry on the next boot. Boot-time writes skip audio suspension because audio has not started. Unknown older layouts are retained read-only, never erased as placeholders. The transition routine can be removed after the installed format-7 stores have been updated; it is not a general historical-format decoder.

Simulated flash tests cut power at points throughout the conversion and ordinary saves, then reboot and verify patch volume and assignments. Actual user flash has not been read or rewritten in this session; it updates when this test firmware boots on a current-format store.

## Focused results

Reproduce with `python3 tools/research/noise_compare.py ec66fee83384c737b30d91c4a14c2a7fccf0a109`. This builds historical DSP only in a temporary directory; the old algorithm is not compiled into firmware. Float renders and machine-readable results go to `build/noise-comparison/`.

Input: the same six-second `docs/nam_di.wav` guitar recording at gain 0.15, preceded by one second of idle and followed by six seconds of idle, mixed with independently generated seeded hiss and 50 Hz interference. HUM/HUSH are off for this gate comparison. Normal THRESH 35, previous Smart SENS 8 and Suppressor THRESH 40 give approximately −59 dBFS nominal floors; releases are approximately 253 ms. Adaptive decision rules differ, so nominal threshold matching is not equal behavior.

| Measurement | Original Noise Gate | Previous Smart Gate | Noise Suppressor |
|---|---:|---:|---:|
| Wanted-signal projection over DI | 0.000 dB | −0.001 dB | approximately 0.000 dB |
| Active 10 ms windows below half reference projection | 0 / 549 | 0 / 549 | 0 / 549 |
| Noise attenuation, 1..3 s after DI ends | 41.61 dB | 42.95 dB | 30.94 dB |
| Settled idle attenuation | numerical silence | 80.07 dB | 80.06 dB |
| Host CPU, one run | 0.007767 µs/sample | 0.011772 µs/sample | 0.008385 µs/sample |

These host timings are indicative, not AC791N CPU measurements. The baseline and working tree also contain other differences, including pre-existing work, so whole-engine RAM differences are not attributed to this refactor. The slower post-note closing is visible and intentional; the new detector protects sustain longer rather than delivering the deepest reduction immediately after a note.

`tests/engine/suppressor_test.c`, in both PX_RUNS modes, verifies exact THRESH-0 bypass, identical detector gain with max Valve and EP Boost before attenuation, a decaying 41.203 Hz note with unity sustain gain, two open/close state transitions without chatter, a largest closing gain increment of about 0.00010121, approximately −80 dB idle gain, and reopening within 128 samples (2.9 ms). `hum_test.c` checks >22 dB rejection at all three 50/60 Hz harmonics, wanted-tone preservation, AUTO acquisition/change and finite bounds. `hush_test.c` checks an independently noise-only tail interval and retained wanted upper harmonics, plus scope/bypass behavior.

The DI-window metric is not an individual-note missed-note count. This session has not established a full listening verdict, threshold-crossing modulation spectra, attack loss for every playing technique, or on-device peak CPU. Those remain hardware/listening acceptance work; the tests do not establish absence of every audible artifact.

## Build and broader verification

`make package-test` builds the isolated MIDI test channel: `firmware/openpocket-midi-test.bin` and `docs/firmware/midi-test/{PocketX_973,BlackBox_973}.fwsc`. The v73 identity is the repository's test-channel identity, not the release version. Normal release packages are not overwritten.

The test firmware links successfully within AC791N limits. The last build reports a 280264-byte application, 53124 / 98304 bytes in the main RAM region, 13476 / 24576 bytes in low RAM, and 75428 / 86016 bytes in the RAM-code window. Firmware size and hashes are also in `build/SHA256SUMS-midi-test` and the package manifest.

Passing checks include suppressor, integrated HUM/HUSH, factory bank, globals, Valve, acoustic/boost effects, pitch shifter, synth engines, tails, preset-store power cuts, MIDI, hardware screen rendering, browser-editor protocol, editor logic, simulator controls, USB audio, installer and simulated firmware flashing. The simulator WebAssembly engine is rebuilt from the updated sources.

The broad suite is not fully green: the two octave/fuzz sweep failures also reproduce with the unmodified HEAD engine; the older protected-render baseline differs; and `make test` stops in its existing embedded-Python lowest-octave command with a quoting error under the container invocation. Remaining tests are run separately rather than treating the suite's piped summaries as proof of success. No unrelated octave/synth processing was changed to address those failures.

Browser page screenshots were not exercised because Playwright is not installed. Hardware UI pages are rendered and checked by `uisim`; editor protocol, logic and simulator audio tests passed separately.
