Worked for 5m 11s

### OpenPocket Synth: dual-firmware implementation specification

The safest approach is one repository, two firmware products, and one shared DSP codebase.

* OpenPocket Full retains every existing effect, feature and capability.

* OpenPocket Synth exposes a focused synthesiser and octave interface, excludes unused DSP implementations from its compiled image, and makes the recovered memory available for better pitch-processing algorithms.

Nothing is deleted from the repository. The Full firmware must remain independently buildable, installable and fully functional.

My review of the repository identifies three important constraints:

* The current preset format has 12 fixed block IDs and a 110-byte preset structure. We should preserve those IDs initially, rather than risk corrupting saved presets and MIDI mappings. [Source](https://github.com/daverage/openpocket/blob/main/engine/px_dsp.h?utm_source=chatgpt.com)

* Both firmwares will initially share the existing 52-slot persistent preset store. Unsupported effects must be preserved as data, even if the Synth firmware ignores them. [Source](https://github.com/daverage/openpocket/blob/main/firmware/px_store.h?utm_source=chatgpt.com)

* The installer currently assumes one firmware package per pedal model, while the web editor infers capabilities partly from protocol version. Both need explicit firmware-variant support. [Installer](https://github.com/daverage/openpocket/blob/main/docs/index.html?utm_source=chatgpt.com) · [Editor protocol](https://github.com/daverage/openpocket/blob/main/docs/EDITOR-PROTOCOL.md?utm_source=chatgpt.com)

The following specification addresses the firmware architecture, retained effects, physical controls, memory reclamation, DSP improvements, installer, editor, storage safety and testing.

# OpenPocket Full + OpenPocket Synth
## Dual-Firmware Architecture and Implementation Specification

**Repository:** https://github.com/daverage/openpocket
**Status:** Implementation specification
**Primary hardware:** Donner Pocket X and M-VAVE BlackBox, JieLi AC791N
**Priority:** Preserve Full firmware functionality while creating a dedicated low-latency Synth + Octave firmware.

---

## Phase 2b update

The Full and Synth profiles below describe a conditional product split. Keep Full as
the default while testing the tracker on the standard memory budget. A second
profile needs a tracking design that improves real playing on the pedal and has a
measured memory or code placement requirement that Full cannot satisfy. Record
these results per physical memory section before implementing the variant UI,
editor and installer work. See [Phase 2b](phase2b.md#9c-decide-the-split).

### Candidate catalogue for both profiles

Use the same effect groups on the pedal and in the editor. Menu grouping can
change independently of stored block IDs, chain order and processing position.

| Group | Effects |
|---|---|
| Octave and pitch | OCTAVER, POLY OCTAVE HQ with OCT −2, WHAMMY, HARMONIZER, DETUNE, BARITONE, 12-STRING |
| Synth, guitar-shaped | ORGAN, CHURCH ORGAN, STRINGS, CHOIR, STRING MACHINE, POLY BRASS |
| Synth, tracked | MONO SYNTH, SQUARE, PWM, BASS, PULSE POLY, SAW POLY, ARCADE POLY, FM POLY, STRING DUO |
| Instruments | TRUMPET, BOWED CELLO, CELLO ENSEMBLE, STRING ENSEMBLE, BRASS ENSEMBLE, ACOUSTIC, SITAR |
| Drive | Existing drive types and POLY OCT FUZZ |
| Special effects | Wah, ring modulation, bitcrusher, slicer and lo-fi |

The v66 poly-test build carries the tracked mono layer, poly voicings, new
ensembles and STRING DUO. These are candidates for the shared catalogue after
their listening and regression gates. OCT −2 on POLY OCTAVE HQ passed the
owner's listening check. LOWEST OCTAVE HQ stays an experiment.

Current poly-test loads map BASS SYNTH to BASS SYNTH HQ, PWM SYNTH HQ to PWM SYNTH,
and POLY OCTAVE to POLY OCTAVE HQ with RANGE at zero. Production retirement
requires the HQ version to match the original's sound and improve tracking.
Requalify BASS, and reverse the PWM mapping once PWM HQ qualifies. For any
cross-block move, define destination conflicts, chain order, editor round trips
and footswitch/MIDI compatibility before accepting the migration. See the
[catalogue and migration gates](phase2b.md#stage-7-retire-originals-to-hq-where-hq-meets-the-rule).

### Measured v66 headroom

| Region | Free in the v66 poly-test build |
|---|---:|
| General RAM | 4,536 B |
| Low RAM | 864 B |
| DSP pool | 1,392 B |
| RAM-code window | 312 B |
| XIP flash | About 95 KB |

The tracker and synth renderers run from flash. The Phase 2b inventory attributes
about 86 KB of DSP pool, 17.7 KB of general RAM, 5.7 KB of RAM code and 108 KB of
flash code plus model weights to NAM. It attributes about 10.3 KB of general RAM,
9 KB of low RAM and 2.2 KB of RAM code to AMP/CAB/IR. Treat those as inputs to a
profile comparison. Compile and compare maps to establish the headroom a stripped
build actually recovers. [v66 build log](../../build/phase2/build-v66.log) ·
[Family breakdown](phase2b.md#9b-update-docsspacesavermd)

The poly path already uses the shared decimator. Prototype low-note waveform
checks with the existing 1/16-rate history before allocating a larger buffer.
The current accuracy limits include whether the detector can distinguish overtones
from second strings and recover weak roots. More memory helps only when a measured
algorithm can use it. If the prototype needs more data space, test exclusive
ownership of the NAM ring pool across model loads, preset changes, NAM enable or
disable, and bench scratch access. The pool does not expand the RAM-code window.

If the split gate passes, keep the requirements below for two packages per device
model, explicit capability reporting, legacy installer/editor behaviour and
shared preset round trips. Update the later sections again with the actual
supported catalogue, IDs and map from both built profiles.

---

## 1. Core requirements

Create two independent firmware variants from the same OpenPocket repository.

### OpenPocket Full

The existing multi-effects firmware.

- Retain every existing effect, model, block, control and feature.
- Retain NAM, conventional amplifiers, cabinets, user IRs, delays, reverbs, modulation, MIDI and USB audio.
- Preserve the existing user interface and physical button assignments.
- Preserve current preset handling and firmware update behaviour.
- Continue receiving bug fixes and appropriate shared DSP improvements.
- Remain the default build and installation choice.

**No existing Full firmware functionality may be removed to accommodate the Synth variant.**

### OpenPocket Synth

A dedicated firmware optimised for synthesiser and polyphonic octave effects.

- Remove unused amp and cabinet processing from the compiled Synth image.
- Retain existing creative effects that support synthesis, octave processing and sound design.
- Remap physical AMP and CAB buttons to SYNTH and OCTAVE.
- Present a simplified, synth-oriented interface.
- Reclaim memory, flash and RAM-code space for improved audio processing.
- Support independently tracked polyphonic oscillator voices.
- Maintain existing HQ full-chord octave processing.
- Continue researching an OC-5-style Lowest Note octave-down mode.
- Support the same hardware, installer, editor, MIDI connection and update mechanism as Full.

**Important:** Removing functionality from the Synth build means conditionally excluding it from that firmware image. It does not mean deleting the implementation from the repository.

### Non-negotiable design rules

1. One codebase, two build profiles.
2. Full must remain backward-compatible.
3. Both firmware variants must be independently buildable and installable.
4. The compiler must genuinely exclude unnecessary DSP from Synth. Hiding it in the UI is insufficient.
5. Persisted presets and user assets must survive switching variants.
6. Use common source code for shared effects and DSP primitives.
7. No heap allocations in real-time DSP.
8. No new algorithm is considered production-ready without host and pedal measurements.
9. Do not change the stock, loader or recovery flash layout without an explicit, tested migration.
10. Never overwrite existing firmware packages with output from another variant.

---

## 2. Build-profile architecture

Introduce one central build-profile configuration, for example:

`firmware/px_build_profile.h`

Proposed compile-time options:

```c
#define OP_BUILD_FULL   0
#define OP_BUILD_SYNTH  1

/* Set exactly one build profile. */

#if OP_BUILD_SYNTH
#define OP_FEATURE_AMP_MODELS  0
#define OP_FEATURE_NAM         0
#define OP_FEATURE_CAB_MODELS  0
#define OP_FEATURE_USER_IR     0

#define OP_FEATURE_GATE        1
#define OP_FEATURE_COMP        1
#define OP_FEATURE_PITCH       1
#define OP_FEATURE_SYNTH       1
#define OP_FEATURE_SFX         1
#define OP_FEATURE_DRIVE       1
#define OP_FEATURE_EQ          1
#define OP_FEATURE_MOD         1
#define OP_FEATURE_DELAY       1
#define OP_FEATURE_REVERB      1
#endif
```

These are illustrative flag names. Implement the actual header with safe defaults, explicit Full configuration and compile-time validation so contradictory combinations fail.

The feature flags must control:

- DSP compilation and code inclusion.
- Persistent processing-state allocation where possible.
- Runtime execution.
- Effect availability.
- UI navigation.
- Editor capabilities.
- Installer metadata.
- Tests and diagnostics.

Do not scatter unrelated `#ifdef` statements throughout the firmware. Use a central feature definition and narrow, clearly documented conditional boundaries.

### Build outputs

Maintain the existing build output:

`firmware/openpocket.bin`

Add:

`firmware/openpocket-synth.bin`

Provide separate object/output directories to prevent cross-profile contamination.

Expose commands equivalent to:

```sh
make build          # Existing Full firmware
make synth          # Dedicated Synth firmware
make package        # Both variants and combined manifest
make test           # Common + Full + Synth tests
```

The existing Full build command must retain its behaviour.

The build system should also retain existing Bluetooth and test-channel support, with explicit variant handling where applicable. Do not allow test packages to overwrite release packages.

### Files to update

- `firmware/build.sh`
- `tools/op.mjs`
- `Makefile`
- `Dockerfile` only if genuinely necessary
- Firmware profile headers
- Test scripts
- Build documentation

---

## 3. Effects retained in OpenPocket Synth

The objective is not to remove every conventional effect. Synth sounds need filtering, distortion, chorus, space and dynamics.

The following is the **initial retained-effects contract**.

| Existing subsystem | Full | Synth | Synth interface |
|---|---|---|---|
| Normal Gate | Retain | Retain | INPUT |
| Smart Gate | Retain | Retain | INPUT |
| All four compressors | Retain | Retain | INPUT |
| Existing octave and pitch effects | Retain | Retain | OCTAVE |
| Existing synth and instrument effects | Retain | Retain | SYNTH |
| Special FX | Retain | Retain | FX / UTILITY |
| Existing drive models | Retain | Retain | TONE |
| Existing EQ models | Retain | Retain | TONE |
| Existing modulation effects | Retain | Retain | MOD |
| Existing delay algorithms | Retain | Retain | DELAY |
| Existing reverb algorithms | Retain | Retain | REVERB |
| Conventional amplifier models | Retain | Exclude DSP | Hidden |
| Neural Amp Modeler | Retain | Exclude DSP | Hidden |
| Cabinet models | Retain | Exclude DSP | Hidden |
| User cabinet IR convolution | Retain | Exclude DSP | Hidden |

### Specific retained effect families

**OCTAVE:** Keep Octaver, Poly Octave, Poly Octave HQ, Whammy, Harmony and Detune. Preserve their current working behaviour while adding new experimental modes separately.

**SYNTH:** Keep all existing organ, church organ, strings, choir, mono synth, PWM, bass synth, HQ variants, String Machine, Brass, Octave Fuzz and current instrument voices. Existing acoustic, sitar, baritone, trumpet and cello implementations remain available in the initial Synth profile.

Add the planned vintage toys:

- Pulse Poly
- Saw Poly
- Arcade Poly

Continue refining String Machine, Poly Brass and Drawbar Organ using existing sounds rather than unnecessarily duplicating their code.

**MOD:** Keep Chorus, Dimension, Flanger, Phase 90, Small Stone, Uni-Vibe, the tremolos, Rotary and Vibrato.

**SFX:** Keep Auto Wah, LFO Wah, Ring Modulation, Bitcrusher, Slicer, Lo-Fi and the existing Square Synth effects.

**TONE:** Keep the existing drive and EQ DSP, but expose them under a simplified Tone interface. Shared filters and saturation routines may also be used internally by synths.

**DELAY and REVERB:** Initially retain all existing algorithms and their current buffer sizes. These are musically important for ambient synths and pads.

Only consider a reduced-memory Delay/Reverb option as a separately benchmarked optimisation. It must not silently reduce quality or parameter ranges.

### Features retained in both firmwares

Keep:

- USB MIDI and MIDI control.
- USB audio where currently supported.
- Preset loading, saving and export.
- Physical footswitches and STOMP/PATCH modes.
- Tuner.
- Encoder and display handling.
- Battery monitoring.
- Firmware update and recovery.
- DSP-load measurement.
- Existing input/output audio handling.
- Preset-level and output protection.
- Current hardware safety and second-core fallback mechanisms.

### Features excluded from Synth only

The following remain in the repository and in Full:

- Neural amp inference.
- NAM factory and user model processing.
- Conventional tube-amplifier processing.
- Amp tone stacks, power-amp modelling and sag processing used only by amplifier models.
- Cabinet simulation and cabinet-specific convolution.
- Cabinet user IR loading and processing.

Do not delete their source files, shared interfaces, model assets, tests or documentation.

Do not erase user NAM or IR slots during Synth installation or operation.

---

## 4. Synth firmware interface

### Physical button reassignment

Use the existing hardware buttons and electrical input mappings.

| Physical button | Full firmware | Synth firmware |
|---|---|---|
| AMP | Amplifier | SYNTH |
| CAB | Cabinet | OCTAVE |
| MOD | Modulation | MOD |
| DLY | Delay | DELAY |
| REV | Reverb | REVERB |
| FX | Existing FX cycle | INPUT / TONE / UTILITY |

The hardware PCB and GPIO assignments do not change.

For Synth, translate the physical AMP button to the existing `PX_B_SYN` block and CAB to `PX_B_PITCH`.

**Do not rename or repurpose the underlying `PX_B_AMP` and `PX_B_CAB` numeric IDs.**

The physical panel will still have AMP and CAB printed on it. Use the screen to show their new function. Optionally provide a printable label overlay.

### Logical user-facing blocks

Organise the Synth interface into seven sections:

1. INPUT
2. OCTAVE
3. SYNTH
4. TONE
5. MOD
6. DELAY
7. REVERB

The FX button cycles INPUT, TONE and UTILITY. Utility provides access to creative SFX and any less frequently edited controls.

Do not force these seven sections to become seven new physical preset blocks immediately. They can initially be UI groups mapped onto the existing 12 internal blocks.

### Processing flow

Recommended default architecture:

```text
                  GUITAR INPUT
                       |
                 Input conditioning
                       |
                Clean analysis tap
                       |
             +---------+---------+
             |                   |
        OCTAVE PATH         SYNTH PATH
             |                   |
      HQ full-chord          Existing audio-
      octave engine          derived synths
             |                   |
      Lowest Note            Polyphonic
      experimental           note detector
             |                   |
             |              Oscillator voices
             |                   |
             +---------+---------+
                       |
                 Level / mix
                       |
                     TONE
                       |
                      MOD
                       |
                     DELAY
                       |
                    REVERB
                       |
                    OUTPUT
```

The shared note analysis must receive an appropriately conditioned copy of the original guitar input, not the already distorted or octave-shifted output.

The analysis sidechain may use separate filtering and noise conditioning. Smart Gate settings must not accidentally suppress sustained notes needed by tracking.

Preserve existing block-order and sound behaviour in Full.

In Synth, implement the proposed parallel OCTAVE/SYNTH arrangement as a dedicated processing mode, using shared functions rather than duplicating the entire DSP engine.

### Display

The PLAY screen should prioritise:

- Preset name.
- Active SYNTH model.
- Active OCTAVE model.
- Synth/octave on-off state.
- Active modulation and space effects.
- DSP-load information.
- Current firmware variant.

Remove AMP/CAB-specific display assumptions only within the Synth profile.

Keep the current Full screens unchanged.

### LEDs and gestures

Update `firmware/ui.h` and `firmware/px_lights.h`.

- The AMP LED represents SYNTH in Synth firmware.
- The CAB LED represents OCTAVE.
- MOD, DLY, REV retain their current indications.
- FX indicates the active utility/effect grouping.
- Maintain STOMP/PATCH switching.
- Retain tuner access and saving gestures.
- Clearly document that the existing hold-CAB gesture becomes hold-OCTAVE in Synth.

Persist footswitch assignments using canonical block IDs. An assignment created with the Synth button must target `PX_B_SYN` or `PX_B_PITCH`, not the old AMP/CAB ID.

Old saved AMP/CAB assignments must be treated as unavailable in Synth, not silently remapped to a different effect.

---

## 5. Memory optimisation

**Goal:** Recover usable SRAM, RAM-code capacity and firmware image space for better synth and octave DSP.

Do not redesign the memory map based only on expected savings. First measure the current allocation.

### Current important allocations

The existing code includes:

| Allocation | Approximate size | Initial Synth action |
|---|---:|---|
| NAM processing rings | 83.4 KB | Remove from compiled image |
| Delay buffer | 88 KiB | Retain |
| Reverb buffer | 32 KiB | Retain |
| Modulation buffer | 8 KiB | Retain |
| Synth pitch-shift buffer | 8 KiB | Retain |
| Other pitch-shift and octave state | Variable | Retain and optimise |
| User IR processing state | Build-dependent | Remove if only used for cabinet IR |
| Amplifier/cabinet state | Build-dependent | Remove or conditionally allocate |

The linker already separates multiple memory regions, including RAM, POOL, LOWRAM, RAMTEXT and CRAM.

Use `firmware/app.ld` and `firmware/build.sh` as the authoritative starting points.

### Required removal strategy

**Step A: Eliminate unused DSP code**

For Synth builds:

- Do not compile or include the NAM inference implementation.
- Do not compile NAM factory weights into the firmware.
- Exclude amp-only and cabinet-only processing functions.
- Exclude cabinet IR convolution where it has no other consumer.
- Remove obsolete model-specific lookup tables from the Synth binary.
- Exclude unnecessary amp/cab functions from RAM-code placement.

Guard these changes using shared build-profile flags.

Full must continue compiling all original implementations.

**Step B: Remove unused DSP state**

The current `px_engine` structure includes state for many effects.

For the Synth build:

- Identify which state fields are exclusively used by AMP, CAB or NAM.
- Conditionally omit those fields when safe.
- Retain shared state used by other effects.
- Preserve a stable Full build structure.
- Do not change persistent preset structures to save runtime memory.

Explicitly distinguish persistent data structures from runtime DSP state.

**Step C: Verify the actual reclaimed memory**

For each build, report:

- Flash image size.
- POOL used/free.
- RAM used/free.
- LOWRAM used/free.
- RAMTEXT used/free.
- CRAM used/free.
- Stack headroom.
- Additional tracker and oscillator state.
- Peak DSP cost.

Use linker symbols and map files, not estimates alone.

The build must fail if a memory region exceeds its permitted bounds.

### RAM-code allocation

OpenPocket already places time-critical audio routines into RAM through `tools/ramcode.py`.

For the Synth build:

- Remove unused amp, NAM and cabinet routines from its RAM-code list.
- Profile the resonator and polyphonic voice processing.
- Move genuinely timing-critical loops into RAM where that improves measured performance.
- Keep coefficient generation, model setup and other non-real-time operations outside the audio loop.
- Benchmark RAM versus flash execution on the actual chip.

Do not assume RAM code is always faster. Follow the existing hardware handbook's measured findings.

### Memory priorities for new DSP

Allocate reclaimed capacity in this order:

1. Polyphonic candidate analysis.
2. Continuous per-note frequency refinement.
3. Voice state, envelopes and oscillator processing.
4. Additional low-frequency evidence for bass tracking.
5. Improved octave-up processing and multi-resolution analysis.
6. Optional transient handling and short lookback buffers.

Prioritise algorithmic quality over simply adding more oscillators.

Do not allocate buffers based on the largest available free region without verifying placement, alignment, memory-bus behaviour and CPU access.

### Optional future lightweight Space profile

If measured headroom remains insufficient, investigate reducing the delay and reverb memory budgets specifically for Synth.

This should be an explicit, separately tested build optimisation, not part of the initial conversion.

Any reduced maximum delay time or changed reverb algorithm must be reflected in parameter labels, presets, documentation and editor capabilities.

---

## 6. Polyphonic octave processing

Retain the existing HQ phase-processing engine as the production baseline.

### Full-chord mode

- Preserve HQ octave-down behaviour.
- Keep the existing full-chord octave-up path.
- Continue evaluating the compact octave-up bank.
- Compare candidates using identical audio and matched output levels.
- Measure spectral target accuracy, unwanted components, attack response, warble and actual pedal CPU.

Do not replace a proven algorithm solely because another performs better on one synthetic fixture.

### Lowest Note mode

This is specifically an **OC-5-style Lowest Note octave-down option within Poly Octave**.

It is not a synth preset and is not intended to drive all synth oscillators.

The effect should:

- Detect the lowest sustained note.
- Apply an octave-down effect to that note only.
- Preserve the unshifted upper notes.
- Avoid octave switching when the lowest fundamental is weak.
- Release the lower voice when that note is independently muted.

The current host experiments demonstrate that:

- The resonator selects the correct lowest note in approximately 69.6% of settled dyad frames.
- Shared-bank spectral masking loses useful low-note output.
- Focused narrow filters are too slow for the desired feel.
- Synthesised sub-octaves can work with a correct note identity but are unreliable on weak-fundamental and bass fixtures.

Therefore, **do not enable the experimental shared-mask, focused-filter or synthesised-sub modes as production defaults**.

Retain fixed-Range octave processing as the playable fallback.

Use the dedicated Synth build's additional resources to investigate improved harmonic confirmation, continuous low-note pitch tracking and a controlled sub-oscillator. Treat alternative true audio-separation approaches as independent experiments.

---

## 7. Polyphonic synth engine

The three new oscillator synths are:

- Pulse Poly.
- Saw Poly.
- Arcade Poly.

Reuse the existing `polyblep()`, `syn_saw()`, LFO, ladder filter and envelope infrastructure.

### Shared tracker

Start from the proven host resonator approach, but do not treat its current performance as complete.

The benchmark's 42.1% correct two-note detection is a useful improvement over the older candidate extractor, not sufficient for a reliable finished polyphonic synth.

Implement a reusable tracking module that provides:

- Candidate note identity.
- Confidence.
- Continuous frequency estimate.
- Pitch variation in cents.
- Per-note amplitude/evidence.
- Independent onset and release evidence.
- Candidate persistence and dropout handling.
- Harmonic/octave consistency checks.

Do not instantiate a complete tracker per oscillator.

### Voice engine

Begin with two independent voices, expanding to four when accuracy and CPU allow.

Each voice should have:

- Oscillator phase.
- Pitch target and smoothed pitch.
- Gain.
- Independent envelope.
- Onset/repick detection.
- Release state.
- Tracking confidence.

Maintain continuous oscillator phase during sustained notes.

Use PolyBLEP saw/pulse waveforms and existing analogue-style filtering.

A tracked oscillator's frequency must not jump to the nearest semitone during ordinary guitar bends and vibrato.

### Tracking versus octave DSP

The synth voice tracker and full-chord octave shifter may use different algorithms.

Share decimation, spectral evidence or other analysis only when doing so saves measured CPU and does not compromise musical quality.

Preserve the ability to replace the tracking algorithm behind a stable internal interface.

### Algorithm experiments enabled by extra memory

Investigate:

- Additional resonator evidence for nearby semitones.
- Multi-resolution low-frequency analysis.
- Candidate-centred continuous phase/frequency refinement.
- Harmonic verification for weak fundamentals.
- Per-note onset and release measurements.
- Hybrid tracking using resonator identities and waveform evidence.
- More reliable octave-error suppression.

Do not automatically adopt an FFT, neural model or larger analysis bank without demonstrating a measurable improvement in accuracy, latency and AC791N cost.

### Performance requirements

Measure both tracking time and audible output onset.

Report latency from the guitar attack to the correct oscillator output, not simply the tracker's update interval.

Require testing with:

- Guitar chords and dyads.
- Close intervals and octave pairs.
- Bends and vibrato.
- Repicks.
- Independently muted notes.
- Weak fundamentals.
- Bass low E and low B.
- Smart Gate enabled and disabled.
- Representative synth + octave + modulation combinations.

Track false-note loudness and duration, not only frame-level accuracy.

The final design must remain usable if CPU1 becomes unavailable. Define a tested fallback policy for computationally expensive combinations rather than risking audio dropouts.

---

## 8. Preset compatibility and persistent storage

### Preserve canonical preset format initially

The existing firmware stores a 110-byte `px_preset_t` with:

- Name.
- 12 block enable flags.
- 12 block type identifiers.
- Five parameters per block.
- 12 block-order entries.

Keep this format initially.

Do not renumber blocks, shorten the structure, reorder existing effect type values or reinterpret old parameter bytes.

Append new synth type identifiers only.

### Existing unsupported effects

When Synth loads a preset containing AMP, CAB or NAM settings:

- Preserve their original stored values.
- Disable their audible processing in an effective runtime copy.
- Hide them from the normal Synth editing interface.
- Do not overwrite the stored values with zero or defaults.
- Do not change other blocks' canonical IDs.
- Do not allow hidden values to trigger unavailable DSP.

The audio engine must receive a safe, validated effective preset rather than relying only on UI visibility.

### Shared preset storage

The existing store uses 52 persistent slots with two alternating flash copies.

The initial implementation should preserve this storage layout.

**Important:** Full and Synth would share the same saved slots unless a separate storage design is implemented. Switching firmware will not automatically create two independent banks.

Required safeguards:

- Warn users before switching variants that saved presets are shared.
- Offer a complete preset backup through the editor.
- Preserve all hidden block values during normal Synth edits and saves.
- Never silently rewrite unsupported effect types.
- Ensure unsaved factory slots use the appropriate variant's factory presets.
- Preserve current flash CRC, alternating-copy and power-cut protections.
- Keep NAM and cabinet IR user flash regions untouched.
- Test full round trips between both variants.

If a Synth-only effect type is loaded by Full, use a safe unsupported-effect fallback without corrupting its saved data.

The current paired Full release must recognise the new type identifiers, even if its DSP implementation is compiled out. Older Full firmware may not understand them, so downgrade handling must be documented and tested.

### Future separate preset banks

Separate Full and Synth preset banks would be preferable long-term, but do not invent a new flash partition.

The existing upper-flash region is shared with OTA records, user NAM models, presets and IRs.

Any new bank must follow a verified flash-sector map, preserve OTA recovery and provide a tested migration and rollback mechanism.

Until then, shared storage plus reliable export/backup is the approved design.

---

## 9. Installer: two firmware options

The existing browser installer must clearly offer:

### OpenPocket Full

Description: Complete guitar multi-effects firmware with amps, NAM, cabinets, synthesis, modulation, delays and reverbs.

### OpenPocket Synth

Description: Dedicated guitar synthesiser and polyphonic octave firmware, optimised for pitch processing and vintage synth effects.

The default selection should be Full for existing users, with a clear, persistent Synth option.

### Required installer behaviour

After detecting the pedal:

1. Identify whether it is a Pocket X or BlackBox.
2. Identify the installed firmware variant, where available.
3. Show the two compatible firmware options.
4. Display the installed and available versions.
5. Explain which features differ.
6. Check package integrity before writing.
7. Warn about shared preset storage and offer a backup link.
8. Install only the explicitly selected package.
9. Show the installed variant and version on completion.
10. Support returning from Synth to Full and to official firmware.

### Package manifest

Extend the manifest to associate each package with both a hardware model and firmware variant.

Illustrative schema:

```json
{
  "schema": 2,
  "version": 51,
  "packages": [
    {
      "model": "PocketX",
      "variant": "full",
      "product": "PocketX_951",
      "file": "PocketX_951.fwsc",
      "size": 0,
      "sha256": "<computed-by-packager>"
    },
    {
      "model": "PocketX",
      "variant": "synth",
      "product": "PocketX_951S",
      "file": "PocketX_951S.fwsc",
      "size": 0,
      "sha256": "<computed-by-packager>"
    }
  ]
}
```

The file sizes and hashes above are deliberately placeholders. The packager must populate the real values. Include equivalent entries for BlackBox.

The product-name suffix is a proposed format, not an existing firmware identity.

### Identity compatibility

The current installer recognises identities such as `PocketX_951`, and some parsing logic assumes a numeric suffix only.

Update the identity parser, product validator, installer and completion screen to support a variant suffix while retaining support for all existing Full and official identities.

The firmware product identity is constrained by the package format's 20-character marker. Validate proposed names against that limit and the in-firmware `OP_PRODUCT` field.

Do not change the OTA loader's hardware identity unnecessarily.

### Switching and interrupted updates

- Always validate the selected package against the detected hardware model.
- Resume must use the original chosen variant, not automatically choose Full.
- Persist enough information to recover an interrupted installation.
- If the selected package cannot be identified reliably after a disconnect, require the user to select it again.
- Preserve the existing rescue flow.
- Reject mismatched packages before flash erasure.
- Do not delete or overwrite another variant's build output during packaging.

### Existing installer compatibility

Legacy manifests without a variant field should be interpreted as Full.

Keep existing official-firmware restoration and test-channel paths functioning.

Required files include:

- `docs/index.html`
- `docs/pxota.js`
- `docs/pxpkg.js`
- `tools/pxpkg.py`
- `tools/op.mjs`
- Installer tests and documentation

---

## 10. Web editor: variant-aware functionality

The web editor must connect to either firmware and present only the features available on that device.

**Do not build a completely separate editor.**

Use capability discovery rather than maintaining two unrelated UIs.

### Firmware capability reporting

Add a new editor protocol command, for example:

`ED_CAPABILITIES = 30`

Do not change existing command numbers.

Bump the editor protocol version when implementing the new command.

The response should provide:

- Firmware variant: Full or Synth.
- Capability schema revision.
- Preset format revision.
- Supported/active block IDs.
- Available effect types.
- NAM availability.
- Cabinet IR availability.
- Relevant optional features.
- Hardware-specific limitations where needed.

Keep the existing INFO response compatible with legacy clients.

Use the existing 7-bit-safe SysEx encoding conventions for multi-byte fields.

### Editor behaviour: Full

Retain the existing editor:

- All blocks.
- Amp models.
- NAM model management.
- Cabinet IR management.
- All existing preset controls.
- MIDI settings.
- Firmware diagnostics.

Existing workflows must continue to function.

### Editor behaviour: Synth

Display:

- INPUT
- OCTAVE
- SYNTH
- TONE
- MOD
- DELAY
- REVERB
- Utility/creative effects
- Presets
- MIDI and device settings

Do not show amp, cabinet, NAM model or cabinet IR editing panels.

Do not offer uploads or erase actions for unsupported NAM and IR features.

Do not show disabled AMP/CAB blocks as ordinary editable Synth effects.

The editor must still preserve their underlying stored preset data.

### Catalogue handling

The editor currently queries the device for blocks, effect types and parameter names.

Keep this device-driven catalogue architecture.

For Synth:

- Return a capability map identifying active and unavailable blocks.
- Allow the editor to group canonical blocks into the new logical UI sections.
- Keep the canonical preset representation internally.
- Hide unsupported controls without changing their numeric IDs.
- Reject unsupported writes in firmware, even if an old or modified editor sends them.

Update NAM/IR detection to use actual capabilities, not just protocol version. A Synth firmware may implement the latest editor protocol while intentionally excluding NAM and IR.

### Preset import/export

Exported presets should carry metadata such as:

```json
{
  "firmwareVariant": "synth",
  "presetSchema": 5,
  "sourceFirmwareVersion": 51
}
```

This metadata is illustrative and belongs in the editor's export wrapper, not necessarily the existing 110-byte firmware preset structure.

On import:

- Validate supported types.
- Preserve unsupported canonical data.
- Warn before changing any saved slot.
- Offer read-only inspection of incompatible types.
- Do not silently substitute a different synth or amp algorithm.
- Allow exporting the original preset unchanged.

### Editor user experience

- Clearly show the connected firmware variant.
- Use the correct block names and colours.
- Show Synth/OCTAVE in the preset summary instead of Amp/Cab.
- Display available DSP headroom and load.
- Disable unavailable controls rather than allowing commands that time out.
- Support both firmwares through the same browser URL.
- Keep accessible keyboard and screen-reader behaviour.

Relevant files:

- `docs/editor.html`
- `docs/pxedit.js`
- `docs/edcore.js`
- `docs/edmodels.js`
- `docs/edmidi.js`
- `firmware/px_editor.h`
- `docs/EDITOR-PROTOCOL.md`

---

## 11. Code organisation

Use the existing project structure rather than creating a separate firmware fork.

Suggested responsibilities:

| File or module | Responsibility |
|---|---|
| `firmware/px_build_profile.h` | Feature definitions and validation |
| `engine/px_dsp.h` | Canonical effect IDs and profile-specific runtime state |
| `engine/px_dsp.c` | Shared DSP and conditional feature implementations |
| `engine/px_poly_track.h` | Polyphonic candidate and continuous pitch analysis |
| `engine/px_poly_voice.h` | Voice allocation, envelopes and oscillators |
| `firmware/openpocket.c` | Hardware lifecycle and selected profile |
| `firmware/ui.h` | Profile-specific navigation and button roles |
| `firmware/px_lights.h` | Profile-specific LED mapping |
| `firmware/px_editor.h` | Editor capabilities and command validation |
| `firmware/px_store.h` | Preset compatibility and persistence |
| `firmware/build.sh` | Distinct build configurations and outputs |
| `tools/pxpkg.py` | Variant-aware packages |
| `docs/index.html` | Installer selection |
| `docs/pxedit.js` | Editor capability discovery |

New module names are suggestions. Reuse existing files if that produces a simpler implementation.

### Code preservation policy

Never delete existing effect implementations as part of this project.

If code needs to be moved into separate source files to support conditional compilation, preserve the implementation and its behaviour in Full.

Use compile-time guards for Synth-only exclusions and shared interfaces.

Keep the existing GPL and third-party notices.

---

## 12. Performance and quality requirements

Maintain a measurable baseline for both firmware variants.

### Full regression gate

Before accepting a change:

- Build Full successfully.
- Run existing unit and audio regression tests.
- Compare representative Full presets with the baseline.
- Confirm NAM and IR functions.
- Confirm flash storage and recovery.
- Confirm existing physical controls and LEDs.
- Confirm existing editor and MIDI behaviour.

Any intentional audio difference requires separate justification.

### Synth performance gate

Measure:

- Firmware and memory footprint against Full.
- Peak processor load per effect.
- Peak load of realistic multi-effect combinations.
- Actual onset latency.
- Correct polyphonic note detection.
- False oscillator notes.
- Independent note release.
- Pitch bend continuity.
- Output glitches and missed audio deadlines.
- CPU1 fallback behaviour.

Prioritise the worst measured block and repeatable upper-percentile load, not just average CPU.

Keep adequate processing headroom for UI activity, USB communication and occasional scheduling variation.

### Audio benchmarking

Use the existing host benchmarking infrastructure and current test recordings.

Retain fixed baseline WAVs and measured results for:

- Full-chord octave up.
- Full-chord octave down.
- Lowest Note experiments.
- Mono tracking.
- Polyphonic tracking.
- String Machine.
- Poly Brass.
- Organ.
- Pulse Poly.
- Saw Poly.
- Arcade Poly.

Use controlled recordings for attacks, bends, close intervals, mutes and low notes.

Judge musical quality through both objective tests and gain-matched listening comparisons.

---

## 13. Test matrix

| Test | Full | Synth |
|---|---|---|
| Clean build | Required | Required |
| Existing DSP regression | Required | Required for retained effects |
| NAM inference | Required | Confirm excluded |
| Amp/cab processing | Required | Confirm excluded |
| User IR editing | Required | Confirm unavailable |
| HQ octave processing | Required | Required |
| Existing synth effects | Required | Required |
| New poly oscillator synths | Compatibility handling | Required |
| Gate, drive, EQ, modulation | Required | Required |
| Delay/reverb | Required | Required |
| Preset save/load | Required | Required |
| Full → Synth → Full | Required | Required |
| Editor capability discovery | Required | Required |
| Hardware button mapping | Original | Remapped |
| Installer package validation | Required | Required |
| Rescue/rollback | Required | Required |
| CPU and RAM reports | Required | Required |

### Specific integration tests

Test the complete sequence:

1. Install Full and create saved presets using AMP, NAM, CAB and user IR.
2. Export the presets.
3. Install Synth.
4. Verify the unsupported processing is bypassed, but the underlying preset values remain intact.
5. Save Synth changes to a supported block.
6. Return to Full.
7. Confirm the original hidden AMP/NAM/CAB values survive.
8. Confirm supported changes remain valid.
9. Verify the preset store's CRC and power-cut recovery logic.
10. Repeat with Synth-only effect types, unsupported footswitch assignments, legacy presets and an interrupted update.

Also test each firmware on both hardware models and through the offline installer.

---

## 14. Implementation phases

### Phase 1: Establish independent builds

Deliver:

- Central feature-profile configuration.
- Full and Synth build targets.
- Separate binary and package outputs.
- Baseline linker and DSP-load reports.
- Identical Full behaviour to the existing release.

Do not redesign presets or the UI yet.

### Phase 2: Exclude unused Synth DSP

Conditionally exclude:

- NAM processing and model assets.
- Conventional amplifier DSP.
- Cabinet DSP.
- User cabinet IR DSP.

Retain all other effects specified above.

Verify the actual reduction in memory and executable size.

### Phase 3: New UI and button mapping

Implement:

- AMP → SYNTH.
- CAB → OCTAVE.
- FX → INPUT / TONE / UTILITY.
- Revised PLAY and EDIT pages.
- Profile-specific LEDs.
- Canonical footswitch assignments.
- Safe handling of hidden blocks.

Preserve Full UI behaviour.

### Phase 4: Installer and editor

Implement:

- Two packages per supported hardware model.
- Variant-aware manifest.
- Updated firmware identity validation.
- Two-option installer.
- Safe update/resume/rollback.
- Variant-aware editor capabilities.
- Variant-specific controls and panels.
- Preset compatibility warnings and backup workflow.

Do not publish the dual-variant installer until both variants pass these tests.

### Phase 5: DSP improvements

Use the measured extra headroom to:

- Integrate the improved polyphonic tracker.
- Implement independent oscillator voices.
- Develop continuous pitch refinement.
- Improve weak-fundamental and bass handling.
- Continue the compact octave-up experiments.
- Experiment with Lowest Note octave processing.
- Evaluate further multi-resolution and transient-preserving approaches.

Keep experimental algorithms opt-in until validated.

### Phase 6: Optimise and release

- Profile and optimise the complete Synth signal chain.
- Reassess RAM versus flash placement of hot DSP routines.
- Validate both hardware models.
- Run complete cross-variant preset tests.
- Check installer and editor compatibility.
- Document firmware differences and any supported limitations.
- Publish both packages with verified checksums.

---

## 15. Explicit exclusions and constraints

Do not:

- Delete NAM, amp or cabinet code from the repository.
- Change Full's physical button mapping.
- Remove current synth, pitch, modulation, delay or reverb effects.
- Renumber existing effect or block IDs.
- Shorten the persisted preset structure without a complete migration.
- Reuse user NAM or IR flash areas as scratch storage.
- Change firmware recovery or OTA flash boundaries casually.
- Introduce unsupported editor commands into legacy clients.
- Allow hidden effects to alter the sound in Synth.
- Allow Synth to erase or modify unsupported model slots.
- Assume memory has been freed without checking the linker output.
- Assume removing inactive effects increases normal DSP throughput.
- Replace the proven HQ octave engine before alternatives pass benchmarking.
- Ship the current resonator or Lowest Note prototype as a finished production feature.
- Create two divergent copies of the DSP code.

---

## 16. Definition of done

The project is complete when:

1. `make build` produces the original Full firmware with its existing functionality preserved.
2. `make synth` produces a genuinely stripped, functional Synth firmware.
3. The build reports demonstrate measurable memory and executable-space savings.
4. Both firmware packages are generated for Pocket X and BlackBox.
5. The installer presents a working Full/Synth choice and recognises the installed variant.
6. Both firmwares can be installed, reinstalled and recovered safely.
7. The web editor automatically adapts to the connected variant.
8. All retained effects work correctly in Synth.
9. AMP/NAM/CAB/IR processing is excluded from Synth but fully retained in Full.
10. Presets survive variant switches without silent data loss.
11. Physical buttons, LEDs, MIDI and footswitches behave correctly in both profiles.
12. The Synth firmware has a measured memory and performance budget available for improved polyphonic octave and synth DSP.
13. Existing Full firmware audio and hardware regression tests pass.

## Final architectural requirement

**OpenPocket Full and OpenPocket Synth must be two products built from one maintained source tree, not two independently developed firmwares.**

Full remains a complete multi-effects processor.

Synth becomes a focused guitar synthesis platform with more DSP resources available for polyphonic note detection, oscillator synthesis, octave effects and future algorithms.

All unused code remains available to Full. All shared improvements should flow into both variants wherever relevant.

Implement the separation in the build system and DSP architecture first, then simplify the UI and add installer/editor support, and only then spend the recovered resources on new algorithms.

### One implementation decision I would enforce

Do not shrink the delay or reverb buffers in the first Synth build. Removing NAM, amp modelling and cabinet processing already provides a substantial opportunity to recover resources, while keeping the time-based effects that are particularly useful for synth sounds.

Get an exact before-and-after memory report first. That will tell you whether further memory reductions are actually necessary.

The most sensitive integration work is likely to be the installer identity handling and persistent preset compatibility, not the physical button reassignment. Both are manageable, but they need to be implemented before the two firmware variants are released together.
