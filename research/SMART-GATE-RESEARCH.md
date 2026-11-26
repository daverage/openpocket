# Smart Gate research

The separate SMART GATE can use a filtered detector and broadband gain, with a learned idle floor. This gives the player direct control over how far the gate attenuates and how quickly it closes. It also lets a note tail stay open below the level needed to start a new note.

## Sources and design choices

| Reference | What it establishes | Proposed use |
| --- | --- | --- |
| [BOSS NS-1X](https://www.boss.info/fi/products/ns-1x/) | The pedal exposes threshold, decay and damping and offers both a gentler reduction mode and a fast gate. It detects at the input and can apply gain after a noisy effects loop. BOSS describes inputs to its analysis, while its implementation is proprietary. | Provide adjustable depth and release. Test natural tails and rhythm stops separately. For v1, detect and apply gain in the GT block. Detecting before and applying after an amp would require separate routing. |
| [Steinberg Cubase Gate manual](https://www.steinberg.help/r/cubase-pro/cubaseplugref/16.0/en/_shared/topics/plug_ref/gate_r.html?contentId=qK1uwHNI4LC2tTK2JaJ6RQ) | A gate can use frequency-filtered sidechain detection, peak or RMS detection, attack, hold, release and an attenuation range. Live mode trades lookahead for immediate processing. | Keep the audio path delay-free and use the filtered signal only for detection. Use separate open and close levels, count hold time in samples, and ramp gain on transitions. |
| [Cohen and Berdugo, minima-controlled recursive averaging](https://webee.technion.ac.il/Sites/People/IsraelCohen/Publications/SPL_Jan2002.pdf) and [WebRTC noise estimator source](https://webrtc.googlesource.com/src/+/d153de6d33a79d450c626b9b8e9613df33947282/modules/audio_processing/ns/noise_estimator.cc) | Noise estimation can combine a low statistical reference with an update that slows when the wanted signal is present. These examples work in spectral bins on speech. | Use two broad sidechain envelopes and a rolling low reference or quiet-state average. The choice of bands and all timing values need guitar-specific validation. |

## Prototype

1. Use sidechain energy in two overlapping roles. Let a low-mid band carry fundamentals and harmonics and an upper band carry pick onsets. Compare each with its own noise floor and impose an absolute input sensitivity floor. Attenuate mains fundamentals in the detector, then check that a quiet low E and softly played bass still open it.
2. Open quickly on excess energy or a transient above the floor. Keep the gate open on weaker sustained energy using hysteresis. Apply a finite, user-adjustable amount of attenuation after hold and release. Shorten attack until repeated muted notes retain their first few milliseconds without clicks. Start with time constants below 1 ms for attack, 20–40 ms for hold and an adjustable release.
3. Estimate the floor from quiet observations and freeze updates while a note is present. Bound upward shifts and decay faster when the floor falls. Include a startup seed that playing immediately cannot train upward. An abrupt noise step can otherwise hold the gate open indefinitely when learning depends solely on the closed state. An explicit quiet calibration, or bounded relearning during a sufficiently stationary interval, gives it a recovery path. Test the latter on sustained notes before relying on it.
4. Use fixed-size state and calculate timing coefficients during parameter changes. Update the floor and slow decisions at a fixed sample cadence independent of callback size. Measure peak callback DSP on the pedal against the roughly 10% target, including with a drive or amp enabled.

## Checks

Use quiet and loud pickups with stationary and stepped 50/60 Hz hum plus harmonics, hiss, low notes, soft legato, ringing decay and repeated palm mutes. Compare the legacy gate and smart gate at comparable idle attenuation. Record false opens in rests, missed onsets, peak loss in the first 5 ms, closure chatter, tail loss and recovery after noise changes. Check startup both idle and already playing, bypass behavior, and output agreement between the engine's scalar and block paths.

A mono gate can attenuate noise in pauses. Noise that overlaps an audible note calls for a different processing stage, such as a targeted hum filter or spectral suppression.

## Open source comparison

Use the existing [Cycfi Q onset gate](https://github.com/cycfi/q/blob/master/q_lib/include/q/fx/onset_gate.hpp) as a
host comparison for detecting soft picks and palm mutes. It addresses the opening decision. Keep idle noise
estimation, hold and attenuation in the comparison because those determine whether a whole gate is useful on
guitar or bass. Include standard bass low E and five-string low B, with both finger and pick attacks. For the
poly synth, retain the project's current oscillator and filter code. Compare candidate detectors in the host
harness before porting any library to the pedal.
