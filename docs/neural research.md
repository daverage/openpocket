Agreed. One firmware, one codebase, one synth-focused interface. No Full/Synth build profiles, no variant switching, no legacy preset migration. Keep the original author's firmware available separately through GitHub.

This prompt includes the complete DSP research, engine replacement, testing and firmware rebuild.

# OpenPocket Synth: Complete Firmware and DSP Rebuild

**Repository:** [https://github.com/daverage/openpocket](https://github.com/daverage/openpocket)

## Objective

Turn my OpenPocket fork into a dedicated, high-quality guitar synthesiser and polyphonic octave pedal for the M-VAVE BlackBox and Donner Pocket X.

This is a **full DSP research, evaluation and implementation project**, not simply a firmware cleanup.

Use one firmware configuration and one codebase. Do not create Full/Synth variants or maintain compatibility with the original multi-effects firmware. The original author's firmware remains available separately.

Priorities: **accurate tracking, low latency, musical quality and reliable hardware operation.**

## 1. Review the existing code

Read the latest repository, especially:

- `docs/phase2b.md`
- `docs/PHASE2B-HANDOFF.md`
- `docs/TRACKING-ARCHITECTURE-REVIEW.md`
- `engine/px_track.h`
- `engine/px_poly_track.h`
- `engine/px_resonator_test.h`
- `engine/px_pulse_poly.h`
- `engine/px_acoustic.h`
- `engine/px_dsp.c`
- `engine/px_dsp.h`
- `firmware/build.sh`
- `firmware/app.ld`

Identify what is implemented, experimentally tested, actually installed and verified on hardware.

Do not assume experimental v67 changes are improvements over v66. Review the recorded listening failures and benchmark limitations.

## 2. Remove unnecessary functionality

Completely remove from this fork's active firmware:

- NAM and neural amplifier models.
- Conventional amplifier modelling.
- Cabinets and user IR processing.
- General-purpose drive pedals, except processing needed for Poly Oct Fuzz.
- Unnecessary guitar simulations.
- Unused model assets and DSP state.
- Redundant legacy algorithms once replacements are validated.

Retain:

- All useful mono and poly synths.
- Solo orchestral instruments and ensembles.
- Organ, Strings, Choir and String Machine.
- Poly Octave HQ, including octave −2.
- Poly Oct Fuzz.
- Whammy, Harmonizer, Detune and useful pitch effects.
- Normal Gate and Smart Gate.
- Compression and EQ.
- Chorus, Phaser, Flanger, Vibrato, Dimension, Rotary and other useful modulation.
- Short delay, approximately 500 ms maximum.
- Efficient, low-memory reverb.
- Tuner, MIDI, USB audio and normal hardware functionality.

Retain working shared DSP components.

Reclaim RAM, RAM-code, DSP pool and flash resources. Report actual savings from linker maps.

## 3. New block layout and physical controls

Reorganise the firmware around these logical blocks:

1. INPUT: Gate, Smart Gate, Compressor.
2. OCTAVE: Poly octave and pitch effects.
3. SYNTH: Synthesiser and instrument engines.
4. TONE: EQ and relevant sound-shaping effects.
5. MOD: Modulation effects.
6. DELAY: Short delay.
7. REVERB: Efficient reverb.

Remap physical buttons:

| Hardware button | New function           |
| --------------- | ---------------------- |
| AMP             | SYNTH                  |
| CAB             | OCTAVE                 |
| FX              | INPUT / TONE / UTILITY |
| MOD             | MOD                    |
| DLY             | DELAY                  |
| REV             | REVERB                 |

Update the display, menus, LEDs, bypass, footswitch assignments and editor accordingly.

Implement the simplest sensible internal block layout. Existing numeric IDs can change if that genuinely simplifies the firmware, but avoid unnecessary restructuring.

Preserve a clean input-analysis path so pitch tracking receives the original conditioned guitar signal, not an already distorted or pitch-shifted signal.

Support useful SYNTH/OCTAVE combinations without unnecessary DSP duplication.

## 4. Presets

**Discard the existing presets. No migration required.**

- Keep the existing reliable preset-storage mechanism where practical.
- Initialise the 52 slots with safe, simple placeholders.
- Remove obsolete factory preset definitions.
- Do not develop compatibility logic for old presets.
- Do not erase firmware recovery, OTA or unrelated flash regions.
- Retain preset save/load and CRC protection.
- Do not reset presets automatically on every firmware update.

We will create a new factory preset collection later.

## 5. Research modern DSP algorithms

Research the best open-source implementations available, prioritising work from 2020–2026.

Include VST3, LV2, JUCE, embedded DSP, guitar-to-MIDI, recent papers and neural/hybrid methods.

Investigate at least:

- [https://github.com/Frieve-A/jit-mpe](https://github.com/Frieve-A/jit-mpe)
- [https://github.com/lars76/swift-f0](https://github.com/lars76/swift-f0)
- [https://github.com/maximmashkov1/gdi2m](https://github.com/maximmashkov1/gdi2m)
- [https://github.com/markusstrasser/pitchplease](https://github.com/markusstrasser/pitchplease)
- [https://github.com/anirbanray1981/PiPitch](https://github.com/anirbanray1981/PiPitch)
- [https://github.com/SonyCSLParis/pesto](https://github.com/SonyCSLParis/pesto)
- [https://github.com/groupmm/real_time_swipe](https://github.com/groupmm/real_time_swipe)
- [https://github.com/Signalsmith-Audio/signalsmith-stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)

Find additional strong candidates.

Prefer proven source implementations over creating approximations of published algorithms.

Do not assume neural processing is superior, but reconsider feasibility using the memory recovered from removing NAM and amplifier processing.

Evaluate actual model weights, activation memory, feature-extraction cost, inference time and causal latency.

## 6. Rebuild and improve the three DSP engines

### A. Full-chord octave processing

Process complete chords without requiring note identification.

Retain the existing ERB-based Poly Octave HQ as the baseline.

Evaluate modern alternatives for:

- Octave −1 and −2.
- Octave +1.
- Chord clarity.
- Transient preservation.
- Low latency.
- Reduced artefacts.
- Efficient CPU and memory use.

Preserve Poly Oct Fuzz.

Improve Lowest Note octave-down only if reliable identification and independent release can be demonstrated.

Do not replace successful audio processing without evidence of improvement.

### B. Monophonic note tracking

Find and implement the strongest practical low-latency mono tracker.

Requirements:

- Fast acquisition.
- No random octave jumps.
- Low E and bass support.
- Accurate bends, slides and vibrato.
- Reliable note-on, repick and release.
- Resistance to harmonics and sympathetic ringing.

Benchmark the existing tracker against modern alternatives, including compact neural models where feasible.

Use the winning implementation for Mono Synth, Bass Synth and solo instruments.

### C. Independent polyphonic note tracking

**Highest research priority.**

Accurately detect at least two independently played notes from a normal guitar pickup.

Requirements:

- Correct octave and close-interval recognition.
- Minimal false harmonic notes.
- Independent note attack and release.
- Stable continuous pitch per voice.
- Correct tracking through bends and individual mutes.
- Reliable quiet second-note acquisition.

Investigate modern multi-F0 estimation, harmonic models, causal neural detectors, hybrid DSP/neural tracking and temporal onset analysis.

Do not keep adding heuristic rejection rules to the existing resonator tracker without independent evidence.

Test original source implementations first. Port or optimise the strongest realistic approach for the AC791N.

Expand beyond two voices only if accuracy, CPU and memory measurements justify it.

## 7. Synth and instrument rendering

Retain and improve:

- Pulse Poly.
- Saw Poly.
- Arcade Poly.
- FM Poly.
- Mono/PWM/Bass synths.
- Organ, String Machine and Choir.
- Solo Violin, Viola, Cello, Trumpet and brass instruments where available.
- String Ensemble, Brass Ensemble and String Duo.

Use shared oscillators, envelopes, filters and sample resources.

Distinguish layered ensembles following one note from independently voiced ensembles following different notes.

Improve timbre, attack, sustain, vibrato and release. Avoid duplicating DSP unnecessarily.

Ensure a tracking error does not cause uncontrolled oscillator volume or stuck notes.

## 8. Benchmark and validate

Use independent electric-guitar DI recordings and player-separated datasets.

Do not optimise solely against the repeatedly used GuitarSet development examples.

Measure:

- Mono pitch accuracy and octave errors.
- Multi-note precision, recall and F1.
- Correct note-on latency.
- Independent note-off latency.
- Harmonic ghost notes.
- Real octave and semitone detection.
- Bends, slides, repicks and mutes.
- Real input-to-output audio latency.
- CPU peak and worst-case callback load.
- Memory use.
- Audible artefacts and musical quality.

Compare every replacement directly against the existing engine.

Evaluate on desktop first, then on BlackBox hardware.

Test representative combinations including Synth + Octave + Gate + Mod + Delay + Reverb.

Require zero late audio blocks, no safety bypass and stable second-core operation.

Do not declare a new engine successful based only on synthetic signals or host benchmarks.

## 9. Firmware, editor and installer

Update the existing firmware to reflect the new dedicated Synth architecture.

- Produce BlackBox and Pocket X builds.
- Update effect selection, controls, displays and LEDs.
- Update the existing editor for the new blocks and effects.
- Remove obsolete amp, cabinet and NAM controls.
- Keep the existing installer and recovery process wherever possible.
- Update firmware identity and package metadata only as necessary.
- Do not build a second editor or dual-firmware installer.
- Preserve safe flashing, rollback and recovery.

Avoid changes to the bootloader or flash layout unless unavoidable.

## 10. Implementation order

1. Audit the latest source and existing test results.
2. Remove unwanted DSP and reclaim memory.
3. Implement the new blocks, physical mappings and blank preset bank.
4. Build and validate the stripped firmware.
5. Research and benchmark modern mono and poly tracking alternatives.
6. Implement the best feasible tracking engines.
7. Evaluate and improve full-chord octave processing.
8. Improve synth and orchestral rendering.
9. Optimise CPU, RAM and combined effects performance.
10. Update editor, installer and documentation.
11. Complete host and hardware regression testing.

Commit working stages independently so regressions can be identified and reverted.

## 11. Deliverables

- A single, dedicated OpenPocket Synth firmware.
- New block layout and remapped hardware controls.
- Clean 52-slot preset bank.
- Removed amplifier, NAM and cabinet infrastructure.
- Improved mono and polyphonic tracking engines.
- Validated full-chord octave processing.
- Improved synthesiser and ensemble rendering.
- Updated editor and installer.
- Firmware builds for BlackBox and Pocket X.
- Before/after resource measurements.
- Benchmark results documenting the algorithms selected.
- Hardware test report with known limitations.

Build and test the complete firmware rather than stopping at recommendations or a design document.

**Do not automatically flash the pedal.** Produce tested binaries for review first.

## Engineering principles

- One firmware, one codebase.
- No legacy preset migration.
- No unnecessary compatibility layers.
- Prefer proven open-source algorithms.
- Use modern research where it demonstrably improves results.
- Remove superseded implementations after successful validation.
- Keep working audio functionality stable.
- Prioritise accurate tracking over adding more effects.
- Measure latency, memory and CPU on real hardware.
- Keep the implementation as simple and maintainable as possible.

**Final objective:** the best achievable low-latency guitar synth and polyphonic octave pedal on this hardware, with practical supporting effects and a purpose-built interface.

One important implementation choice: **use the existing preset storage and bootloader infrastructure even though the presets themselves can be discarded.** Rewriting those systems adds risk without improving the sound or tracking.
