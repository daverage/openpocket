Worked for 1m 56s

Worked for 1m 32s

The synth bank should use the oscillator and filter code already in OpenPocket, with the new resonator tracker providing independent note pitches. The key is to distinguish true polyphonic oscillator synths from chord-friendly effects that transform the guitar audio directly.

Your latest Lowest Note experiment also changes one design decision: we should not use the unreliable shared-band masking or synthesized-sub selection as the basis for these synths. The oscillator voices need their own pitch-confidence and note-allocation system.

Here is a specification intended to be passed directly to your coding agent.

# OpenPocket: Retro Poly Synth Toys

## Objective

Add a collection of fun, vintage-inspired synthesiser effects to OpenPocket, using the existing DSP engine wherever possible.

The sounds should evoke analogue polysynths, string machines, vintage organs, early digital keyboards and 8-bit game sound chips.

**Primary requirements:**

- Work from a normal mono guitar input without a special pickup.
- Support chords and independent simultaneous notes wherever genuine oscillator synthesis is used.
- Prioritise low latency, musical tracking and convincing sounds over recreating specific hardware exactly.
- Reuse OpenPocket's existing oscillators, filters, envelopes, modulation, octave processing and synth infrastructure.
- Avoid unnecessary external frameworks, sample banks or duplicate DSP implementations.
- Preserve the existing v50/v51 firmware behaviour and preset compatibility.
- Keep new effects experimental until they pass host and AC791N testing.

## 1\. Existing code to reuse

Repository: https://github.com/daverage/openpocket

| Existing component | Source | Required use |
| --- | --- | --- |
| PolyBLEP saw and pulse generation | `engine/px_dsp.c`: `polyblep()`, `syn_saw()`, `synth_hq_pulse()` | Core oscillator DSP |
| Resonant four-stage ladder filter | `engine/px_dsp.c`: `syn_ladder()` | Shared analogue filter |
| Synth envelope and gating | `engine/px_dsp.c`: `syn_keys()`, existing synth envelope logic | Reuse principles and coefficients |
| LFOs, chorus and ensemble | `syn_lfo`, `syn_tap`, existing STRINGS processing | PWM, detuning, vintage ensemble |
| HQ polyphonic octave engine | `erb_octaves()`, `erb_octaves2()` | Chord-derived effects, not oscillator note identification |
| Existing tracker and note layer | `engine/px_track.h` and current integrated note-layer implementation | Legacy fallback and tracking reference |
| Resonator host prototype | `docs/research/POLY-TRACKER-RESONATOR-BENCH.md` and associated host tools | Candidate source for the new poly tracker |
| Existing synth configuration | `engine/px_dsp.h`, `px_engine`, `PX_SYN_*` | Effect integration and preset compatibility |

Avoid adding a separate synthesiser library. Use float32 DSP and the existing 44.1 kHz engine.

## 2\. New synth effects

Build six vintage-inspired effects. The first three should be derived from the existing guitar-driven effects and remain playable on chords without multi-note pitch detection. The last three require genuine per-note polyphonic tracking.

### A. String Machine

**Sound:** Vintage Solina-style string synthesiser, warm and swirling.

**Implementation:**

- Reuse the existing `PX_SYN_STRING_MACHINE` and STRINGS HQ processing.
- Retain the shaped guitar signal as the centre voice.
- Add the existing HQ octave-up voice where required.
- Reuse the three-tap ensemble and its dual-LFO modulation.
- Apply swell and mellow tone shaping.
- Preserve chord response independent of note-tracker confidence.

**Controls:** OCTAVE, ENSEMBLE, TONE, SWELL, MIX.

**Priority:** Refine and reuse the existing implementation rather than create another effect type.

### B. Poly Brass

**Sound:** Vintage analogue brass stabs and sweeping polysynth horns.

**Implementation:**

- Reuse the existing `PX_SYN_BRASS` audio-derived path.
- Keep the processed guitar signal as the excitation so chords remain intact.
- Use the existing body and bite filters.
- Introduce a short opening filter envelope for brass attacks.
- Control drive carefully to avoid excessive intermodulation between chord tones.
- Do not require note detection for the initial version.

**Controls:** BITE, FILTER, ATTACK, SWELL, MIX.

**Priority:** Revoice the existing brass effect where possible, retaining old preset behaviour.

### C. Drawbar Organ

**Sound:** Vintage transistor and tonewheel-inspired organ.

