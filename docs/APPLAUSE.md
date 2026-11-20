The brief below is designed to give directly to the OpenPocket coding agent. It prioritises believable applause, reliable guitar triggering, low CPU usage and retaining the existing effect architecture.

# OpenPocket: Guitar-Triggered Applause Synthesiser

## Objective

Improve the existing `PX_SFX_APPLAUSE` effect into a convincing, procedurally synthesised audience applause generator for the M-VAVE ANN Blackbox.

**The critical requirement is that applause must ONLY start when the guitar produces a qualifying signal. Enabling or selecting the effect must NOT start applause.**

This is a novelty sound effect: play a guitar note or chord, and an imaginary audience bursts into applause.

Prioritise natural, recognisable applause rather than perfect acoustic modelling. The sound should resemble real people clapping, not white noise, rainfall, static or a drum machine.

Retain the existing effect ID, parameter integration and five ROOM modes.

## 1. Guitar Triggering (Mandatory)

Implement an independent, lightweight guitar signal detector. Do not use the pitch tracker, FFT or note allocation.

### Trigger detection

- Measure the incoming dry guitar signal using a smoothed amplitude envelope.
- Detect a genuine attack using a configurable threshold, optionally combined with a positive envelope slope.
- Use hysteresis to prevent repeated triggering around the threshold.
- Include a short qualification period to reject isolated noise spikes.
- Evaluate the input signal BEFORE effect mixing or processing.
- Detect attacks regardless of the current wet/dry mix.

### Trigger behaviour

1. Effect enabled: silent, with no applause generation.
2. Guitar played: a qualifying attack triggers one complete applause event.
3. Applause begins with a brief, natural audience reaction delay (approximately 50–150 ms).
4. Applause builds, sustains and fades naturally.
5. Additional guitar notes during the applause do not restart or extend it.
6. Once the applause finishes, the effect rearms when the guitar input has fallen below the release threshold.
7. The next qualifying guitar attack starts another applause event.

**Important:** If the guitar is already ringing when the effect is enabled, do not trigger immediately. Require the input to become quiet and then produce a new attack.

Use a state machine:

`IDLE → REACTION → BUILD → SUSTAIN → DECAY → IDLE`

Include explicit armed/disarmed handling. Reset trigger state correctly when switching presets, changing effects or bypassing the effect.

Do not use a fixed timer as the sole trigger lockout. The actual state of the applause event and guitar signal should determine when another trigger is permitted.

The trigger must work with single notes, chords, clean signals and reasonably distorted guitar signals.

## 2. More Realistic Individual Claps

The current implementation generates filtered white noise multiplied by a shared exponential envelope.

This approximates clap density but can sound like broadband hiss instead of applause.

Replace the single `ap_clap` accumulator with a small pool of approximately 8–16 lightweight clap voices.

Each clap should have:

- A sharp, broadband initial impact.
- A short, coloured noise decay.
- Optional closely spaced secondary impulses simulating hand contact.
- Variable duration, amplitude and spectral character.
- Small resonant peaks to give different claps distinctive tonal characteristics.

Use lightweight noise generators, filters and envelope stages.

Avoid expensive physical modelling where perceptual approximations achieve similar results.

Clap events should overlap naturally without cutting one another off.

## 3. Human Crowd Behaviour

A real crowd is not simply a Poisson distribution of unrelated clap events.

Use a hybrid event scheduler combining:

- A small number of individually modelled clappers.
- Slightly different natural clapping speeds.
- Timing jitter and gradual drift.
- Variable clap intensity.
- A stochastic background population for larger crowds.

Do not allow all simulated people to synchronise unintentionally.

Small crowds should contain individually recognisable claps. Large crowds should create dense applause while retaining audible transient structure.

Retain CROWD as the density/intensity control, but avoid unrealistic density and excessive compression at maximum settings.

Normalise energy according to actual overlapping events, not simply the nominal number of claps per second.

## 4. Audience Event Envelope

A triggered applause event should evolve naturally rather than immediately producing a constant wall of sound.

Suggested default event:

| Stage    | Behaviour                                      |
| -------- | ---------------------------------------------- |
| Reaction | Brief delay following guitar attack            |
| Build    | Scattered claps rapidly become a crowd         |
| Sustain  | Full applause with naturally varying intensity |
| Decay    | People gradually stop clapping                 |
| Tail     | Remaining claps and room ambience fade         |

The current WAVES control should influence the natural fluctuations during the event, without causing unrealistic periodic volume pumping.

Choose sensible default timings for a short, amusing applause burst, approximately 3–5 seconds.

The generator must stop scheduling new claps during the final decay stage and eventually reach digital silence.

Avoid long-running background noise after an event has completed.

## 5. Five Existing ROOM Modes

Preserve the current five room selections and their general acoustic identities.

Improve the simulation through two complementary layers:

**Near audience**

- Bright, distinct individual claps.
- Stronger transients.
- Minimal diffusion.

**Distant audience**

- Darker filtering.
- Softer, overlapping transients.
- More diffuse acoustic energy.

The existing `ap_field` follower can remain as a low-cost approximation, but should not become a continuous hiss generator.

Add a lightweight early-reflection or short-delay network only if listening tests demonstrate a meaningful improvement.

