# OpenPocket octave and synth roadmap

## 1. Finish Lowest Note octave-down

Use the installed v52 Lowest Octave HQ as the pedal baseline and v53 as the better
hold experiment. In the recorded bass mixes, v53 raised equal-level held-note
accuracy from 33.7% to 52.8%. Its longest mute tail grew from 51.0 to 71.3 ms,
and tracking after a soft repick under a quieter upper note got worse.

1. Keep v53's evidence from harmonics that the upper string cannot explain.
   Add a faster check for the low string going quiet while the higher string
   rings. Confirm new and repicked low notes separately from the hold rule.
2. Compare against v52 and v53 on the same DI. Include isolated attacks, both
   notes starting together, an equal-level upper note entering, a quiet lower
   repick, hard mutes and bends. Cover standard and down-tuned guitar, bass E1
   and B0. Track wrong-note duration and level, onset, settled accuracy and
   release. Score the audible sub separately from the candidate reports.
3. Choose the candidate that improves equal-level hold over v52 while
   recovering its cleaner mutes and soft repicks. If a change only improves
   the held percentage by leaving the sub on longer, keep testing release.
4. Build both AC791N packages and replay the same DI through the pedal. Compare
   lowest-only against fixed Range with matched wet levels. Measure peak
   callback load, late blocks, bypass, core status and actual audible onset.
   Use the effect alone and with Smart Gate, then repeat with octave-up on.
   Report RAM by linker region, application size and package checksums.

Keep the fixed Range control available for cutoff-based playing. Lowest Note
controls octave-down only. The dry chord and full-chord octave-up remain useful
independently. Advance to Poly Oct Fuzz after the Lowest Note build and report.
[v52 and v53 results](BASS-DI-BENCH.md#blackbox-v52-smoke-test)

## 2. Fix Poly Oct Fuzz

Use this signal chain, with all three voices mixed before the drive stage:

```text
Guitar + Poly Octave DOWN + Poly Octave UP → shared Drive Fuzz Face → LEVEL
```

- **DOWN** sets the polyphonic octave-down contribution. **UP** sets the
  polyphonic octave-up contribution. **FUZZ** uses the standalone Fuzz Face's
  drive response. **LEVEL** controls output volume after the fuzz.
- Use the existing Poly Octave audio path and the existing Drive Fuzz Face
  implementation. Share its full signal processing, including input voicing,
  gain, clipping, oversampling, DC handling and output voicing. Give the two
  effect instances independent state so using or bypassing one cannot disturb
  the other. Keep their configuration and processing in common code.
- Add **TONE** as the fifth control if it fits the measured DSP budget. Give
  it a neutral position that preserves the shared Fuzz Face sound. Keep the
  stored indices for SUB/DOWN, OCT UP/UP, DRIVE/FUZZ and LEVEL. If TONE is
  omitted, leave its saved slot reserved and expose LEVEL without shifting
  existing preset values. Verify the pedal and editor on saved presets.
- Start with the proven octave paths. If the combined load needs reducing,
  compare a lighter octave analysis against the reference on the same DI.
  Keep the shared Fuzz Face processing identical. The fuzz effect should
  work on full chords without relying on Lowest Note or the oscillator tracker.

With UP and DOWN at zero and optional TONE neutral, compare the output with
standalone Drive Fuzz Face at matched gain and level. Run the same comparison
at several FUZZ values. With octaves enabled, compare against the explicit
Poly Octave → Fuzz Face chain on single notes, chords, palm mutes and fast
repicks. Verify octave switching, control endpoints, combined peak DSP and
bypass/preset changes on the pedal. Use the standalone sound as the reference
for whether there is enough fuzz.
[Current effect and controls](SOUNDS-AND-CONTROLS.md)

## 3. Build a lean Synth profile and prove its value

Create an internal AC791N build profile from the same source tree. Compile out
AMP, NAM, CAB and user IR DSP, model assets and private runtime state in Synth.
Keep the octave and synth engines, time-based effects, Smart Gate and other
retained effects. Keep Full buildable as the reference. Preserve stored block
IDs, type IDs, preset bytes and user assets. Skip unsupported blocks in the
effective DSP preset while preserving their original stored settings.

Build Full and Synth from the same source and compare flash, POOL, RAM, LOWRAM,
RAM-code, cache RAM and peak load. Report where every saving lands. Put the
recovered budget into a candidate with a clear musical benefit: better close
dyads, more reliable low E/B tracking, faster independent release, or two
independent oscillator voices. Compare the same audio and settings with the
Full baseline. The lean profile earns its place when it enables an improvement
that fits and runs comfortably on the pedal.

Keep existing build outputs and recovery paths distinct. Defer button remaps,
variant-aware editor work and the public installer choice until that DSP
comparison shows a benefit.
[Full and Synth architecture spec](spacesaver.md#5-memory-optimisation)

## 4. Improve tracking across the existing effects and build Pulse Poly

1. Use the same down-tuned guitar and bass DI across the effects. Validate
   simultaneous note candidates before feeding them to independent voices.
   Prioritise close intervals, false octaves, weak fundamentals, repicks and
   individual string mutes. Keep chord audio shifting available separately
   from tracked oscillator synthesis.
2. Apply improvements to MONO SYNTH, BASS SYNTH HQ, PWM SYNTH HQ and SQUARE
   SYNTH HQ where each effect benefits. Compare standard and HQ attack feel,
   tracking accuracy and DSP load. Measure octave-up flutter separately
   against current HQ on the same DI, and preserve the +12 and detuning
   behaviour of existing sounds such as 12-STRING.
3. Use the existing PolyBLEP pulse/saw and ladder filter for Pulse Poly.
   Start with two voices. Give each voice continuous pitch, independent
   attack and release, its own oscillator phase, and repick evidence.
   Bench the voice engine on known note events, then test real playing
   for false voices, bends, onset, release and peak pedal load.
4. Build Saw Poly, Arcade Poly and FM Poly in that order using the same
   two-voice engine. Audition each voice on known note events and recorded
   guitar. Measure FM CPU cost and aliasing on its brightest settings.
   Continue improving the common tracker before releasing the new sounds.
5. Keep the existing String Machine, Poly Brass and Organ HQ controls.
   Develop monophonic orchestral ensembles as a separate track of work.

[Poly synth brief and acceptance tests](synth.md#7-testing-and-acceptance)

## 5. Release Full and Synth when both are ready

Publish compatible packages for BlackBox and Pocket X after the DSP and
compatibility checks pass. Add the Synth-focused controls and editor catalogue,
variant selection in the installer, and package identity and resume validation.
Test Full → Synth → Full with saved presets and user assets, including
unsupported effect types and rollback. Provide the final report for both
builds with the installer paths, shared and variant-specific features, measured
tracking and audible latency, peak DSP load, memory use and known limits.
[Installer and compatibility spec](spacesaver.md#9-installer-two-firmware-options)