**Implementation:**

- Reuse ORGAN HQ's existing guitar-driven drawbar voices.
- 16′: octave below.
- 8′: fundamental.
- 4′: octave above.
- Reuse the existing key-click generator and rotary effect where practical.
- Keep sufficient 8′ signal to preserve chord definition.
- Use the existing HQ octave engine, not new per-note oscillators.

**Controls:** 16′, 8′, 4′, CLICK, DRIVE.

**Priority:** Create a suitable factory voicing using ORGAN HQ. Add a separate type only if essential.

### D. Pulse Poly

**Sound:** Classic 1970s/1980s square-wave polysynth.

**Implementation:**

- Genuine polyphonic oscillators controlled by individually tracked notes.
- One PolyBLEP pulse oscillator per active note.
- Continuous pulse-width modulation using the existing LFO.
- Independent oscillator phase and amplitude envelope for every note.
- Shared resonant ladder filter after voice summation.
- Smooth continuous pitch following for bends and vibrato.
- Avoid retriggering all notes when a new string is picked.

**Controls:** WIDTH, PWM, CUTOFF, RESONANCE, RELEASE.

**First implementation target:** Two reliable independent voices. Expand only when tracking accuracy and CPU permit.

### E. Saw Poly

**Sound:** Classic detuned analogue polysynth, from bright synth stabs to thick vintage pads.

**Implementation:**

- Reuse `syn_saw()` and the existing PolyBLEP correction.
- One or two saw oscillators per note.
- Small controllable detuning between oscillator pairs.
- Independent voice envelopes, with a shared resonant low-pass filter.
- Filter-envelope sweep triggered by confirmed note events.
- Limit the second oscillator or voice count when required by the DSP budget.

**Controls:** DETUNE, OCTAVE, CUTOFF, SWEEP, RELEASE.

**Special consideration:** Detuning should be oscillator detuning, not artificial instability in the detected guitar pitch.

### F. Arcade Poly

**Sound:** NES-style 8-bit tones, early game music and primitive digital synthesis.

**Implementation:**

- Reuse the pulse oscillator for square and narrow-pulse waveforms.
- Add a lightweight triangle waveform derived from a band-limited oscillator.
- Add a simple deterministic pseudorandom noise source where useful.
- Optional semitone pitch quantisation for deliberately stepped retro tones.
- Independent per-note decay envelopes for chiptune plucks.
- Optional octave doubling using oscillator frequency ratios.

**Controls:** WAVE, OCTAVE, CRUSH, DECAY, MIX.

**Important:** Pitch quantisation is a creative option, not a limitation inherited from the resonator tracker. Other modes must follow bends continuously.

### G. FM Poly

**Sound:** Early digital FM in the spirit of the Yamaha DX7. Use a two-operator
sine voice for electric pianos, bells and metallic textures.

**Implementation:**

- Give each note its own carrier and modulator phase.
- Use an independent decaying modulation envelope over the shared voice envelope.
- Offer harmonic and fractional ratios and cap modulation when the sidebands
  approach Nyquist. Measure aliasing and callback load on bright high notes.

**Controls:** RATIO, INDEX, DECAY, RELEASE, LEVEL.

## 3\. Shared polyphonic tracking engine

The four oscillator synths must use a **single reusable polyphonic note engine**, not separate pitch detectors.

### Candidate source

Start from the quarter-rate resonator approach used in OpenPocket's host benchmark.

Baseline configuration:

- 11.025 kHz analysis rate.
- 128 input-sample reporting interval, approximately 2.9 ms.
- Three-report confirmation for secondary candidates.
- Fixed-size resonator state and candidate arrays.
- Explicit support for the guitar register, with a separate validated extension for bass low E and low B.

The reported host holdout improved simultaneous two-note detection from 20.1% to 42.1%, while reducing single-note extra candidates from 39.9% to 37.4%.

**This is an experimental starting point, not adequate final polyphonic tracking.**

Do not directly connect every resonator detection to an oscillator.

### Required processing pipeline

``` text
Guitar audio
    |
    v
Existing input conditioning / decimation
    |
    v
Quarter-rate resonator candidate analysis
    |
    v
Fundamental and harmonic verification
    |
    v
Per-candidate confidence and frequency refinement
    |
    v
Persistent polyphonic voice allocator
    |
    +--> Voice 1: pitch / amplitude / envelope / oscillator
    +--> Voice 2: pitch / amplitude / envelope / oscillator
    +--> Voice 3: optional
    +--> Voice 4: optional
    |
    v
Voice summation
    |
    v
Existing ladder filter / modulation
    |
    v
Synth output
```