A convincing dry audience is more important than sophisticated reverb.

Avoid large delay buffers or expensive convolution.

## 6. Controls

Retain the existing five-parameter interface and avoid breaking existing presets.

| Control              | Purpose                                  |
| -------------------- | ---------------------------------------- |
| ROOM                 | Existing five crowd/venue profiles       |
| CROWD                | Number and density of simulated clappers |
| WAVES                | Natural variation in applause intensity  |
| Existing parameter 4 | Preserve current mapping                 |
| Existing parameter 5 | Preserve current mapping                 |

Do not introduce additional controls unless essential.

Trigger sensitivity and applause duration can initially use carefully selected internal defaults.

If adjustable triggering proves necessary, reuse an appropriate existing parameter only after checking backwards compatibility.

## 7. DSP and Hardware Constraints

Target the JieLi AC791N / WL82 processor used by OpenPocket.

- No pitch detection or spectral analysis.
- No sample libraries or external audio files.
- Avoid dynamic allocation in the audio callback.
- No expensive transcendental functions in the per-sample processing path.
- Use fixed-size voice arrays.
- Update crowd scheduling and slowly varying parameters at control rate where practical.
- Reuse existing PRNG, biquad, envelope and filter infrastructure.
- Prevent denormals, NaNs and clipping.
- Keep audio callback execution deterministic and bounded.
- Ensure the effect doesn't compromise other OpenPocket processing blocks.

Reuse existing implementation patterns wherever possible.

Do not rewrite unrelated effects or alter firmware architecture unnecessarily.

## 8. Testing and Validation

Extend `tests/engine/synth_test.c` and the existing offline rendering harness.

### Trigger tests

Verify:

1. Enabling the effect with silence produces absolute silence.
2. Continuous low-level input noise does not trigger applause.
3. A guitar attack triggers exactly one applause event.
4. A sustained note does not repeatedly trigger.
5. Additional notes during applause do not restart it.
6. Applause ends and the system correctly rearms.
7. A second distinct guitar attack triggers a second event.
8. Enabling the effect while guitar input is already sustained does not trigger.
9. Bypass, preset changes and effect switching leave no stuck voices or unintended triggers.
10. Silence after an event consumes negligible processing.

### Audio tests

Generate offline WAV files for all five ROOM settings at low, medium and high CROWD values.

Listen for:

- Recognisable individual claps.
- Natural crowd growth and decay.
- Absence of metallic or robotic repetition.
- No excessive hiss or frying-noise character.
- No clipping or sudden level jumps.
- Clearly different room sizes and crowd densities.

Measure callback CPU usage, peak level, RMS level and memory use.

Compare against the existing implementation.

## 9. Implementation Priorities

Work in this order:

**Stage 1: Trigger reliability**

Implement the guitar attack detector, applause state machine, event timing and rearming behaviour. Verify that effect activation alone NEVER produces applause.

**Stage 2: Individual clap realism**

Improve transient shapes, spectral variation and clap envelopes.

**Stage 3: Crowd realism**

Introduce independent clappers, timing variation and controlled stochastic crowd density.

**Stage 4: Acoustic improvement**

Improve near/far audience balance and room diffusion only where necessary.

**Stage 5: Optimisation and validation**

Benchmark on the target hardware, inspect offline WAV renders and eliminate unnecessary processing.

## 10. Research References

Use these as DSP design references rather than assuming any implementation can be copied directly.

1. **Peltola et al. (2007), Synthesis of Hand Clapping Sounds.** Physics-based clap synthesis and separation of individual clap generation from crowd event scheduling.\
   [https://vbn.aau.dk/en/publications/synthesis-of-hand-clapping-sounds/](https://vbn.aau.dk/en/publications/synthesis-of-hand-clapping-sounds/)
2. **Lee and Reiss (2020), Real-Time Sound Synthesis of Audience Applause.** Procedural audience modelling and perceptually convincing applause.\
   [https://aes.org/publications/elibrary/elibrary-page/?id=20732](https://aes.org/publications/elibrary/elibrary-page/?id=20732)
3. **El Applauso, open-source Disting NT plugin.** A practical reference using multiple individual clappers with varying timing, velocity and tone. Review the implementation for transferable ideas and check licensing before copying code.\
   [https://github.com/kuttor/Disting-NT-Plugin--El-Applauso](https://github.com/kuttor/Disting-NT-Plugin--El-Applauso)

## Definition of Done

The effect must pass this practical test:

**Switch to APPLAUSE. Hear nothing. Strum the guitar once. A realistic audience applauds, builds, fades and stops. Play again after it has finished, and the audience responds again.**

No uncontrolled retriggering, background hiss or applause when the effect is merely switched on.

The result should be recognisably human applause even without additional reverb.

Preserve compatibility with the rest of OpenPocket and provide comparative audio renders and CPU measurements before considering the implementation complete.

One architectural decision I'd emphasise is **separating the guitar detector from the applause generator**. That allows the synthesis to be tested independently of triggering, and avoids the current effect's potential to produce sound simply because its processing block is active.

The three research references were verified as existing publications or projects.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://vbn.aau.dk\&sz=32)

Aalborg Universitets forskningsportal+2
