# Tracking architecture review

## What OpenPocket actually runs

| Musical job | v66/v67 routing | Direction |
| --- | --- | --- |
| Shift or colour everything played | POLY OCTAVE HQ and POLY OCT FUZZ call the complex ERB octave bank. HQ ORGAN, CHURCH ORGAN, STRINGS and CHOIR also use that bank for octave voices. The original organ and ensemble versions use delay-line shifting and related audio processing. | Keep the working waveform octave and fuzz. Improve pitch-shift artifacts and voicing in that path if an A/B identifies a problem. |
| Follow one intended played pitch | MONO SYNTH in poly-test uses `pxt_run` and `pxt_layer_run`. SQUARE HQ, BASS HQ and the PWM HQ implementation use the same lead-note design. SQUARE HQ has separate state because SFX and SYN can run together. PWM HQ presets currently resolve to the original comparator in poly-test. Acoustic leads use the one-note layer too. | Keep one lead-note policy with separate state per active block. Run pure synths as one voice and score how well it keeps the intended pitch when the input contains another string or overtone. Preserve each original's voicing until its replacement passes listening. |
| Render two independently articulated pitches | PULSE, SAW, ARCADE and FM POLY and STRING DUO call `pulse_poly_block` and `pxp_input`. They share one resonator picker and two-slot allocator. STRING ENSEMBLE and BRASS ENSEMBLE instead run through `pxa_run`. Their supporting voice follows the same tracked lead. | Reuse the two-slot allocator for acoustic ensembles that need independent parts. Compare with the current one-pitch layered sound. Keep the existing explicitly Poly synth variants on the shared allocator. |

The bank can shift chord audio without identifying individual notes. A lead
tracker can examine several candidate peaks and emit one pitch. Two timbres
or octave voices can also follow a single detected pitch. Those distinctions
explain why three musical jobs need only two note policies.
[Waveform and effect dispatch](../engine/px_dsp.c) ·
[Lead note layer](../engine/px_track.h) · [Acoustic renderers](../engine/px_acoustic.h) ·
[Two-slot processing](../engine/px_pulse_poly.h) · [Allocator](../engine/px_poly_track.h)

## What the references support