### Per-note data

Each active oscillator voice requires:

``` c
typedef struct {
    float frequency_hz;
    float target_hz;
    float confidence;
    float amplitude;
    float phase[2];
    float envelope;
    float pitch_velocity;
    unsigned age;
    unsigned missing_frames;
    uint8_t active;
    uint8_t gate;
    uint8_t onset;
} px_poly_voice;
```

This is a suggested logical representation, not a requirement to copy the structure verbatim.

### Pitch accuracy and bends

The upstream resonator reports semitone-centred candidates. Those are suitable as note identities but are insufficient for expressive synth tracking.

Implement continuous frequency refinement for each accepted note using candidate-centred complex phase tracking or a small group of neighbouring resonators.

Requirements:

- Follow natural guitar vibrato and bends.
- Avoid semitone-stepped pitch in analogue modes.
- Preserve pitch continuity during short candidate dropouts.
- Distinguish a genuine new note from a bent existing note.
- Prevent octave jumps caused by missing or weak fundamentals.

Reuse the existing tracker’s pitch refinement and onset logic where it is applicable, but avoid applying a monophonic waveform-period estimate to an entire chord.

### Voice allocation

Start with two voices, then assess three or four. Support six only if tracking quality and hardware headroom justify it.

The allocator must:

- Assign each candidate to at most one oscillator voice.
- Match existing voices before allocating new ones.
- Maintain oscillator phase across brief pitch-tracker dropouts.
- Use pitch continuity, confidence, local harmonic evidence and amplitude to associate notes.
- Trigger a new envelope only for a confirmed new or repicked note.
- Release individual voices when their note disappears.
- Reject likely harmonic aliases and unsupported octave substitutions.
- Fade uncertain voices rather than generating confident-sounding false notes.

**Prefer a missing quiet note over an invented loud note.**

Use separate attack and release evidence where possible. Resonator decay alone must not determine note-off timing.

## 4\. Lessons from the Lowest Note octave experiment

The new host experiment establishes several useful constraints:

- The shared-band harmonic mask could not reliably isolate a lower note and lost substantial wanted output even when given the correct pitches.
- A narrowly tuned complex filter improved isolation but took approximately 73 ms for guitar E2 with tracking, and longer for bass.
- Synthesised sub-octave output was much cleaner when the note identity was correct, but weak fundamentals caused octave-selection errors.
- Real bass E1 and B0 remain particularly difficult.

Therefore:

**Do not reuse the shared-bank Lowest mask as a synth voice separator.**

The oscillator synths should generate their own waveform once a credible pitch has been identified. Their challenge is reliable note identity, continuous frequency, envelope timing and release, not extracting a clean isolated guitar waveform.

Octave changes within synth voices should normally be implemented by oscillator frequency multiplication or division, rather than running the entire synthesis through the HQ octave bank.

Preserve the existing fixed-Range octave effect as the playable fallback.

## 5\. DSP architecture and performance

Implement a shared oscillator-voice layer rather than duplicating three complete synth engines.

Suggested modules:

- `engine/px_poly_track.h`: resonator candidates, confidence and continuous pitch refinement.
- `engine/px_poly_voice.h`: voice assignment, envelopes, phase and retrigger logic.
- Existing `engine/px_dsp.c`: shared oscillator helpers, ladder filtering and effect dispatch.
- Existing `engine/px_dsp.h`: appended synth types and engine state.

Module names are suggestions; follow the repository's existing organisation and compilation model.

### CPU and memory rules

- Keep resonator updates at quarter rate.
- Compute oscillator waveforms at full audio rate using the existing PolyBLEP functions.
- Share LFOs and filter stages where musically appropriate.
- Use one shared four-stage ladder initially.
- Avoid heap allocations in the audio path.
- Avoid large per-voice delay buffers or FFTs.
- Do not instantiate a complete tracker for every oscillator.
- Skip inaudible oscillator and modulation work where safe.
- Maintain bounded, deterministic work per 128-frame hardware processing interval.

If simultaneous synth and other effects exceed the target budget, reduce optional oscillator layering or voice count rather than silently degrading note-tracking accuracy.

## 6\. User interface and compatibility

