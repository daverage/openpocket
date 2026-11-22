Yes. The cleanest implementation is a **stateful Fuzz Face replacement** inside your existing drive engine, not a new pedal or DSP framework.

Your current `engine/px_dsp.c` already provides the oversampling, input filtering, tone filtering, DC removal and Poly Oct Fuzz routing. We can retain all of that.

I'd add a lightweight experimental model with interstage feedback, signal-dependent bias and recovery, while retaining the original clipping function for A/B testing.

This is **not a transistor-accurate circuit simulation**. It's a low-cost, circuit-inspired experiment designed to determine whether the missing dynamic behaviour improves the sound.

## **1. Add state to&#x20;**`engine/px_dsp.h`

Find `px_drive_state` and add four floats:

```
typedef struct {
    px_bq hp, shelf, lp, mid, t1, t2, t3;
    float gain, level, blend, bias, off, gate;
    float dcs, dck, shelf_lo, mix;
    int clip, tm, np, pre, dcb;

    /* Stateful Fuzz Face experiment */
    float ff_env;
    float ff_prev;
    float ff_attack;
    float ff_release;
} px_drive_state;
```

This adds 16 bytes per `px_drive_state` instance.

## **2. Add coefficient setup in&#x20;**`engine/px_dsp.c`

In `drive_setup()`, immediately after:

```
state->dck = 2.0f * PI_F * 12.0f / osr;
```

Add:

```
/* Fuzz Face dynamic bias envelope: approx. 8 ms attack,
   90 ms release, evaluated at the oversampled rate. */
state->ff_attack  = 1.0f / (0.008f * osr);
state->ff_release = 1.0f / (0.090f * osr);
```

These coefficients are calculated only during setup, not per sample.

## **3. Replace the Fuzz Face clipper section**

Find `fuzz_face_clip()` around line 2855.

Replace it with:

```
/*
 * Original Fuzz Face waveshaper.
 * Kept for controlled A/B comparison.
 */
static inline float fuzz_face_clip_legacy(float input)
{
    const float first =
        px_tanh(0.82f * input + 0.22f) -
        px_tanh(0.22f);

    return px_tanh(4.1f * first - 0.32f) +
           px_tanh(0.32f);
}

/*
 * Experimental dynamic Fuzz Face.
 *
 * Adds:
 * - signal-dependent bias shift
 * - slow bias recovery
 * - interstage negative feedback approximation
 *
 * Reuses existing oversampling, filters and DC blocking.
 * Not a physically exact transistor circuit simulation.
 */
static inline float fuzz_face_clip_dynamic(
    px_drive_state *state, float input)
{
    /* Limit envelope input to keep behaviour bounded. */
    float magnitude = px_abs(input);
    if (magnitude > 4.0f)
        magnitude = 4.0f;

    /* Fast attack, slower recovery. */
    const float k =
        magnitude > state->ff_env
            ? state->ff_attack
            : state->ff_release;

    state->ff_env +=
        k * (magnitude - state->ff_env);

    /* Signal-dependent operating-point shift. */
    const float shift = 0.10f * state->ff_env;

    /*
     * Delayed negative feedback.
     * Deliberately conservative to avoid instability.
     */
    const float u =
        input - 0.055f * state->ff_prev;

    /* First transistor-like nonlinear stage. */
    const float bias1 = 0.22f - shift;

    const float first =
        px_tanh(0.82f * u + bias1) -
        px_tanh(bias1);

    /* Second stage with dynamic bias. */
    const float bias2 =
        -0.32f + 0.06f * state->ff_env;

    const float output =
        px_tanh(4.1f * first + bias2) -
        px_tanh(bias2);

    state->ff_prev = output;

    return output;
}
```

The original transfer curve is preserved as the reference. The new path introduces dynamic behaviour without adding a circuit solver, large buffers or neural inference.

## **4. Update&#x20;**`clip_run()`

Change the function signature from:

```
static inline float clip_run(
    const px_drive_state *state, float d)
```

to:

```
static inline float clip_run(
    px_drive_state *state, float d)
```

Then replace:

```
case CL_FUZZ_FACE:
    return fuzz_face_clip(d);
```

with:

```
case CL_FUZZ_FACE:
#ifdef PX_FUZZ_FACE_DYNAMIC
    return fuzz_face_clip_dynamic(state, d);
#else
    return fuzz_face_clip_legacy(d);
#endif
```

Now the new model is opt-in at compile time, and the original is still available for comparison.

Both `drive_run()` and `drive_block()` already pass mutable `px_drive_state` objects to `clip_run()`, so neither processing loop needs restructuring.

## **5. Update the reset function**

Find `syn_fuzz_reset()` around line 2382.

Replace it with:

```
static void syn_fuzz_reset(px_engine *e)
{
    memset(e->syn_fuzz_os, 0,
           sizeof e->syn_fuzz_os);

    bq_reset(&e->syn_fuzz.hp);
    bq_reset(&e->syn_fuzz.lp);
    bq_reset(&e->syn_fuzz.t1);

    e->syn_fuzz.dcs = 0.0f;

    /* Reset dynamic Fuzz Face state */
    e->syn_fuzz.ff_env = 0.0f;
    e->syn_fuzz.ff_prev = 0.0f;

    e->syn_lp = 0.0f;
}
```

This prevents bias and feedback state persisting when resetting Poly Oct Fuzz.

## **6. Enable the new model**

For the experimental build, add:

```
-DPX_FUZZ_FACE_DYNAMIC
```

to the appropriate compiler flags in `firmware/build.sh`.

Do not enable it globally until tested. Compile both configurations separately so the old and new model can be compared using identical settings.

## **7. What to test**

| Test                   | Expected behaviour                                  |
| ---------------------- | --------------------------------------------------- |
| Silence and startup    | No DC spikes or residual noise                      |
| Single sustained note  | Slightly more dynamic compression and bias movement |
| Note decay             | Smooth recovery, no pumping or gated tail           |
| Power chords           | No uncontrolled low-frequency intermodulation       |
| Poly Oct Fuzz          | Octave voices remain clear and musically balanced   |
| Switching presets      | No stale bias or feedback state                     |
| 2× and 4× oversampling | Stable output at both processing rates              |
| CPU                    | Minimal increase in processing load                 |

Measure gain-matched output and actual callback peaks. The new model is likely to be computationally inexpensive because it adds two nonlinear stages already present in the original, plus a few arithmetic operations and state updates.

### **Important limitation**

This improves the **behavioural model**, not circuit accuracy. The feedback is a delayed approximation rather than the nonlinear transistor feedback network of a real Fuzz Face. It should be judged against your original on the pedal.

**I'd implement exactly this as the first A/B experiment.** If the difference is audible and preferable, the next step is a fixed-topology circuit-derived model based on validated transistor equations. If it isn't better, keep the original rather than introducing complexity for its own sake.

I have checked the relevant functions and structures in your GitHub source, but I have not compiled or hardware-tested these changes.