**Polyphonic octave and octave fuzz.** A fixed octave can be generated from the
mixed guitar signal directly. Thuillier's guitar experiments compared octave-up
methods with three contemporary pedals and favored ERB-PS2, which doubles the
phase of each analysis sub-band and sums the results. It has no note allocation
step. The public Terrarium implementation also provides octave-down voices.
That demonstrates a practical route for octave effects. The thesis's result
specifically supports octave up, and the down voices need their own artifact and
latency tests. Evaluate these effects on chords, low notes, close intervals and
repicks using wet-output measurements and listening.
[Thuillier, 2016](https://aaltodoc.aalto.fi/bitstreams/ff9e52cf-fd79-45eb-b695-93038244ec0e/download) ·
[Terrarium implementation](https://github.com/schult/terrarium-poly-octave/blob/main/main.cpp#L27-L60) ·
[OpenPocket octave research and bench results](POLYPHONIC-PITCH-RESEARCH.md)

**Pure synths.** A one-note oscillator needs a continuous pitch estimate and a
reliable decision about when the current note starts, changes or ends. YIN
provides a published periodicity estimator. The Cycfi Q reference shows a
practical causal design with periodicity thresholds, a bias against harmonic
jumps once a pitch has settled, and a short median filter. Its guitar testing
includes individual strings, so it offers a design precedent for stability
without proving immunity to two strings on a mixed pickup. The useful behavior
for OpenPocket is one voice that follows bends and legato, holds its pitch during
briefly ambiguous input and releases when the evidence fades. Compare a
periodicity detector as a host reference before replacing existing code.
[de Cheveigné and Kawahara, 2002](https://pubmed.ncbi.nlm.nih.gov/12002874/) ·
[Cycfi Q pitch detector source](https://github.com/cycfi/q/blob/master/q_lib/include/q/pitch/pitch_detector.hpp) ·
[OpenPocket mono comparisons](NOTE-TRACKING-RESEARCH.md#cycfi-q-on-isolated-bass-notes)

**Ensembles with independently played parts.** Rendering a different instrument
or envelope for each detected note requires multiple pitch estimates and
per-voice state. Weighted harmonic salience, cancellation of already explained
partials and bounded polyphony are established approaches. Schramm et al. trained
spectral templates on the specific guitar and used a short causal median plus
separate thresholds for activation and release. Guitarists still heard octave
errors and spurious harmonics on low notes. Their Raspberry Pi implementation
and instrument-specific training limit its use as a pedal firmware reference.
Use a single multivoice allocation policy where independently articulated
parts are an intentional feature. Two different timbres following the same
played note can share that pitch and use separate render envelopes.
[Klapuri, 2006](https://archives.ismir.net/ismir2006/paper/000125.pdf) ·
[Schramm et al., 2018, §§2.1.2–2.1.4 and 3.3](https://nime.org/proceedings/2018/nime2018_paper0027.pdf)

The Source Audio C4 manual documents oscillator and pitch-shifter sources with
independent voice and envelope controls. BOSS's OC-5 manual distinguishes
single-note Vintage from chord-capable Poly behavior and offers a lowest-note
option. These are useful product precedents for separate musical behaviors.
Their manuals describe controls and routing, while the proprietary detection
algorithms and their thresholds are outside the published evidence.
[Source Audio C4 manual, Voice Controls and Pitch Detector](https://sourceaudio.net/uploads/1/1/5/1/115104065/sa249_c4_synth_user_guide.pdf#page=23) ·
[BOSS OC-5 manual](https://static.roland.com/assets/media/pdf/OC-5_eng03_W.pdf)

## Which guards have precedent

- **Established ideas.** Use evidence from multiple harmonics or periodicity,
  reject weak candidates, hold a plausible pitch through short gaps, and use
  hysteresis or brief confirmation for starts and stops. Relative onset or
  modulation can provide evidence when two pitches share partials. Zhou et
  al. showed this for a piano and violin octave and presented it as future
  work for their estimator. It gives a testable hypothesis for guitar.
  [Klapuri, 2006](https://archives.ismir.net/ismir2006/paper/000125.pdf) ·
  [Schramm et al., §§2.1.3–2.1.4](https://nime.org/proceedings/2018/nime2018_paper0027.pdf) ·
  [Zhou et al., 2009, §5 and Figure 8](https://www.research-collection.ethz.ch/bitstreams/32aa5768-22e5-47f4-9d8a-60343651d0d3/download)
- **OpenPocket's tuning.** The fixed even/odd thresholds, short eviction
  window, higher floor for a second voice, low-register exceptions, bin-based
  fallback and faster release are engineering trials. The cited work gives
  a rationale for testing harmonic rejection and temporal stability. The
  exact stack and its interactions need evidence from OpenPocket. A release
  or confirmation rule that clears the scale but loses real notes elsewhere
  can make the playing response worse.
  [Allocator implementation](../engine/px_poly_track.h) ·
  [Phase 2b measurements](phase2b.md#stage-1-progress)

These three musical behaviors can share DSP primitives such as filter banks,
envelope followers or pitch candidates where that saves measured cost. Keep
the octave signal path free of note-allocation dependencies. Give the mono path
one continuity policy and the independent ensemble parts one bounded allocator.
Compare the smallest implementation of each behavior to v66 and the v67 test
build before carrying the whole set of experimental guards forward.

## Where the implementation drifted

The measurable drift is in the two-slot allocator. v67 layers candidate
rescue, a stronger second-voice floor, a late-root exception, timed harmonic
eviction, a fast eviction release, quiet low-note slew limiting, a nominal
octave check for settled low notes, and a different starting pitch for a new
low voice. Each has a plausible rationale. Their interaction now matters
more than any one threshold, and the rules are hard to validate from a
small set of recordings. A rule that moves a report across a scoring
threshold provides weaker evidence than one that prevents an audible wrong note.
The lead-note change in this build extends the existing waveform period
check's ceiling for ambiguous octave choices. It did not improve the
build's 91.5% mono note accuracy and moved octave errors from 120 to 121
on the 6,187-note holdout. Keep the v66 lead layer as the reference.
The brass correction changes
the renderer's balance. The waveform octave and octave fuzz have no
dependency on these note guards.

Treat v67 as a comparison. For the mono path, start with the existing
confidence, pick and continuity policy and judge it by unwanted switches,
bend and legato continuity, and clean handoff after a mute. For two
independently voiced parts, compare a simpler candidate picker and
allocator with one harmonic-evidence decision and one entry/release policy
against v66 and v67. Run controlled feature removals before increasing the
number of rules. Check genuine octave pairs and independent mutes on the
same recordings as ghosts, use an independent slice, and measure audible
output latency on the pedal. The Arcade register split stays behind the
core work.
[Stage 1 and Stage 5 decisions](phase2b.md)

## What the first feature-removal runs show

These runs remove one rule at a time from v67, using both owner DIs,
GuitarSet takes 24–47, and the synthetic octave matrix.

| Removed | Observed effect | Review |
| --- | --- | --- |
| Even/odd octave evidence, including its candidate rescue | Extra comping voices fell from 28.8% to 28.4%, but the medium-level simultaneous and +20 ms synthetic octaves no longer entered. | Keep an independent-octave cue in the comparison. Ghost count alone rewards dropping true notes. |
| Quiet low-note slew limit | The second low-E sustain's neighbour slips rose from 0.35% to 3.05%. Held-out entry counts and latency stayed the same. | There is a concrete low-E benefit. Test whether the lead/voice pitch continuity rule can cover it without a special tail path. |
| Nominal octave check | Extra comping voices rose from 28.8% to 29.2%, with the same 129 entries and latency. | Useful on this slice. Evaluate it together with harmonic evidence as one decision, especially on other players and through bends. |
| Higher floor for upper voices | Extra comping voices rose to 29.2%, one additional comping entry hit, and second-note p90 rose from 62.4 to 67.8 ms. | The floor trades false notes against weak note entries. The evidence does not yet support carrying both the higher floor and its late-root exception into production. |

All four alternatives retained 102/270 sustained entries on this slice. The
audible quality A/B and independent recordings still determine whether
the smaller policy earns its place.