OpenPocket has five parameters per effect, stored as values from 0 to 100.

- Preserve that preset format.
- Append new `PX_SYN_*` identifiers; do not reorder existing enum values.
- Preserve the current String Machine, Brass and Organ patches unless a deliberate compatibility migration is implemented.
- Integrate new names, parameter labels, value formatting and defaults into the existing firmware and web editor.
- Provide sensible musical parameter curves rather than mapping every control linearly.
- Ensure preset switches, note releases and effect bypasses do not click.
- Include at least one ready-to-play factory preset for each new synth type.

Use descriptive, generic names rather than third-party product branding.

## 7\. Testing and acceptance

### Stage 1: Host oscillator prototypes

Build and audition Pulse Poly, Saw Poly, Arcade Poly and FM Poly with ideal MIDI-style note events or annotated pitch tracks.

This isolates sound quality and oscillator cost from detector errors.

Confirm correct:

- PolyBLEP anti-aliasing.
- Envelope timing.
- Independent note release.
- Pitch bends and vibrato.
- Filter behaviour on chords.
- Stable output level with multiple voices.

### Stage 2: Real polyphonic tracking

Connect the resonator candidate engine and implement the continuous pitch and allocation layers.

Benchmark:

- Single notes across the guitar.
- Major/minor thirds and fifths.
- Close semitones and whole tones.
- Octave pairs.
- Unequal pick levels.
- Vibrato and two-semitone bends.
- Individual string repicks and mutes.
- Palm muting and rapidly changing chords.
- Bass low E and B using real DI recordings.

Record complete-note precision and recall, both-notes-correct rate, false voices, bend error, acquisition-time distributions and release delay.

A measurable advance over the 42.1% raw dyad baseline is necessary, but oscillator behaviour must be judged using **confirmed, persistent voices**, not just raw frame accuracy.

### Stage 3: Hardware integration

Build an opt-in AC791N firmware variant.

Measure:

- Worst-case 128-frame block processing time.
- Peak DSP with Smart Gate and representative amp/effects chains.
- Actual input-to-audible-synth onset.
- Actual additional RAM and RAM-code usage.
- Clicks, aliasing, false notes and stuck voices.
- Behaviour with two, three and four oscillator voices.

Never infer pedal performance from desktop timing alone.

### Stage 4: Release gating

Do not enable the true poly oscillator synths as normal production effects until tracking produces acceptable musical results on real recordings and pedal tests.

If tracking remains unreliable, retain the three chord-friendly effects as usable synth toys and keep the oscillator synths experimental.

Do not modify the existing v50/v51 sounds merely to accommodate unfinished polyphonic tracking.

## 8\. Implementation order

1. Refine and preset the existing String Machine, Poly Brass and Drawbar Organ sounds.
2. Build a reusable two-voice PolyBLEP oscillator and envelope layer.
3. Implement Pulse Poly with synthetic note events.
4. Integrate the resonator tracker and persistent two-note allocator.
5. Add continuous pitch refinement and individual repick/release detection.
6. Validate tracking and latency on the pedal.
7. Add Saw Poly, Arcade Poly and FM Poly in that order using the shared voice engine. Continue monophonic orchestral ensembles separately.
8. Consider three- and four-voice polyphony after measured CPU and accuracy improvements.

## Final design principle

These should feel like inexpensive, enjoyable vintage synthesisers rather than complicated modern synth workstations.

The tracking engine should be sophisticated internally, but the user experience should remain simple: **choose a synth, turn five controls, play guitar chords and hear a stable, expressive synthesiser.**

Use what OpenPocket already has. Spend new DSP effort on the genuinely missing capabilities: reliable simultaneous pitch identities, continuous pitch tracking and independent per-note envelopes.

The main technical risk is still the tracker, not oscillator synthesis. OpenPocket already contains nearly everything required for the sounds themselves.

The relevant existing work is in [POLY-SYNTH-DESIGN.md](../research/POLY-SYNTH-DESIGN.md), [px_dsp.c](https://github.com/daverage/openpocket/blob/main/engine/px_dsp.c?utm_source=chatgpt.com), and the [resonator benchmark](../research/POLY-TRACKER-RESONATOR-BENCH.md).

I'd implement Pulse Poly first because it exercises all the difficult requirements, independent pitch, phase, note-on, note-off and voice allocation, with the fewest additional sound-design variables. Once that works, Saw Poly and Arcade Poly are comparatively straightforward.
