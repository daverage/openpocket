/* OpenPocket — guitar sound engine v3 (see px_dsp.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Engine rules (learned on the pedal):
 *  - single precision everywhere (AC791N float FPU), never double;
 *  - coefficients are computed in px_engine_set (outside real time); processing is only + and x;
 *  - no modulo in hot loops; anti-denormal offset at the input;
 *  - ALWAYS verify with firmware/px_math.c (the pedal's libm), never the PC libm.
 *
 * Voicing sources (circuit analyses, datasheets, manuals): ElectroSmash (TS808, RAT, DS-1, Big Muff, Fuzz Face,
 * Klon, Distortion+, Bluesbreaker), Electric Druid, R.G. Keen / GEOFEX, Rob Robinette & Duncan's TSC (amp tone
 * stacks), Celestion / Jensen / EV / JBL speaker curves, Roland/Boss/Ampeg/Electro-Harmonix manuals.
 */
#include "px_dsp.h"
#include "px_triode_table.h"
/* v40: on the pedal the audio path runs from RAM, as the stock firmware's does: firmware/build.sh compiles this file with
 * -ffunction-sections and tools/ramcode.py moves the per-block functions (px_engine_part, the delay and reverb loops,
 * the NAM network, the oversampling, the IR...) and the hand-written routines into the RAM code window, which sits
 * within call range of the flash (firmware/app.ld). Nothing to mark here: the code is the compiler's, unchanged. */
#define PX_HOT
#define PX_INL
#define PX_AINL __attribute__((always_inline))
#ifndef PX_RUNS
#define PX_RUNS 1                                      /* v40.4: 0 = every block sample by sample, as before ("blocks over a run of
                                                          samples", below): the Bluetooth test image, whose flash is full
                                                          (firmware/build.sh). The same output, more DSP time */
#endif
#include <math.h>
#include <stddef.h>
#include <string.h>
#ifdef __PI32V2__   /* on the pedal: a buffer out of the 96 KB RAM. v40.4: in the 24 KB below it (firmware/app.ld .bss_lo); it was in
                     * the free end of the RAM code window (.bss_hi), which the audio path's code now needs */
#define PX_LO(name) __attribute__((section(".bss.lo." #name)))
#define PX_POOL_TAIL(name) __attribute__((section(".pool.tail." #name)))   /* after the pool's own buffers (app.ld) */
#else
#define PX_LO(name)
#define PX_POOL_TAIL(name)
#endif

#define PI_F 3.14159265f
/* v49: the octave voices kept at v48's level (the mean over the bundled chords, lead and riff loops, through POLY
 * OCTAVE HQ: tools/sim/px_engine.wasm of v48 against v49) */
#define ERB_GAIN_UP   0.631f                           /* v62: the 26-band bank at the 29-band one's wet level (loops) */
#define ERB_GAIN_DOWN 1.451f
#ifndef PX_SUB2_LEVEL
#define PX_SUB2_LEVEL 1.79f                            /* v64: OCT -2 at OCT -1's wet level, same setting (loops) */
#endif
#ifndef ERB_GAIN_DOWN2
#define ERB_GAIN_DOWN2 1.0f                            /* v64: the -24 voice (calibrated in rc_pitch's level) */
#endif
#ifndef ERB_GAIN_UP2
#define ERB_GAIN_UP2  0.46f                            /* v49: the 2' at CHURCH ORGAN's level (its shifter's, -6.7 dB) */
#endif

/* ================================================================ tools ===== */
static inline float px_tanh(float x)                   /* rational, err < 1e-3 on [-3,3] */
{
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline float px_abs(float x) { return x < 0.0f ? -x : x; }
static inline float lfo_sin(float ph)                  /* parabolic sine, ph in [0,1) -> -1..1 */
{
    if (ph < 0.5f) return 16.0f * ph * (0.5f - ph);
    ph -= 0.5f;
    return -16.0f * ph * (0.5f - ph);
}
static inline float sin_hq(float ph)                   /* refined parabola (0.1 % error): audio-rate carrier */
{
    float y = lfo_sin(ph);
    return 0.225f * (y * px_abs(y) - y) + y;
}
static inline float lfo_tri(float ph) { return 4.0f * px_abs(ph - 0.5f) - 1.0f; }
static inline float f01(uint8_t v) { return (float)(v > 100 ? 100 : v) * 0.01f; }
static inline float db2lin(float db) { return powf(10.0f, db / 20.0f); }
static inline float wrap1(float ph) { if (ph >= 1.0f) ph -= 1.0f; else if (ph < 0.0f) ph += 1.0f; return ph; }
static float onepole_k(float sr, float sec) { return 1.0f - expf(-1.0f / (sec * sr)); }

/* HQ octave (and v49 note tracker) analysis: complex band-pass filters (Noga's transform of the cookbook low-pass,
 * Thuillier's phase scaling; docs/research/POLYPHONIC-PITCH-RESEARCH.md). v49: 29 bands from 60 Hz, each ERB_S0 + ERB_SC x
 * its centre from the next and ERB_WIDTH of that wide, at a quarter of the rate after two half-band stages
 * (docs/archive/NOTE-TRACKING.md). The coefficients
 * depend only on the rate: shared by every engine and every user (PITCH, SYN, the tracker), each with its own state. */
#define ERB_LOW   60.0f                                /* the first band's centre (Hz) */
#define ERB_S0    37.5f                                /* the spacing to the next band: ERB_S0 + ERB_SC x centre (Hz) */
#define ERB_SC    0.04f
#define ERB_WIDTH 0.8f                                 /* bandwidth, of the spacing */
static px_erb_band erb_band[PX_ERB_BANDS];
static float erb_hb[8], erb_fc[PX_ERB_BANDS], erb_bw[PX_ERB_BANDS], erb_rate;
/* v62 (poly-test): the octave voices get their own bank, 26 bands 60 Hz + 2% apart and 0.7 of the spacing wide
 * (docs/archive/OCTAVE-LAYOUT-v62.md: less added wobble, faster low notes); the note tracker keeps erb_band */
#define ERB_O_BANDS 26
#define ERB_O_S0    60.0f
#define ERB_O_SC    0.02f
#define ERB_O_WIDTH 0.7f
static px_erb_band erb_oband[ERB_O_BANDS];
static float erb_ofc[ERB_O_BANDS];
#define ERB_FULL  ((float)ERB_O_BANDS)
#ifndef LOW_SEL_MIN
#define LOW_SEL_MIN 0.1f
#endif
#ifndef LOW_MAKEUP
#define LOW_MAKEUP 2.8f                                /* v73: +9 dB at RANGE 1 */
#endif
#ifndef PX_LOWEST_HQ_GAIN
#define PX_LOWEST_HQ_GAIN 1.4f                         /* Synth2: LOWEST OCTAVE HQ's own octave-down gain, on top of LOW_MAKEUP (not shared with POLY OCTAVE HQ) */
#endif
#ifndef PX_POLY_RANGE_MAKEUP
#define PX_POLY_RANGE_MAKEUP 0.196f                 /* POLY OCTAVE HQ: the -1's make-up exponent as RANGE cuts (0 = none): the measured drop is about 1.7 dB per ln step of RANGE */
#endif
#ifndef PX_POLY_RANGE_LIFT
#define PX_POLY_RANGE_LIFT 9.0f                     /* POLY OCTAVE HQ: dB added to the -1 once RANGE is engaged (LOWEST OCTAVE HQ's level), full by RANGE 90 */
#endif
#ifndef LOW_RANGE_BOOST
#define LOW_RANGE_BOOST 0.7f                           /* Synth2: LOWEST OCTAVE HQ extra make-up at RANGE 1 (x1.7, +4.6 dB) */
#endif
#ifndef LOW_ATTACK
#define LOW_ATTACK  0.06f                              /* v73: 1.5 ms at 11.025 kHz */
#define LOW_RELEASE 0.0018f                            /* v73: 50 ms */
#endif
static void erb_layout(px_erb_band *band, float *fc, float *bw, int n, float s0, float sc, float width, float rate)
{
    float center = ERB_LOW;
    for (int i = 0; i < n; ++i, center += s0 + sc * center) {
        const float omega = PI_F * width * (s0 + sc * center) / rate;
        const float a0 = 1.0f / (1.0f + 0.70710678f * sinf(omega));
        const float half_sine = sinf(0.5f * omega);
        const float b0 = a0 * half_sine * half_sine;
        const float a1 = -2.0f * cosf(omega) * a0, a2 = 2.0f * a0 - 1.0f;
        const float phase = 2.0f * PI_F * center / rate;
        const float c = cosf(phase), s = sinf(phase);
        const float c2 = c * c - s * s, s2 = 2.0f * c * s;
        fc[i] = center; if (bw) bw[i] = width * (s0 + sc * center);
        band[i] = (px_erb_band){ b0, {2.0f * b0 * c, 2.0f * b0 * s},
                                 {b0 * c2, b0 * s2}, {a1 * c, a1 * s}, {a2 * c2, a2 * s2} };
    }
}
static void erb_prepare(px_engine *e)
{
    const float rate = e->sr * 0.25f;
    if (erb_rate == rate) return;
    erb_layout(erb_band, erb_fc, erb_bw, PX_ERB_BANDS, ERB_S0, ERB_SC, ERB_WIDTH, rate);
    erb_layout(erb_oband, erb_ofc, NULL, ERB_O_BANDS, ERB_O_S0, ERB_O_SC, ERB_O_WIDTH, rate);
    for (int i = 0; i < 8; ++i) {
        const float offset = (float)(2 * i + 1);
        const float window = 0.54f + 0.46f * cosf(2.0f * PI_F * offset / 30.0f);
        erb_hb[i] = (i & 1 ? -window : window) / (PI_F * offset);
    }
    erb_rate = rate;
}

static inline float erb_halfband(const float *samples, unsigned pos, const float *coeff)
{
    float y = 0.5f * samples[(pos - 15) & 31];
    for (unsigned i = 0; i < 8; ++i) {
        const unsigned offset = 2 * i + 1;
        y += coeff[i] * (samples[(pos - 15 + offset) & 31] + samples[(pos - 15 - offset) & 31]);
    }
    return y;
}

static inline float erb_inverse_root(float power)
{
    union { float f; uint32_t u; } root = { .f = power };
    root.u = 0x5f3759dfu - (root.u >> 1);
    float inv = root.f;
    inv *= 1.5f - (0.5f * power * inv) * inv;
    inv *= 1.5f - (0.5f * power * inv) * inv;
    return inv;
}

/* the bank at 11.025 kHz: one sample in, the octaves out, mixed by their levels: up (+12), down (-12) and, v49, up2
 * (+24: CHURCH ORGAN HQ's 2', phase x4, from the bands below ERB_UP2_TOP, whose fourfold stays under the rate's limit) */
#define ERB_UP2_TOP 1250.0f
#ifdef PX_LOWEST_HOST
static float erb_host_mask[ERB_O_BANDS];
#endif
#ifdef ERB_HOST_EXPERIMENTS
/* v64 host experiments (tools/research/octave_banks.c, docs/archive/OCTAVE-LAYOUT-v62.md): reconstruction weighting by each
 * band's dominance over its neighbours, (p_i / max(p_i-1, p_i, p_i+1))^beta from the previous sample's powers;
 * and ducking the octave voices after a pick (erb_octaves2) */
static float erb_host_beta, erb_host_pw[ERB_O_BANDS + 2], erb_host_duck_ms, erb_host_duck_ratio = 2.0f, erb_host_duck_depth;
#endif
#ifdef PX_LOWSIG_HOST
static float px_lowsig_acc;
#endif
static PX_HOT float erb_bank(const px_engine *e, px_erb_octave *state, float x, float up, float down, float up2, float range)
{
    (void)e;
    float raised = 0.0f, lowered = 0.0f, raised2 = 0.0f;
    float lowered2 = 0.0f, raised3 = 0.0f, mid8 = 0.0f;
    float below = 0.0f, neighbour = 0.0f;              /* v73: the level of the bands under this one (bar the next) */
    const float sel = state->low_sel;
    for (int i = 0; i < ERB_O_BANDS; ++i) {
        const px_erb_band *b = &erb_oband[i];
        const float real = b->b0 * x + state->z1[i].r;
        const float imag = state->z1[i].i;
        state->z1[i].r = b->b1.r * x - (b->a1.r * real - b->a1.i * imag) + state->z2[i].r;
        state->z1[i].i = b->b1.i * x - (b->a1.r * imag + b->a1.i * real) + state->z2[i].i;
        state->z2[i].r = b->b2.r * x - (b->a2.r * real - b->a2.i * imag);
        state->z2[i].i = b->b2.i * x - (b->a2.r * imag + b->a2.i * real);

        /* Phase doubling is Re(z² / |z|). For phase halving, track the
         * square root's sign across each crossing of the negative real axis. */
        const float rr = real * real, ii = imag * imag, power = rr + ii + 1e-24f;
        const float inv = erb_inverse_root(power);
        float g = 1.0f;
        if (sel > 0.0f) {                              /* v73: the lowest sounding band (Roland EP3929910B1's rule): a
                                                          band with much more level under it (bar its neighbour) is muted */
            float *env = &state->low_env[i];
            const float lvl = power * inv;             /* |z| */
            *env += (lvl - *env) * (lvl > *env ? LOW_ATTACK : LOW_RELEASE);
            const float r = below / (*env + 1e-12f);
            g = r < sel ? 1.0f : (r > 2.0f * sel ? 0.0f : 2.0f - r / sel);
            below += neighbour; neighbour = *env;
        }
#ifdef ERB_HOST_EXPERIMENTS
        float cohere = 1.0f;
        if (erb_host_beta > 0.0f) {
            float m = erb_host_pw[i + 1];
            if (erb_host_pw[i] > m) m = erb_host_pw[i];
            if (erb_host_pw[i + 2] > m) m = erb_host_pw[i + 2];
            cohere = powf(erb_host_pw[i + 1] / (m + 1e-30f), erb_host_beta);
        }
        erb_host_pw[i + 1] = power;
        raised += cohere * (rr - ii) * inv;
#else
        raised += (rr - ii) * inv;
#endif
        if (up2 != 0.0f && erb_ofc[i] < ERB_UP2_TOP) {     /* v49: Re(z⁴) / |z|³, two octaves up */
            const float d = rr - ii;
            raised2 += (d * d - 4.0f * rr * ii) * inv * inv * inv;
        }
        if (state->up3 != 0.0f && erb_ofc[i] < 1800.0f) raised3 += real * (rr - 3.0f * ii) * inv * inv;   /* ORGAN: Re(z³) / |z|², the 2 2/3' */
        mid8 += real;                                  /* ORGAN: the band itself, the 8' */
        const unsigned negative = imag < 0.0f;
        unsigned phase = state->half_phase[i];
        /* v64: two octaves down, |z| cos((a + 2 pi k) / 4): k counts the crossings of the negative real axis modulo 4
         * (tools/research/sub2_proto.c); a band whose -24 would sound under 35 Hz fades out (a low E's 20.6 Hz) */
        if (real < 0.0f && (phase & 1) != negative) state->quarter[i] = (uint8_t)((state->quarter[i] + (negative ? 1 : 3)) & 3);
        if (state->down2 != 0.0f) {
            const float floor2 = (0.25f * erb_ofc[i] - 30.0f) * 0.1f;
            if (floor2 > 0.0f) {
                const unsigned k = state->quarter[i];
                const float hc = 0.5f + 0.5f * real * inv;
                const float h = hc > 0.0f ? hc * erb_inverse_root(hc + 1e-30f) : 0.0f;
                const float qc = 0.5f + 0.5f * ((k & 1) ? -h : h);
                const float qa = qc > 0.0f ? qc * erb_inverse_root(qc + 1e-30f) : 0.0f;
                const float sign = k == 0 ? 1.0f : k == 2 ? -1.0f : ((k == 1) == (negative != 0) ? 1.0f : -1.0f);
                lowered2 += power * inv * sign * qa * (floor2 < 1.0f ? floor2 : 1.0f) * g;
            }
        }
        if (real < 0.0f && (phase & 1) != negative) phase ^= 2;
        state->half_phase[i] = (uint8_t)((phase & 2) | negative);
        /* Taper the octave-down band mix around the selected input pitch.
         * Keep phase history for every band while the dial moves. */
        float weight = range + 0.5f - (float)i;
        if (g < 1.0f) weight = weight < g ? weight : g;
#ifdef PX_LOWSIG_HOST
        px_lowsig_acc += real * g;                     /* (host bench: the selected bands at their own pitch) */
#endif
        if (down != 0.0f && weight > 0.0f) {
            /* |z| cos(arg(z)/2), evaluated without a per-band division. */
            const float half_cosine_squared = 0.5f + 0.5f * real * inv;
            const float squared = half_cosine_squared > 0.0f ? power * half_cosine_squared + 1e-30f : 1e-30f;
            float sample = squared * erb_inverse_root(squared);
            if (weight < 1.0f) sample *= weight;
#ifdef PX_LOWEST_HOST
            sample *= erb_host_mask[i];
#endif
#ifdef ERB_HOST_EXPERIMENTS
            sample *= cohere;
#endif
            lowered += (phase & 2) ? -sample : sample;
        }
    }
    return up * raised * ERB_GAIN_UP + down * lowered * ERB_GAIN_DOWN + up2 * raised2 * ERB_GAIN_UP2
         + state->down2 * lowered2 * ERB_GAIN_DOWN2 + state->up3 * raised3 * 0.55f + state->mid8 * mid8;
}

/* one 44.1 kHz sample in, the octave voices (mixed by their levels) out: down to 11.025 kHz in two half-band stages,
 * the bank, and back up */
#define ERB_IDLE 64                                    /* v49: (the halfband tails are 32 samples) */
static PX_HOT float erb_octaves2(const px_engine *e, px_erb_octave *state, float input, float up, float down, float up2, float range)
{
    /* v49: with every voice at zero (STRINGS HQ with OCT at 0, say) the bank's output is silent: once its tail is out
     * the bank rests and its state is cleared. A voice turned up again starts from silence and builds up with the
     * bands (their own ~10-17 ms), so it fades in rather than stepping in on stale filter state. */
    if (up == 0.0f && down == 0.0f && up2 == 0.0f && state->down2 == 0.0f && state->up3 == 0.0f && state->mid8 == 0.0f) {
        if (state->idle >= ERB_IDLE) return 0.0f;
        if (++state->idle == ERB_IDLE) { memset(state, 0, sizeof *state); state->idle = ERB_IDLE; return 0.0f; }
    } else state->idle = 0;
    const unsigned pos = state->pos;
    state->input[pos] = input;
    if ((pos & 1) == 0) {                              /* 22.05 kHz */
        const unsigned p2 = state->pos2;
        state->in2[p2] = erb_halfband(state->input, pos, erb_hb);
        if ((p2 & 1) == 0)                             /* 11.025 kHz: the bank */
            state->up2[p2] = erb_bank(e, state, erb_halfband(state->in2, p2, erb_hb), up, down, up2, range);
        /* back up to 22.05 kHz (the odd slots of up2 stay zero: their centre tap alone) */
        state->up[pos] = (p2 & 1) ? state->up2[(p2 - 15) & 31] : 2.0f * erb_halfband(state->up2, p2, erb_hb);
        state->pos2 = (p2 + 1) & 31;
    }
    state->pos = (pos + 1) & 31;
    /* The upsampler's odd samples need only the centre tap. Its even samples
     * use the surrounding taps. The odd history slots stay zero. */
#ifdef ERB_HOST_EXPERIMENTS
    if (erb_host_duck_ms > 0.0f) {                     /* (host only: one octave state at a time) */
        static float fast, slow, duck = 1.0f; static int since = 1 << 30;
        const float a = input < 0.0f ? -input : input;
        fast += (a - fast) * 0.0224f; slow += (a - slow) * 0.00057f;   /* 1 ms, 40 ms at 44.1 kHz */
        if (fast > erb_host_duck_ratio * slow && fast > 1e-3f && since > 2646) { duck = erb_host_duck_depth; since = 0; }
        since++;                                                       /* (one duck a pick: 60 ms apart) */
        duck += (1.0f - duck) * (1.0f / (0.001f * erb_host_duck_ms * 44100.0f));
        const float y = (pos & 1) ? state->up[(pos - 15) & 31] : 2.0f * erb_halfband(state->up, pos, erb_hb);
        return y * duck;
    }
#endif
    if (pos & 1) return state->up[(pos - 15) & 31];
    return 2.0f * erb_halfband(state->up, pos, erb_hb);
}
static inline float erb_octaves(const px_engine *e, px_erb_octave *state, float input, float up, float down, float range)
{ return erb_octaves2(e, state, input, up, down, 0.0f, range); }

/* Match the HQ bank's voices to the delay-line Poly Octave at the same settings. */
static float octave_down_level(float level, int hq) { return (level * 1.2f) * (hq ? 1.50f : 1.0f); }
static float octave_up_level(float level, int hq) { return level * (hq ? 3.02f : 1.0f); }

static void bq_reset(px_bq *b) { b->z1 = b->z2 = 0.0f; }
static inline float bq_run(px_bq *b, float x)
{
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}
/* the same over a buffer, in place: the coefficients and state in registers through the loop (same operations, same
 * result as bq_run sample by sample) */
static void bq_block(px_bq *q, float *b, int n)
{
    const float b0 = q->b0, b1 = q->b1, b2 = q->b2, a1 = q->a1, a2 = q->a2;
    float z1 = q->z1, z2 = q->z2;
    for (int k = 0; k < n; k++) {
        const float x = b[k], y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        b[k] = y;
    }
    q->z1 = z1;
    q->z2 = z2;
}
/* two filters in series over a buffer, interleaved: the first on sample k while the second does sample k-1, so that
 * their two feedback loops overlap in the FPU instead of waiting for each other (same operations per filter, same
 * result as bq_block twice) */
static void bq_block2(px_bq *p, px_bq *q, float *b, int n)
{
    if (n <= 0) return;
    const float pb0 = p->b0, pb1 = p->b1, pb2 = p->b2, pa1 = p->a1, pa2 = p->a2;
    const float qb0 = q->b0, qb1 = q->b1, qb2 = q->b2, qa1 = q->a1, qa2 = q->a2;
    float pz1 = p->z1, pz2 = p->z2, qz1 = q->z1, qz2 = q->z2;
    float x = b[0], pend = pb0 * x + pz1;
    pz1 = pb1 * x - pa1 * pend + pz2;
    pz2 = pb2 * x - pa2 * pend;
    for (int k = 1; k < n; k++) {
        const float xp = b[k], yp = pb0 * xp + pz1;
        pz1 = pb1 * xp - pa1 * yp + pz2;
        pz2 = pb2 * xp - pa2 * yp;
        const float u = pend, yq = qb0 * u + qz1;
        qz1 = qb1 * u - qa1 * yq + qz2;
        qz2 = qb2 * u - qa2 * yq;
        b[k - 1] = yq;
        pend = yp;
    }
    {
        const float u = pend, yq = qb0 * u + qz1;
        qz1 = qb1 * u - qa1 * yq + qz2;
        qz2 = qb2 * u - qa2 * yq;
        b[n - 1] = yq;
    }
    p->z1 = pz1; p->z2 = pz2; q->z1 = qz1; q->z2 = qz2;
}
uint32_t (*px_clock)(void);                            /* v38: a clock for the per-block times (px_engine.pf), or none */
/* a chain of filters over a buffer, two at a time */
static void bq_chain(px_bq *f, int nf, float *b, int n)
{
    int c = 0;
    for (; c + 1 < nf; c += 2) bq_block2(&f[c], &f[c + 1], b, n);
    if (c < nf) bq_block(&f[c], b, n);
}
static void bq_set(px_bq *b, float b0, float b1, float b2, float a0, float a1, float a2)
{
    float z1 = b->z1, z2 = b->z2;                      /* keep state: no click on change */
    b->b0 = b0 / a0; b->b1 = b1 / a0; b->b2 = b2 / a0; b->a1 = a1 / a0; b->a2 = a2 / a0;
    b->z1 = z1; b->z2 = z2;
}
static float clampf_hz(float fs, float f) { if (f > fs * 0.45f) f = fs * 0.45f; if (f < 5.0f) f = 5.0f; return f; }
static void bq_lowpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, (1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + al, -2 * c, 1 - al);
}
static void bq_highpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, (1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + al, -2 * c, 1 - al);
}
static void bq_bandpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, al, 0, -al, 1 + al, -2 * c, 1 - al);
}
/* a notch with its own width and depth: (s^2 + 2 zz s + 1) / (s^2 + 2 zp s + 1) at the notch frequency, by the bilinear transform with the frequency prewarped;
 * zp = width / (2 f) sets the -3 dB width in hertz (the skirts stay out of the notes next to the hum), zz = zp * 10^(-depth/20) the depth at the centre
 * (a peaking filter's skirts are as wide as its depth: a 27 dB cut of "4 Hz" took 3 dB off an A at 110 Hz against a 100 Hz notch) */
static void bq_notch(px_bq *b, float fs, float f, float width, float depth_db)
{
    const float w = PI_F * f / fs, K = sinf(w) / cosf(w), zp = 0.5f * width / f, zz = zp * db2lin(-depth_db), K2 = K * K;
    bq_set(b, 1.0f + 2.0f * zz * K + K2, 2.0f * K2 - 2.0f, 1.0f - 2.0f * zz * K + K2, 1.0f + 2.0f * zp * K + K2, 2.0f * K2 - 2.0f, 1.0f - 2.0f * zp * K + K2);
}
static void bq_peak(px_bq *b, float fs, float f, float q, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}
static void bq_lowshelf(px_bq *b, float fs, float f, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w);
    float al = s / 2.0f * 1.4142135f, ap = A + 1, am = A - 1, tsa = 2.0f * sqrtf(A) * al;
    bq_set(b, A * (ap - am * c + tsa), 2 * A * (am - ap * c), A * (ap - am * c - tsa),
           ap + am * c + tsa, -2 * (am + ap * c), ap + am * c - tsa);
}
static void bq_highshelf(px_bq *b, float fs, float f, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w);
    float al = s / 2.0f * 1.4142135f, ap = A + 1, am = A - 1, tsa = 2.0f * sqrtf(A) * al;
    bq_set(b, A * (ap + am * c + tsa), -2 * A * (am + ap * c), A * (ap + am * c - tsa),
           ap - am * c + tsa, 2 * (am - ap * c), ap - am * c - tsa);
}
#include "px_track.h"                                  /* v49: the note tracker (band bank, salience, note layer) */
#include "px_mono.h"                                   /* Synth2: the one mono tracker's interface (pitch, onset, release) */
#include "px_synth_patches.h"                          /* Synth2: ANALOG's patch table */
static void px_synth_run(px_engine *e, const float *in, float *out, int n);   /* px_synth_adapter.h, included at the end */
static void px_s2_reset(px_engine *e, int type, const uint8_t *q);
static void px_s2_set_q(int type, const uint8_t *q);
static void px_s2_dry(float *g0, float *g1);
#include "px_resonator_test.h"
static float tan_pi(float fs, float f) { float w = PI_F * clampf_hz(fs, f) / fs; return sinf(w) / cosf(w); }
static void bq_hp1(px_bq *b, float fs, float f)          /* first order (RC coupling, like the real circuits) */
{
    float K = tan_pi(fs, f);
    bq_set(b, 1.0f, -1.0f, 0.0f, 1.0f + K, K - 1.0f, 0.0f);
}

/* ============================================================ catalog ===== */
static const char *const BLK_LABEL[PX_NBLK] = {"GT", "CMP", "OCT", "SYN", "SFX", "EQ", "MOD", "DLY", "REV", "BST"};
static const char *const BLK_NAME[PX_NBLK] = {"NOISE GATE", "COMPRESSOR", "OCTAVE", "SYNTH", "SPECIAL FX", "EQUALIZER",
                                              "MODULATION", "DELAY", "REVERB", "PRE / BOOST"};
/* default processing order (v29: each preset carries its own): the enum's, the signal order */
static const uint8_t ORDER_DEF[PX_NBLK] = {PX_B_GATE, PX_B_BST, PX_B_COMP, PX_B_PITCH, PX_B_SYN, PX_B_SFX, PX_B_EQ, PX_B_MOD,
                                           PX_B_DELAY, PX_B_REVERB};   /* (v75: BST, the pedals that boost and colour the guitar, goes in right after the gate) */
void px_order_default(uint8_t *o) { for (int i = 0; i < PX_NBLK; i++) o[i] = ORDER_DEF[i]; }
int px_order_valid(const uint8_t *o)
{
    unsigned seen = 0;
    for (int i = 0; i < PX_NBLK; i++) { if (o[i] >= PX_NBLK || (seen >> o[i]) & 1u) return 0; seen |= 1u << o[i]; }
    return 1;
}
typedef struct { const char *name; uint8_t np; const char *const *pn; } tdef;

static const char *const PN_GATE[] = {"THRESH", "RELEASE"};
static const char *const PN_SUPPRESSOR[] = {"THRESH", "RELEASE", "HUM", "HUSH"};
static const tdef T_GATE[] = {{"NOISE GATE", 2, PN_GATE}, {"NOISE SUPPRESSOR", 4, PN_SUPPRESSOR}};

static const char *const PN_CMP[] = {"SUSTAIN", "ATTACK", "LEVEL", "MIX"};
static const char *const PN_OPTO[] = {"COMPRESS", "LEVEL", "MIX"};
static const char *const PN_SWELL[] = {"RISE", "SENS", "LEVEL"};
static float valve_drive_db(float control) { return 24.0f * control * control; }  /* +6 dB at noon, +24 dB at full */
int px_valve_drive_db(int control) { return (int)(valve_drive_db((float)control * 0.01f) + 0.5f); }
static float valve_output_gain(uint8_t level)
{
    return 1.05074736f * db2lin(0.5f * ((int)level - (int)PX_VALVE_LEVEL_UNITY));  /* +0.43 dB calibration before Level */
}
static const tdef T_COMP[PX_CMP_COUNT] = {
    {"SQUASH", 3, PN_CMP}, {"STUDIO", 4, PN_CMP}, {"OPTO LA-2", 3, PN_OPTO}, {"SLOW SWELL", 3, PN_SWELL},
    {"", 0, 0}};   /* old VALVE type ID, migrated into the preset's input settings */
static int cmp_type(uint8_t t) { return t % PX_CMP_VALVE; }

static const char *const PN_OCT[] = {"DRY", "SUB -1", "SUB -2", "UP +1", "TONE"};
static const char *const PN_POLY[] = {"DRY", "OCT -1", "OCT +1"};
static const char *const PN_POLY_HQ[] = {"DRY", "OCT -1", "OCT +1", "RANGE", "OCT -2"};   /* v64: OCT -2 (PX_SUB2_TEST) */
static const char *const PN_WHAM[] = {"SHIFT", "MIX", "GLIDE"};
static const char *const PN_HARM[] = {"INTERVAL", "MIX"};
static const char *const PN_DET[] = {"CENTS", "MIX"};
static const char *const PN_PSH[] = {"SHIFT", "FINE", "MIX", "ATTACK"};   /* v75 POLY SHIFTER */
static const tdef T_PITCH[PX_PIT_COUNT] = {
    {"OCTAVER", 5, PN_OCT},
    {"WHAMMY", 3, PN_WHAM}, {"HARMONIZER", 2, PN_HARM},
    {"DETUNE", 2, PN_DET}, {"POLY OCTAVE HQ", 5, PN_POLY_HQ},
    {"LOWEST OCTAVE HQ", 4, PN_POLY_HQ},   /* DRY, OCT -1, OCT +1, RANGE (the same note cut as POLY OCTAVE HQ) */
    {"POLY SHIFTER", 4, PN_PSH},           /* v75: SHIFT -12..+12 semitones, FINE -50..+50 cents, MIX, ATTACK (how readily a pick restarts the phases) */
    {"", 0, 0},                          /* retired Drop Tune ID */
};

static const char *const PN_AWAH[] = {"SENS", "RESO", "RANGE", "DECAY"};
static const char *const PN_LWAH[] = {"RATE", "DEPTH", "RESO", "CENTER"};
static const char *const PN_RING[] = {"FREQ", "MIX", "WOBBLE"};
static const char *const PN_CRUSH[] = {"BITS", "SR DIV", "MIX"};
static const char *const PN_SLICE[] = {"RATE", "DEPTH", "PATTERN"};
static const char *const PN_LOFI[] = {"TONE", "DRIVE", "MIX"};
static const char *const PN_SYN[] = {"SWEEP", "RESO", "SUB", "MIX"};
static const char *const PN_RM[] = {"VOLUME", "PICKUP", "BODY"};   /* RANGEMASTER: VOLUME (unity at 50), PICKUP is the source (single coil 0 .. humbucker 100), BODY the input capacitor (5 nF treble booster .. 100 nF full range): engine/px_pedals.h */
static const char *const PN_EP[] = {"GAIN", "VOICE"};   /* EP BOOST: the 500k log volume pot; VOICE is the source bypass (EARLY .. LATE .. FAT): engine/px_pedals.h */
static const char *const PN_BBE[] = {"LO CONTOUR", "PROCESS"};   /* MAXIMIZER: the BBE process, fitted to the real unit: engine/px_pedals.h */
static const char *const PN_VW[] = {"POSITION", "PEAK", "LEVEL"};   /* BST VOX FIXED WAH: the parked pedal's position (heel .. toe, shown in Hz), the peak's Q and height (R7), the boost 0..+12 dB: engine/px_pedals.h */
static const char *const PN_FF[] = {"FUZZ", "VOLUME", "PICKUP", "BIAS", "TRANSISTOR"};   /* BST FUZZ FACE: the 1k fuzz pot, the 500k volume, the source (single coil .. humbucker), the Q2 collector load (the bias trim), germanium or silicon */
static const char *const PN_AP[] = {"LOW CUT", "DEQUACK", "BODY", "NOTCH", "LEVEL"};   /* BST ACOUSTIC PREAMP (v76): the high-pass (Hz), the piezo de-quack (dip + shelf + attack dip), the body (below 50 cuts the box, above adds low and air), the feedback notch (0 = off, else Hz), -12..+12 dB */
static const char *const PN_SIGNATURE[] = {"BODY", "FOCUS", "RICHNESS", "PUNCH", "LEVEL"};
static const char *const PN_AD[] = {"DRIVE", "BLEND", "PIEZO", "TONE", "LEVEL"};   /* SFX ACOUSTIC DRIVE (v76): the clipper, the dry blend, the pre-clip shaping for a piezo (high-pass, low-pass, dip), the post low-pass, -12..+12 dB */
static const char *const PN_CROSS[] = {"CUTOFF", "RESONANCE", "SUB", "MIX"};
static const char *const PN_FORMANT[] = {"VOWEL", "RESONANCE", "SUB", "MIX"};
static const char *const PN_CROSS_OSC[] = {"CUTOFF", "RESONANCE", "LEVEL", "MIX"};
static const char *const PN_ENH[] = {"BETTER", "LOUDER"};   /* ENHANCE v4: amount (harmonics + compression), clean boost 0..+12 dB: engine/px_enhance.h */
static const tdef T_SFX[PX_SFX_COUNT] = {
    {"ENVELOPE WAH", 4, PN_AWAH}, {"AUTO WAH", 4, PN_LWAH}, {"RING MOD", 3, PN_RING}, {"BITCRUSHER", 3, PN_CRUSH},
    {"SLICER", 3, PN_SLICE}, {"LO-FI RADIO", 3, PN_LOFI}, {"SQUARE SYNTH", 4, PN_SYN},
    {"", 0, 0}, {"", 0, 0}, {"", 0, 0}, {"", 0, 0}, {"", 0, 0}, {"", 0, 0}, {"ACOUSTIC DRIVE", 5, PN_AD}, {"", 0, 0}, {"", 0, 0}, {"", 0, 0}, {"", 0, 0}, {"", 0, 0}, {"", 0, 0}, {"", 0, 0}};   /* CROSS OSC: retired 2026-11-27 (the owner prefers SQUARE) */

/* SYNTH block (Synth2): POLY OCT FUZZ, and the tombstones of every retired v29..v73 type ("" name: the UI skips it, the
 * engine bypasses it, its stored settings are kept; px_dsp.h) */
static const char *const PN_OCT_FUZZ[] = {"DOWN", "UP", "FUZZ", "TONE", "LEVEL"};
static const char *const PN_ORGAN[] = {"REGISTER", "PERC", "SUSTAIN", "TONE", "LEVEL"};
static const char *const PN_BS_PAD[] = {"TONE", "ATTACK", "RELEASE", "CHORUS", "LEVEL"};
static const char *const PN_BS_CHOIR[] = {"VOWEL", "ATTACK", "RELEASE", "CHORUS", "LEVEL"};
static const char *const PN_BS_PAD_OCT[] = {"TONE", "ATTACK", "RELEASE", "OCTAVE MIX", "LEVEL"};
static const char *const PN_BS_CHOIR_OCT[] = {"VOWEL", "ATTACK", "RELEASE", "OCTAVE MIX", "LEVEL"};
static const char *const PN_BS_BRASS[] = {"TONE", "BITE", "RELEASE", "WIDTH", "LEVEL"};
static const char *const PN_BS_PIPE[] = {"STOPS", "SWELL", "RELEASE", "TONE", "LEVEL"};
static const char *const PN_BS_LEAD[] = {"CUTOFF", "RESONANCE", "ENVELOPE", "SUB", "LEVEL"};
#define SYN_TOMB {"", 0, 0}
static const tdef T_SYN[PX_SYN_COUNT] = {
    SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB,
    SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB,
    {"POLY OCT FUZZ", 5, PN_OCT_FUZZ},
    SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB,
    SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB,   /* ANALOG .. CHOIR (mono tracker): retired 2026-11-27 */
    {"BANK ORGAN", 5, PN_ORGAN},
    {"STRINGS", 5, PN_BS_PAD}, {"CHOIR PAD", 5, PN_BS_CHOIR}, {"BRASS", 5, PN_BS_BRASS}, {"PIPE ORGAN", 5, PN_BS_PIPE},
    SYN_TOMB, SYN_TOMB,                                /* 12-STRING, BELL PAD: retired 2026-11-28 */
    SYN_TOMB, SYN_TOMB, SYN_TOMB, SYN_TOMB,            /* SAW, PULSE, SUB BASS, ANALOG LEAD: retired 2026-11-28 */
    {"SQ BRASS", 5, PN_BS_LEAD}, {"SQ BASS", 5, PN_BS_LEAD}, {"SQ FLUTE", 5, PN_BS_LEAD},
    {"STRINGS OCTAVE", 5, PN_BS_PAD_OCT}, {"CHOIR OCTAVE", 5, PN_BS_CHOIR_OCT},
};
static const tdef T_BST[PX_BST_COUNT] = {{"RANGEMASTER", 3, PN_RM}, {"EP BOOST", 2, PN_EP}, {"MAXIMIZER", 2, PN_BBE}, {"FIXED WAH", 3, PN_VW}, {"ENHANCE", 2, PN_ENH}, {"FUZZ FACE", 5, PN_FF}, {"ACOUSTIC PRE", 5, PN_AP}, {"SIGNATURE", 5, PN_SIGNATURE}};
/* v75: the SFX types that moved to the BST block and the BST type each became (-1: still an SFX type). The one place that lists them
 * (docs/pxedit.js MOVED is the browser's copy). */
static int px_sfx_to_bst(int t)
{
    switch (t) {
    case PX_SFX_RANGEMASTER: return PX_BST_RANGEMASTER;
    case PX_SFX_EPBOOST:     return PX_BST_EPBOOST;
    case PX_SFX_MAXIMIZER:   return PX_BST_MAXIMIZER;
    case PX_SFX_ENHANCE:     return PX_BST_ENHANCE;
    default:                 return -1;
    }
}
int px_type_retired(int b, int t)
{
    return (b == PX_B_SYN && !px_syn_runs(t)) || (b == PX_B_SFX && (t == PX_SFX_APPLAUSE || t == PX_SFX_CROSS_OSC || t == PX_SFX_SYNTH_HQ || px_sfx_to_bst(t) >= 0)) ||
           (b == PX_B_COMP && t == PX_CMP_VALVE) || (b == PX_B_PITCH && t == PX_PIT_DROP);
}
int px_nlive(int b) { int n = 0; for (int t = 0; t < px_ntypes(b); t++) n += !px_type_retired(b, t); return n; }
int px_live_type(int b, int i)
{
    for (int t = 0, n = 0; t < px_ntypes(b); t++) if (!px_type_retired(b, t) && n++ == i) return t;
    return 0;
}
int px_live_index(int b, int type)
{
    for (int t = 0, n = 0; t < px_ntypes(b); t++) { if (t == type) return n; n += !px_type_retired(b, t); }
    return 0;
}

/* ============================================================== drives ===== */
enum { CL_SOFT, CL_ASYM, CL_HARD, CL_GE, CL_LED, CL_MOS, CL_MUFF, CL_FUZZ, CL_OCTA, CL_BOOST, CL_FUZZ_FACE };
enum { TM_LP, TM_MUFF, TM_HM2, TM_MT2 };
typedef struct {
    float hp, shelf, lp, mid, mid_db, mid_q10;                       /* pre-clip (base rate) */
    float g0, g1;                                                    /* gain range, dB */
    uint8_t clip, tm; float t0, t1;                                  /* clipper, post tone (oversampled) */
    float p2, p2db;                                                  /* fixed post filter (0 dB = low-pass) */
    float blend, bias, gate;                                         /* /100 */
} drv_v;          /* (floats: fitted to a capture in v34, the fitting tool since removed) */
/* the Fuzz Face voicing (v34 fit): POLY OCT FUZZ's clipper */
static const drv_v FUZZ_FACE = {28.0f, 0.0f, 7200.0f, 0.0f, 0.0f, 7.0f, 12.0f, 44.0f, CL_FUZZ_FACE, TM_LP, 6100.0f, 6100.0f,
                                0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

static float drive_gain(const drv_v *drive, float setting)
{
    return db2lin(drive->g0 + (drive->g1 - drive->g0) * setting);
}

/* ================================================================== EQ ===== */
static const char *const PN_EQ5[] = {"100 HZ", "400 HZ", "1 KHZ", "2.5 KHZ", "6 KHZ"};
static const char *const PN_GEQ[] = {"80 HZ", "240 HZ", "750 HZ", "2.2 KHZ", "6.6 KHZ"};
static const char *const PN_PEQ[] = {"LOW", "MID FREQ", "MID GAIN", "MID Q", "HIGH"};
static const tdef T_EQ[PX_EQ_COUNT] = {{"5 BAND", 5, PN_EQ5}, {"GRAPHIC 5", 5, PN_GEQ}, {"PARAMETRIC", 5, PN_PEQ}};

/* ========================================================== modulation ===== */
static const char *const PN_RDM[] = {"RATE", "DEPTH", "MIX"};
static const char *const PN_DIM[] = {"MODE", "MIX"};
static const char *const PN_FLG[] = {"RATE", "DEPTH", "MANUAL", "FDBK", "MIX"};
static const char *const PN_P90[] = {"SPEED", "DEPTH"};
static const char *const PN_STN[] = {"RATE", "COLOR", "DEPTH"};
static const char *const PN_UVB[] = {"SPEED", "INTENS", "MIX"};
static const char *const PN_RD[] = {"RATE", "DEPTH"};
static const char *const PN_BTR[] = {"RATE", "DEPTH", "SHAPE"};
static const char *const PN_HTR[] = {"RATE", "DEPTH", "TONE"};
static const char *const PN_ROT[] = {"SPEED", "DEPTH", "BALANCE"};
static const tdef T_MOD[PX_MOD_COUNT] = {
    {"CE CHORUS", 3, PN_RDM}, {"DIMENSION", 2, PN_DIM}, {"FLANGER", 5, PN_FLG}, {"PHASE 90", 2, PN_P90},
    {"SMALL STONE", 3, PN_STN}, {"UNI-VIBE", 3, PN_UVB}, {"OPTO TREMOLO", 2, PN_RD}, {"BIAS TREMOLO", 3, PN_BTR},
    {"HARMONIC TREM", 3, PN_HTR}, {"ROTARY", 3, PN_ROT}, {"VIBRATO", 2, PN_RD}};

/* =============================================================== delay ===== */
static const char *const PN_DT[] = {"TIME", "FDBK", "MIX", "TONE"};
static const char *const PN_DMM[] = {"TIME", "FDBK", "MIX", "RATE", "DEPTH"};
static const char *const PN_DTP[] = {"TIME", "FDBK", "MIX", "TONE", "WOW"};
static const char *const PN_DSP[] = {"TIME", "FDBK", "MIX", "HEADS", "WOW"};
static const char *const PN_DRV2[] = {"TIME", "FDBK", "MIX"};
static const char *const PN_DLF[] = {"TIME", "FDBK", "MIX", "GRIT"};
static const tdef T_DLY[PX_DLY_COUNT] = {
    {"DIGITAL", 4, PN_DT}, {"ANALOG BBD", 4, PN_DT}, {"MEMORY MAN", 5, PN_DMM}, {"TAPE EP-3", 5, PN_DTP},
    {"SPACE ECHO", 5, PN_DSP}, {"REVERSE", 3, PN_DRV2}, {"LO-FI", 4, PN_DLF}, {"DUAL TAP", 4, PN_DT}};

/* ============================================================== reverb ===== */
static const char *const PN_RV[] = {"DECAY", "TONE", "MIX"};
static const char *const PN_RSP[] = {"DECAY", "TONE", "MIX", "DRIP"};
static const char *const PN_RSH[] = {"DECAY", "TONE", "MIX", "SHIMMER"};
static const char *const PN_RGT[] = {"HOLD", "TONE", "MIX"};
static const tdef T_REV[PX_REV_COUNT] = {
    {"ROOM", 3, PN_RV}, {"HALL", 3, PN_RV}, {"CATHEDRAL", 3, PN_RV}, {"PLATE", 3, PN_RV}, {"SPRING", 4, PN_RSP},
    {"SHIMMER", 4, PN_RSH}, {"GATED", 3, PN_RGT}, {"AMBIENT", 3, PN_RV}};

/* ------------------------------------------------------------ catalog API --- */
const char *px_block_label(int b) { return (b >= 0 && b < PX_NBLK) ? BLK_LABEL[b] : "?"; }
const char *px_block_name(int b)  { return (b >= 0 && b < PX_NBLK) ? BLK_NAME[b] : "?"; }
int px_ntypes(int b)
{
    static const uint8_t N[PX_NBLK] = {PX_GATE_COUNT, PX_CMP_COUNT, PX_PIT_COUNT, PX_SYN_COUNT, PX_SFX_COUNT,
                                       PX_EQ_COUNT, PX_MOD_COUNT, PX_DLY_COUNT, PX_REV_COUNT, PX_BST_COUNT};
    return (b >= 0 && b < PX_NBLK) ? N[b] : 1;
}
/* v70: the retired types (v63/64: BASS SYNTH, PWM SYNTH HQ, TRUMPET MODEL, the delay-line POLY OCTAVE) are removed, so
 * every type is its own; kept for the callers (the store, MIDI, the screen) */
int px_type_canonical(int b, int t) { (void)b; return t; }
void px_block_canonical(int b, uint8_t *type, uint8_t *p) { (void)b; (void)type; (void)p; }
/* v75: ENHANCE (SFX type 9), RANGEMASTER, EP BOOST and MAXIMIZER (SFX types 10..12, test builds 929..931); they live in the BST block now. A preset that still holds one has it
 * moved (type, on/off and settings) when the BST block is still as the factory and the older formats leave it (RANGEMASTER, off, default settings); the SFX
 * block is then an ENVELOPE WAH, off, with its default settings. With BST already used (on, or another type or settings), the old SFX pedal stays as a
 * tombstone (off; the engine bypasses it) and BST is never overwritten. Idempotent. */
static int bst_untouched(const px_preset_t *p)   /* BST as a format 6 slot, an older editor file or the factory has it: RANGEMASTER, off, its defaults */
{
    static const uint8_t DEF[PX_NPAR] = {50, 0, 0, 0, 0};
    return !p->on[PX_B_BST] && p->type[PX_B_BST] == PX_BST_RANGEMASTER && !memcmp(p->p[PX_B_BST], DEF, PX_NPAR);
}
void px_preset_canonical(px_preset_t *p)
{
    if (p->type[PX_B_COMP] == PX_CMP_VALVE) {
        p->valve_on = p->on[PX_B_COMP];
        p->valve_drive = p->p[PX_B_COMP][0];
        p->valve_level = PX_VALVE_LEVEL_UNITY;
        p->on[PX_B_COMP] = 0;
        p->type[PX_B_COMP] = PX_CMP_SQUASH;
        px_type_defaults(PX_B_COMP, PX_CMP_SQUASH, p->p[PX_B_COMP]);
    }
    for (int b = 0; b < PX_NBLK; b++) px_block_canonical(b, &p->type[b], p->p[b]);
    const int to = px_sfx_to_bst(p->type[PX_B_SFX]);
    if (to >= 0 && bst_untouched(p)) {
        p->type[PX_B_BST] = (uint8_t)to;
        p->on[PX_B_BST] = p->on[PX_B_SFX];
        memcpy(p->p[PX_B_BST], p->p[PX_B_SFX], PX_NPAR);
        p->on[PX_B_SFX] = 0; p->type[PX_B_SFX] = PX_SFX_AUTOWAH;
        static const uint8_t DEF[PX_NPAR] = {60, 60, 60, 40, 0};
        memcpy(p->p[PX_B_SFX], DEF, PX_NPAR);
    }
    /* v75: a retired type (the SYNTH types of v29..v73, SFX APPLAUSE and the SFX pedals that moved to BST) is never shown or kept: the block becomes the first live type of
     * its block, switched off, its settings as they were. The screen and the editor have no "retired type" any more (the engine still bypasses a tombstone it is
     * handed directly). Idempotent. */
    for (int b = 0; b < PX_NBLK; b++)
        if (px_type_retired(b, p->type[b])) { p->on[b] = 0; p->type[b] = (uint8_t)px_live_type(b, 0); }
}
static const tdef *tdef_of(int b, int t)
{
    switch (b) {
    case PX_B_GATE: return &T_GATE[t];
    case PX_B_COMP: return &T_COMP[t];
    case PX_B_PITCH: return &T_PITCH[t];
    case PX_B_SFX: return &T_SFX[t];
    case PX_B_EQ: return &T_EQ[t];
    case PX_B_MOD: return &T_MOD[t];
    case PX_B_DELAY: return &T_DLY[t];
    case PX_B_REVERB: return &T_REV[t];
    case PX_B_SYN: return &T_SYN[t];
    case PX_B_BST: return &T_BST[t];
    }
    return 0;
}
const char *px_type_name(int b, int t)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    const tdef *d = tdef_of(b, t);
    return d ? d->name : "-";
}
int px_nparams(int b, int t)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    const tdef *d = tdef_of(b, t);
    return d ? d->np : 0;
}
const char *px_param_name(int b, int t, int k)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    if (k < 0 || k >= px_nparams(b, t)) return "";
    return tdef_of(b, t)->pn[k];
}

/* value text for knobs where a number 0..100 says little (intervals, ms, bits...) */
static const int8_t HARM_ST[10] = {-12, -7, -5, -4, -3, 3, 4, 5, 7, 12};
static const char *const HARM_TXT[10] = {"-OCT", "-5TH", "-4TH", "-3RD", "-m3", "+m3", "+3RD", "+4TH", "+5TH", "+OCT"};
static const char *const HEAD_TXT[7] = {"1", "2", "3", "1+2", "2+3", "1+3", "ALL"};
static int harm_idx(int v) { int i = v * 10 / 101; return i < 0 ? 0 : (i > 9 ? 9 : i); }
/* TIME (v35): 20..1000 ms in steps of 12.25 ms up to 80, then to 2 s in steps of 50 ms; REVERSE: 250..1000 ms */
static float dly_ms(int t, int v)
{
    if (t == PX_DLY_REVERSE) return 250.0f + (float)v * 7.5f;
    return v <= 80 ? 20.0f + (float)v * 12.25f : 1000.0f + (float)(v - 80) * 50.0f;
}
static void put_int(char *b, int v, int plus)
{
    char t[8]; int n = 0, neg = v < 0;
    if (neg) v = -v;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 6);
    int i = 0;
    if (neg) b[i++] = '-'; else if (plus && n && !(n == 1 && t[0] == '0')) b[i++] = '+';
    while (n) b[i++] = t[--n];
    b[i] = 0;
}
static void put_str(char *b, const char *s) { int i = 0; while (s[i] && i < 7) { b[i] = s[i]; i++; } b[i] = 0; }
static const char *const NOTE_TXT[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/* RANGE of POLY OCTAVE HQ, as the Boss OC-3's RANGE (restored from v72; v73 had replaced it): the octave-down is tapered out above the chosen note, B1..E5.
 * Zero preserves the full range in presets created before the control existed. */
static int erb_range_note(int value) { return 35 + (value - 1) * 42 / 100; } /* B1–E5 */

static float erb_range_band(int value)
{
    if (value == 0) return ERB_FULL;
    const float hz = 440.0f * powf(2.0f, ((float)erb_range_note(value) + 0.5f - 69.0f) / 12.0f);
    /* v49: the band position of hz in the bank (fractional, between its centres) */
    float c = ERB_LOW, i = 0.0f;
    while (i < ERB_FULL - 1.0f) { const float next = c + ERB_O_S0 + ERB_O_SC * c; if (hz < next) return i + (hz - c) / (next - c); c = next; i += 1.0f; }
    return i;
}

static float syn_fuzz_level_db(float level)
{
    return (level - 0.5f) * (level < 0.5f ? 24.0f : 12.0f);
}
/* presets saved before v34 (store format 1 / 2): the old amp / drive / cab numbers -> the nearest of the v34 set */
/* the TIME knob value (0..100) whose delay is nearest to `ms` (MIDI clock sync) */
int px_dly_value_for_ms(int type, float ms)
{
    int best = 0;
    float bd = 1e30f;
    for (int v = 0; v <= 100; v++) {
        float d = dly_ms(type, v) - ms;
        if (d < 0.0f) d = -d;
        if (d < bd) { bd = d; best = v; }
    }
    return best;
}
int px_param_text(int b, int t, int k, int v, char *buf)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    float x = (float)v * 0.01f;
    switch (b) {
    case PX_B_GATE:
        if (t == PX_GATE_SUPPRESSOR && k == 2) {
            static const char *const names[] = {"OFF", "50 HZ", "60 HZ", "AUTO"};
            put_str(buf, names[v / 26]); return 1;
        }
        break;
    case PX_B_PITCH:
        if ((t == PX_PIT_POLYOCT_HQ || t == PX_PIT_LOWEST_HQ) && k == 3) {
            if (v == 0) put_str(buf, "FULL");
            else {
                const int note = erb_range_note(v);
                const char *name = NOTE_TXT[note % 12];
                put_str(buf, name);
                put_int(buf + (name[1] ? 2 : 1), note / 12 - 1, 0);
            }
            return 1;
        }
        if (t == PX_PIT_POLYSHIFT && k == 0) { put_int(buf, (int)(x * 24.0f + 0.5f) - 12, 1); return 1; }              /* SHIFT in semitones */
        if (t == PX_PIT_POLYSHIFT && k == 1) { put_int(buf, (int)((x - 0.5f) * 100.0f + (x >= 0.5f ? 0.5f : -0.5f)), 1); return 1; }   /* FINE in cents */
        if (t == PX_PIT_HARMONY && k == 0) { put_str(buf, HARM_TXT[harm_idx(v)]); return 1; }
        if (t == PX_PIT_WHAMMY && k == 0) { float st = (x - 0.5f) * 48.0f; put_int(buf, (int)(st + (st >= 0 ? 0.5f : -0.5f)), 1); return 1; }
        if (t == PX_PIT_DETUNE && k == 0) { put_int(buf, (int)(3.0f + x * 27.0f + 0.5f), 0); return 1; }
        break;
    case PX_B_SFX:
        if (t == PX_SFX_CRUSH && k == 0) { put_int(buf, 16 - (int)(x * 14.0f + 0.5f), 0); return 1; }
        if (t == PX_SFX_CRUSH && k == 1) { put_int(buf, 1 + (int)(x * 39.0f + 0.5f), 0); return 1; }
        if (t == PX_SFX_CROSS_OSC && k == 0) { static const char *const waves[] = {"SQUARE", "PULSE", "TRI", "SAW", "BRASS", "STRING", "FLUTE"}; put_str(buf, waves[v * 7 / 101]); return 1; }
        if (t == PX_SFX_ACOUSTIC && k == 4) { put_int(buf, (int)((x - 0.5f) * 24.0f + (x >= 0.5f ? 0.5f : -0.5f)), 1); return 1; }   /* LEVEL in dB */
        if (t == PX_SFX_RING && k == 0) { put_int(buf, (int)(20.0f * powf(100.0f, x) + 0.5f), 0); return 1; }
        if (t == PX_SFX_SLICER && k == 2) { buf[0] = 'P'; put_int(buf + 1, 1 + (int)(x * 7.99f), 0); return 1; }
        break;
    case PX_B_BST:
        if (t == PX_BST_EPBOOST && k == 1) { const int v = (int)(x * 100.0f + 0.5f); put_str(buf, v < 34 ? "EARLY" : v < 67 ? "LATE" : "FAT"); return 1; }   /* VOICE */
        if (t == PX_BST_ACOUSTIC && k == 0) { put_int(buf, (int)(20.0f * powf(10.0f, x) + 0.5f), 0); return 1; }   /* LOW CUT in Hz */
        if (t == PX_BST_ACOUSTIC && k == 3) { if (x < 0.005f) put_str(buf, "OFF"); else put_int(buf, (int)(80.0f * powf(50.0f, x) + 0.5f), 0); return 1; }   /* NOTCH in Hz */
        if ((t == PX_BST_ACOUSTIC || t == PX_BST_SIGNATURE) && k == 4) { put_int(buf, (int)((x - 0.5f) * 24.0f + (x >= 0.5f ? 0.5f : -0.5f)), 1); return 1; }   /* LEVEL in dB */
        if (t == PX_BST_RANGEMASTER && k == 2) { const int v = (int)(x * 100.0f + 0.5f); put_str(buf, v < 34 ? "TREBLE" : v < 67 ? "MID" : "FULL"); return 1; }   /* BODY */
        if (t == PX_BST_FUZZFACE && k == 2) { const int v2 = (int)(x * 100.0f + 0.5f); put_str(buf, v2 < 34 ? "SINGLE" : v2 < 67 ? "P90" : "HUMBKR"); return 1; }   /* PICKUP */
        if (t == PX_BST_FUZZFACE && k == 4) { const int v2 = (int)(x * 100.0f + 0.5f); put_str(buf, v2 > 50 ? "SILICON" : "GERMAN"); return 1; }   /* TRANSISTOR */
        if (t == PX_BST_RANGEMASTER && k == 1) { const int v = (int)(x * 100.0f + 0.5f); put_str(buf, v < 34 ? "SINGLE" : v < 67 ? "P90" : "HUMBKR"); return 1; }   /* PICKUP */
        if (t == PX_BST_ENHANCE && k == 1) { put_int(buf, (int)(x * 12.0f + 0.5f), 1); return 1; }   /* LOUDER in dB */
        if (t == PX_BST_VOXWAH && k == 0) { put_int(buf, (int)(pxp_vw_hz(x) + 0.5f), 0); return 1; }   /* POSITION: the peak's frequency in Hz */
        if (t == PX_BST_VOXWAH && k == 2) { put_int(buf, (int)(x * 12.0f + 0.5f), 1); return 1; }   /* LEVEL in dB */
        break;
    case PX_B_EQ:
        if (t == PX_EQ_PARAM && k == 1) { put_int(buf, (int)(150.0f * powf(33.0f, x) + 0.5f), 0); return 1; }
        if (t == PX_EQ_PARAM && k == 3) { int q = (int)((0.4f + x * 5.6f) * 10.0f + 0.5f);
            buf[0] = (char)('0' + q / 10); buf[1] = '.'; buf[2] = (char)('0' + q % 10); buf[3] = 0; return 1; }
        { float db = (x - 0.5f) * 24.0f; put_int(buf, (int)(db + (db >= 0 ? 0.5f : -0.5f)), 1); return 1; }
    case PX_B_MOD:
        if (t == PX_MOD_DIMENSION && k == 0) { put_int(buf, 1 + (int)(x * 3.99f), 0); return 1; }
        if (t == PX_MOD_ROTARY && k == 0) { put_str(buf, v < 50 ? "SLOW" : "FAST"); return 1; }
        break;
    case PX_B_SYN:
        if (t == PX_SYN_ORGAN && k == 0) { static const char *const r[] = {"JAZZ", "FULL", "GOSPEL", "CHURCH"}; put_str(buf, r[v >= 75 ? 3 : v >= 50 ? 2 : v >= 25 ? 1 : 0]); return 1; }
        if ((t == PX_SYN_BS_CHOIR || t == PX_SYN_BS_CHOIR_OCT) && k == 0) { static const char *const r[] = {"AH", "OH", "OO", "EH", "EE"}; put_str(buf, r[v * 5 / 101]); return 1; }
        if (t == PX_SYN_BS_PIPE && k == 0) { static const char *const r[] = {"FLUTES", "DIAPASON", "FULL", "MIXTURE"}; put_str(buf, r[v * 4 / 101]); return 1; }
        break;
    case PX_B_DELAY:
        if (k == 0) { put_int(buf, (int)(dly_ms(t, v) + 0.5f), 0); int i = 0; while (buf[i]) i++;
                      buf[i] = ' '; buf[i + 1] = 'M'; buf[i + 2] = 'S'; buf[i + 3] = 0; return 1; }   /* "450 MS" */
        if (t == PX_DLY_SPACE && k == 3) { put_str(buf, HEAD_TXT[(int)(x * 6.99f)]); return 1; }
        break;
    }
    return 0;
}

#include "px_trim.h"                                   /* each type's level at noon (calibrated on PC) */

/* ============================================================= presets ===== */
static void blk(px_preset_t *p, int b, int on, int type, int a, int c, int d, int e, int f)
{
    p->on[b] = (uint8_t)on; p->type[b] = (uint8_t)type;
    p->p[b][0] = (uint8_t)a; p->p[b][1] = (uint8_t)c; p->p[b][2] = (uint8_t)d; p->p[b][3] = (uint8_t)e; p->p[b][4] = (uint8_t)f;
}
static void base_preset(px_preset_t *p)
{
    memset(p, 0, sizeof(*p));
    p->patch_volume = 50;
    p->valve_drive = PX_VALVE_DEFAULT;
    p->valve_level = PX_VALVE_LEVEL_UNITY;
    blk(p, PX_B_GATE,   1, 0, 40, 20, 0, 0, 0);
    blk(p, PX_B_COMP,   0, PX_CMP_SQUASH, 50, 30, 50, 100, 0);
    blk(p, PX_B_PITCH,  0, PX_PIT_OCTAVER, 70, 50, 0, 0, 40);
    blk(p, PX_B_SFX,    0, PX_SFX_AUTOWAH, 60, 60, 60, 40, 0);
    blk(p, PX_B_EQ,     0, PX_EQ_5BAND, 50, 50, 50, 50, 50);
    blk(p, PX_B_MOD,    0, PX_MOD_CHORUS, 30, 40, 40, 30, 50);
    blk(p, PX_B_DELAY,  0, PX_DLY_DIGITAL, 28, 30, 25, 50, 30);
    blk(p, PX_B_REVERB, 0, PX_REV_HALL, 50, 50, 20, 40, 0);
    blk(p, PX_B_SYN,    0, PX_SYN_OCT_FUZZ, 50, 70, 40, 30, 50);   /* (v76: LEVEL 50 = 0 dB; 0 was -12 dB, a quiet octave fuzz in every new preset) */
    blk(p, PX_B_BST,    0, PX_BST_RANGEMASTER, 50, 0, 0, 0, 0);
    px_order_default(p->order);
}
static void setname(px_preset_t *p, const char *s)
{
    int i = 0;
    for (; s[i] && i < (int)sizeof(p->name) - 1; i++) p->name[i] = s[i];
    p->name[i] = 0;
}
enum { NPRESETS = 52 };
int px_preset_count(void) { return NPRESETS; }
void px_preset_base(px_preset_t *p) { base_preset(p); }
/* Factory presets (v75: a new bank of 52, in six groups; tools/research/preset_levels.c matches their loudness). Each is the blank base (the gate on, everything else off)
 * with up to six blocks switched on; lvl is its PRESET LEVEL (the gate block's 3rd setting, 50 = 0 dB, 0.3 dB a step; 0 here means 50) as calibrated, so that every sound
 * plays as loud as the guitar alone at noon (the mean over the five bundled loops):
 *   01..10 clean and boost (RANGEMASTER, EP BOOST, MAXIMIZER, the parked FIXED WAH, ENHANCE)   11..18 pitch (the octaves, WHAMMY, HARMONIZER, DETUNE, POLY SHIFTER)
 *   19..30 synths   31..43 modulation, delays and reverbs   44..51 special effects, SLOW SWELL and the hum filter   52 INIT: a blank to start from */
typedef struct { const char *name; uint8_t lvl; struct { int8_t b1; uint8_t type, p[5]; } b[6]; } fact_spec;   /* b1: the block + 1 (0: none) */
#define FB(blk, type, a, b, c, d, e) {(blk) + 1, type, {a, b, c, d, e}}
static const fact_spec FACT[] = {
    /* 01..10 clean and boost */
    {"CLEAN HALL",    48, {FB(PX_B_COMP, PX_CMP_SQUASH, 35, 40, 50, 100, 0), FB(PX_B_REVERB, PX_REV_HALL, 45, 55, 18, 0, 0)}},
    {"STUDIO CLEAN",  46, {FB(PX_B_COMP, PX_CMP_STUDIO, 40, 35, 50, 55, 0), FB(PX_B_EQ, PX_EQ_PARAM, 52, 45, 58, 45, 55), FB(PX_B_REVERB, PX_REV_ROOM, 30, 55, 14, 0, 0)}},
    {"TREBLE BOOST",  42, {FB(PX_B_BST, PX_BST_RANGEMASTER, 58, 25, 10, 0, 0), FB(PX_B_DELAY, PX_DLY_ANALOG, 38, 32, 16, 50, 0), FB(PX_B_REVERB, PX_REV_PLATE, 40, 50, 14, 0, 0)}},
    {"RM FULL BODY",  43, {FB(PX_B_BST, PX_BST_RANGEMASTER, 52, 65, 80, 0, 0), FB(PX_B_MOD, PX_MOD_CHORUS, 22, 40, 30, 0, 0), FB(PX_B_REVERB, PX_REV_HALL, 40, 50, 15, 0, 0)}},
    {"EP PUSH",       32, {FB(PX_B_BST, PX_BST_EPBOOST, 40, 30, 0, 0, 0), FB(PX_B_MOD, PX_MOD_CHORUS, 28, 45, 35, 0, 0), FB(PX_B_REVERB, PX_REV_SPRING, 45, 50, 18, 50, 0)}},
    {"EP FAT",        29, {FB(PX_B_BST, PX_BST_EPBOOST, 62, 92, 0, 0, 0), FB(PX_B_DELAY, PX_DLY_TAPE, 36, 32, 18, 50, 25), FB(PX_B_REVERB, PX_REV_HALL, 45, 50, 16, 0, 0)}},
    {"BBE MAX",       42, {FB(PX_B_BST, PX_BST_MAXIMIZER, 45, 60, 0, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 30, 55, 12, 0, 0)}},
    {"WAH PARKED LO", 39, {FB(PX_B_BST, PX_BST_VOXWAH, 22, 55, 20, 0, 0), FB(PX_B_DELAY, PX_DLY_DIGITAL, 30, 25, 15, 50, 0)}},
    {"WAH PARKED HI", 35, {FB(PX_B_BST, PX_BST_VOXWAH, 72, 62, 20, 0, 0), FB(PX_B_REVERB, PX_REV_PLATE, 40, 50, 14, 0, 0)}},
    {"ENHANCED",      38, {FB(PX_B_BST, PX_BST_ENHANCE, 55, 20, 0, 0, 0), FB(PX_B_COMP, PX_CMP_OPTO, 40, 50, 60, 0, 0), FB(PX_B_REVERB, PX_REV_HALL, 40, 55, 14, 0, 0)}},
    /* 11..18 pitch */
    {"POLY OCTAVES",  52, {FB(PX_B_PITCH, PX_PIT_POLYOCT_HQ, 55, 60, 45, 100, 40), FB(PX_B_REVERB, PX_REV_HALL, 45, 55, 22, 0, 0)}},
    {"LOWEST OCT",    37, {FB(PX_B_PITCH, PX_PIT_LOWEST_HQ, 55, 70, 0, 100, 0), FB(PX_B_REVERB, PX_REV_ROOM, 28, 50, 12, 0, 0)}},
    {"WHAMMY +12",    52, {FB(PX_B_PITCH, PX_PIT_WHAMMY, 75, 50, 35, 0, 0), FB(PX_B_DELAY, PX_DLY_DIGITAL, 30, 30, 18, 50, 0)}},
    {"HARMONY +3RD",  45, {FB(PX_B_PITCH, PX_PIT_HARMONY, 66, 60, 0, 0, 0), FB(PX_B_REVERB, PX_REV_HALL, 40, 55, 15, 0, 0)}},
    {"HARMONY +5TH",  52, {FB(PX_B_PITCH, PX_PIT_HARMONY, 86, 55, 0, 0, 0), FB(PX_B_DELAY, PX_DLY_ANALOG, 32, 28, 15, 50, 0)}},
    {"DETUNE",        47, {FB(PX_B_PITCH, PX_PIT_DETUNE, 45, 50, 0, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 30, 50, 12, 0, 0)}},
    {"POLY SHIFT +7", 56, {FB(PX_B_PITCH, PX_PIT_POLYSHIFT, 79, 50, 55, 50, 0), FB(PX_B_REVERB, PX_REV_HALL, 45, 50, 20, 0, 0)}},
    {"SHIFT -12 PAD", 56, {FB(PX_B_PITCH, PX_PIT_POLYSHIFT, 0, 50, 70, 50, 0), FB(PX_B_MOD, PX_MOD_CHORUS, 22, 40, 30, 0, 0), FB(PX_B_REVERB, PX_REV_SHIMMER, 60, 50, 35, 40, 0)}},
    /* 19..30 synths (the first ten as the synth showcase: docs/presets/synth-showcase.json) */
    {"SQUARE LEAD",   32, {FB(PX_B_SFX, PX_SFX_SYNTH, 60, 40, 55, 100, 0), FB(PX_B_DELAY, PX_DLY_ANALOG, 45, 35, 25, 50, 0), FB(PX_B_REVERB, PX_REV_PLATE, 40, 50, 20, 0, 0)}},
    {"JAZZ ORGAN",    32, {FB(PX_B_SYN, PX_SYN_ORGAN, 10, 50, 25, 50, 50), FB(PX_B_MOD, PX_MOD_ROTARY, 40, 60, 50, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 30, 50, 15, 0, 0)}},
    {"STRING PAD",    27, {FB(PX_B_SYN, PX_SYN_BS_STRINGS, 50, 35, 50, 50, 50), FB(PX_B_REVERB, PX_REV_HALL, 60, 50, 30, 0, 0)}},
    {"CHOIR AAH",     44, {FB(PX_B_SYN, PX_SYN_BS_CHOIR, 5, 40, 55, 50, 50), FB(PX_B_REVERB, PX_REV_CATHEDRAL, 60, 45, 30, 0, 0)}},
    {"BRASS SECTION", 23, {FB(PX_B_SYN, PX_SYN_BS_BRASS, 50, 50, 40, 40, 50), FB(PX_B_REVERB, PX_REV_PLATE, 35, 50, 18, 0, 0)}},
    {"PIPE ORGAN",    27, {FB(PX_B_SYN, PX_SYN_BS_PIPE, 40, 30, 50, 50, 50), FB(PX_B_REVERB, PX_REV_CATHEDRAL, 65, 45, 35, 0, 0)}},
    {"FULL ORGAN",    29, {FB(PX_B_SYN, PX_SYN_ORGAN, 30, 60, 25, 55, 50), FB(PX_B_MOD, PX_MOD_ROTARY, 60, 60, 50, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 30, 50, 15, 0, 0)}},
    {"CHOIR OOH",     32, {FB(PX_B_SYN, PX_SYN_BS_CHOIR, 50, 45, 60, 60, 50), FB(PX_B_REVERB, PX_REV_CATHEDRAL, 60, 45, 30, 0, 0)}},
    {"SQ FLUTE",      35, {FB(PX_B_SYN, PX_SYN_SQ_FLUTE, 45, 20, 30, 0, 50), FB(PX_B_DELAY, PX_DLY_DIGITAL, 30, 35, 20, 50, 0), FB(PX_B_REVERB, PX_REV_HALL, 45, 50, 25, 0, 0)}},
    {"SQ BASS",       39, {FB(PX_B_SYN, PX_SYN_SQ_BASS, 30, 30, 50, 30, 50)}},
    {"SQ BRASS",      30, {FB(PX_B_SYN, PX_SYN_SQ_BRASS, 50, 40, 50, 0, 50), FB(PX_B_REVERB, PX_REV_PLATE, 35, 50, 18, 0, 0)}},
    {"POLY FUZZ",     24, {FB(PX_B_SYN, PX_SYN_OCT_FUZZ, 60, 50, 60, 50, 100), FB(PX_B_REVERB, PX_REV_ROOM, 30, 50, 15, 0, 0)}},
    /* 31..43 modulation, delays, reverbs */
    {"CE CHORUS",     46, {FB(PX_B_MOD, PX_MOD_CHORUS, 30, 50, 50, 0, 0), FB(PX_B_REVERB, PX_REV_HALL, 35, 55, 12, 0, 0)}},
    {"DIMENSION",     48, {FB(PX_B_MOD, PX_MOD_DIMENSION, 40, 60, 0, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 30, 50, 10, 0, 0)}},
    {"JET FLANGER",   51, {FB(PX_B_MOD, PX_MOD_FLANGER, 30, 60, 50, 45, 55)}},
    {"PHASE 90",      47, {FB(PX_B_MOD, PX_MOD_PHASE90, 40, 65, 0, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 25, 50, 10, 0, 0)}},
    {"UNI-VIBE",      45, {FB(PX_B_MOD, PX_MOD_UNIVIBE, 45, 65, 70, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 25, 50, 10, 0, 0)}},
    {"OPTO TREM",     49, {FB(PX_B_MOD, PX_MOD_OPTOTREM, 45, 60, 0, 0, 0), FB(PX_B_REVERB, PX_REV_SPRING, 45, 50, 18, 40, 0)}},
    {"ROTARY",        48, {FB(PX_B_MOD, PX_MOD_ROTARY, 40, 60, 55, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 25, 50, 12, 0, 0)}},
    {"SPRING SURF",   50, {FB(PX_B_MOD, PX_MOD_BIASTREM, 42, 55, 50, 0, 0), FB(PX_B_REVERB, PX_REV_SPRING, 55, 50, 35, 60, 0)}},
    {"SHIMMER PAD",   52, {FB(PX_B_REVERB, PX_REV_SHIMMER, 72, 50, 45, 50, 0), FB(PX_B_DELAY, PX_DLY_DIGITAL, 40, 35, 25, 50, 0)}},
    {"AMBIENT WASH",  52, {FB(PX_B_REVERB, PX_REV_AMBIENT, 72, 50, 40, 0, 0), FB(PX_B_DELAY, PX_DLY_DIGITAL, 45, 40, 25, 50, 0)}},
    {"TAPE ECHO",     49, {FB(PX_B_DELAY, PX_DLY_TAPE, 40, 42, 32, 50, 30), FB(PX_B_REVERB, PX_REV_ROOM, 28, 50, 10, 0, 0)}},
    {"SPACE ECHO",    47, {FB(PX_B_DELAY, PX_DLY_SPACE, 38, 45, 35, 50, 35), FB(PX_B_REVERB, PX_REV_SPRING, 40, 50, 14, 40, 0)}},
    {"REVERSE",       47, {FB(PX_B_DELAY, PX_DLY_REVERSE, 50, 35, 55, 0, 0), FB(PX_B_REVERB, PX_REV_HALL, 45, 55, 20, 0, 0)}},
    /* 44..51 special effects, SLOW SWELL, the hum filter */
    {"ENV WAH",       50, {FB(PX_B_COMP, PX_CMP_SQUASH, 40, 40, 50, 100, 0), FB(PX_B_SFX, PX_SFX_AUTOWAH, 60, 55, 60, 45, 0)}},
    {"LFO WAH",       52, {FB(PX_B_SFX, PX_SFX_LFOWAH, 40, 60, 55, 50, 0), FB(PX_B_DELAY, PX_DLY_ANALOG, 35, 28, 15, 50, 0)}},
    {"SLICER",        51, {FB(PX_B_SFX, PX_SFX_SLICER, 50, 75, 30, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 28, 50, 12, 0, 0)}},
    {"RING MOD",      46, {FB(PX_B_SFX, PX_SFX_RING, 45, 50, 30, 0, 0), FB(PX_B_REVERB, PX_REV_PLATE, 35, 50, 14, 0, 0)}},
    {"LO-FI RADIO",   41, {FB(PX_B_SFX, PX_SFX_LOFI, 45, 45, 70, 0, 0), FB(PX_B_REVERB, PX_REV_ROOM, 25, 50, 12, 0, 0)}},
    {"BIT CRUSH",     50, {FB(PX_B_SFX, PX_SFX_CRUSH, 45, 40, 50, 0, 0)}},
    {"SLOW SWELL",    52, {FB(PX_B_COMP, PX_CMP_SWELL, 55, 50, 50, 0, 0), FB(PX_B_REVERB, PX_REV_HALL, 55, 50, 25, 0, 0)}},
    {"NOISY ROOM",    48, {FB(PX_B_GATE, PX_GATE_SUPPRESSOR, 67, 20, 100, 0, 0), FB(PX_B_COMP, PX_CMP_STUDIO, 30, 35, 50, 50, 0), FB(PX_B_REVERB, PX_REV_ROOM, 25, 55, 12, 0, 0)}},
    /* 52 */
    {"INIT",          0, {{0}}},
};
void px_preset_factory(px_preset_t *p, int idx)
{
    base_preset(p);
    if (idx < 0 || idx >= NPRESETS) idx = 0;
    if (idx < (int)(sizeof FACT / sizeof FACT[0])) {
        setname(p, FACT[idx].name);
        for (int j = 0; j < 6; j++) {
            if (!FACT[idx].b[j].b1) continue;
            const int b = FACT[idx].b[j].b1 - 1;
            if (b != PX_B_GATE) p->on[b] = 1;
            p->type[b] = FACT[idx].b[j].type; memcpy(p->p[b], FACT[idx].b[j].p, 5);
        }
        p->patch_volume = FACT[idx].lvl ? FACT[idx].lvl : 50;
        return;
    }
    char n[14] = "INIT ";
    n[5] = (char)('0' + (idx + 1) / 10); n[6] = (char)('0' + (idx + 1) % 10); n[7] = 0;
    setname(p, n);
}

/* ============================================================ recalc ======= */
/* reverb (v35). FDN: 8 lines (prime lengths at 44.1 kHz, x the type's size), Hadamard feedback, each line with its own
 * low / high frequency decay (Jot); the first 4 lines move slowly (chorus in the tail: no metallic ringing).
 * Plate: J. Dattorro, "Effect Design Part 1" (JAES 1997), lengths at 29761 Hz. Both: Dattorro's input diffusers. */
static const int RV_FDN[8] = {1171, 1373, 1601, 1847, 2111, 2393, 2689, 3011};
static const int RV_DIF[4] = {142, 107, 379, 277};
static const float RV_DIFG[4] = {0.75f, 0.75f, 0.625f, 0.625f};
static const int PL_LEN[8] = {672, 4453, 1800, 3720, 908, 4217, 2656, 3163};
static const int PL_TAP[14] = {266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121};
static const uint8_t PL_TU[14] = {10, 10, 11, 12, 6, 7, 8, 6, 6, 7, 8, 10, 11, 12};
static const float ER_T[4] = {0.11f, 0.31f, 0.57f, 1.0f};
static const float ER_G[4] = {0.95f, -0.72f, 0.5f, -0.33f};
typedef struct { float size, pre_ms, er, er_ms, mod, rt0, rt1, hf0, hf1, out; } rv_def;
static const rv_def RVD[PX_REV_COUNT] = {
    /* size  pre   ER  ER ms mod (samples at 44.1 kHz)  RT min +range  HF decay ratio  output */
    {0.30f,  4, 0.70f, 25,  4, 0.3f, 0.9f,  0.15f, 0.60f, 0.502f},   /* ROOM */
    {0.85f, 18, 0.35f, 60, 20, 1.2f, 2.8f,  0.15f, 0.60f, 0.475f},   /* HALL */
    {1.35f, 35, 0.25f, 90, 24, 3.0f, 7.0f,  0.12f, 0.55f, 0.393f},   /* CATHEDRAL */
    {1.0f,   0, 0.0f,   0, 16, 0.6f, 4.4f,  0.0f,  0.0f,  1.478f},   /* PLATE (size: Dattorro's) */
    {0.40f,  0, 0.0f,   0,  2, 1.5f, 2.5f,  0.10f, 0.40f, 0.279f},   /* SPRING */
    {1.0f,  25, 0.0f,   0, 20, 2.0f, 6.0f,  0.20f, 0.60f, 0.368f},   /* SHIMMER */
    {0.55f,  3, 0.50f, 30,  8, 2.5f, 0.0f,  0.30f, 0.50f, 0.468f},   /* GATED */
    {1.35f, 30, 0.0f,   0, 28, 6.0f, 14.0f, 0.10f, 0.50f, 0.368f}};  /* AMBIENT */
static const uint16_t SLICE_PAT[8] = {0xAAAA, 0xCCCC, 0xF0F0, 0xB6B6, 0x9249, 0xEEEE, 0xDB6D, 0x8888};

static void triode_reset(px_engine *e)
{
    memset(&e->triode.up, 0, sizeof e->triode.up);
    memset(&e->triode.down, 0, sizeof e->triode.down);
    e->triode.cathode_v = PXT_BIAS;
    e->triode.output_dc = 0.0f;
    e->triode.cathode_step = onepole_k(2.0f * e->sr, 0.0022f * PXT_CATHODE_KOHM);
    e->triode.dc_step = onepole_k(2.0f * e->sr, 0.022f);
}

static void rc_dyn(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_GATE];
    e->gate_thr = db2lin(-80.0f + f01(q[0]) * 60.0f);
    e->gate_rel = expf(-1.0f / (0.001f * (5.0f + f01(q[1]) * 495.0f) * sr));

    const int active = p->type[PX_B_GATE] == PX_GATE_SUPPRESSOR;
    e->suppressor.active = active;
    e->suppressor.disabled = q[0] == 0;
    e->suppressor.hush = f01(q[3]);
    e->suppressor.peak_rel = expf(-1.0f / (0.018f * sr));
    e->suppressor.sustain_rel = expf(-1.0f / (0.080f * sr));
    e->suppressor.learn = onepole_k(sr, 8.0f);
    e->suppressor.fall = onepole_k(sr, 0.15f);
    e->suppressor.attack = onepole_k(sr, 0.00015f);
    e->suppressor.release = onepole_k(sr, 0.030f + 0.970f * f01(q[1]));
    e->suppressor.sensitivity = db2lin(-80.0f + 52.0f * f01(q[0]));
    if (e->suppressor.floor > e->suppressor.sensitivity * 0.5f) e->suppressor.floor = e->suppressor.sensitivity * 0.5f;
    e->suppressor.split_k = onepole_k(sr, 0.000053f); /* fixed 3 kHz complementary split */
    e->suppressor.hold = (unsigned)(0.035f * sr);
    /* Keep delayed pitch output alive without adding audio latency. */
    for (int i = 0; i < PX_NBLK; ++i) {
        if (p->order[i] == PX_B_GATE) break;
        if (p->order[i] == PX_B_PITCH && p->on[PX_B_PITCH]) e->suppressor.hold += (unsigned)(0.100f * sr);
    }
    e->hum_mode = active && p->on[PX_B_GATE] ? q[2] / 26 : 0;

    q = p->p[PX_B_COMP];
    const int t = cmp_type(p->type[PX_B_COMP]);
    e->cmp_rel = expf(-1.0f / (0.15f * sr));
    e->cmp_mix = 1.0f;
    switch (t) {
    case PX_CMP_SQUASH:
        e->cmp_k = f01(q[0]) * 10.0f;
        e->cmp_att = expf(-1.0f / (0.001f * (1.0f + f01(q[1]) * 49.0f) * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        break;
    case PX_CMP_STUDIO:
        e->cmp_k = f01(q[0]) * 4.0f;
        e->cmp_att = expf(-1.0f / (0.001f * (1.0f + f01(q[1]) * 49.0f) * sr));
        e->cmp_rel = expf(-1.0f / (0.2f * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        e->cmp_mix = f01(q[3]);
        break;
    case PX_CMP_OPTO:                                  /* optical cell: 10 ms attack, slow program-like release */
        e->cmp_k = f01(q[0]) * 6.0f;
        e->cmp_att = expf(-1.0f / (0.010f * sr));
        e->cmp_rel = expf(-1.0f / (0.6f * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[1]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        e->cmp_mix = f01(q[2]);
        break;
    default:                                           /* slow gear: volume swell after each pick attack */
        e->cmp_rise = onepole_k(sr, 0.05f + f01(q[0]) * f01(q[0]) * 1.95f);
        e->cmp_thr = db2lin(-30.0f - f01(q[1]) * 30.0f);
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f);
        e->cmp_att = expf(-1.0f / (0.001f * sr));
        e->cmp_rel = expf(-1.0f / (0.05f * sr));
        e->cmp_slowk = onepole_k(sr, 0.03f);
        break;
    }
}

#include "px_banksyn.h"                                 /* the waveform-following synths */

static void rc_pitch(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_PITCH];
    const int raw = p->type[PX_B_PITCH] % PX_PIT_COUNT;
    int t = px_type_canonical(PX_B_PITCH, raw);
    int hq = t == PX_PIT_POLYOCT_HQ;
    hq |= t == PX_PIT_LOWEST_HQ;
    if (hq && !e->erb_pitch_active) {
        erb_prepare(e);
        memset(&e->erb_pitch, 0, sizeof e->erb_pitch);
    }
    e->erb_pitch_active = hq;
    e->ps_nv = 0; e->ps_glide = 1.0f;
    switch (t) {
    case PX_PIT_OCTAVER:                               /* analog: flip-flop divider (-1, -2) + rectifier (+1) */
        bq_lowpass(&e->oct_lp, sr, 500.0f, 0.7f);
        bq_lowpass(&e->oct_lp2, sr, 500.0f, 0.7f);
        bq_lowpass(&e->oct_sublp, sr, 150.0f + f01(q[4]) * 650.0f, 0.7f);
        bq_lowpass(&e->oct_sublp2, sr, 100.0f + f01(q[4]) * 400.0f, 0.7f);
        bq_bandpass(&e->oct_bp, sr, 900.0f, 0.7f);
        bq_highpass(&e->oct_uphp, sr, 60.0f, 0.7f);
        e->oct_dry = f01(q[0]); e->oct_s1 = f01(q[1]) * 2.5f; e->oct_s2 = f01(q[2]) * 2.5f; e->oct_up = f01(q[3]) * 2.0f;
        break;
    case PX_PIT_POLYOCT_HQ:
    case PX_PIT_LOWEST_HQ:
        e->ps_nv = hq ? 0 : 2; e->ps_dry = f01(q[0]);
        e->ps_W[0] = 1400.0f; e->ps_inc[0] = 0.5f / 1400.0f; e->ps_lvl[0] = octave_down_level(f01(q[1]), hq);
        e->ps_W[1] = 1000.0f; e->ps_inc[1] = -1.0f / 1000.0f; e->ps_lvl[1] = octave_up_level(f01(q[2]), hq);
        e->erb_pitch.down2 = t == PX_PIT_POLYOCT_HQ && raw == t ? octave_down_level(f01(q[4]), 1) * PX_SUB2_LEVEL : 0.0f;
        /* v73: RANGE (and LOWEST OCTAVE HQ) no longer cut the bank at a fixed note, which left a chord over that
         * note silent: the octave-down follows the lowest sounding band (erb_bank), RANGE 1 the lowest note
         * alone, up to 100 nearly every note; the make-up gain keeps the level (tools/research/low_bench.py) */
        e->erb_pitch.low_sel = 0.0f;
        e->erb_pitch_range = ERB_FULL;
        if (t == PX_PIT_POLYOCT_HQ) e->erb_pitch_range = erb_range_band(raw != t || q[3] > 100 ? (raw != t ? 0 : 100) : q[3]);   /* RANGE: the OC-3's note cut */
        else if (hq) {
            e->erb_pitch_range = erb_range_band(q[3] > 100 ? 100 : q[3]);                 /* LOWEST OCTAVE HQ has it too (FULL = 0): the octave-down follows the lowest sounding band (v73, erb_bank), with make-up gain */
            e->erb_pitch.low_sel = LOW_SEL_MIN;
            /* the more RANGE cuts, the more make-up: x1 at FULL (0) and 100, up to x1.7 (+4.6 dB) at 1 (the lowest note alone, which RANGE then thins out) */
            const int v = q[3] > 100 ? 100 : q[3];
            const float rboost = v == 0 ? 1.0f : 1.0f + LOW_RANGE_BOOST * (1.0f - (float)(v - 1) / 99.0f);
            e->ps_lvl[0] *= LOW_MAKEUP * PX_LOWEST_HQ_GAIN * rboost; e->erb_pitch.down2 *= LOW_MAKEUP;
        }
        if (t == PX_PIT_POLYOCT_HQ && raw == t && q[3] >= 1 && q[3] < 100) {
            /* RANGE engaged: the -1 gets (a) the make-up for what RANGE thins out, (100 / RANGE)^0.196 (a single low E was -7 dB at RANGE 1, -2.7 dB at 25; a chord -13 dB at 1), and (b) the volume LOWEST OCTAVE HQ
             * keeps (it is about 9 dB louder than this type on a single low note; owner: "still too quiet when RANGE is engaged"): + PX_POLY_RANGE_LIFT dB, reached by RANGE 90 and fading out to nothing at 100 (no jump) */
            const float fade = (float)(100 - q[3]) * 0.1f;
            e->ps_lvl[0] *= powf(100.0f / (float)q[3], PX_POLY_RANGE_MAKEUP) * powf(10.0f, PX_POLY_RANGE_LIFT * 0.05f * (fade > 1.0f ? 1.0f : fade));
        }
        break;
    case PX_PIT_POLYSHIFT: {                           /* on POLY OCTAVE HQ's bank (px_banksyn.h): a whole number of semitones and the cents */
        const int st = (int)(f01(q[0]) * 24.0f + 0.5f) - 12;
        const float semis = (float)st + (f01(q[1]) - 0.5f);
        e->psh.rt = semis == 12.0f ? 2.0f : semis == -12.0f ? 0.5f : expf(semis * 0.05776227f);         /* 2^(st/12), the cents: +-50 of 100 per semitone */
        if (e->psh.r <= 0.0f) e->psh.r = e->psh.rt;
        e->psh.mix = f01(q[2]); e->psh.attack = f01(q[3]);
        e->psh.gain = ps_gain(semis);
        e->psh.octave_gain = powf(10.0f, (PIT_DB[PX_PIT_POLYOCT_HQ] - PIT_DB[PX_PIT_POLYSHIFT]) * 0.05f);
        erb_prepare(e);
    } break;
    case PX_PIT_WHAMMY: {
        float st = (f01(q[0]) - 0.5f) * 48.0f, m = f01(q[1]);
        e->ps_nv = 1; e->ps_dry = 1.0f - m; e->ps_lvl[0] = m;
        e->ps_W[0] = 1400.0f; e->ps_invW = 1.0f / 1400.0f;
        e->ps_rt = expf(st * 0.05776227f);             /* 2^(st/12) */
        if (e->ps_r <= 0.0f) e->ps_r = e->ps_rt;
        e->ps_glide = onepole_k(sr, 0.003f + f01(q[2]) * f01(q[2]) * 0.6f);
    } break;
    case PX_PIT_HARMONY: {
        float r = expf((float)HARM_ST[harm_idx(q[0])] * 0.05776227f);
        e->ps_nv = 1; e->ps_dry = 1.0f; e->ps_lvl[0] = f01(q[1]) * 1.1f;
        e->ps_W[0] = r < 1.0f ? 1400.0f : 1100.0f; e->ps_inc[0] = (1.0f - r) / e->ps_W[0];
    } break;
    default: {                                         /* doubler: +c and -c cents */
        float c = 3.0f + f01(q[0]) * 27.0f, m = f01(q[1]);
        float r0 = expf(c * 0.000577623f), r1 = expf(-c * 0.000577623f);
        e->ps_nv = 2; e->ps_dry = 1.0f - 0.5f * m;
        e->ps_W[0] = 1200.0f; e->ps_inc[0] = (1.0f - r0) / 1200.0f; e->ps_lvl[0] = 0.6f * m;
        e->ps_W[1] = 1100.0f; e->ps_inc[1] = (1.0f - r1) / 1100.0f; e->ps_lvl[1] = 0.6f * m;
    } break;
    }
    if (e->ps_ph[1] == 0.0f && e->ps_ph[0] == 0.0f) e->ps_ph[1] = 0.37f;
}

static void synth_hq_filters(px_bq *first, px_bq *second, float rate)
{
    bq_lowpass(first, rate, 150.0f, 0.54f);
    bq_lowpass(second, rate, 150.0f, 1.31f);
}

/* v76: the settings a type starts with when it is picked (the pedal's model knob, the editor, MIDI): every setting at the middle, so a mix and a level are never left at 0 from the
 * type before it, except the types where the middle is not the neutral setting (the boosts' unity points, the fuzz, the acoustic pair, the hum filter). The gate block keeps its
 * settings (its third one is the preset level). */
void px_type_defaults(int blk, int type, uint8_t *p)
{
    if (blk == PX_B_GATE) return;
    uint8_t d[PX_NPAR] = {50, 50, 50, 50, 50};
    if (blk == PX_B_BST) switch (type) {
    case PX_BST_RANGEMASTER: d[1] = 0; d[2] = 0; break;                        /* VOLUME 50 = unity, single coil, treble booster */
    case PX_BST_EPBOOST: d[0] = 0; break;                                      /* GAIN 0 = unity */
    case PX_BST_ENHANCE: d[1] = 0; break;                                      /* LOUDER 0 = unity */
    case PX_BST_VOXWAH: d[2] = 0; break;                                       /* LEVEL 0 = the guitar's loudness */
    case PX_BST_FUZZFACE: d[0] = 65; d[4] = 0; break;                          /* FUZZ, VOLUME 50, P90, BIAS 50, germanium */
    case PX_BST_ACOUSTIC: d[0] = 55; d[1] = 40; d[3] = 0; break;               /* LOW CUT about 70 Hz, DEQUACK 40, BODY flat, NOTCH off, LEVEL 0 dB */
    case PX_BST_SIGNATURE: d[0] = 60; d[1] = 55; d[2] = 25; d[3] = 30; break;  /* BODY, FOCUS, RICHNESS, PUNCH, LEVEL 0 dB */
    }
    if (blk == PX_B_SFX && type == PX_SFX_ACOUSTIC) d[0] = 60;                 /* DRIVE: breaking up */
    if (blk == PX_B_SFX && type == PX_SFX_PULSE) { d[0] = 50; d[1] = 50; d[2] = 50; d[3] = 70; }
    if (blk == PX_B_SFX && type == PX_SFX_CHIP) { d[0] = 45; d[1] = 55; d[2] = 60; d[3] = 75; }
    if (blk == PX_B_SFX && type == PX_SFX_CROSS) { d[0] = 45; d[1] = 40; d[2] = 55; d[3] = 80; }
    if (blk == PX_B_SFX && type == PX_SFX_FORMANT) { d[0] = 55; d[1] = 65; d[2] = 45; d[3] = 80; }
    if (blk == PX_B_SFX && (type == PX_SFX_CROSS_SAW || type == PX_SFX_CROSS_TRI)) { d[0] = 45; d[1] = 40; d[2] = 70; d[3] = 80; }
    if (blk == PX_B_SFX && type == PX_SFX_CROSS_OSC) { d[0] = 0; d[1] = 45; d[2] = 40; d[3] = 50; d[4] = 80; }
    if (blk == PX_B_SYN) switch (type) {                                       /* the waveform-following synths */
    case PX_SYN_ORGAN: d[0] = 10; d[2] = 25; break;                            /* JAZZ, SUSTAIN low: the picks still shape it */
    case PX_SYN_BS_STRINGS: case PX_SYN_BS_CHOIR:
    case PX_SYN_BS_STRINGS_OCT: case PX_SYN_BS_CHOIR_OCT: d[1] = 35; break;
    case PX_SYN_BS_PIPE: d[0] = 40; d[1] = 30; break;
    case PX_SYN_BS_BELL: d[3] = 40; break;
    case PX_SYN_BS_SAW: case PX_SYN_BS_PULSE: case PX_SYN_BS_ANALOG: d[1] = 35; d[3] = 0; break;
    case PX_SYN_BS_SUBBASS: d[0] = 30; d[1] = 30; d[3] = 0; break;           /* (SUB here is two octaves down) */
    case PX_SYN_SQ_BASS: d[0] = 30; d[1] = 30; d[3] = 30; break;
    case PX_SYN_SQ_BRASS: d[1] = 40; d[3] = 0; break;
    case PX_SYN_SQ_FLUTE: d[0] = 45; d[1] = 20; d[2] = 30; d[3] = 0; break;
    }
    for (int k = 0; k < PX_NPAR; k++) p[k] = d[k];
}
/* ---- v76: ACOUSTIC PREAMP (BST) and ACOUSTIC DRIVE (SFX), for a piezo pickup into a PA or a recorder (docs/research/ACOUSTIC-BOOSTS-RESEARCH.md) ---- */
static void bq_ident(px_bq *b) { bq_set(b, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); }
static void ap_set(px_ap *a, float sr, const uint8_t *q)
{
    const float lc = f01(q[0]), dq = f01(q[1]), body = f01(q[2]), nt = f01(q[3]);
    bq_highpass(&a->hp, sr, 20.0f * powf(10.0f, lc), 0.7071f);               /* LOW CUT 20 Hz .. 200 Hz */
    bq_peak(&a->dq1, sr, 2600.0f, 0.8f, -6.0f * dq);                          /* the quack dip */
    bq_highshelf(&a->dq2, sr, 6500.0f, -5.0f * dq);                           /* the brittle top */
    bq_peak(&a->dyn, sr, 2900.0f, 1.0f, -7.0f * dq);                          /* the extra dip on the pick attack (blended by the attack detector) */
    if (body < 0.5f) { bq_peak(&a->b1, sr, 300.0f, 0.9f, -16.0f * (0.5f - body)); bq_ident(&a->b2); }   /* BODY below noon: the box cut out of the lows-mids */
    else { bq_lowshelf(&a->b1, sr, 160.0f, 10.0f * (body - 0.5f)); bq_highshelf(&a->b2, sr, 8000.0f, 6.0f * (body - 0.5f)); }   /* above: low body and air */
    a->nt_on = nt > 0.005f;
    if (a->nt_on) { const float f = 80.0f * powf(50.0f, nt); bq_notch(&a->nt, sr, f, f * 0.125f, 24.0f); }   /* the feedback notch, 80 Hz .. 4 kHz, Q 8, 24 dB */
    a->amt = dq;
    a->kef = onepole_k(sr, 0.003f); a->kes = onepole_k(sr, 0.06f); a->kta = onepole_k(sr, 0.002f); a->ktr = onepole_k(sr, 0.05f); a->kl = onepole_k(sr, 0.01f);
    a->lvl_t = db2lin((f01(q[4]) - 0.5f) * 24.0f + 0.8f);                    /* (+0.8 dB: the default dips cost that much loudness on the guitar loops) */
    if (a->lvl == 0.0f) a->lvl = a->lvl_t;
}
static inline float ap_tick(px_ap *a, float x)
{
    const float ax = px_abs(x);
    a->ef += (ax - a->ef) * a->kef; a->es += (ax - a->es) * a->kes;
    float y = bq_run(&a->dq2, bq_run(&a->dq1, bq_run(&a->hp, x)));
    if (a->amt > 0.01f) {                                                      /* the attack follows the fast level over the slow one; noise under -60 dB does not count */
        const float want = a->ef > 0.001f ? (a->ef / (a->es + 1e-4f) - 1.3f) * (1.0f / 1.5f) : 0.0f, w = want < 0.0f ? 0.0f : want > 1.0f ? 1.0f : want;
        a->tr += (w - a->tr) * (w > a->tr ? a->kta : a->ktr);
        const float d = bq_run(&a->dyn, y); y += a->tr * (d - y);
    }
    y = bq_run(&a->b2, bq_run(&a->b1, y));
    if (a->nt_on) y = bq_run(&a->nt, y);
    a->lvl += (a->lvl_t - a->lvl) * a->kl;
    return y * a->lvl;
}
#ifndef PX_AD_OS
#define PX_AD_OS 2                                     /* the clipper's rate factor: 2 (half-band in and out; the dry is delayed to match) or 1 */
#endif
static void bq_lp1(px_bq *b, float fs, float f)     /* a one-pole low-pass: a gentle slope keeps the phase near the dry signal's, so the blend does not comb */
{
    const float w = PI_F * clampf_hz(fs, f) / fs, K = sinf(w) / cosf(w);
    bq_set(b, K, K, 0.0f, 1.0f + K, K - 1.0f, 0.0f);
}
static void ad_set(px_ad *d, float sr, const uint8_t *q)
{
    const float dr = f01(q[0]), pz = f01(q[2]), tn = f01(q[3]);
    bq_highpass(&d->hp, sr, 90.0f + 170.0f * pz, 0.7071f);                     /* the piezo's lows are taken off before the clipper (woofy bass) */
    bq_lp1(&d->lp, sr, 9000.0f - 5200.0f * pz);                                /* and its top (fizz) */
    bq_peak(&d->dip, sr, 2500.0f, 0.9f, -5.0f * pz);                           /* and a little of its quack */
    bq_lp1(&d->tone, sr, 2500.0f * powf(4.0f, tn));                            /* TONE after the clipper: 2.5 .. 10 kHz */
    d->g = 1.0f + 60.0f * dr * dr; d->bias = 0.2f; d->off = px_tanh(d->bias);
    d->wet_n = 1.3f / (1.0f + 0.6f * (d->g - 1.0f));
    d->blend = f01(q[1]);
    d->kdc = onepole_k(sr, 0.0057f); d->kl = onepole_k(sr, 0.01f);
    d->lvl_t = db2lin((f01(q[4]) - 0.5f) * 24.0f);
    if (d->lvl == 0.0f) d->lvl = d->lvl_t;
}
static inline float ad_clip(const px_ad *d, float u) { return px_tanh(d->g * u + d->bias) - d->off; }   /* a soft clipper, a little asymmetric (even harmonics) */
static inline float ad_tick(px_ad *d, float x)
{
    float w = bq_run(&d->dip, bq_run(&d->lp, bq_run(&d->hp, x)));
#if PX_AD_OS == 2
    float u0, u1; pxp_up(&d->os, w, &u0, &u1);
    pxp_push(&d->os, ad_clip(d, u0)); pxp_push(&d->os, ad_clip(d, u1));
    w = pxp_down(&d->os);
    d->dl[d->di] = x; d->di = (d->di + 1u) % 20u;                              /* the dry waits as long as the half-band filters take (18.5 samples) */
    const float dry = 0.5f * (d->dl[d->di] + d->dl[(d->di + 1u) % 20u]);   /* the delays 19 and 18 */
#else
    w = ad_clip(d, w); const float dry = x;
#endif
    w *= d->wet_n;
    d->dc += (w - d->dc) * d->kdc; w -= d->dc;
    w = bq_run(&d->tone, w);
    d->lvl += (d->lvl_t - d->lvl) * d->kl;
    return (dry + (w - dry) * d->blend) * d->lvl;
}
static void signature_set(px_signature *s, float sr, const uint8_t *q)
{
    const float body = f01(q[0]), focus = f01(q[1]);
    bq_lowshelf(&s->low, sr, 230.0f, -8.0f * focus);
    bq_peak(&s->body, sr, 350.0f, 0.65f, 8.0f * (body - 0.5f));
    bq_highshelf(&s->definition, sr, 2800.0f, 4.8f * focus);
    s->color_k = onepole_k(sr, 1.0f / (2.0f * PI_F * 4000.0f));
    s->dc_k = onepole_k(sr, 1.0f / (2.0f * PI_F * 20.0f));
    s->fast_k = onepole_k(sr, 0.003f);
    s->slow_k = onepole_k(sr, 0.070f);
    s->glide_k = onepole_k(sr, 0.010f);
    s->richness_target = 1.95f * f01(q[2]);
    s->punch_target = 0.90f * f01(q[3]);
    s->level_target = db2lin(24.0f * (f01(q[4]) - 0.5f));
    if (s->level == 0.0f) {
        s->richness = s->richness_target;
        s->punch = s->punch_target;
        s->level = s->level_target;
    }
}

static inline float signature_tick(px_signature *s, float x)
{
    float y = bq_run(&s->body, bq_run(&s->low, x));
    /* The AC-coupled quadratic residual of a common-source JFET, with unity
     * small-signal gain. Low-pass the drive and bound the color on hot inputs. */
    s->color_lp[0] += s->color_k * (y - s->color_lp[0]);
    s->color_lp[1] += s->color_k * (s->color_lp[0] - s->color_lp[1]);
    const float square = s->color_lp[1] * s->color_lp[1];
    const float bounded = square < 0.25f ? square : 0.25f;
    const float color = bounded * (1.0f - 2.0f * bounded);
    s->color_dc += s->dc_k * (color - s->color_dc);
    s->richness += s->glide_k * (s->richness_target - s->richness);
    y += s->richness * (color - s->color_dc);

    /* Add up to 2.3 dB on a rising envelope. The floor suppresses gain
     * modulation on very quiet inputs and the slow envelope catches up on sustain. */
    const float magnitude = px_abs(y);
    s->fast += s->fast_k * (magnitude - s->fast);
    s->slow += s->slow_k * (magnitude - s->slow);
    const float rise = s->fast - 1.15f * s->slow;
    const float attack = rise > 0.0f ? rise / (s->fast + 0.004f) : 0.0f;
    s->punch += s->glide_k * (s->punch_target - s->punch);
    y *= 1.0f + s->punch * attack;

    s->level += s->glide_k * (s->level_target - s->level);
    return s->level * bq_run(&s->definition, y);
}

static void rc_bst(px_engine *e)
{
    const uint8_t *q = e->cur.p[PX_B_BST];
    switch (e->cur.type[PX_B_BST] % PX_BST_COUNT) {
    case PX_BST_MAXIMIZER: pxp_bbe_set(&e->bbe, (float)q[0], (float)q[1]); break;   /* LO CONTOUR, PROCESS */
    case PX_BST_EPBOOST: pxp_ep_set(&e->ep, (float)q[0], (float)q[1], e->sr); break;   /* GAIN, VOICE */
    case PX_BST_RANGEMASTER: pxp_rm_set(&e->rm, (float)q[0], (float)q[1], (float)q[2]); break;   /* VOLUME, PICKUP, BODY: the state is kept on a knob move (the volume glides) */
    case PX_BST_ENHANCE: pxe_set(&e->enh, (float)q[0], (float)q[1]); break;   /* BETTER, LOUDER; the filter state is kept on a knob move (the mixes glide over 10 ms) */
    case PX_BST_ACOUSTIC: ap_set(&e->ap, e->sr, q); break;   /* LOW CUT, DEQUACK, BODY, NOTCH, LEVEL */
    case PX_BST_SIGNATURE: signature_set(&e->signature, e->sr, q); break;
    case PX_BST_FUZZFACE: pxp_ff_set(&e->ff, (float)q[0], (float)q[1], (float)q[2], (float)q[3], (float)q[4]); break;   /* FUZZ, VOLUME, PICKUP, BIAS, TRANSISTOR (a new transistor, bias or pickup: the operating point again) */
    default: pxp_vw_set(&e->vw, (float)q[0], (float)q[1], (float)q[2]); break;   /* VOX FIXED WAH: POSITION, PEAK, LEVEL */
    }
}
static void rc_sfx(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_SFX];
    int t = p->type[PX_B_SFX] % PX_SFX_COUNT;
    switch (t) {
    case PX_SFX_AUTOWAH:                               /* envelope filter (Mu-Tron style) */
        e->sfx_c = 1.0f + f01(q[0]) * 15.0f;           /* sensitivity */
        e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_d = 800.0f + f01(q[2]) * 2700.0f;       /* range */
        e->sfx_a = onepole_k(sr, 0.008f);
        e->sfx_b = onepole_k(sr, 0.05f + f01(q[3]) * 0.45f);
        break;
    case PX_SFX_LFOWAH:
        e->sfx_inc = (0.1f + f01(q[0]) * f01(q[0]) * 7.9f) / sr;
        e->sfx_c = f01(q[1]);
        e->sfx_qd = 1.0f - f01(q[2]) * 0.85f;
        e->sfx_d = f01(q[3]);
        break;
    case PX_SFX_RING:
        e->sfx_inc = 20.0f * powf(100.0f, f01(q[0])) / sr;
        e->sfx_mix = f01(q[1]);
        e->sfx_c = f01(q[2]) * 0.5f;
        e->sfx_d = 1.3f / sr;                          /* wobble LFO */
        break;
    case PX_SFX_CRUSH: {
        int bits = 16 - (int)(f01(q[0]) * 14.0f + 0.5f);
        e->crush_q = powf(2.0f, (float)(bits - 1));
        e->crush_k = 1 + (int)(f01(q[1]) * 39.0f + 0.5f);
        e->sfx_mix = f01(q[2]);
    } break;
    case PX_SFX_SLICER:
        e->sfx_inc = (2.0f + f01(q[0]) * 14.0f) / (16.0f * sr);
        e->sfx_c = f01(q[1]);
        e->sfx_pat = SLICE_PAT[(int)(f01(q[2]) * 7.99f)];
        e->sfx_a = onepole_k(sr, 0.002f);
        break;
    case PX_SFX_LOFI:
        bq_highpass(&e->sfx_f1, sr, 350.0f + f01(q[0]) * 250.0f, 0.7f);
        bq_lowpass(&e->sfx_f2, sr, 1500.0f + f01(q[0]) * 2500.0f, 1.4f);
        e->sfx_c = 1.0f + f01(q[1]) * 8.0f;
        e->sfx_mix = f01(q[2]);
        break;
    case PX_SFX_ACOUSTIC: ad_set(&e->ad, sr, q); break;   /* v76: DRIVE, BLEND, PIEZO, TONE, LEVEL */
    case PX_SFX_PULSE:
    case PX_SFX_CHIP:
        bq_lowpass(&e->sfx_f1, sr, 700.0f, 0.7f);
        e->sfx_c = f01(q[0]); e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_a = onepole_k(sr, 0.002f); e->sfx_b = onepole_k(sr, 0.15f);
        e->sfx_d = f01(q[2]); e->sfx_mix = f01(q[3]); e->sfx_prev = -1.0f; e->sfx_ff = 1.0f;
        e->sfx_pat = 0x1ACEB00C; break;
    case PX_SFX_CROSS:
        bq_lowpass(&e->sfx_f1, sr, 700.0f, 0.7f);
        e->sfx_c = f01(q[0]); e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_d = f01(q[2]); e->sfx_mix = f01(q[3]);
        e->sfx_a = onepole_k(sr, 0.002f); e->sfx_b = onepole_k(sr, 0.15f);
        e->sfx_prev = -1.0f; e->sfx_ff = 1.0f; break;
    case PX_SFX_FORMANT:
        bq_bandpass(&e->sfx_f1, sr, 500.0f, 3.0f);
        bq_bandpass(&e->sfx_f2, sr, 1500.0f, 3.0f);
        e->sfx_c = f01(q[0]); e->sfx_qd = f01(q[1]); e->sfx_d = f01(q[2]);
        e->sfx_mix = f01(q[3]); e->sfx_a = onepole_k(sr, 0.002f); e->sfx_b = onepole_k(sr, 0.15f);
        e->sfx_prev = -1.0f; e->sfx_ff = 1.0f; break;
    case PX_SFX_CROSS_SAW: case PX_SFX_CROSS_TRI:
        bq_lowpass(&e->sfx_f1, sr, 700.0f, 0.7f);
        e->sfx_c = f01(q[0]); e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_d = f01(q[2]); e->sfx_mix = f01(q[3]);
        e->sfx_a = onepole_k(sr, 0.002f); e->sfx_b = onepole_k(sr, 0.15f);
        e->sfx_prev = -1.0f; e->sfx_sync_age = 0.0f; e->sfx_sync_freq = 110.0f; e->sfx_sync_phase = 0.0f; break;
    case PX_SFX_CROSS_OSC:
        bq_lowpass(&e->sfx_f1, sr, 700.0f, 0.7f);
        e->sfx_c = f01(q[1]); e->sfx_qd = 1.0f - f01(q[2]) * 0.85f;
        e->sfx_d = f01(q[3]); e->sfx_mix = f01(q[4]);
        e->sfx_a = onepole_k(sr, 0.002f); e->sfx_b = onepole_k(sr, 0.15f);
        e->sfx_prev = -1.0f; e->sfx_sync_age = 0.0f; e->sfx_sync_freq = 110.0f; e->sfx_sync_phase = 0.0f;
        e->sfx_sync_inc = (float)((q[0] * 7u) / 101u);
        bq_bandpass(&e->sfx_f2, sr, 850.0f, 1.4f); break;
    case PX_SFX_APPLAUSE: break;                       /* retired: bypassed, nothing to set up */
    case PX_SFX_MAXIMIZER: case PX_SFX_EPBOOST: case PX_SFX_RANGEMASTER: case PX_SFX_ENHANCE: break;   /* v75: moved to the BST block (tombstones here, px_sfx_to_bst) */
    default:                                           /* square synth: square + sub through a swept low-pass */
#ifdef PX_SFX_SYNTH_HQ_LIVE                            /* SQUARE SYNTH HQ (the band-bank tracker's square): retired 2026-11-28, bypassed;
                                                          compiled out so the tracker (px_track.h, ~9 KB code, ~3 KB RAM) leaves the image */
        if (t == PX_SFX_SYNTH_HQ) {
            if (!e->sfx_track_active) {
                memset(&e->sfx_track, 0, sizeof e->sfx_track); pxt_reset(e, &pxs_trk, &pxs_lay);
                bq_reset(&e->sfx_f1); bq_reset(&e->sfx_f2);
            }
            synth_hq_filters(&e->sfx_f1, &e->sfx_f2, sr);
        } else
#endif
        bq_lowpass(&e->sfx_f1, sr, 700.0f, 0.7f);
        e->sfx_c = f01(q[0]);
        e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_d = f01(q[2]);
        e->sfx_mix = f01(q[3]);
        e->sfx_a = onepole_k(sr, 0.002f);
        e->sfx_b = onepole_k(sr, 0.15f);
        if (e->sfx_ff == 0.0f) e->sfx_ff = 1.0f;
        break;
    }
    e->sfx_track_active = t == PX_SFX_SYNTH_HQ;
}

/* POLY OCT FUZZ's clipper: the Fuzz Face voicing (FUZZ_FACE) with FUZZ 0..1, TONE and LEVEL at noon. The same
 * arithmetic as the v66 drive setup for that row, so the sound is unchanged. */
static void fuzz_setup(px_drive_state *state, float sr, int os, float fuzz)
{
    const drv_v *d = &FUZZ_FACE;
    const float osr = sr * (float)os, tone = 0.5f, level = 0.5f;
    state->gain = drive_gain(d, fuzz);
    bq_hp1(&state->hp, sr, (float)d->hp);
    state->pre = 0;
    bq_lowpass(&state->lp, sr, (float)d->lp, 0.7f); state->pre |= 2;
    state->clip = d->clip;
    state->bias = (float)d->bias * 0.01f;
    state->gate = 0.0f;
    state->off = px_tanh(state->bias);
    state->dcb = 1;
    state->dck = 2.0f * PI_F * 12.0f / osr;
    state->tm = d->tm; state->np = 1;
    bq_lowpass(&state->t1, osr, (float)d->t0 * powf((float)d->t1 / (float)d->t0, tone), 0.7f);
    state->blend = 0.0f;
    state->level = 0.07f * 2.0f * level * powf(10.0f, -3.0f * 0.05f);   /* (v36 trim of the Fuzz Face row) */
}

static void lowest_pitch_reset(px_engine *e)
{
    const float sel = e->erb_pitch.low_sel, down2 = e->erb_pitch.down2;
    memset(&e->erb_pitch, 0, sizeof e->erb_pitch);
    e->erb_pitch.low_sel = sel; e->erb_pitch.down2 = down2;
}

static __attribute__((noinline)) float lowest_pitch_run(px_engine *e, float x)
{
    /* v73: the bank's octave-down of the lowest sounding band (was px_lowest_sub.h's tracked sub) */
    return x * e->ps_dry + erb_octaves(e, &e->erb_pitch, x, e->ps_lvl[1], e->ps_lvl[0], e->erb_pitch_range);
}

/* v75 HUM FILTER, MAINS = AUTO: which mains is in the signal. Six resonators (Goertzel: 50 / 100 / 150 Hz and 60 / 120 / 180 Hz) run over the block's input
 * (before the notches); at the end of each half-second window the 60 Hz family's power is compared with the 50 Hz family's. A guitar's own notes land on
 * both families about alike, a hum lands on one, so a ratio over 4 (about 6 dB) two windows running switches the mains (about one second), and the notches
 * are re-aimed. Costs 12 multiply-adds a sample while AUTO is chosen. */
static void rc_eq(px_engine *e);
static void hum_detect(px_engine *e, const float *x, int n)
{
    for (int r = 0; r < 6; r++) {
        float s1 = e->hum_s1[r], s2 = e->hum_s2[r]; const float c = e->hum_c[r];
        for (int k = 0; k < n; k++) { const float s0 = x[k] + c * s1 - s2; s2 = s1; s1 = s0; }
        e->hum_s1[r] = s1; e->hum_s2[r] = s2;
    }
    e->hum_n += n;
    if (e->hum_n < (int)(e->sr * 0.5f)) return;
    float p[6];
    for (int r = 0; r < 6; r++) { p[r] = e->hum_s1[r] * e->hum_s1[r] + e->hum_s2[r] * e->hum_s2[r] - e->hum_c[r] * e->hum_s1[r] * e->hum_s2[r]; e->hum_s1[r] = e->hum_s2[r] = 0.0f; }
    e->hum_n = 0;
    const float p50 = p[0] + p[1] + p[2] + 1e-12f, p60 = p[3] + p[4] + p[5] + 1e-12f;
    const int want = p60 > 4.0f * p50 && p[3] > 4*p[0] && p[4] > 4*p[1] && p[5] > 4*p[2] ? 60 : p50 > 4.0f * p60 && p[0] > 4*p[3] && p[1] > 4*p[4] && p[2] > 4*p[5] ? 50 : e->hum_hz;
    if (e->suppressor.open && e->hum_hz && want != e->hum_hz) {
        const int a = want == 60 ? 3 : 0, b = want == 60 ? 0 : 3;
        for (int i = 0; i < 3; ++i) if (p[a+i] < 16.0f * p[b+i]) { e->hum_vote = 0; return; }
    }
    if (want == e->hum_hz) { e->hum_vote = 0; return; }
    if (++e->hum_vote < 2) return;
    e->hum_vote = 0; e->hum_hz = want;
    /* The input stage crossfades to the newly selected bank. */
}

static void rc_eq(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_EQ];
    int t = p->type[PX_B_EQ] % PX_EQ_COUNT;
#define EQDB(k) ((f01(q[k]) - 0.5f) * 24.0f)
    if (t == PX_EQ_5BAND) {
        bq_lowshelf(&e->eq[0], sr, 100.0f, EQDB(0));
        bq_peak(&e->eq[1], sr, 400.0f, 1.0f, EQDB(1));
        bq_peak(&e->eq[2], sr, 1000.0f, 1.0f, EQDB(2));
        bq_peak(&e->eq[3], sr, 2500.0f, 1.0f, EQDB(3));
        bq_highshelf(&e->eq[4], sr, 6000.0f, EQDB(4));
        e->eq_n = 5;
    } else if (t == PX_EQ_GEQ) {                       /* the 5-band graphic of the Californian lead amps */
        bq_peak(&e->eq[0], sr, 80.0f, 1.4f, EQDB(0));
        bq_peak(&e->eq[1], sr, 240.0f, 1.4f, EQDB(1));
        bq_peak(&e->eq[2], sr, 750.0f, 1.4f, EQDB(2));
        bq_peak(&e->eq[3], sr, 2200.0f, 1.4f, EQDB(3));
        bq_peak(&e->eq[4], sr, 6600.0f, 1.4f, EQDB(4));
        e->eq_n = 5;
    } else {
        bq_lowshelf(&e->eq[0], sr, 100.0f, EQDB(0));
        bq_peak(&e->eq[1], sr, 150.0f * powf(33.0f, f01(q[1])), 0.4f + f01(q[3]) * 5.6f, EQDB(2));
        bq_highshelf(&e->eq[2], sr, 5000.0f, EQDB(4));
        e->eq_n = 3;
    }
#undef EQDB
}

static void rc_mod(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_MOD];
    int t = p->type[PX_B_MOD] % PX_MOD_COUNT;
    float r = f01(q[0]);
    e->mod_fb = 0.0f; e->mod_mix = 1.0f;
    switch (t) {
    case PX_MOD_CHORUS:                                /* bucket-brigade chorus: triangle LFO, dark wet path */
        e->mod_inc = (0.1f + r * r * 6.0f) / sr;
        e->mod_base = 0.006f * sr; e->mod_sweep = (0.0005f + 0.0055f * f01(q[1])) * sr;
        e->mod_mix = f01(q[2]);
        bq_lowpass(&e->mod_lp, sr, 6600.0f, 0.7f);
        break;
    case PX_MOD_DIMENSION: {
        static const float RATE[4] = {0.25f, 0.25f, 0.5f, 0.5f}, DEP[4] = {0.0005f, 0.0009f, 0.0007f, 0.0012f};
        int m = (int)(r * 3.99f);
        e->mod_inc = RATE[m] / sr; e->mod_base = 0.006f * sr; e->mod_sweep = DEP[m] * sr;
        e->mod_mix = f01(q[1]);
        bq_lowpass(&e->mod_lp, sr, 8000.0f, 0.7f);
    } break;
    case PX_MOD_FLANGER:
        e->mod_inc = (0.05f + r * r * 4.95f) / sr;
        e->mod_sweep = f01(q[1]) * 0.008f * sr;
        e->mod_base = (0.0003f + f01(q[2]) * 0.0047f) * sr;
        e->mod_fb = f01(q[3]) * 0.92f;
        e->mod_mix = f01(q[4]);
        break;
    case PX_MOD_PHASE90:                               /* 4 identical JFET stages, no feedback (script) */
        e->mod_inc = (0.1f + r * r * 7.9f) / sr;
        e->ph_lo = 60.0f; e->ph_hi = 300.0f + f01(q[1]) * 1700.0f;
        e->mod_fb = 0.0f;
        break;
    case PX_MOD_STONE:
        e->mod_inc = (0.05f + r * r * 7.0f) / sr;
        e->mod_fb = f01(q[1]) * 0.7f;
        e->ph_lo = 100.0f; e->ph_hi = 400.0f + f01(q[2]) * 3100.0f;
        break;
    case PX_MOD_UNIVIBE:                               /* 4 stages with staggered caps, light-bulb LFO */
        e->mod_inc = (0.5f + r * r * 11.5f) / sr;
        e->mod_depth = f01(q[1]);
        e->mod_mix = f01(q[2]);
        e->uv_k[0] = 1.0f; e->uv_k[1] = 0.068f; e->uv_k[2] = 31.9f; e->uv_k[3] = 3.19f;
        e->uv_up = onepole_k(sr, 0.006f); e->uv_dn = onepole_k(sr, 0.03f);
        break;
    case PX_MOD_OPTOTREM: {
        float hz = 1.0f + r * r * 9.0f;
        e->mod_inc = hz / sr; e->mod_depth = f01(q[1]);
        e->trem_att = onepole_k(sr, 0.005f);
        e->trem_rel = onepole_k(sr, 0.2f - (hz - 1.0f) * 0.0167f);
    } break;
    case PX_MOD_BIASTREM:
        e->mod_inc = (1.0f + r * r * 11.0f) / sr; e->mod_depth = f01(q[1]); e->mod_shape = f01(q[2]);
        break;
    case PX_MOD_HARMTREM:
        e->mod_inc = (1.0f + r * r * 9.0f) / sr; e->mod_depth = f01(q[1]);
        bq_lowpass(&e->mod_lp, sr, 300.0f + f01(q[2]) * 900.0f, 0.6f);
        break;
    case PX_MOD_ROTARY:
        e->rot_ht = (q[0] < 50) ? 0.83f : 6.7f;
        e->rot_dt = (q[0] < 50) ? 0.67f : 5.7f;
        if (e->rot_hs <= 0.0f) { e->rot_hs = e->rot_ht; e->rot_ds = e->rot_dt; }
        e->rot_hk = onepole_k(sr, 1.0f); e->rot_dk = onepole_k(sr, 4.0f);
        e->mod_depth = f01(q[1]); e->rot_bal = f01(q[2]);
        bq_lowpass(&e->mod_lp, sr, 800.0f, 0.7f);
        break;
    default:                                           /* vibrato: wet only */
        e->mod_inc = (2.0f + r * r * 12.0f) / sr;
        e->mod_base = 0.005f * sr; e->mod_sweep = f01(q[1]) * 0.004f * sr;
        break;
    }
}

static void rc_delay(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr * 0.5f;                           /* v40: the line's rate (half rate, as the stock delays): ticks */
    const float lpmax = 0.45f * sr;                    /* (the filters in the loop stay below its Nyquist) */
    const uint8_t *q = p->p[PX_B_DELAY];
    int t = p->type[PX_B_DELAY] % PX_DLY_COUNT;
    float len = dly_ms(t, q[0]) * 0.001f * sr;
    if (len > (float)(PX_DLY_N - 600)) len = (float)(PX_DLY_N - 600);
    if (e->dly_len < 1.0f || e->dly_type != t) { e->dly_len = len; e->rev_cnt = 0; e->rev_start = e->dly_w; }
    e->dly_type = t;
    e->dly_tgt = len;
    e->dly_fb = f01(q[1]) * 0.95f;
    e->dly_satn = 1.0f;
    e->dly_mix = f01(q[2]);
    e->dly_wow = 0.0f; e->dly_flut = 0.0f; e->dly_sat = 0.0f; e->dly_q = 0.0f; e->dly_mod = 0.0f;
    e->dly_inc = 0.7f / sr; e->dly_inc2 = 8.0f / sr;
    float tone = f01(q[3]), lp;
    /* (depths in ticks: half the samples they were at the full rate, the same times) */
    switch (t) {
    case PX_DLY_DIGITAL: case PX_DLY_DUAL:
        lp = 3000.0f + tone * 13000.0f;
        bq_lowpass(&e->dly_lp, sr, lp < lpmax ? lp : lpmax, 0.7f); bq_highpass(&e->dly_hp, sr, 40.0f, 0.7f); break;
    case PX_DLY_ANALOG:                                /* bucket brigade: dark repeats that saturate */
        bq_lowpass(&e->dly_lp, sr, 1500.0f + tone * 2000.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 80.0f, 0.7f);
        e->dly_sat = 1.0f; e->dly_mod = 0.75f; break;
    case PX_DLY_MEMORY:                                /* analog with modulated repeats */
        bq_lowpass(&e->dly_lp, sr, 2800.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 80.0f, 0.7f);
        e->dly_sat = 1.0f; e->dly_inc = (0.5f + f01(q[3]) * 4.5f) / sr; e->dly_mod = 1.0f + f01(q[4]) * 0.004f * sr; break;
    case PX_DLY_TAPE:                                  /* tape echo: wow 0.7 Hz + flutter 8 Hz */
        bq_lowpass(&e->dly_lp, sr, 4000.0f + tone * 4000.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 120.0f, 0.7f);
        e->dly_sat = 2.0f; e->dly_wow = 0.5f + f01(q[4]) * (len * 0.004f + 3.0f); e->dly_flut = f01(q[4]) * 1.5f; break;
    case PX_DLY_SPACE: {                               /* 3 playback heads 1:2:3 */
        static const uint8_t HM[7] = {1, 2, 4, 3, 6, 5, 7};
        int h = HM[(int)(tone * 6.99f)], nh = (h & 1) + ((h >> 1) & 1) + ((h >> 2) & 1);
        e->dly_heads = h; e->dly_hn = 1.0f / sqrtf((float)nh); e->dly_hf = 1.0f / (float)nh;
        bq_lowpass(&e->dly_lp, sr, 4500.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 150.0f, 0.7f);
        e->dly_sat = 2.0f; e->dly_wow = 0.5f + f01(q[4]) * (len * 0.003f + 2.5f); e->dly_flut = f01(q[4]) * 1.25f;
    } break;
    case PX_DLY_REVERSE:
        bq_lowpass(&e->dly_lp, sr, 9000.0f < lpmax ? 9000.0f : lpmax, 0.7f); bq_highpass(&e->dly_hp, sr, 60.0f, 0.7f);
        e->rev_xf = 1.0f / (0.01f * sr); break;
    default:                                           /* lo-fi: few bits, narrow band, wobbly */
        bq_lowpass(&e->dly_lp, sr, 4000.0f - tone * 2000.0f, 0.9f); bq_highpass(&e->dly_hp, sr, 300.0f, 0.7f);
        e->dly_q = powf(2.0f, 11.0f - tone * 6.0f); e->dly_wow = 2.0f + tone * 4.0f; e->dly_inc = 0.5f / sr; e->dly_sat = 1.5f; break;
    }
    if (e->dly_sat > 0.0f) {                           /* saturating loop: tops out at 1.08 = self-oscillation, like the real units */
        e->dly_sat = 1.0f + e->dly_sat * 0.5f;         /* drive into the loop clipper */
        e->dly_satn = 1.0f / e->dly_sat;
        float f = f01(q[1]);
        e->dly_fb = f < 0.85f ? f * 0.95f : 0.8075f + (f - 0.85f) * 1.817f;
    }
}

/* the reverb's memory, laid out for type t (only when the type changes: the lines are cleared) */
static void rv_layout(px_engine *e, int t, float srs)
{
    const rv_def *v = &RVD[t];
    int n[13];
    float f = 1.0f;
    e->rv_alg = t == PX_REV_PLATE;
    for (int pass = 0; pass < 2; pass++) {
        float k = e->rv_alg ? srs * f * (44100.0f / 29761.0f) : srs * f * v->size;
        float kd = srs * f * (44100.0f / 29761.0f) * (e->rv_alg ? 1.0f : (v->size < 1.0f ? 0.4f + 0.6f * v->size : 1.0f));
        int pre = (int)(v->pre_ms * 0.001f * 44100.0f * srs) + 1, span = (int)(v->er_ms * 0.001f * 44100.0f * srs);
        e->rv_pd = pre;
        e->rv_ner = v->er > 0.0f ? 4 : 0;
        for (int j = 0; j < 4; j++) { e->rv_ert[j] = pre + (int)(ER_T[j] * (float)span); e->rv_erg[j] = ER_G[j] * v->er; }
        n[0] = pre + span + 2;
        for (int i = 0; i < 4; i++) { n[1 + i] = (int)((float)RV_DIF[i] * kd) + 1; e->rv_dg[i] = RV_DIFG[i]; }
        e->rv_mdep = (e->rv_alg ? 16.0f * (44100.0f / 29761.0f) : v->mod) * srs;
        int ext = (int)(2.0f * e->rv_mdep) + 3;          /* room for the modulation and the interpolation */
        for (int i = 0; i < 8; i++) {
            float d = (float)(e->rv_alg ? PL_LEN[i] : RV_FDN[i]) * k;
            int mod = e->rv_alg ? (i == 0 || i == 4) : i < 4;
            e->rv_d[5 + i] = d;
            n[5 + i] = (int)d + (mod ? ext : 0);
        }
        int tot = 0;
        for (int u = 0; u < 13; u++) tot += n[u] + 1;
        if (tot <= PX_RV_M) break;
        f = (float)PX_RV_M / (float)tot * 0.98f;           /* (above 44.1 kHz only) shorter lines */
    }
    int b = 0;
    for (int u = 0; u < 13; u++) { e->rv_b[u] = b; e->rv_n[u] = n[u]; b += n[u] + 1; }   /* a unit lives n + 1 slots */
    for (int j = 0; j < 14; j++) {
        int tp = (int)((float)PL_TAP[j] * srs * (44100.0f / 29761.0f));
        e->rv_tap[j] = tp < n[PL_TU[j]] ? tp : n[PL_TU[j]] - 1;
    }
    memset(e->rv_m, 0, sizeof e->rv_m);
    for (int i = 0; i < 8; i++) e->rv_s[i] = 0.0f;
    e->rv_fl = e->rv_fr = 0.0f;
    e->rv_inc1 = (e->rv_alg ? 1.0f : 0.53f) / (srs * 44100.0f);
    e->rv_inc2 = 0.79f / (srs * 44100.0f);
    e->rv_lay = t + 1;
}
#ifndef SHIM_K
#define SHIM_K 0.8f                                    /* shimmer: octave fed back (v35: about as much as v34) */
#endif
static void rc_reverb(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_REVERB];
    int t = p->type[PX_B_REVERB] % PX_REV_COUNT;
    const rv_def *v = &RVD[t];
    float d = f01(q[0]), tone = f01(q[1]);
    e->rv_shim = 0.0f; e->rv_gate = 0; e->sp_on = 0;
    if (t == PX_REV_SPRING) { e->sp_on = 1; e->sp_a = 0.5f + f01(q[3]) * 0.25f; }
    if (t == PX_REV_SHIMMER) e->rv_shim = f01(q[3]) * SHIM_K;
    if (t == PX_REV_GATED) { e->rv_gate = 1; e->rv_hold = (0.15f + d * 0.45f) * sr; e->rv_gate_rel = onepole_k(sr, 0.02f); }
    e->rv_sr = sr * 0.5f;                              /* the tank runs at half rate (tails need no more than 11 kHz) */
    if (e->rv_lay != t + 1) rv_layout(e, t, e->rv_sr / 44100.0f);
    float rt = v->rt0 + d * v->rt1;
    sr = e->rv_sr;
    if (e->rv_alg) {                                   /* plate: 4 decay gains per trip round the tank */
        float loop = 0.0f;
        for (int i = 0; i < 8; i++) loop += e->rv_d[5 + i];
        loop += e->rv_mdep * 2.0f;
        e->rv_dec = powf(10.0f, -3.0f * loop / (4.0f * rt * sr));
        float dd = e->rv_dec + 0.15f;
        e->rv_dd2 = dd < 0.25f ? 0.25f : (dd > 0.5f ? 0.5f : dd);
        e->rv_damp = 0.05f + (1.0f - tone) * 0.3f;
        e->rv_bw = 0.9995f;
    } else {                                           /* FDN: decay time per line at low frequencies and at 4 kHz */
        float rth = rt * (v->hf0 + v->hf1 * tone), c = cosf(2.0f * PI_F * 4000.0f / sr);
        for (int i = 0; i < 8; i++) {
            float L = e->rv_d[5 + i] + (i < 4 ? e->rv_mdep : 0.0f);
            float g = powf(10.0f, -3.0f * L / (rt * sr)), r = powf(10.0f, -3.0f * L / (rth * sr)) / g;
            /* one pole g (1 - p) / (1 - p z^-1): |H(4 kHz)| / H(0) = r  ->  (1-r^2) p^2 - 2 (1 - r^2 c) p + (1-r^2) = 0 */
            float r2 = r * r, A = 1.0f - r2, B = 1.0f - r2 * c, pl = 0.0f;
            if (A > 1e-6f) pl = (B - sqrtf(B * B - A * A)) / A;
            e->rv_p[i] = pl;
            e->rv_g[i] = g * (1.0f - pl);
        }
    }
    e->rv_in = 0.25f;
    e->rv_out = v->out;
    e->rv_mix = f01(q[2]);
    /* v40: the low cut after the tank runs at its rate (the stock reverbs' post too); TONE stays at the full rate */
    bq_highpass(&e->rv_hp, sr, t == PX_REV_SPRING ? 120.0f : 50.0f, 0.7f);
    sr = e->sr;
    if (t == PX_REV_SPRING) bq_lowpass(&e->rv_tone, sr, 2500.0f + tone * 2500.0f, 0.8f);
    else bq_lowpass(&e->rv_tone, sr, 3000.0f + tone * 9000.0f, 0.7f);
    e->sh_k = onepole_k(sr, 1.0f / (2.0f * PI_F * 5000.0f));
    e->sh_hk = onepole_k(sr, 1.0f / (2.0f * PI_F * 300.0f));
}


/* ---- SYN: the SYNTH block (Synth2). Only POLY OCT FUZZ runs; every other legacy type is a tombstone (docs/research/synth2/SYNTH2-PLAN.md) ---- */
static void syn_fuzz_reset(px_engine *e)
{
    memset(e->syn_fuzz_os, 0, sizeof e->syn_fuzz_os);
    bq_reset(&e->syn_fuzz.hp);
    bq_reset(&e->syn_fuzz.lp);
    bq_reset(&e->syn_fuzz.t1);
    e->syn_fuzz.dcs = e->syn_lp = 0.0f;
}
static void rc_syn(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_SYN];
    const int t = p->type[PX_B_SYN];
    float a = f01(q[0]), b = f01(q[1]), d = f01(q[3]), f = f01(q[4]);
    if (t != e->syn_type) {                            /* new instrument: start from a quiet state */
        if (t == PX_SYN_OCT_FUZZ || t == PX_SYN_ORGAN) { memset(&e->erb_syn, 0, sizeof e->erb_syn); syn_fuzz_reset(e); }
        if (px_syn_bs(t)) bs_reset(e);
        if (t == PX_SYN_ORGAN) e->org_since = 1 << 20;   /* (the first pick counts) */
        if (px_syn_mono(t)) px_s2_reset(e, t, q);
        e->syn_type = t;
    } else if (px_syn_mono(t)) px_s2_set_q(t, q);
    if (!px_syn_runs(t)) return;                       /* a tombstone: nothing to set up, the block is bypassed */
    if (px_syn_mono(t)) return;                        /* its state is the adapter's (px_synth_adapter.h) */
    if (px_syn_bs(t)) { bs_setup(e, t, q); return; }
    if (t == PX_SYN_ORGAN) {                           /* drawbars 16' 8' 4' 2 2/3' 2' per REGISTER (tests/experiments/organ_bank) */
        static const float R[4][5] = { {0.8f, 1.0f, 0.6f, 0.0f, 0.0f},      /* JAZZ */
                                       {0.8f, 1.0f, 0.8f, 0.6f, 0.6f},      /* FULL */
                                       {0.0f, 1.0f, 0.7f, 0.6f, 0.0f},      /* GOSPEL: no 16' */
                                       {0.9f, 0.8f, 0.7f, 0.0f, 0.5f} };    /* CHURCH: slow swell */
        const int r = q[0] >= 75 ? 3 : q[0] >= 50 ? 2 : q[0] >= 25 ? 1 : 0;
        memcpy(e->org_bar, R[r], sizeof e->org_bar);
        e->org_perc_lvl = r == 3 ? 0.0f : 1.2f * b;
        e->org_sus = f01(q[2]);
        e->org_swk = r == 3 ? 0.00025f : 0.05f;
        erb_prepare(e);
        e->syn_lpk = onepole_k(sr, 1.0f / (2.0f * PI_F * (800.0f + 5200.0f * d)));
        e->syn_out = 2.0f * (f < 0.5f ? 4.0f * f * f : db2lin(12.0f * (f - 0.5f)));   /* +6 dB: the owner found it quiet on 990 */
        return;
    }
    e->syn_lvl[0] = e->syn_lvl[1] = e->syn_lvl[2] = 0.0f; e->syn_out = 1.0f;
    e->syn_lpk = opk(sr, 3000.0f);
    erb_prepare(e);
    fuzz_setup(&e->syn_fuzz, sr, PX_OVERSAMPLE, f01(q[2]));   /* Fuzz Face at noon LEVEL */
    e->syn_lvl[0] = octave_up_level(b, 1);
    e->syn_lvl[1] = octave_down_level(a, 1);
    e->syn_fuzz_tone = d - 0.5f;
    e->syn_lpk = onepole_k(sr, 1.0f / (2.0f * PI_F * 1500.0f));
    /* v66: LEVEL 0..100 as the owner asked: 0 silent, rising as the square to 0 dB at noon, then up to +6 dB */
    e->syn_out = f < 0.5f ? 4.0f * f * f : db2lin(syn_fuzz_level_db(f));
}

/* ORGAN over a block: the bank's drawbars (the percussion restarts the 2 2/3' on a pick), then the organ's hold: the
 * wet levelled by a slow follower (SUSTAIN: a guitar's decay becomes a held tone), gated with the guitar */
static void organ_run(px_engine *e, float *xb, int n, float strim)
{
    px_erb_octave *s = &e->erb_syn;
    const float *bar = e->org_bar;
    s->mid8 = bar[1];
    for (int k = 0; k < n; ++k) {
        const float x = xb[k], a = x < 0.0f ? -x : x;
        e->org_fast += (a - e->org_fast) * 0.0224f; e->org_slow += (a - e->org_slow) * 0.0011f;
        if (e->org_fast > 1.8f * e->org_slow && e->org_fast > 2e-3f && e->org_since > 2646) { e->org_perc = 1.0f; e->org_since = 0; }
        if (e->org_since < (1 << 30)) e->org_since++;
        e->org_perc *= 0.99985f;                       /* ~150 ms */
        s->up3 = bar[3] + e->org_perc_lvl * e->org_perc + 1e-9f;
        const float w = erb_octaves2(e, s, x, bar[2], bar[0], bar[4], ERB_FULL);
        const float wa = w < 0.0f ? -w : w;
        e->org_agc += (wa - e->org_agc) * (wa > e->org_agc ? 0.004f : 0.0003f);
        const float open = e->org_fast > 1.5e-3f ? 1.0f : 0.0f;
        e->org_gate += (open - e->org_gate) * (open > e->org_gate ? 0.02f : 0.0015f);
        e->org_swell += (e->org_gate - e->org_swell) * e->org_swk;
        float g = 0.12f / (e->org_agc + 0.02f); if (g > 6.0f) g = 6.0f;
        g = 1.0f + e->org_sus * (g - 1.0f);
        float y = w * g * e->org_swell * 1.3f;
        y = y / (1.0f + (y < 0.0f ? -y : y)) / 1.3f;   /* soft preamp grit (no tanhf in px_math) */
        e->syn_lp += e->syn_lpk * (y - e->syn_lp);
        xb[k] = e->syn_lp * e->syn_out * strim;
    }
}

static void px_recalc(px_engine *e)
{
    float sr = e->sr, osr = sr * PX_OVERSAMPLE;
    e->triode.drive = 4.0f * db2lin(valve_drive_db(f01(e->cur.valve_drive)));
    e->triode.level = valve_output_gain(e->cur.valve_level);
    rc_dyn(e);
    rc_pitch(e);
    rc_sfx(e);
    rc_bst(e);
    (void)osr;                                         /* (v38: the half-band filters have fixed coefficients) */
    rc_eq(e);
    rc_mod(e);
    if (!e->spill[0]) rc_delay(e);                      /* v75: a tail playing out keeps the settings it had */
    if (!e->spill[1]) rc_reverb(e);
    rc_syn(e);
    /* v36: each type's level at noon, measured (px_trim.h): switching a block on keeps the loudness */
    const px_preset_t *p = &e->cur;
    static const float *const DB[PX_NBLK] = {0, CMP_DB, PIT_DB, SYN_DB, SFX_DB, 0, MOD_DB, DLY_DB, REV_DB, 0};   /* (BST: the circuits set their own level) */
    static const uint8_t NT[PX_NBLK] = {1, PX_CMP_COUNT, PX_PIT_COUNT, PX_SYN_COUNT, PX_SFX_COUNT, 1,
                                        PX_MOD_COUNT, PX_DLY_COUNT, PX_REV_COUNT, 1};
    for (int b = 0; b < PX_NBLK; b++) {
        const int type = b == PX_B_COMP ? cmp_type(p->type[b]) : p->type[b] % NT[b];
        e->btr[b] = DB[b] ? powf(10.0f, DB[b][type] * 0.05f) : 1.0f;
    }
    /* preset LEVEL (v36): -15..+15 dB, 0.3 dB a step, kept in the gate block's spare 5th setting */
    e->patch_g = powf(10.0f, ((float)p->patch_volume - 50.0f) * 0.3f * 0.05f);
}

/* ---- v75: the global settings ---- */
void px_global_default(px_global_t *g) { memset(g, 0, sizeof *g); for (int i = 0; i < 5; i++) g->eq[i] = 24; }
void px_engine_globals(px_engine *e, const px_global_t *g)
{
    const float sr = e->sr;
    px_global_t q = *g;
    if (q.hpf && q.hpf < 20u) q.hpf = 20u;
    if (q.hpf > 140u) q.hpf = 140u;
    for (int i = 0; i < 5; i++) if (q.eq[i] > 48u) q.eq[i] = 48u;
    if (q.lim > 24u) q.lim = 24u;
    e->glob = q;
    e->g_hp_on = q.hpf != 0;
    if (e->g_hp_on) bq_highpass(&e->g_hp, sr, (float)q.hpf, 0.70710678f);
    int any = 0;
    for (int i = 0; i < 5; i++) any |= q.eq[i] != 24u;
    if (any) {
        static const float F[5] = {80.0f, 250.0f, 1000.0f, 3500.0f, 9000.0f};
        const float db0 = ((float)q.eq[0] - 24.0f) * 0.5f, db4 = ((float)q.eq[4] - 24.0f) * 0.5f;
        bq_lowshelf(&e->g_eq[0], sr, F[0], db0);
        for (int i = 1; i < 4; i++) bq_peak(&e->g_eq[i], sr, F[i], 0.9f, ((float)q.eq[i] - 24.0f) * 0.5f);
        bq_highshelf(&e->g_eq[4], sr, F[4], db4);
    }
    e->g_eq_n = any ? 5 : 0;
    e->g_lim_c = q.lim ? powf(10.0f, -0.5f * (float)q.lim * 0.05f) : 0.0f;
    e->g_lim_g = 1.0f;
}

/* Return the PRE / BOOST models to rest. */
static void bst_reset(px_engine *e)
{
    pxe_init(&e->enh, e->sr); pxp_rm_init(&e->rm, e->sr); pxp_ep_init(&e->ep, e->sr); pxp_bbe_init(&e->bbe, e->sr); pxp_vw_init(&e->vw, e->sr); pxp_ff_init(&e->ff, e->sr, 0); memset(&e->ap, 0, sizeof e->ap);
    memset(&e->signature, 0, sizeof e->signature);
}
/* v75: tails. A delay or reverb that was running and is now off (the user switched it, or the new preset has it off) plays out for a few seconds instead of
 * stopping, if the global TAILS setting is on: its input is muted, its settings stay as they were, and the block ends after the time (px_tick). A block
 * that is on in the new state is not spilling. was_on / was_type: the old state of the delay (0) and the reverb (1); p: the new preset. */
static void px_spill_plan(px_engine *e, const px_preset_t *p, const uint8_t *was_on, const uint8_t *was_type, int full)
{
    for (int L = 0; L < 2; L++) {
        const int b = L ? PX_B_REVERB : PX_B_DELAY;
        if (p->on[b]) {
            if (e->spill[L]) { e->spill[L] = 0; if (!full) { if (L) rc_reverb(e); else rc_delay(e); } }   /* back to the preset's own settings (px_recalc does it for a full set) */
            continue;
        }
        if (e->glob.tails && was_on[L] && e->run.on[b] && e->bf[b] > 0.0f && !e->spill[L]) {
            e->spill[L] = (int16_t)((L ? 6.0f : 3.0f) * e->sr / 64.0f);
            e->sp_type[L] = was_type[L];
        }
    }
}
void px_engine_init(px_engine *e, float sr)
{
    memset(e, 0, sizeof(*e));
    e->sr = sr;
    e->suppressor.gain = e->suppressor.hf_gain = 1.0f;
    e->hum_fade = 1.0f;
    { static const float f[6] = {50, 100, 150, 60, 120, 180};
      for (int i = 0; i < 6; ++i) e->hum_c[i] = 2.0f * cosf(2.0f * PI_F * f[i] / sr); }

    e->oct_ff = 1.0f; e->oct_ff2 = 1.0f;
    e->dly_type = -1;
    e->syn_type = -1;
    px_global_default(&e->glob); e->g_lim_g = 1.0f;
    bst_reset(e);
    px_preset_t p; px_preset_factory(&p, 0);
    px_engine_set(e, &p);
}
static void wl_clear(px_engine *e);
void px_engine_set(px_engine *e, const px_preset_t *p)
{
    e->shed = 0;                                       /* v75: a new preset: nothing shed */
    memset(e->pfs, 0, sizeof e->pfs);
    { const uint8_t wo[2] = {e->run.on[PX_B_DELAY], e->run.on[PX_B_REVERB]}, wt[2] = {e->run.type[PX_B_DELAY], e->run.type[PX_B_REVERB]}; px_spill_plan(e, p, wo, wt, 1); }
    if (p->valve_on && e->triode.mix == 0.0f) triode_reset(e);
    e->triode.mix = p->valve_on ? 1.0f : 0.0f;
    if (e->cur.type[PX_B_SFX] != p->type[PX_B_SFX] || e->cur.on[PX_B_SFX] != p->on[PX_B_SFX]) memset(&e->ad, 0, sizeof e->ad);   /* v76: ACOUSTIC DRIVE: no old state */
    if (e->cur.type[PX_B_BST] != p->type[PX_B_BST] || e->cur.on[PX_B_BST] != p->on[PX_B_BST]) bst_reset(e);   /* a different pedal, or on/off: no old state */
    e->cur = *p;
    if (px_order_valid(p->order)) for (int i = 0; i < PX_NBLK; i++) e->ord[i] = p->order[i];
    else px_order_default(e->ord);
    px_recalc(e);
    if (e->cur.on[PX_B_PITCH] && e->cur.type[PX_B_PITCH] == PX_PIT_LOWEST_HQ)
        lowest_pitch_reset(e);
    if (e->cur.type[PX_B_PITCH] == PX_PIT_POLYSHIFT) ps_reset(e);   /* a new preset: the bank from silence */
    e->run = e->cur;                                   /* v38: nothing gliding, nothing fading */
    for (int L = 0; L < 2; L++) if (e->spill[L]) { const int b = L ? PX_B_REVERB : PX_B_DELAY; e->run.on[b] = 1; e->run.type[b] = e->sp_type[L]; }   /* v75: the tails play on */
    e->rv_spill_dry = e->spill[1] > 0;
    wl_clear(e);                                       /* v39: the lanes start empty (the output fades in) */
    e->gl_left = 0;
    for (int b = 0; b < PX_NBLK; b++) { e->bf[b] = e->bfa[b] = e->run.on[b] ? 1.0f : 0.0f; e->bfd[b] = 0; }
}

/* ---- v38: changes without clicks (px_dsp.h) ---- */
#define PX_GLIDE_TICKS 16                              /* knobs: the new values in 16 blocks of 64 frames (23 ms) */
#define PX_FADE_TICKS 16                               /* a block on / off: faded over the same time */
/* the settings that glide: gains, mixes, depths, delay times (each a plain float used as it is in the processing; the
 * filters' coefficients change at once, as they always did: a small step in a filter does not click) */
#define GLF_(f) (uint32_t)offsetof(px_engine, f)
static const uint32_t GL_F[] = {
    GLF_(cmp_make), GLF_(cmp_mix), GLF_(cmp_k), GLF_(triode.drive), GLF_(triode.level),
    GLF_(oct_dry), GLF_(oct_s1), GLF_(oct_s2), GLF_(oct_up), GLF_(ps_dry), GLF_(ps_lvl[0]), GLF_(ps_lvl[1]), GLF_(erb_pitch_range),
    GLF_(psh.mix), GLF_(psh.gain),
    GLF_(sfx_mix), GLF_(sfx_c), GLF_(sfx_d),
    GLF_(bs.w[0]), GLF_(bs.w[2]),
    GLF_(syn_out), GLF_(syn_lvl[0]), GLF_(syn_lvl[1]), GLF_(syn_lvl[2]),
    GLF_(syn_fuzz.gain), GLF_(syn_fuzz_tone),
    GLF_(mod_mix), GLF_(mod_depth), GLF_(mod_fb), GLF_(mod_base), GLF_(mod_sweep), GLF_(mod_shape), GLF_(ph_lo),
    GLF_(ph_hi), GLF_(rot_bal),
    GLF_(dly_mix), GLF_(dly_fb),
    GLF_(rv_mix), GLF_(rv_out), GLF_(rv_shim),
    GLF_(patch_g),
};
#undef GLF_
#define GL_N ((int)(sizeof GL_F / sizeof GL_F[0]))
typedef char px_gl_room[GL_N <= PX_GL_MAX ? 1 : -1];
#define GLP(e, i) ((float *)((char *)(e) + GL_F[i]))

int px_engine_change(const px_engine *e, const px_preset_t *p)
{
    const px_preset_t *c = &e->cur;
    uint8_t od[PX_NBLK];
    const uint8_t *o = p->order;
    if (!px_order_valid(o)) { px_order_default(od); o = od; }
    if (memcmp(o, e->ord, PX_NBLK)) return PX_CH_FULL;
    int k = c->valve_on != p->valve_on || c->valve_drive != p->valve_drive || c->valve_level != p->valve_level || c->patch_volume != p->patch_volume;
    for (int b = 0; b < PX_NBLK; b++) {
        if (c->type[b] != p->type[b]) return PX_CH_FULL;
        if (c->on[b] != p->on[b] || memcmp(c->p[b], p->p[b], PX_NPAR)) k = 1;
    }
    return k ? PX_CH_KNOBS : PX_CH_NONE;
}
void px_engine_prepare(const px_engine *e, const px_preset_t *p)
{
    (void)e; (void)p;                                  /* (nothing to make away from the audio side any more) */
}
void px_engine_knobs(px_engine *e, const px_preset_t *p)
{
    float was[GL_N];
    for (int i = 0; i < GL_N; i++) was[i] = *GLP(e, i);
    const px_preset_t old = e->cur;
    e->cur = *p;
    if (old.on[PX_B_GATE] != p->on[PX_B_GATE] || old.on[PX_B_PITCH] != p->on[PX_B_PITCH]) rc_dyn(e);
    if (p->valve_on && !old.valve_on && e->triode.mix == 0.0f) triode_reset(e);
    e->triode.drive = 4.0f * db2lin(valve_drive_db(f01(p->valve_drive)));
    e->triode.level = valve_output_gain(p->valve_level);
    if (px_order_valid(p->order)) for (int i = 0; i < PX_NBLK; i++) e->ord[i] = p->order[i];
    for (int b = 0; b < PX_NBLK; b++)                  /* v75: a shed block that is switched or given another type is the user's again */
        if (old.on[b] != p->on[b] || old.type[b] != p->type[b]) e->shed &= (uint16_t)~(1u << b);
    { const uint8_t wo[2] = {old.on[PX_B_DELAY], old.on[PX_B_REVERB]}, wt[2] = {e->run.type[PX_B_DELAY], e->run.type[PX_B_REVERB]}; px_spill_plan(e, p, wo, wt, 0); }   /* v75: a delay or reverb switched off plays out; one switched on is the user's again */
    for (int b = 0; b < PX_NBLK; b++) {
        if (!memcmp(old.p[b], p->p[b], PX_NPAR)) continue;
        switch (b) {
        case PX_B_GATE: case PX_B_COMP: rc_dyn(e); break;
        case PX_B_PITCH: rc_pitch(e); break;
        case PX_B_SFX: rc_sfx(e); break;
        case PX_B_BST: rc_bst(e); break;
        case PX_B_EQ: rc_eq(e); break;
        case PX_B_MOD: rc_mod(e); break;
        case PX_B_DELAY: if (!e->spill[0]) rc_delay(e); break;
        case PX_B_REVERB: if (!e->spill[1]) rc_reverb(e); break;
        default: rc_syn(e); break;
        }
    }
    e->patch_g = powf(10.0f, ((float)p->patch_volume - 50.0f) * 0.3f * 0.05f);
    int any = 0;
    for (int i = 0; i < GL_N; i++) {                   /* from where they were (mid-glide too) to the new values */
        float *v = GLP(e, i);
        e->gl_t[i] = *v;
        if (*v != was[i]) any = 1;
        *v = was[i];
    }
    e->gl_left = any ? PX_GLIDE_TICKS : 0;
    if (!any) for (int i = 0; i < GL_N; i++) *GLP(e, i) = e->gl_t[i];
    for (int b = 0; b < PX_NBLK; b++) {                /* blocks switched on or off (or shed): they fade */
        const int pon = (p->on[b] && !(e->shed >> b & 1)) || (b == PX_B_DELAY && e->spill[0]) || (b == PX_B_REVERB && e->spill[1]);
        if (pon && (e->bfd[b] < 0 || !e->run.on[b] || e->bf[b] < 1.0f)) e->bfd[b] = 1;
        else if (!pon && e->run.on[b]) e->bfd[b] = -1;
    }
    for (int b = 0; b < PX_NBLK; b++) {                /* run: cur, the blocks still fading out kept on */
        uint8_t on = (uint8_t)((p->on[b] && !(e->shed >> b & 1)) || e->bfd[b] < 0 || (b == PX_B_DELAY && e->spill[0]) || (b == PX_B_REVERB && e->spill[1]));
        if (on && !e->run.on[b]) {
            e->bf[b] = e->bfa[b] = 0.0f;
            if (b == PX_B_PITCH && p->type[b] == PX_PIT_POLYOCT_HQ)
                memset(&e->erb_pitch, 0, sizeof e->erb_pitch);
            if (b == PX_B_PITCH && p->type[b] == PX_PIT_LOWEST_HQ) lowest_pitch_reset(e);
            if (b == PX_B_PITCH && p->type[b] == PX_PIT_POLYSHIFT) ps_reset(e);
            if (b == PX_B_SYN && (p->type[b] == PX_SYN_OCT_FUZZ || p->type[b] == PX_SYN_ORGAN)) {
                memset(&e->erb_syn, 0, sizeof e->erb_syn);
                syn_fuzz_reset(e);
            }
            if (b == PX_B_SYN && px_syn_bs(p->type[b])) {
                bs_reset(e);
                bs_setup(e, p->type[b], p->p[b]);
            }
            if (b == PX_B_SYN && px_syn_mono(p->type[b])) px_s2_reset(e, p->type[b], p->p[b]);
#ifdef PX_SFX_SYNTH_HQ_LIVE
            if (b == PX_B_SFX && p->type[b] == PX_SFX_SYNTH_HQ) {
                memset(&e->sfx_track, 0, sizeof e->sfx_track); pxt_reset(e, &pxs_trk, &pxs_lay);
            }
#endif
        }
        e->run.on[b] = on;
    }
    memcpy(e->run.p, p->p, sizeof p->p);
    memcpy(e->run.type, p->type, sizeof p->type);
    memcpy(e->run.order, p->order, sizeof p->order);
    e->run.valve_on = p->valve_on;
    e->run.valve_drive = p->valve_drive;
    e->run.valve_level = p->valve_level;
    e->run.patch_volume = p->patch_volume;
    for (int L = 0; L < 2; L++) if (e->spill[L]) e->run.type[L ? PX_B_REVERB : PX_B_DELAY] = e->sp_type[L];   /* v75: a tail keeps the type it had */
}
/* ---- v75: load shedding ---- */
int px_engine_heaviest(const px_engine *e)
{
    int best = -1; float bv = 0.5f;
    for (int b = 0; b < PX_NBLK; b++) {
        if (b == PX_B_GATE || !e->run.on[b] || (e->shed >> b & 1) || e->bf[b] < 1.0f) continue;
        if (e->wl_on && (b == PX_B_DELAY || b == PX_B_REVERB)) continue;
        if (e->pfs[b] > bv) { bv = e->pfs[b]; best = b; }
    }
    return best;
}
void px_engine_shed(px_engine *e, int b)
{
    if (b < 0 || b >= PX_NBLK || b == PX_B_GATE || (e->shed >> b & 1)) return;
    e->shed |= (uint16_t)(1u << b);
    px_engine_knobs(e, &e->cur);                       /* the same settings: the block fades out */
}
void px_engine_unshed(px_engine *e)
{
    if (!e->shed) return;
    e->shed = 0;
    px_engine_knobs(e, &e->cur);                       /* the blocks fade back in, from rest */
}
/* once per block, before it: the glides and the fades move on (called by the stage that runs first) */
static void px_tick(px_engine *e)
{
    if (e->gl_left > 0) {
        const float k = 1.0f / (float)e->gl_left;
        for (int i = 0; i < GL_N; i++) { float *v = GLP(e, i); *v += (e->gl_t[i] - *v) * k; }
        if (--e->gl_left == 0) for (int i = 0; i < GL_N; i++) *GLP(e, i) = e->gl_t[i];
    }
    for (int L = 0; L < 2; L++)                        /* v75: a tail's time is up: the block goes off for good, and takes the settings of the preset again */
        if (e->spill[L] > 0 && --e->spill[L] == 0) {
            const int b = L ? PX_B_REVERB : PX_B_DELAY;
            if (L) rc_reverb(e); else rc_delay(e);
            if (!e->cur.on[b]) e->bfd[b] = -1;
        }
    for (int b = 0; b < PX_NBLK; b++) {
        e->bfa[b] = e->bf[b];
        if (!e->bfd[b]) continue;
        float f = e->bf[b] + (float)e->bfd[b] * (1.0f / (float)PX_FADE_TICKS);
        if (f >= 1.0f) { f = 1.0f; e->bfd[b] = 0; }
        else if (f <= 0.0f) { f = 0.0f; e->bfd[b] = 0; e->run.on[b] = 0; }
        e->bf[b] = f;
    }
}

/* ========================================================== processing ===== */
/* Reduced two-stage germanium fuzz voicing. The biased stages clip at
 * different levels. Coupling and bandwidth are set around this shared core. */
static inline float fuzz_face_clip(float input)
{
    const float first = px_tanh(0.82f * input + 0.22f) - px_tanh(0.22f);
    return px_tanh(4.1f * first - 0.32f) + px_tanh(0.32f);
}

static inline float clip_run(const px_drive_state *state, float d)
{
    float a, y;
    switch (state->clip) {
    case CL_SOFT: return px_tanh(d);
    case CL_ASYM: return px_tanh(d + state->bias) - state->off;
    case CL_HARD:
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return d < 0.0f ? -y : y;
    case CL_GE: return d / (1.0f + px_abs(d));
    case CL_LED:
        a = px_abs(d); y = a >= 1.0f ? 1.0f : a * (1.5f - 0.5f * a * a);
        return d < 0.0f ? -y : y;
    case CL_MOS:
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return 0.5f * (px_tanh(d) + (d < 0.0f ? -y : y));
    case CL_MUFF:                                      /* two cascaded clipping stages */
        d = px_tanh(d) * 3.0f;
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return d < 0.0f ? -y : y;
    case CL_FUZZ:                                      /* starved transistor: asymmetric, optional gate */
        if (state->gate > 0.0f) { a = px_abs(d); if (a < state->gate) d *= a / state->gate; }
        d += state->bias;
        return d >= 0.0f ? px_tanh(d) : 0.7f * px_tanh(1.4f * d);
    case CL_OCTA:                                      /* full-wave rectifier: octave up, then fuzz */
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return 0.7f * px_tanh(1.5f * a) + 0.3f * (d < 0.0f ? -y : y);
    case CL_FUZZ_FACE: return fuzz_face_clip(d);
    default: return px_tanh(d * 0.4f) * 2.5f;          /* clean boost */
    }
}
static inline float drive_pre(px_drive_state *state, float x)   /* base rate */
{
    x = bq_run(&state->hp, x);
    if (state->pre & 2) x = bq_run(&state->lp, x);
    if (state->pre & 4) x = bq_run(&state->mid, x);
    if (state->pre & 1) x = x * state->shelf_lo + bq_run(&state->shelf, x);
    return x;
}
/* ---- v38: oversampling by polyphase IIR half-band filters (as the stock firmware does: two all-pass branches, each
 * section y = c (x - y1) + x1, one multiply; the branches run at the lower rate). x4 = two x2 stages: stage 1 (44.1 <->
 * 88.2 kHz) passes 0..20 kHz and stops 24.1..44.1 kHz by 78 dB with 6 sections; stage 2 (88.2 <-> 176.4 kHz) has 20..68.2
 * kHz to turn and does it with 2 sections (66 dB). Coefficients: Laurent de Soras' design (HIIR), tools/halfband.py. The
 * 4th-order Butterworth filters of v37 stopped 24..30 kHz by only 6..13 dB: that much of what the clipping made up there
 * came back down as aliasing. */
static const float HB1[6] = {0.062889286f, 0.224015205f, 0.424695820f, 0.616864442f, 0.782113520f, 0.927573355f};
static const float HB2[2] = {0.132097462f, 0.574998436f};
/* One branch at a time (each branch is a chain of its own: 3 sections in stage 1, 1 in stage 2), so that its state and
 * coefficients stay in registers through the loop (on the pedal the FPU works in the 16 general registers) */
static void hb_br3(float *xs, float *ys, float c0, float c1, float c2, const float *in, float *out, int n)
{   /* out[2i] (stride 2) = the branch on in[2i] (stride 2): x2 up and down both take every other sample */
    float x0 = xs[0], x1 = xs[1], x2 = xs[2], y0 = ys[0], y1 = ys[1], y2 = ys[2];
    for (int i = 0; i < n; i++) {
        float a = in[2 * i], t;
        t = (a - y0) * c0 + x0; x0 = a; y0 = t; a = t;
        t = (a - y1) * c1 + x1; x1 = a; y1 = t; a = t;
        t = (a - y2) * c2 + x2; x2 = a; y2 = t;
        out[2 * i] = t;
    }
    xs[0] = x0; xs[1] = x1; xs[2] = x2; ys[0] = y0; ys[1] = y1; ys[2] = y2;
}
static void hb_br1(float *xs, float *ys, float c0, const float *in, float *out, int n)
{
    float x0 = xs[0], y0 = ys[0];
    for (int i = 0; i < n; i++) {
        float a = in[2 * i], t = (a - y0) * c0 + x0;
        x0 = a; y0 = t;
        out[2 * i] = t;
    }
    xs[0] = x0; ys[0] = y0;
}
/* x2 up: in[n] -> out[2n]: the even branch (coefficients 0, 2, 4) makes out[2i], the odd one out[2i + 1], both from in[i]
 * (state: s->x/y[0..2] the even branch, [3..5] the odd one); x2 down: in[2i + 1] through the even branch, in[2i] through
 * the odd one, out[i] = their mean. The stride-2 reads and writes go through px_os_d. */
static float px_os_d[4 * PX_MAX_BLOCK] PX_POOL_TAIL(px_os_d);
static void hb1_up(px_hb *s, const float *in, float *out, int n, float *d)
{
    for (int i = 0; i < n; i++) d[2 * i] = in[i];
    hb_br3(s->x, s->y, HB1[0], HB1[2], HB1[4], d, out, n);
    hb_br3(s->x + 3, s->y + 3, HB1[1], HB1[3], HB1[5], d, out + 1, n);
}
static void hb1_down(px_hb *s, const float *in, float *out, int n, float *d)
{
    hb_br3(s->x, s->y, HB1[0], HB1[2], HB1[4], in + 1, d, n);
    hb_br3(s->x + 3, s->y + 3, HB1[1], HB1[3], HB1[5], in, d + 1, n);
    for (int i = 0; i < n; i++) out[i] = 0.5f * (d[2 * i] + d[2 * i + 1]);
}
static void hb2_up(px_hb *s, const float *in, float *out, int n, float *d)
{
    for (int i = 0; i < n; i++) d[2 * i] = in[i];
    hb_br1(s->x, s->y, HB2[0], d, out, n);
    hb_br1(s->x + 1, s->y + 1, HB2[1], d, out + 1, n);
}
static void hb2_down(px_hb *s, const float *in, float *out, int n, float *d)
{
    hb_br1(s->x, s->y, HB2[0], in + 1, d, n);
    hb_br1(s->x + 1, s->y + 1, HB2[1], in, d + 1, n);
    for (int i = 0; i < n; i++) out[i] = 0.5f * (d[2 * i] + d[2 * i + 1]);
}
#undef HB_SEC
static float px_os_m[2 * PX_MAX_BLOCK] PX_LO(px_os_m);                /* (stage 1 <-> 2) */
static void os_up(px_hb *st, const float *in, float *out, int n, int os, float *middle, float *exchange)   /* os 2 or 4: st[0] stage 1, st[1] stage 2 */
{
    if (os == 2) { hb1_up(&st[0], in, out, n, exchange); return; }
    hb1_up(&st[0], in, middle, n, exchange);
    hb2_up(&st[1], middle, out, 2 * n, exchange);
}
static void os_down(px_hb *st, const float *in, float *out, int n, int os, float *middle, float *exchange)   /* st[2] stage 2, st[3] stage 1 */
{
    if (os == 2) { hb1_down(&st[3], in, out, n, exchange); return; }
    hb2_down(&st[2], in, middle, 2 * n, exchange);
    hb1_down(&st[3], middle, out, n, exchange);
}

/* the clipper over n oversampled samples in place, stage by stage; the anti-denormal offset follows the base-rate
 * sample (os samples each) */
static float px_os_t[PX_OVERSAMPLE * PX_MAX_BLOCK] PX_LO(px_os_t),
             px_os_u[PX_OVERSAMPLE * PX_MAX_BLOCK] PX_LO(px_os_u);
/* v40.4: the delay's and the reverb's wet output when they run in the chain: not on px_engine_part's stack (the second
 * core's is 2.8 KB) */
static float px_wb[2][PX_MAX_BLOCK] PX_LO(px_wb);

static inline float triode_lookup(const float *table, unsigned last, float minimum, float steps, float volts)
{
    const float index = (volts - minimum) * steps;
    if (!(index > 0.0f)) return table[0];
    if (index >= (float)last) return table[last];
    const unsigned i = (unsigned)index;
    return table[i] + (index - (float)i) * (table[i + 1] - table[i]);
}

static inline float triode_grid_current(float volts)
{
    if (volts <= PXT_GRID_TAIL_MIN)
        return triode_lookup(PXT_GRID, sizeof PXT_GRID / sizeof *PXT_GRID - 1, PXT_GRID_MIN, PXT_TABLE_STEPS, volts);
    const unsigned last = sizeof PXT_GRID_TAIL / sizeof *PXT_GRID_TAIL - 1;
    if (volts > PXT_GRID_TAIL_MAX)
        return PXT_GRID_TAIL[last] + (volts - PXT_GRID_TAIL_MAX) * PXT_GRID_TAIL_SLOPE;
    return triode_lookup(PXT_GRID_TAIL, last, PXT_GRID_TAIL_MIN, PXT_GRID_TAIL_STEPS, volts);
}

typedef struct { float plate, cathode; } triode_currents;

static inline triode_currents triode_load_line(float vin, float vk)
{
    const float ig = triode_grid_current(vin - vk);
    const float vg = vin - PXT_STOPPER * ig;
    const float effective = vg - PXT_GRID_FACTOR * vk + PXT_SUPPLY_TERM + PXT_PLATE_FEEDBACK * ig;
    const float ik = triode_lookup(PXT_CATHODE, sizeof PXT_CATHODE / sizeof *PXT_CATHODE - 1,
                                   PXT_CATHODE_MIN, PXT_TABLE_STEPS, effective);
    const float ia = ik - ig;
    /* The measured fit loses accuracy below 20 V plate-to-cathode. From that point,
     * roll off excess current with a continuous slope toward the available load-line
     * current. Charge the cathode with the corrected plate current plus grid current.
     */
    const float knee_v = 20.0f;
    const float knee_current = (PXT_SUPPLY - vk - knee_v) / PXT_PLATE_KOHM;
    if (ia > knee_current) {
        const float excess = ia - knee_current;
        const float plate = knee_current + excess / (1.0f + (PXT_PLATE_KOHM / knee_v) * excess);
        return (triode_currents){plate, plate + ig};
    }
    return (triode_currents){ia, ik};
}

/* Common-cathode 12AX7, 250 V / 100k plate, 1.5k || 2.2uF cathode and 33k grid stopper.
 * The tables solve the plate and grid load lines from Dempwolf & Zölzer (DAFx 2011).
 * Integrate the cathode capacitor with current held over each 2x sample. A 22 ms output
 * coupling time constant represents a high-impedance load. DRIVE raises the 4 V per
 * engine unit input scale by up to 24 dB. Reciprocal makeup matches small-signal
 * gain and the output polarity is corrected. The half-band pair filters the stage's
 * correction to the dry signal.
 */
static __attribute__((noinline)) void triode_block(px_engine *e, float *samples, int n)
{
    const float drive = e->triode.drive, makeup = PXT_PLATE_SCALE / drive;
    hb1_up(&e->triode.up, samples, px_os_u, n, px_os_d);
    for (int i = 0; i < 2 * n; ++i) {
        const float x = px_os_u[i], vin = drive * x, vk = e->triode.cathode_v;
        const triode_currents current = triode_load_line(vin, vk);
        e->triode.cathode_v += e->triode.cathode_step * (PXT_CATHODE_KOHM * current.cathode - vk);
        const float wet = (current.plate - PXT_PLATE_IDLE) * makeup;
        e->triode.output_dc += e->triode.dc_step * (wet - e->triode.output_dc);
        px_os_u[i] = wet - e->triode.output_dc - x;
    }
    hb1_down(&e->triode.down, px_os_u, px_os_t, n, px_os_d);
    const float level = e->triode.level;
    if (e->triode.mix == 1.0f && e->run.valve_on) {
        for (int i = 0; i < n; ++i) samples[i] = (samples[i] + px_os_t[i]) * level;
        return;
    }
    const float goal = e->run.valve_on ? 1.0f : 0.0f;
    const float delta = (goal ? 1.0f : -1.0f) / (0.02f * e->sr);
    for (int i = 0; i < n; ++i) {
        float mix = e->triode.mix + delta;
        e->triode.mix = mix < 0.0f ? 0.0f : mix > 1.0f ? 1.0f : mix;
        samples[i] += e->triode.mix * ((samples[i] + px_os_t[i]) * level - samples[i]);
    }
}

static void drive_block(px_drive_state *state, float *u, int n, int os, float *t, float *h)   /* (static buffers: off the interrupt's stack) */
{
    for (int i = 0; i < n; i++) {
        const float dn = ((i / os) & 1) ? 1e-15f : -1e-15f;
        float d = clip_run(state, u[i] * state->gain) + dn;
        if (state->dcb) { state->dcs += (d - state->dcs) * state->dck; d -= state->dcs; }
        t[i] = d;
    }
    if (state->tm == TM_MUFF) {
        memcpy(h, t, (unsigned)n * sizeof(float));
        bq_block(&state->t1, t, n);
        bq_block(&state->t2, h, n);
        for (int i = 0; i < n; i++) t[i] = t[i] + (h[i] - t[i]) * state->mix;
    } else if (state->np & 2) {
        bq_block2(&state->t1, &state->t2, t, n);
    } else bq_block(&state->t1, t, n);
    if (state->np & 4) bq_block(&state->t3, t, n);
    for (int i = 0; i < n; i++) u[i] = t[i] * state->level + u[i] * state->blend;
}
/* POLY OCT FUZZ's clipper over a block, oversampled (shares px_os_* with VALVE) */
static __attribute__((noinline)) void drive_buffer(px_drive_state *state, px_hb *filters, float *samples, int n, int os)
{
    float *up = px_os_u, *tone = px_os_t, *middle = px_os_m, *exchange = px_os_d;
    for (int i = 0; i < n; ++i) samples[i] = drive_pre(state, samples[i]);
    os_up(filters, samples, up, n, os, middle, exchange);
    drive_block(state, up, os * n, os, tone, exchange);
    os_down(filters, up, samples, n, os, middle, exchange);
}

static inline float rd_frac(const float *buf, int mask, float pos)   /* pos in [0, mask+1) */
{
    int i = (int)pos; float fr = pos - (float)i;
    return buf[i] + (buf[(i + 1) & mask] - buf[i]) * fr;
}
static inline float hermite(float y0, float y1, float y2, float y3, float f)   /* 4-point cubic between y1 and y2 */
{
    float c1 = 0.5f * (y2 - y0), c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2), c2 = y0 - y1 + c1 - c3;
    return ((c3 * f + c2) * f + c1) * f + y1;
}
static inline float rd_herm(const float *buf, int mask, float pos)
{
    int i = (int)pos; float f = pos - (float)i;
    return hermite(buf[(i - 1) & mask], buf[i & mask], buf[(i + 1) & mask], buf[(i + 2) & mask], f);
}
/* 16-bit memories: values are rounded (exact decay times); once a tail is down near the last bits, they are cut
 * towards zero instead, so the loop dies out instead of holding a tiny pattern for ever (limit cycle) */
#define DL_Q 16384.0f                                  /* delay memory: 16 bits, +-2.0 */
static inline int16_t dl_q(float v, float rq)
{
    v = v * DL_Q + (v >= 0.0f ? rq : -rq);
    if (v > 32767.0f) v = 32767.0f; else if (v < -32767.0f) v = -32767.0f;
    return (int16_t)v;
}
static inline float dly_read(const int16_t *buf, int i, float f)   /* between i and i + 1 (i in 0..N-1) */
{
    int a = i - 1, b = i + 1, c = i + 2;
    if (a < 0) a += PX_DLY_N;
    if (b >= PX_DLY_N) b -= PX_DLY_N;
    if (c >= PX_DLY_N) c -= PX_DLY_N;
    return hermite((float)buf[a], (float)buf[i], (float)buf[b], (float)buf[c], f) * (1.0f / DL_Q);
}
static inline float dly_tap(const px_engine *e, float d)
{
    int di = (int)d;                                   /* whole samples apart from the fraction: no float wrap */
    float f = d - (float)di;
    int i = e->dly_w - di - 1;                         /* d = di + f behind the write: between i and i + 1, 1 - f */
    if (i < 0) i += PX_DLY_N;
    return dly_read(e->dly_buf, i, 1.0f - f);
}
static inline float mod_tap(const px_engine *e, float d)
{
    float pos = (float)e->mod_w - d;
    if (pos < 0.0f) pos += 2048.0f;
    return rd_herm(e->mod_buf, 2047, pos);
}
/* ---- reverb (v35) ----
 * One 16-bit ring for every line: the write pointer turns once per tick, unit u writes at (ptr + base u) and what it
 * wrote k ticks ago is at (ptr + base u + k). The tank works in the ring's units (x RV_Q): no scaling per access. */
#define RV_Q 16384.0f                                  /* reverb memory: 16 bits, +-2.0 */
#define RV_MASK (PX_RV_M - 1)
#define RV_RD(u, k) ((float)m[(p + b[u] + (k)) & RV_MASK])
static inline int16_t rv_q(float v, float rq)          /* rq: 0.5 = rounded, 0 = cut towards zero */
{
    union { float f; uint32_t u; } a, r;
    a.f = v; r.f = rq; r.u |= a.u & 0x80000000u;        /* rq with v's sign */
    v += r.f;
    v = v > 32767.0f ? 32767.0f : v;
    v = v < -32767.0f ? -32767.0f : v;
    return (int16_t)v;
}
#define RV_WR(u, v) (m[(p + b[u]) & RV_MASK] = rv_q((v), rq))
static inline float rv_lin(const int16_t *m, int at, float k)      /* fractional read, k ticks back from `at` */
{
    int ki = (int)k; float f = k - (float)ki;
    float a = (float)m[(at + ki) & RV_MASK];
    return a + ((float)m[(at + ki + 1) & RV_MASK] - a) * f;
}
/* pre-delay, early reflections, Dattorro's 4 input diffusers */
PX_AINL static inline float rv_front(px_engine *e, int16_t *m, int p, const int *b, float rq, float in, float *er)
{
    RV_WR(0, in);
    float r = 0.0f;
    for (int j = 0; j < e->rv_ner; j++) r += e->rv_erg[j] * RV_RD(0, e->rv_ert[j]);
    *er = r;
    float d = RV_RD(0, e->rv_pd);
    if (e->rv_alg) { e->rv_s[0] += e->rv_bw * (d - e->rv_s[0]); d = e->rv_s[0]; }
    float t, w;                                        /* gains 0.75, 0.75, 0.625, 0.625 */
    t = RV_RD(1, e->rv_n[1]); w = d - 0.75f * t;  RV_WR(1, w); d = t + 0.75f * w;
    t = RV_RD(2, e->rv_n[2]); w = d - 0.75f * t;  RV_WR(2, w); d = t + 0.75f * w;
    t = RV_RD(3, e->rv_n[3]); w = d - 0.625f * t; RV_WR(3, w); d = t + 0.625f * w;
    t = RV_RD(4, e->rv_n[4]); w = d - 0.625f * t; RV_WR(4, w); d = t + 0.625f * w;
    return d;
}
PX_INL static inline float rv_fdn(px_engine *e, float in)
{
    int16_t *m = e->rv_m; const int p = e->rv_ptr, *b = e->rv_b; const float rq = e->rv_rq;
    float er, d = rv_front(e, m, p, b, rq, in * RV_Q, &er), y[8];
    e->rv_ph1 = wrap1(e->rv_ph1 + e->rv_inc1);
    e->rv_ph2 = wrap1(e->rv_ph2 + e->rv_inc2);
    float md = e->rv_mdep;
    y[0] = rv_lin(m, p + b[5], e->rv_d[5] + md * (1.0f + lfo_sin(e->rv_ph1)));
    y[1] = rv_lin(m, p + b[6], e->rv_d[6] + md * (1.0f + lfo_sin(wrap1(e->rv_ph1 + 0.25f))));
    y[2] = rv_lin(m, p + b[7], e->rv_d[7] + md * (1.0f + lfo_sin(e->rv_ph2)));
    y[3] = rv_lin(m, p + b[8], e->rv_d[8] + md * (1.0f + lfo_sin(wrap1(e->rv_ph2 + 0.25f))));
    y[4] = RV_RD(9, e->rv_n[9]); y[5] = RV_RD(10, e->rv_n[10]); y[6] = RV_RD(11, e->rv_n[11]); y[7] = RV_RD(12, e->rv_n[12]);
    float o = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * 0.35355339f;
    float *s = e->rv_s;
    for (int i = 0; i < 8; i++) s[i] = e->rv_g[i] * y[i] + e->rv_p[i] * s[i];   /* decay + damping per line */
    float a0 = s[0] + s[1], a1 = s[0] - s[1], a2 = s[2] + s[3], a3 = s[2] - s[3];    /* Hadamard 8 */
    float a4 = s[4] + s[5], a5 = s[4] - s[5], a6 = s[6] + s[7], a7 = s[6] - s[7];
    float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
    float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
    const float h = 0.35355339f;
    RV_WR(5, (b0 + b4) * h + d);  RV_WR(6, (b1 + b5) * h - d);
    RV_WR(7, (b2 + b6) * h + d);  RV_WR(8, (b3 + b7) * h - d);
    RV_WR(9, (b0 - b4) * h + d);  RV_WR(10, (b1 - b5) * h - d);
    RV_WR(11, (b2 - b6) * h + d); RV_WR(12, (b3 - b7) * h - d);
    return (o + er) * (1.0f / RV_Q);
}
PX_INL static inline float rv_plate(px_engine *e, float in)
{
    int16_t *m = e->rv_m; const int p = e->rv_ptr, *b = e->rv_b; const float rq = e->rv_rq;
    float er, d = rv_front(e, m, p, b, rq, in * RV_Q, &er);
    e->rv_ph1 = wrap1(e->rv_ph1 + e->rv_inc1);
    float md = e->rv_mdep, dec = e->rv_dec, dm = e->rv_damp, g2 = e->rv_dd2, t, w;
    /* left half of the tank: modulated all-pass (-0.7), delay, damping, decay, all-pass, delay, decay */
    t = rv_lin(m, p + b[5], e->rv_d[5] + md * (1.0f + lfo_sin(e->rv_ph1)));
    w = d + e->rv_fr + 0.7f * t; RV_WR(5, w);
    float l = t - 0.7f * w;
    t = RV_RD(6, e->rv_n[6]); RV_WR(6, l);
    e->rv_s[1] = t + (e->rv_s[1] - t) * dm;
    t = RV_RD(7, e->rv_n[7]); w = e->rv_s[1] * dec - g2 * t; RV_WR(7, w);
    l = t + g2 * w;
    t = RV_RD(8, e->rv_n[8]); RV_WR(8, l);
    float lend = t * dec;
    /* right half */
    t = rv_lin(m, p + b[9], e->rv_d[9] + md * (1.0f + lfo_sin(wrap1(e->rv_ph1 + 0.25f))));
    w = d + e->rv_fl + 0.7f * t; RV_WR(9, w);
    float r = t - 0.7f * w;
    t = RV_RD(10, e->rv_n[10]); RV_WR(10, r);
    e->rv_s[2] = t + (e->rv_s[2] - t) * dm;
    t = RV_RD(11, e->rv_n[11]); w = e->rv_s[2] * dec - g2 * t; RV_WR(11, w);
    r = t + g2 * w;
    t = RV_RD(12, e->rv_n[12]); RV_WR(12, r);
    e->rv_fl = lend; e->rv_fr = t * dec;
    /* output: Dattorro's left and right taps, summed */
    const int *tp = e->rv_tap;
    float o = RV_RD(10, tp[0]) + RV_RD(10, tp[1]) - RV_RD(11, tp[2]) + RV_RD(12, tp[3]) - RV_RD(6, tp[4]) - RV_RD(7, tp[5]) - RV_RD(8, tp[6])
            + RV_RD(6, tp[7]) + RV_RD(6, tp[8]) - RV_RD(7, tp[9]) + RV_RD(8, tp[10]) - RV_RD(10, tp[11]) - RV_RD(11, tp[12]) - RV_RD(12, tp[13]);
    return o * (0.3f / RV_Q);
}
static inline float ps_voice(const float *buf, int w, float *ph, float inc, float W)
{                                                      /* plain 2-tap shifter (shimmer: smeared anyway) */
    float a = *ph + inc;
    a = wrap1(a);
    *ph = a;
    float b = a + 0.5f; if (b >= 1.0f) b -= 1.0f;
    float pa = (float)w - (3.0f + a * W); if (pa < 0.0f) pa += (float)PX_PS_N;
    float pb = (float)w - (3.0f + b * W); if (pb < 0.0f) pb += (float)PX_PS_N;
    float s = lfo_sin(a * 0.5f), g = s * s;            /* sin^2 / cos^2 crossfade: sums to 1 */
    return rd_frac(buf, PX_PS_N - 1, pa) * g + rd_frac(buf, PX_PS_N - 1, pb) * (1.0f - g);
}
#define PS_OFFMAX 512                                  /* one period of low E */
#if !PX_RUNS
PX_HOT static float ps_align(const float *buf, int w, float d_ref, float d_new)
{                                                      /* SOLA: offset that puts the fading-in tap in phase */
    int r0 = w - (int)d_ref, best = 0;
    float bc = -1e30f;
    for (int pass = 0; pass < 2; pass++) {             /* coarse (every 8 offsets, every 4th sample), then fine */
        int lo = pass ? best - 7 : 0, hi = pass ? best + 7 : PS_OFFMAX, st = pass ? 1 : 8, b2 = best, js = pass ? 2 : 4;
        if (lo < 0) lo = 0;
        for (int o = lo; o <= hi; o += st) {
            int c0 = w - (int)(d_new + (float)o);
            float acc = 0.0f, en = 1e-9f;
            for (int j = 0; j < 64; j += js) {
                float x = buf[(r0 - j) & (PX_PS_N - 1)], y = buf[(c0 - j) & (PX_PS_N - 1)];
                acc += x * y; en += y * y;
            }
            acc = acc / sqrtf(en);                     /* normalised: loudness does not bias the choice */
            if (acc > bc) { bc = acc; b2 = o; }
        }
        best = b2;
    }
    return (float)best;
}
#else
/* v40.4: the same search, the same sums in the same order, with less work a term (a search took 41,000 CPU cycles, 4 %
 * of an audio block, and the church organ starts one every 100 samples): the reference's 16 (then 32) samples are
 * read once for all the offsets, the candidate's are read straight on where its window does not wrap, and the terms are
 * written out four at a time. The square root is px_math.c's, operation for operation. */
PX_AINL static inline float ps_sqrt(float x)            /* firmware/px_math.c sqrtf */
{
    if (x <= 0.0f) return 0.0f;
    union { float f; uint32_t i; } b;
    b.f = x;
    b.i = 0x1FBD1DF5u + (b.i >> 1);
    float y = b.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}
#define PS_T(i) { const float y_ = q[-(i) * js]; acc += xs[i] * y_; en += y_ * y_; }
#define PS_TM(i) { const float y_ = buf[(c0 - (i) * js) & (PX_PS_N - 1)]; acc += xs[i] * y_; en += y_ * y_; }
PX_HOT static float ps_align(const float *buf, int w, float d_ref, float d_new, float *xs)   /* xs: 32 floats of scratch */
{                                                      /* SOLA: offset that puts the fading-in tap in phase */
    const int r0 = w - (int)d_ref;
    int best = 0;
    float bc = -1e30f;
    for (int pass = 0; pass < 2; pass++) {             /* coarse (every 8 offsets, every 4th sample), then fine */
        int lo = pass ? best - 7 : 0, hi = pass ? best + 7 : PS_OFFMAX, st = pass ? 1 : 8, b2 = best;
        const int js = pass ? 2 : 4, nt = 64 / js;
        if (lo < 0) lo = 0;
        for (int i = 0; i < nt; i++) xs[i] = buf[(r0 - i * js) & (PX_PS_N - 1)];
        for (int o = lo; o <= hi; o += st) {
            const int c0 = (w - (int)(d_new + (float)o)) & (PX_PS_N - 1);
            float acc = 0.0f, en = 1e-9f;
            if (c0 >= 63) {                            /* (the window in one piece) */
                const float *q = buf + c0;
                for (int i = 0; i < nt; i += 4) { PS_T(i) PS_T(i + 1) PS_T(i + 2) PS_T(i + 3) }
            } else
                for (int i = 0; i < nt; i += 4) { PS_TM(i) PS_TM(i + 1) PS_TM(i + 2) PS_TM(i + 3) }
            acc = acc / ps_sqrt(en);                   /* normalised: loudness does not bias the choice */
            if (acc > bc) { bc = acc; b2 = o; }
        }
        best = b2;
    }
    return (float)best;
}
#undef PS_T
#undef PS_TM
#endif
/* v40.4: the runs' work buffers are not on the stack (the second core's is 2.8 KB, and px_engine_part alone takes 1.9 KB
 * of it): one set a block, in the low RAM. A block's run is never on both cores at once (it is before the amp or after
 * it), and nothing is kept in them from one call to the next */
#if !PX_RUNS                                           /* (the Bluetooth test image's pitch shifters, sample by sample; PX_RUNS=1: ps_step / pitch_run) */
PX_INL static inline float ps_voice_al(float *buf, int w, float *ph, float *off, float inc, float W)
{
    float a0 = *ph, a = wrap1(a0 + inc);
    *ph = a;
    float b = a + 0.5f; if (b >= 1.0f) b -= 1.0f;
    int wa = inc > 0.0f ? (a < a0) : (a > a0);                                  /* tap A restarts (silent) */
    int wb = inc > 0.0f ? (a0 < 0.5f && a >= 0.5f) : (a0 >= 0.5f && a < 0.5f);   /* tap B restarts (silent) */
    float da = 3.0f + a * W, db = 3.0f + b * W;
    if (wa) off[0] = ps_align(buf, w, db + off[1], da);
    if (wb) off[1] = ps_align(buf, w, da + off[0], db);
    float pa = (float)w - (da + off[0]); if (pa < 0.0f) pa += (float)PX_PS_N;
    float pb = (float)w - (db + off[1]); if (pb < 0.0f) pb += (float)PX_PS_N;
    float s = lfo_sin(a * 0.5f), g = s * s;
    return rd_frac(buf, PX_PS_N - 1, pa) * g + rd_frac(buf, PX_PS_N - 1, pb) * (1.0f - g);
}
#endif
#if PX_RUNS
static float px_ps_xs[2][32] PX_LO(px_ps_xs);          /* ps_align's reference samples: [0] PITCH, [1] SYN */
static float px_run_y[2][32] PX_LO(px_run_y);          /* a run's output so far: [0] PITCH, [1] SYN */
#endif


/* ---- SYN: synth / instrument, one sample ---- */
static inline float polyblep(float t, float dt)        /* band-limited step correction (saw / pulse edges) */
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static inline float syn_saw(float ph, float dt) { return 2.0f * ph - 1.0f - polyblep(ph, dt); }

/* Two stable periods acquire a note. A rolling median rejects isolated bad
 * crossings afterwards. One quarter-rate phase keeps the two subs coherent. */

static inline float synth_hq_pulse(const px_synth_tracking *track, int harmonic, float width)
{
    if (track->step <= 0.0f) return 0.0f;
    float phase = track->phase * (float)harmonic;
    phase -= (float)(int)phase;
    const float advance = track->step * (float)harmonic;
    return syn_saw(phase, advance) - syn_saw(wrap1(phase + width), advance);
}

/* Clean-input control path. Peak release bridges low fundamentals; the sustain
 * follower and hold prevent rectifier ripple from modulating the audio gain.
 * Upward noise learning is capped below threshold and frozen throughout notes. */
static void suppressor_detect(px_engine *e, float x, float *gain, float *hf)
{
    const float a = px_abs(x);
    e->suppressor.peak = a > e->suppressor.peak ? a : a + (e->suppressor.peak - a) * e->suppressor.peak_rel;
    const float pk = e->suppressor.peak;
    e->suppressor.mean += (a - e->suppressor.mean) * (1.0f - e->suppressor.peak_rel);
    float env = e->suppressor.sustain;
    env += (pk - env) * (pk > env ? 0.02f : 1.0f - e->suppressor.sustain_rel);
    e->suppressor.sustain = env;
    const float minimum = e->suppressor.sensitivity;
    float floor = e->suppressor.floor;
    if (pk < floor) floor += (pk - floor) * e->suppressor.fall;
    else if (!e->suppressor.open && pk < minimum * 0.5f)
        floor += (pk - floor) * e->suppressor.learn;
    e->suppressor.floor = floor;
    const float threshold = minimum + 2.0f * floor;
    if (pk > threshold || (e->suppressor.open && env > threshold * 0.45f)) {
        e->suppressor.open = 1; e->suppressor.remaining = e->suppressor.hold;
    } else if (e->suppressor.remaining) --e->suppressor.remaining;
    else e->suppressor.open = 0;
    float target = 1.0f;
    if (!e->suppressor.disabled && !e->suppressor.open) {
        float u = env / (threshold + 1e-12f);
        if (u > 1.0f) u = 1.0f;
        const float u2 = u * u, u4 = u2 * u2;
        target = u4 * u4;
        if (target < 0.0001f) target = 0.0001f;
    }
    e->suppressor.gain += (target - e->suppressor.gain) *
        (target > e->suppressor.gain ? e->suppressor.attack : e->suppressor.release);
    *gain = e->suppressor.gain;
    *hf = 1.0f;
    if (e->suppressor.hush > 0.0f) {
        e->suppressor.hf_split += e->suppressor.split_k * (x - e->suppressor.hf_split);
        const float high = px_abs(x - e->suppressor.hf_split);
        e->suppressor.hf_env += (high - e->suppressor.hf_env) * (high > e->suppressor.hf_env ? 0.04f : 0.0005f);
        float nf = e->suppressor.hf_floor;
        const float he = e->suppressor.hf_env;
        if (he < nf) nf += (he - nf) * e->suppressor.fall;
        else if ((!e->suppressor.open || (e->suppressor.disabled && pk < 0.006f)) && he > pk * 0.12f && pk > e->suppressor.mean * 1.85f)
            nf += (he - nf) * (e->suppressor.fall * 0.3f);
        e->suppressor.hf_floor = nf;
        const float noise = nf + 0.00001f;
        float u = (e->suppressor.hf_env - noise) / (noise * 2.5f);
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f || pk > env * 1.4f) u = 1.0f;
        const float ht = 1.0f - e->suppressor.hush * 0.85f * (1.0f - u);
        e->suppressor.hf_gain += (ht - e->suppressor.hf_gain) * (ht > e->suppressor.hf_gain ? e->suppressor.attack : e->suppressor.release);
        *hf = e->suppressor.hf_gain;
    } else if (e->suppressor.hf_gain < 1.0f) {
        e->suppressor.hf_gain += (1.0f - e->suppressor.hf_gain) * e->suppressor.attack;
        if (e->suppressor.hf_gain > 0.99999f) e->suppressor.hf_gain = 1.0f;
        *hf = e->suppressor.hf_gain;
    }
}
static float suppressor_sample(px_engine *e, float x, float gain, float hf)
{
    if (e->suppressor.hush > 0.0f || hf < 1.0f) {
        e->suppressor.output_lp += e->suppressor.split_k * (x - e->suppressor.output_lp);
        x -= (1.0f - hf) * (x - e->suppressor.output_lp);
    }
    return x * gain;
}
/* Two independent banks avoid coefficient switching in a live recursive filter. */
static void hum_input(px_engine *e, float *x, int n)
{
    int want = e->hum_mode == 1 ? 50 : e->hum_mode == 2 ? 60 : e->hum_mode == 3 ? (e->hum_hz == 60 ? 60 : 50) : 0;
    if (!e->hum_mode && !e->hum_current && e->hum_fade >= 1.0f) return;
    if (e->hum_mode == 3) hum_detect(e, x, n);
    if (want != e->hum_current && e->hum_fade >= 1.0f) {
        memcpy(e->hum_old, e->hum, sizeof e->hum);
        e->hum_pending = e->hum_current;
        e->hum_current = want;
        memset(e->hum, 0, sizeof e->hum);
        for (int i = 0; i < 3; ++i) if (want) bq_notch(&e->hum[i], e->sr, (float)(want * (i + 1)), 2.0f, 25.0f);
        e->hum_fade = 0.0f;
    }
    for (int k = 0; k < n; ++k) {
        float a = x[k], b = x[k];
        if (e->hum_current) for (int i = 0; i < 3; ++i) a = bq_run(&e->hum[i], a);
        if (e->hum_fade < 1.0f) {
            if (e->hum_pending) for (int i = 0; i < 3; ++i) b = bq_run(&e->hum_old[i], b);
            x[k] = b + (a - b) * e->hum_fade;
            e->hum_fade += 1.0f / (0.020f * e->sr);
            if (e->hum_fade > 1.0f) e->hum_fade = 1.0f;
        } else x[k] = a;
    }
}
/* ---- v40.4: blocks over a run of samples, pass by pass ----
 * px_engine_part ran these blocks sample by sample inside its own loop: every sample went through the block switch and
 * the type switch, and the block's state was read from and written to the engine at each one (the function is too large
 * for the compiler to keep any of it in registers). Each routine below runs one block type over the run, its state in
 * locals; a block with several voices runs them one after the other over the run (a pass each), which leaves few
 * enough values alive for the registers. The operations on each sample, and their order, are the per-sample code's:
 * the output is the same, bit for bit (the engine's golden output). */
#if PX_RUNS
#define PS_RUN 32                                      /* a run: the shifters read up to 1,985 samples back in a 2,048-sample
                                                          ring, so no more than 63 new samples may be written ahead of them */
/* a shifter voice over n samples from ring position w: y[k] += its sample x lvl (ps_voice_al, with *ph and off in locals) */
PX_AINL static inline float ps_step(const float *buf, int w, float *pa0, float *po0, float *po1, float inc, float W, float *xs)
{
    const float a0 = *pa0, a = wrap1(a0 + inc);
    *pa0 = a;
    float b = a + 0.5f; if (b >= 1.0f) b -= 1.0f;
    const int wa = inc > 0.0f ? (a < a0) : (a > a0);
    const int wb = inc > 0.0f ? (a0 < 0.5f && a >= 0.5f) : (a0 >= 0.5f && a < 0.5f);
    const float da = 3.0f + a * W, db = 3.0f + b * W;
    if (wa) *po0 = ps_align(buf, w, db + *po1, da, xs);
    if (wb) *po1 = ps_align(buf, w, da + *po0, db, xs);
    float pa = (float)w - (da + *po0); if (pa < 0.0f) pa += (float)PX_PS_N;
    float pb = (float)w - (db + *po1); if (pb < 0.0f) pb += (float)PX_PS_N;
    const float s = lfo_sin(a * 0.5f), g = s * s;
    return rd_frac(buf, PX_PS_N - 1, pa) * g + rd_frac(buf, PX_PS_N - 1, pb) * (1.0f - g);
}
static __attribute__((noinline)) void ps_voice_run(const float *buf, int w, float *ph, float *off, float inc, float W,
                                                   float lvl, float *y, int n, float *xs)
{
    float a0 = *ph, o0 = off[0], o1 = off[1];
    for (int k = 0; k < n; k++) {
        y[k] = y[k] + ps_step(buf, w, &a0, &o0, &o1, inc, W, xs) * lvl;
        if (++w >= PX_PS_N) w = 0;
    }
    *ph = a0; off[0] = o0; off[1] = o1;
}
/* PITCH's shifters: POLY OCT, WHAMMY, HARMONY, DETUNE */
static __attribute__((noinline)) void pitch_run(px_engine *e, int type, float *xb, int n0, float strim)
{
    if (type == PX_PIT_LOWEST_HQ) {
        for (int k = 0; k < n0; ++k) xb[k] = lowest_pitch_run(e, xb[k]) * strim;
        e->dn = ((n0 - 1) & 1) ? 1e-15f : -1e-15f;
        return;
    }
    if (type == PX_PIT_POLYSHIFT) {                    /* the bank's shifted voice, mixed with the input (no delay: the bank's own few ms) */
        const float m = e->psh.mix;
        for (int k = 0; k < n0; ++k) {
            const float x = xb[k];
            xb[k] = x * (1.0f - m) + ps_run(e, x) * m * strim;   /* (the level trim is the wet voice's: MIX 0 is the input, unchanged) */
        }
        e->dn = ((n0 - 1) & 1) ? 1e-15f : -1e-15f;
        return;
    }
    if (type == PX_PIT_POLYOCT_HQ) {
        for (int k = 0; k < n0; ++k) {
            const float x = xb[k];
            xb[k] = (x * e->ps_dry + erb_octaves(e, &e->erb_pitch, x, e->ps_lvl[1], e->ps_lvl[0], e->erb_pitch_range)) * strim;
        }
        e->dn = ((n0 - 1) & 1) ? 1e-15f : -1e-15f;
        return;
    }
    for (int at = 0; at < n0; at += PS_RUN, xb += PS_RUN) {
        const int n = n0 - at < PS_RUN ? n0 - at : PS_RUN, w0 = e->ps_w;
        float *const y = px_run_y[0], *const xs = px_ps_xs[0];
        {
            const float dry = e->ps_dry;
            float *buf = e->ps_buf;
            int w = w0;
            for (int k = 0; k < n; k++) {
                const float x = xb[k];
                buf[w] = x;
                if (++w >= PX_PS_N) w = 0;
                y[k] = x * dry;
            }
            e->ps_w = w;
        }
        if (type == PX_PIT_WHAMMY) {                   /* (one voice, its ratio gliding at every sample) */
            float r = e->ps_r, a0 = e->ps_ph[0], o0 = e->ps_off[0][0], o1 = e->ps_off[0][1], inc = e->ps_inc[0];
            const float rt = e->ps_rt, gl = e->ps_glide, invW = e->ps_invW, W = e->ps_W[0], lvl = e->ps_lvl[0];
            int w = w0;
            for (int k = 0; k < n; k++) {
                r += (rt - r) * gl;
                inc = (1.0f - r) * invW;
                if (e->ps_nv > 0) y[k] = y[k] + ps_step(e->ps_buf, w, &a0, &o0, &o1, inc, W, xs) * lvl;
                if (++w >= PX_PS_N) w = 0;
            }
            e->ps_r = r; e->ps_inc[0] = inc;
            e->ps_ph[0] = a0; e->ps_off[0][0] = o0; e->ps_off[0][1] = o1;
        } else
            for (int v = 0; v < e->ps_nv; v++)
                ps_voice_run(e->ps_buf, w0, &e->ps_ph[v], e->ps_off[v], e->ps_inc[v], e->ps_W[v], e->ps_lvl[v], y, n, xs);
        for (int k = 0; k < n; k++) xb[k] = y[k] * strim;
    }
    e->dn = ((n0 - 1) & 1) ? 1e-15f : -1e-15f;
}
/* GATE, COMP */
static __attribute__((noinline)) void gate_run(px_engine *e, int on, float *xb, int n, float strim, const float *sg, const float *hf)
{
    float env = e->gate_env, g = e->gate_g;
    const float thr = e->gate_thr, rel = e->gate_rel;
    for (int k = 0; k < n; k++) {
        float x = xb[k];
        if (on && e->suppressor.active) x = suppressor_sample(e, x, sg[k], hf[k]);
        else if (on) {
            const float a = px_abs(x);
            env = (a > env) ? a : a + (env - a) * 0.9995f;
            const float tg = (env > thr) ? 1.0f : 0.0f;
            g = (tg > g) ? tg : tg + (g - tg) * rel;
            if (g < 1e-6f) g = 0.0f;                   /* closed: exact zero, never a denormal */
            x *= g;
        }
        x += (k & 1) ? 1e-15f : -1e-15f;
        xb[k] = x * strim;
    }
    e->gate_env = env; e->gate_g = g;
    if (n > 0) e->dn = ((n - 1) & 1) ? 1e-15f : -1e-15f;
}
static __attribute__((noinline)) void comp_run(px_engine *e, int swell, float *xb, int n, float strim)
{
    float env = e->cmp_env;
    const float att = e->cmp_att, rel = e->cmp_rel, make = e->cmp_make;
    if (swell) {
        float slow = e->cmp_slow, sw = e->cmp_sw;
        const float slowk = e->cmp_slowk, thr = e->cmp_thr, rise = e->cmp_rise;
        int lw = e->cmp_lw;
        for (int k = 0; k < n; k++) {
            const float x = xb[k], a = px_abs(x), c = (a > env) ? att : rel;
            env = a + (env - a) * c;
            slow += (env - slow) * slowk;
            if (env > 1.5f * slow + thr) sw = 0.0f;    /* new pick attack */
            else sw += (1.0f - sw) * rise;
            e->cmp_la[lw] = x;
            lw = (lw + 1) & 127;
            xb[k] = e->cmp_la[lw] * sw * sw * make * strim;
        }
        e->cmp_slow = slow; e->cmp_sw = sw; e->cmp_lw = lw;
    } else {
        const float ck = e->cmp_k, mix = e->cmp_mix;
        for (int k = 0; k < n; k++) {
            float x = xb[k];
            const float a = px_abs(x), c = (a > env) ? att : rel;
            env = a + (env - a) * c;
            const float y = x * make / (1.0f + ck * env * 4.0f);
            x = x + (y - x) * mix;
            xb[k] = x * strim;
        }
    }
    e->cmp_env = env;
    if (n > 0) e->dn = ((n - 1) & 1) ? 1e-15f : -1e-15f;
}
#endif

/* ---- v38: the delay and the reverb over a block, each type's branches decided once for the block (on the pedal a taken
 * branch costs about 8 cycles: the per-sample code had 6 to 10 of them). Each call with constants is its own copy of
 * the loop; the operations and their order are the per-sample code's, so the output is the same, bit for bit. */
PX_AINL static inline void dly_loop(px_engine *e, const float *xb, float *wo, int n, const int T, const int M, const int FL,
                                    const int Q, const int S)
{
    /* v39: in a wet lane its output is heard lg frames late: the taps heard read lg frames less far back, the ones fed
     * back keep the delay time (the repeats keep their spacing). v40: the line runs at half rate, as the stock delays
     * (one tick every two samples), so lg is in ticks there */
    const float lg = e->wl_dlag * 0.5f;
    for (int k = 0; k < n; k++) {
        float x = xb[k];
        if (!e->dly_tick) {                            /* v40: the first sample of a pair: kept, the last output held */
            e->dly_xa = x; e->dly_tick = 1;
            wo[k] = e->dly_y1 * e->dly_mix;
            continue;
        }
        x = 0.5f * (e->dly_xa + x);                    /* the tick's input: the pair averaged (a zero at 22.05 kHz) */
            e->dly_len += (e->dly_tgt - e->dly_len) * 0.0008f;   /* time glides (no click); 0.0004 a sample before */
            float rd = e->dly_len, d, fsrc;
            if (M) {
                e->dly_ph = wrap1(e->dly_ph + e->dly_inc);
                rd += (e->dly_wow + e->dly_mod) * (1.0f + lfo_sin(e->dly_ph));
                if (FL) { e->dly_ph2 = wrap1(e->dly_ph2 + e->dly_inc2); rd += e->dly_flut * (1.0f + lfo_sin(e->dly_ph2)); }
            }
            if (T == PX_DLY_REVERSE) {
                int G = (int)e->dly_len;
                if (e->rev_cnt >= G) { e->rev_cnt = 0; e->rev_start = e->dly_w; }
                int rp = e->rev_start - 1 - e->rev_cnt; if (rp < 0) rp += PX_DLY_N;
                float c = (float)e->rev_cnt, wg = c * e->rev_xf, w2 = (float)(G - e->rev_cnt) * e->rev_xf;
                if (wg > w2) wg = w2;
                if (wg > 1.0f) wg = 1.0f;
                d = (float)e->dly_buf[rp] * (1.0f / DL_Q) * wg;
                e->rev_cnt++;
                fsrc = d;
            } else if (T == PX_DLY_SPACE) {
                float s = 0.0f;
                if (e->dly_heads & 1) s += dly_tap(e, rd * 0.33333f);
                if (e->dly_heads & 2) s += dly_tap(e, rd * 0.66667f);
                if (e->dly_heads & 4) s += dly_tap(e, rd);
                d = s * e->dly_hn; fsrc = s * e->dly_hf;   /* loop gain stays <= FDBK whatever the heads */
                if (lg > 0.0f) {
                    s = 0.0f;
                    if (e->dly_heads & 1) s += dly_tap(e, rd * 0.33333f - lg);
                    if (e->dly_heads & 2) s += dly_tap(e, rd * 0.66667f - lg);
                    if (e->dly_heads & 4) s += dly_tap(e, rd - lg);
                    d = s * e->dly_hn;
                }
            } else if (T == PX_DLY_DUAL) {
                float a = dly_tap(e, rd), b = dly_tap(e, rd * 0.75f);
                fsrc = a;
                if (lg > 0.0f) { a = dly_tap(e, rd - lg); b = dly_tap(e, rd * 0.75f - lg); }
                d = 0.6f * (a + 0.8f * b);
            } else {
                d = dly_tap(e, rd); fsrc = d;
                if (lg > 0.0f) d = dly_tap(e, rd - lg);
            }
            if (Q) {
                float v = fsrc * e->dly_q;
                fsrc = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) / e->dly_q;
                if (lg > 0.0f) { v = d * e->dly_q; d = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) / e->dly_q; }
                else d = fsrc;
            }
            float fbk = bq_run(&e->dly_hp, bq_run(&e->dly_lp, fsrc)) * e->dly_fb;
            if (S) fbk = px_tanh(fbk * e->dly_sat) * e->dly_satn;   /* unity for small signals */
            float wv = x + fbk, av = px_abs(wv);
            e->dly_env = av > e->dly_env ? av : e->dly_env * 0.9999f;   /* (0.99995 a sample before) */
            e->dly_rq = e->dly_env < 2e-4f ? 0.0f : 0.5f;
            e->dly_buf[e->dly_w] = dl_q(wv, e->dly_rq);
            if (++e->dly_w >= PX_DLY_N) e->dly_w = 0;
        {   /* v40: a gentle lift of the top (+1 dB at 5 kHz), for what the pair's average and the interpolation take off */
            const float dl = 1.15f * d - 0.15f * e->dly_dp;
            e->dly_dp = d; d = dl;
        }
        e->dly_y0 = e->dly_y1; e->dly_y1 = d; e->dly_tick = 0;
        wo[k] = 0.5f * (e->dly_y0 + d) * e->dly_mix;   /* v39: the wet part only (wo may be xb: x is read first) */
    }
}
static void dly_block(px_engine *e, const float *xb, float *wo, int n, int t)
{
    const int m = e->dly_wow > 0.0f || e->dly_mod > 0.0f, fl = e->dly_flut > 0.0f, q = e->dly_q > 0.0f, s = e->dly_sat > 0.0f;
    switch (t) {                                       /* the combinations each type sets (rc_delay) */
    case PX_DLY_DIGITAL: if (!m && !fl && !q && !s) { dly_loop(e, xb, wo, n, PX_DLY_DIGITAL, 0, 0, 0, 0); return; } break;
    case PX_DLY_DUAL:    if (!m && !fl && !q && !s) { dly_loop(e, xb, wo, n, PX_DLY_DUAL, 0, 0, 0, 0); return; } break;
    case PX_DLY_REVERSE: if (!m && !fl && !q && !s) { dly_loop(e, xb, wo, n, PX_DLY_REVERSE, 0, 0, 0, 0); return; } break;
    case PX_DLY_ANALOG:  if (m && !fl && !q && s) { dly_loop(e, xb, wo, n, PX_DLY_ANALOG, 1, 0, 0, 1); return; } break;
    case PX_DLY_MEMORY:  if (m && !fl && !q && s) { dly_loop(e, xb, wo, n, PX_DLY_MEMORY, 1, 0, 0, 1); return; } break;
    case PX_DLY_TAPE:
        if (m && !q && s) { if (fl) dly_loop(e, xb, wo, n, PX_DLY_TAPE, 1, 1, 0, 1); else dly_loop(e, xb, wo, n, PX_DLY_TAPE, 1, 0, 0, 1); return; }
        break;
    case PX_DLY_SPACE:
        if (m && !q && s) { if (fl) dly_loop(e, xb, wo, n, PX_DLY_SPACE, 1, 1, 0, 1); else dly_loop(e, xb, wo, n, PX_DLY_SPACE, 1, 0, 0, 1); return; }
        break;
    case PX_DLY_LOFI:    if (m && !fl && q && s) { dly_loop(e, xb, wo, n, PX_DLY_LOFI, 1, 0, 1, 1); return; } break;
    }
    dly_loop(e, xb, wo, n, t, m, fl, q, s);                /* (anything else: decided per sample, as before) */
}
PX_AINL static inline void rv_loop(px_engine *e, const float *xb, float *wo, int n, const int ALG, const int SP, const int SH,
                                   const int GT)
{
    for (int k = 0; k < n; k++) {
        e->dn = (k & 1) ? 1e-15f : -1e-15f;
        float x = xb[k];
            float inr = x * e->rv_in + e->sh_out + e->dn;
            if (SP) {                            /* spring dispersion: chirpy all-pass chain */
                float a = e->sp_a;
                for (int i = 0; i < 6; i++) { float y = -a * inr + e->sp_z[i]; e->sp_z[i] = inr + a * y; inr = y; }
            }
            float acc;
            if (!e->rv_tick) { e->rv_xa = inr; e->rv_tick = 1; acc = e->rv_y1; }
            else {                                     /* one tick of the tank every 2 samples */
                float in2 = 0.5f * (e->rv_xa + inr);
                float y = ALG ? rv_plate(e, in2) : rv_fdn(e, in2), aa = px_abs(y) + px_abs(in2);
                e->rv_ptr = (e->rv_ptr - 1) & RV_MASK;
                e->rv_env = aa > e->rv_env ? aa : e->rv_env * 0.9999f;
                /* the last bits of a tail: fade it out over ~0.5 s, then let the ring empty itself (cut towards zero) */
                if (e->rv_env < 5e-4f) e->rv_fade *= 0.9997f; else e->rv_fade = 1.0f;
                e->rv_rq = e->rv_fade < 0.03f ? 0.0f : 0.5f;
                y = bq_run(&e->rv_hp, y * e->rv_fade * e->rv_out);   /* v40: the low cut at the tank's rate, as the stock */
                e->rv_y0 = e->rv_y1; e->rv_y1 = y; e->rv_tick = 0;
                acc = 0.5f * (e->rv_y0 + y);
            }
            acc = bq_run(&e->rv_tone, acc);            /* (TONE at the full rate: it also takes the half rate's images down) */
            if (SH) {                   /* octave-up shifter in the feedback loop */
                e->sh_buf[e->sh_w] = acc;
                float s = ps_voice(e->sh_buf, e->sh_w, &e->sh_ph, -1.0f / 1024.0f, 1024.0f);
                if (++e->sh_w >= PX_PS_N) e->sh_w = 0;
                e->sh_lp += (s - e->sh_lp) * e->sh_k;
                e->sh_hp += (e->sh_lp - e->sh_hp) * e->sh_hk;   /* v35: only the upper octave goes round again */
                e->sh_out = (e->sh_lp - e->sh_hp) * e->rv_shim;
            } else e->sh_out = 0.0f;
            if (GT) {
                float a = px_abs(x);
                e->rv_genv = a > e->rv_genv ? a : a + (e->rv_genv - a) * 0.999f;
                if (e->rv_genv > 0.01f) e->rv_hold_n = (int)e->rv_hold;
                float tg = 0.0f;
                if (e->rv_hold_n > 0) { e->rv_hold_n--; tg = 1.0f; }
                e->rv_gg += (tg - e->rv_gg) * (tg > e->rv_gg ? 0.05f : e->rv_gate_rel);
                if (e->rv_gg < 1e-6f) e->rv_gg = 0.0f;
                acc *= e->rv_gg;
            }
        wo[k] = acc * e->rv_mix * 1.4f;                /* v39: the wet part only; the dry part: x * (1 - 0.4 mix) */
    }
}
static void rv_block(px_engine *e, const float *xb, float *wo, int n)
{
    const int alg = e->rv_alg != 0, sp = e->sp_on != 0, sh = e->rv_shim > 0.0f, gt = e->rv_gate != 0;
    if (!alg && !sp && !sh && !gt) rv_loop(e, xb, wo, n, 0, 0, 0, 0);          /* room, hall, cathedral, ambient */
    else if (alg && !sp && !sh && !gt) rv_loop(e, xb, wo, n, 1, 0, 0, 0);      /* plate */
    else if (!alg && sp && !sh && !gt) rv_loop(e, xb, wo, n, 0, 1, 0, 0);      /* spring */
    else rv_loop(e, xb, wo, n, alg, sp, sh, gt);                                 /* shimmer, gated (and anything else) */
}

/* ---- v39: the wet lanes (px_dsp.h). [lane][buffer][frame]: 2 KB of RAM ---- */
static float px_wl[2][2][PX_WL_N] PX_LO(px_wl);
static void wl_clear(px_engine *e)
{
    e->wl_b = 0;
    for (int L = 0; L < 2; L++) {
        e->wl_rd[L] = e->wl_p[L] = 0;
        for (int b = 0; b < 2; b++) { e->wl_n[L][b] = 0; e->wl_w[L][b] = 0; }
    }
}
void px_wet_mode(px_engine *e, int on, int lag)
{
    wl_clear(e);
    e->wl_late = 0;
    e->wl_on = (uint8_t)(on != 0);
    e->wl_dlag = on ? (float)lag : 0.0f;
}
void px_wet_swap(px_engine *e)
{
    const int b = e->wl_b ^ 1;                         /* the buffer made wet meanwhile: the chain's now */
    e->wl_b = (uint8_t)b;
    for (int L = 0; L < 2; L++) {
        e->wl_rd[L] = e->wl_w[L][b] ? e->wl_n[L][b] : 0;
        e->wl_w[L][b] = 0; e->wl_n[L][b] = 0; e->wl_p[L] = 0;
    }
}
int px_wet_run(px_engine *e, uint32_t deadline, volatile const uint8_t *stop)
{
    const int b = e->wl_b ^ 1;                         /* the sends of the block before */
    for (int L = 0; L < 2; L++) {
        const int n = e->wl_n[L][b];
        if (!n || e->wl_w[L][b]) continue;
        const uint32_t t0 = px_clock ? px_clock() : 0u;
        float *bw = px_wl[L][b];
        const int dly_t = e->run.type[PX_B_DELAY] % PX_DLY_COUNT;
        for (int o = 0; o < n; o += 16) {
            if ((stop && *stop) || (deadline && px_clock && (int32_t)(px_clock() - deadline) > 0)) {
                e->wl_n[L][b] = (uint16_t)o;           /* the rest: silence (the lines miss it too) */
                e->wl_w[L][b] = 1;
                e->wl_late++;
                if (px_clock) e->wl_pf[L] += px_clock() - t0;
                return 0;
            }
            const int m = n - o < 16 ? n - o : 16;
            if (L) rv_block(e, bw + o, bw + o, m); else dly_block(e, bw + o, bw + o, m, dly_t);
        }
        e->wl_w[L][b] = 1;
        if (px_clock) e->wl_pf[L] += px_clock() - t0;
    }
    return 1;
}

/* OpenPocket Synth: one pass over the chain, block by block (part: PX_PART_ALL only). */
PX_HOT void px_engine_part(px_engine *e, const float *in, float *out, int n, int part)
{
    px_tick(e);                                        /* v38: glides and fades move on, once a block */
    const px_preset_t *p = &e->run;                    /* v38: with the blocks still fading out */
    const float sr = e->sr;
    const int cmp_t = cmp_type(p->type[PX_B_COMP]);
    const int pit_t = px_type_canonical(PX_B_PITCH, p->type[PX_B_PITCH] % PX_PIT_COUNT);
    const int sfx_t = p->type[PX_B_SFX] % PX_SFX_COUNT;
    const int mod_t = p->type[PX_B_MOD] % PX_MOD_COUNT;
    const int bst_t = p->type[PX_B_BST] % PX_BST_COUNT;
    const int dly_t = p->type[PX_B_DELAY] % PX_DLY_COUNT;
    const int syn_t = p->type[PX_B_SYN];               /* (a retired or unknown type is not run: px_syn_runs) */
    /* the blocks to run, in this preset's order (GATE always: it carries the anti-denormal offset) */
    uint8_t st[PX_NBLK];
    float tr[PX_NBLK];                                 /* each block's level trim */
    int ns = 0;
    const int sfx_gone = px_type_retired(PX_B_SFX, sfx_t);
    for (int i = 0; i < PX_NBLK; i++) {
        int b = e->ord[i];
        if (b != PX_B_GATE && !p->on[b]) continue;
        if (b == PX_B_SYN && !px_syn_runs(syn_t)) continue;   /* Synth2: a retired SYNTH type is bypassed (docs/research/synth2/SYNTH2-PLAN.md) */
        if (b == PX_B_PITCH && pit_t == PX_PIT_DROP) continue;
        if (b == PX_B_SFX && sfx_gone) continue;   /* APPLAUSE was removed, and v75 moved three pedals to BST: a stored one is bypassed, its settings kept */
        tr[ns] = e->btr[b];
        st[ns++] = (uint8_t)b;
    }
    const float pg = e->patch_g;
    (void)part;
    {
    /* v37: block by block over the n samples (each block's state stays in registers through its loop; the order of
     * each sample's operations is unchanged, so the output is the same as sample by sample) */
    float xb[PX_MAX_BLOCK], sg[PX_MAX_BLOCK], hf[PX_MAX_BLOCK];
    for (int k = 0; k < n; k++) {
        e->dn = (k & 1) ? 1e-15f : -1e-15f;            /* anti-denormal (-300 dB): digital silence never decays */
        float x = in[k] + e->dn;
        { float y = x - e->dc_x1 + 0.9995f * e->dc_y1; e->dc_x1 = x; e->dc_y1 = y; x = y; }
        xb[k] = x;
    }
    if (e->g_hp_on) bq_block(&e->g_hp, xb, n);         /* v75: the global input high-pass */
    hum_input(e, xb, n);
    if (e->suppressor.active && p->on[PX_B_GATE]) for (int k = 0; k < n; ++k) suppressor_detect(e, xb[k], &sg[k], &hf[k]);
    /* Synth2: the mono synth follows the clean guitar, before the gate, the octave and every other effect (docs/research/synth2/SYNTH2-PLAN.md): its
     * output waits here and replaces the signal where the SYNTH block stands in the order */
    float sy[PX_MAX_BLOCK];
    const int syn2 = p->on[PX_B_SYN] && px_syn_mono(syn_t);
    if (syn2) px_synth_run(e, xb, sy, n);
    if (p->valve_on || e->triode.mix > 0.0f) triode_block(e, xb, n);
    for (int s = 0; s < ns; s++) {
        const int sblk = st[s];
        const float strim = tr[s];
        /* v38: a block fading in or out: its output mixed with its input, the mix ramped over the block */
        const uint32_t pt0 = px_clock ? px_clock() : 0u;   /* v38: each block's time (px_clock: the firmware's) */
        const float fa = e->bfa[sblk], fz = e->bf[sblk];
        const int fading = p->on[sblk] && (fa < 1.0f || fz < 1.0f);   /* (GATE runs off too: not a fade) */
        float dryb[PX_MAX_BLOCK];
        if (fading) for (int k = 0; k < n; k++) dryb[k] = xb[k];
        if (sblk == PX_B_DELAY || sblk == PX_B_REVERB) {   /* v38: over the block, each type's own loop */
            if (p->on[sblk]) {
                const int L = sblk == PX_B_REVERB, sp = e->spill[L] > 0;   /* v75: a tail playing out: nothing is sent in, the dry stays whole */
                float dry = 1.0f, dry_step = 0.0f;
                if (L) {
                    const float live_dry = 1.0f - 0.4f * e->rv_mix;
                    dry = e->rv_spill_dry ? 1.0f : live_dry;
                    if (sp != e->rv_spill_dry) {
                        const float target = sp ? 1.0f : live_dry;
                        dry_step = (target - dry) / (float)n;
                        e->rv_spill_dry = (uint8_t)sp;
                    }
                }
                if (e->wl_on && e->wl_p[L] + n <= PX_WL_N) {   /* v39: the lane: the send in, two blocks old wet out */
                    float *bw = px_wl[L][e->wl_b] + e->wl_p[L];
                    const int rd = (int)e->wl_rd[L] - (int)e->wl_p[L];
                    for (int k = 0; k < n; k++) {
                        const float x = xb[k], w = k < rd ? bw[k] : 0.0f;
                        bw[k] = sp ? 0.0f : x;
                        xb[k] = x * dry + w;
                        dry += dry_step;
                    }
                    e->wl_p[L] = (uint16_t)(e->wl_p[L] + n);
                    e->wl_n[L][e->wl_b] = e->wl_p[L];
                } else {
                    float *const wb = px_wb[L];           /* (the lanes off: one core runs the whole chain) */
                    float zb[PX_MAX_BLOCK];
                    if (sp) for (int k = 0; k < n; k++) zb[k] = 0.0f;
                    const float *const src = sp ? zb : xb;
                    if (L) {
                        rv_block(e, src, wb, n);
                        for (int k = 0; k < n; k++) {
                            xb[k] = xb[k] * dry + wb[k];
                            dry += dry_step;
                        }
                    } else {
                        dly_block(e, src, wb, n, dly_t);
                        for (int k = 0; k < n; k++) xb[k] = xb[k] + wb[k];
                    }
                }
            }
            for (int k = 0; k < n; k++) xb[k] *= strim;
            goto faded;
        }
        /* v40.4: the blocks that run over the block pass by pass (above); the others sample by sample, below */
        if (sblk == PX_B_SYN && px_syn_mono(syn_t)) {
            float g0, g1; px_s2_dry(&g0, &g1);             /* ARCADE's guitar share: the signal that reaches this block (the blocks before it in the order have worked on it) */
            const float dg = (g1 - g0) / (float)n;
            for (int k = 0; k < n; ++k) xb[k] = sy[k] * strim + xb[k] * (g0 + dg * (float)k);   /* (the type's level trim is the synth's: the guitar's own share stays as the MIX sets it) */
            goto faded;
        }
        if (sblk == PX_B_SYN && syn_t == PX_SYN_ORGAN) { organ_run(e, xb, n, strim); goto faded; }
        if (sblk == PX_B_SYN && px_syn_bs(syn_t)) { bs_run(e, syn_t, xb, n, strim); goto faded; }
        if (sblk == PX_B_SYN && syn_t == PX_SYN_OCT_FUZZ) {
            for (int k = 0; k < n; ++k) {
                const float x = xb[k];
                xb[k] = x + erb_octaves(e, &e->erb_syn, x, e->syn_lvl[0], e->syn_lvl[1], ERB_FULL);
            }
            drive_buffer(&e->syn_fuzz, e->syn_fuzz_os, xb, n, PX_OVERSAMPLE);
            for (int k = 0; k < n; ++k) {
                const float x = xb[k];
                e->syn_lp += e->syn_lpk * (x - e->syn_lp);
                xb[k] = (x + e->syn_fuzz_tone * (x - e->syn_lp)) * e->syn_out * strim;
            }
            goto faded;
        }
#if PX_RUNS
        if (sblk == PX_B_PITCH && p->on[PX_B_PITCH] && pit_t != PX_PIT_OCTAVER) {
            pitch_run(e, pit_t, xb, n, strim);
            goto faded;
        }
        if (sblk == PX_B_GATE) { gate_run(e, p->on[PX_B_GATE], xb, n, strim, sg, hf); goto faded; }
        if (sblk == PX_B_COMP && p->on[PX_B_COMP]) { comp_run(e, cmp_t == PX_CMP_SWELL, xb, n, strim); goto faded; }
        if (sblk == PX_B_EQ && p->on[PX_B_EQ]) {       /* its filters one after the other over the block, as the cabinet's */
            bq_chain(e->eq, e->eq_n, xb, n);
            for (int k = 0; k < n; k++) xb[k] *= strim;
            goto faded;
        }
#endif
        for (int k = 0; k < n; k++) {
        e->dn = (k & 1) ? 1e-15f : -1e-15f;
        float x = xb[k];
        switch (sblk) {
        case PX_B_GATE:
            /* ---- gate ---- */
            if (p->on[PX_B_GATE] && e->suppressor.active) x = suppressor_sample(e, x, sg[k], hf[k]);
            else if (p->on[PX_B_GATE]) {
                float a = px_abs(x);
                e->gate_env = (a > e->gate_env) ? a : a + (e->gate_env - a) * 0.9995f;
                float tg = (e->gate_env > e->gate_thr) ? 1.0f : 0.0f;
                e->gate_g = (tg > e->gate_g) ? tg : tg + (e->gate_g - tg) * e->gate_rel;
                if (e->gate_g < 1e-6f) e->gate_g = 0.0f;   /* closed: exact zero, never a denormal */
                x *= e->gate_g;
            }
            x += e->dn;
        break;
        case PX_B_COMP:
            /* ---- compressor ---- */
            if (p->on[PX_B_COMP]) {
                float a = px_abs(x);
                float c = (a > e->cmp_env) ? e->cmp_att : e->cmp_rel;
                e->cmp_env = a + (e->cmp_env - a) * c;
                if (cmp_t == PX_CMP_SWELL) {
                    e->cmp_slow += (e->cmp_env - e->cmp_slow) * e->cmp_slowk;
                    if (e->cmp_env > 1.5f * e->cmp_slow + e->cmp_thr) e->cmp_sw = 0.0f;   /* new pick attack (v36: 1.5, was 1.8) */
                    else e->cmp_sw += (1.0f - e->cmp_sw) * e->cmp_rise;
                    /* v36: the gain acts 2.9 ms late (look-ahead): the attack is caught before it is heard */
                    e->cmp_la[e->cmp_lw] = x;
                    e->cmp_lw = (e->cmp_lw + 1) & 127;
                    x = e->cmp_la[e->cmp_lw] * e->cmp_sw * e->cmp_sw * e->cmp_make;
                } else {
                    float y = x * e->cmp_make / (1.0f + e->cmp_k * e->cmp_env * 4.0f);
                    x = x + (y - x) * e->cmp_mix;
                }
            }
        break;
        case PX_B_PITCH:
            /* ---- pitch ---- */
            if (p->on[PX_B_PITCH]) {
                if (pit_t == PX_PIT_OCTAVER) {
                    float b = bq_run(&e->oct_lp2, bq_run(&e->oct_lp, x));
                    float a = px_abs(b);
                    e->oct_env = a + (e->oct_env - a) * 0.995f;
                    if (e->oct_prev <= 0.0f && b > 0.0f && e->oct_env > 0.002f) {
                        e->oct_ff = -e->oct_ff;
                        if (e->oct_ff > 0.0f) e->oct_ff2 = -e->oct_ff2;
                    }
                    e->oct_prev = b;
                    float s1 = bq_run(&e->oct_sublp, e->oct_ff * e->oct_env);
                    float s2 = bq_run(&e->oct_sublp2, e->oct_ff2 * e->oct_env);
                    float u = px_abs(bq_run(&e->oct_bp, x));
                    u = bq_run(&e->oct_uphp, u);
                    x = x * e->oct_dry + s1 * e->oct_s1 + s2 * e->oct_s2 + u * e->oct_up;
                } else {
#if !PX_RUNS                                           /* (with the runs: pitch_run) */
                    if (pit_t == PX_PIT_LOWEST_HQ) {
                        x = lowest_pitch_run(e, x);
                        break;
                    }
                    if (pit_t == PX_PIT_POLYOCT_HQ) {
                        x = x * e->ps_dry + erb_octaves(e, &e->erb_pitch, x, e->ps_lvl[1], e->ps_lvl[0], e->erb_pitch_range);
                        break;
                    }
                    e->ps_buf[e->ps_w] = x;
                    if (pit_t == PX_PIT_WHAMMY) {
                        e->ps_r += (e->ps_rt - e->ps_r) * e->ps_glide;
                        e->ps_inc[0] = (1.0f - e->ps_r) * e->ps_invW;
                    }
                    float y = x * e->ps_dry;
                    for (int v = 0; v < e->ps_nv; v++)
                        y += ps_voice_al(e->ps_buf, e->ps_w, &e->ps_ph[v], e->ps_off[v], e->ps_inc[v], e->ps_W[v]) * e->ps_lvl[v];
                    if (++e->ps_w >= PX_PS_N) e->ps_w = 0;
                    x = y;
#endif
                }
            }
        break;
        case PX_B_SFX:
            /* ---- special fx ---- */
            if (p->on[PX_B_SFX]) {
                switch (sfx_t) {
                case PX_SFX_AUTOWAH: case PX_SFX_LFOWAH: {
                    float t;
                    if (sfx_t == PX_SFX_AUTOWAH) {
                        float a = px_abs(x);
                        e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                        t = e->sfx_env * e->sfx_c; if (t > 1.0f) t = 1.0f;
                        t = 250.0f + t * t * e->sfx_d;
                    } else {
                        e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc);
                        t = e->sfx_d + e->sfx_c * 0.5f * lfo_sin(e->sfx_ph);
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        t = 350.0f + t * t * 1900.0f;
                    }
                    float f = 2.0f * PI_F * t / sr;
                    float hp = x - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    x = e->sfx_bp * 1.6f + e->sfx_lo * 0.15f;
                } break;
                case PX_SFX_RING: {
                    e->sfx_ph2 = wrap1(e->sfx_ph2 + e->sfx_d);
                    e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc * (1.0f + e->sfx_c * lfo_sin(e->sfx_ph2)));
                    float c = sin_hq(e->sfx_ph);
                    x = x * (1.0f - e->sfx_mix) + x * c * e->sfx_mix * 1.4f;
                } break;
                case PX_SFX_CRUSH:
                    if (++e->crush_n >= e->crush_k) {
                        float v = x * e->crush_q;
                        e->crush_v = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) / e->crush_q;
                        e->crush_n = 0;
                    }
                    x = x + (e->crush_v - x) * e->sfx_mix;
                    break;
                case PX_SFX_SLICER: {
                    e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc);
                    int step = (int)(e->sfx_ph * 16.0f) & 15;
                    float tg = ((e->sfx_pat >> (15 - step)) & 1) ? 1.0f : 1.0f - e->sfx_c;
                    e->sfx_g += (tg - e->sfx_g) * e->sfx_a;
                    if (e->sfx_g < 1e-6f) e->sfx_g = 0.0f;
                    x *= e->sfx_g;
                } break;
                case PX_SFX_ACOUSTIC: x = ad_tick(&e->ad, x); break;
                case PX_SFX_PULSE: {
                    float a = px_abs(x); e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    float b = bq_run(&e->sfx_f1, x);
                    const float up = e->sfx_env * (0.01f + 0.32f * e->sfx_c);
                    const float down = e->sfx_env * (0.32f - 0.30f * e->sfx_c);
                    if (e->sfx_prev <= 0.0f && b > up) { e->sfx_prev = 1.0f; e->sfx_ff = -e->sfx_ff; }
                    else if (e->sfx_prev > 0.0f && b < -down) e->sfx_prev = -1.0f;
                    float sig = (e->sfx_prev + e->sfx_ff * e->sfx_d) * e->sfx_env;
                    float t = e->sfx_env * 24.0f; if (t > 1.0f) t = 1.0f;
                    float f = 2.0f * PI_F * (180.0f + t * 3200.0f) / sr;
                    float hp = sig - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    x = x * (1.0f - e->sfx_mix) + e->sfx_lo * 2.0f * e->sfx_mix;
                } break;
                case PX_SFX_CHIP: {
                    float a = px_abs(x); e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    float b = bq_run(&e->sfx_f1, x), h = e->sfx_env * 0.1f;
                    if (e->sfx_prev <= 0.0f && b > h) {
                        e->sfx_prev = 1.0f; e->sfx_ff = -e->sfx_ff;
                        uint32_t r = (uint32_t)e->sfx_pat; r ^= r << 13; r ^= r >> 17; r ^= r << 5; e->sfx_pat = (int)r;
                    } else if (e->sfx_prev > 0.0f && b < -h) e->sfx_prev = -1.0f;
                    const float noise = (e->sfx_pat & 1) ? 1.0f : -1.0f;
                    float sig = (e->sfx_prev * (1.0f - e->sfx_c) + noise * e->sfx_c + e->sfx_ff * e->sfx_d * (1.0f - e->sfx_c)) * e->sfx_env;
                    float t = e->sfx_env * 24.0f; if (t > 1.0f) t = 1.0f;
                    float f = 2.0f * PI_F * (180.0f + t * 3200.0f) / sr;
                    float hp = sig - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    x = x * (1.0f - e->sfx_mix) + e->sfx_lo * 2.0f * e->sfx_mix;
                } break;
                case PX_SFX_CROSS: {
                    const float a = px_abs(x);
                    e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    const float b = bq_run(&e->sfx_f1, x), h = e->sfx_env * 0.1f;
                    if (e->sfx_prev <= 0.0f && b > h) { e->sfx_prev = 1.0f; e->sfx_ff = -e->sfx_ff; }
                    else if (e->sfx_prev > 0.0f && b < -h) e->sfx_prev = -1.0f;
                    const float sig = (e->sfx_prev + e->sfx_ff * e->sfx_d) * e->sfx_env;
                    const float env = e->sfx_env > 1.0f ? 1.0f : e->sfx_env;
                    const float cutoff = 180.0f + 6200.0f * e->sfx_c + 7000.0f * env;
                    /* The two-integrator update is stable for f² + 2*damping*f < 4.
                     * Keep headroom at full damping when the envelope drives cutoff high. */
                    const float step = 2.0f * PI_F * cutoff / sr;
                    const float f = step < 1.2f ? step : 1.2f;
                    const float hp = sig - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    x = x * (1.0f - e->sfx_mix) + e->sfx_lo * 2.0f * e->sfx_mix;
                } break;
                case PX_SFX_FORMANT: {
                    const float a = px_abs(x);
                    e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    const float b = x, h = e->sfx_env * 0.1f;
                    if (e->sfx_prev <= 0.0f && b > h) { e->sfx_prev = 1.0f; e->sfx_ff = -e->sfx_ff; }
                    else if (e->sfx_prev > 0.0f && b < -h) e->sfx_prev = -1.0f;
                    const float sig = (e->sfx_prev + e->sfx_ff * e->sfx_d) * e->sfx_env;
                    const float open = e->sfx_c * 0.7f * e->sfx_env;
                    bq_bandpass(&e->sfx_f1, sr, 300.0f + 900.0f * e->sfx_c + 1200.0f * open, 1.2f + 7.0f * e->sfx_qd);
                    bq_bandpass(&e->sfx_f2, sr, 800.0f + 2300.0f * e->sfx_c + 2200.0f * open, 1.2f + 7.0f * e->sfx_qd);
                    const float formed = bq_run(&e->sfx_f1, sig) + bq_run(&e->sfx_f2, sig) * 0.7f;
                    x = x * (1.0f - e->sfx_mix) + formed * e->sfx_mix;
                } break;
                case PX_SFX_CROSS_SAW: case PX_SFX_CROSS_TRI: {
                    const float a = px_abs(x);
                    e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    const float b = bq_run(&e->sfx_f1, x), h = e->sfx_env * 0.1f;
                    e->sfx_sync_age += 1.0f / sr;
                    if (e->sfx_prev <= 0.0f && b > h) {
                        e->sfx_prev = 1.0f;
                        if (e->sfx_sync_age > 1.0f / 1400.0f && e->sfx_sync_age < 1.0f / 45.0f)
                            e->sfx_sync_freq = 1.0f / e->sfx_sync_age;
                        e->sfx_sync_age = 0.0f; e->sfx_sync_phase = 0.0f;
                    } else if (e->sfx_prev > 0.0f && b < -h) e->sfx_prev = -1.0f;
                    e->sfx_sync_phase += e->sfx_sync_freq / sr;
                    if (e->sfx_sync_phase >= 1.0f) e->sfx_sync_phase -= (int)e->sfx_sync_phase;
                    const float phase = e->sfx_sync_phase;
                    const float wave = sfx_t == PX_SFX_CROSS_SAW ? 2.0f * phase - 1.0f : 1.0f - 4.0f * px_abs(phase - 0.5f);
                    const float sig = wave * e->sfx_env;
                    const float cutoff = 180.0f + 6200.0f * e->sfx_c + 7000.0f * e->sfx_env;
                    const float step = 2.0f * PI_F * cutoff / sr;
                    const float f = step < 1.2f ? step : 1.2f;
                    const float hp = sig - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    x = x * (1.0f - e->sfx_mix) + e->sfx_lo * e->sfx_d * 2.0f * e->sfx_mix;
                } break;
                case PX_SFX_CROSS_OSC: {
                    const float a = px_abs(x);
                    e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    const float b = bq_run(&e->sfx_f1, x), h = e->sfx_env * 0.1f;
                    e->sfx_sync_age += 1.0f / sr;
                    const int wave = (int)e->sfx_sync_inc;
                    const float pulse_start = -0.30f * e->sfx_env;
                    const float pulse_end = 0.50f * e->sfx_env;
                    const int rising = e->sfx_prev <= 0.0f && b > (wave == 1 ? pulse_start : h);
                    if (rising) {
                        e->sfx_prev = 1.0f;
                        const float period = e->sfx_sync_age;
                        if (period > 1.0f / 1400.0f && period < 1.0f / 45.0f) {
                            const float measured = 1.0f / period;
                            if (measured > e->sfx_sync_freq * 0.5f && measured < e->sfx_sync_freq * 2.0f)
                                e->sfx_sync_freq += (measured - e->sfx_sync_freq) * 0.22f;
                        }
                        e->sfx_sync_age = 0.0f; e->sfx_sync_phase = 0.0f;
                        if (e->sfx_sync_inc < 0.5f) e->sfx_ff = -e->sfx_ff;
                    } else if (e->sfx_prev > 0.0f && b < (wave == 1 ? pulse_end : -h)) e->sfx_prev = -1.0f;
                    float wave_sample;
                    if (wave == 0) wave_sample = e->sfx_prev;
                    else if (wave == 1) wave_sample = e->sfx_prev;
                    else {
                        e->sfx_sync_phase += e->sfx_sync_freq / sr;
                        if (e->sfx_sync_phase >= 1.0f) e->sfx_sync_phase -= (int)e->sfx_sync_phase;
                        wave_sample = wave == 2 ? 1.0f - 4.0f * px_abs(e->sfx_sync_phase - 0.5f) : 2.0f * e->sfx_sync_phase - 1.0f;
                    }
                    float sig = (wave_sample + e->sfx_ff * e->sfx_d) * e->sfx_env;
                    if (wave == 4) sig = px_tanh(sig * 3.5f) * 1.3f;
                    else if (wave == 5) sig = x * e->sfx_env * 1.8f;
                    else if (wave == 6) sig = px_tanh(sig * 0.75f) * 1.3f;
                    const float cutoff = 180.0f + 6200.0f * e->sfx_c + 7000.0f * e->sfx_env;
                    const float f = 2.0f * PI_F * cutoff / sr;
                    const float hp = sig - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    float shaped = e->sfx_lo * 2.0f;
                    if (wave == 4) shaped += bq_run(&e->sfx_f2, sig) * 2.0f;
                    else if (wave == 5) shaped = bq_run(&e->sfx_f2, sig) * 4.0f;
                    else if (wave == 6) shaped = bq_run(&e->sfx_f2, sig) * 2.5f;
                    x = x * (1.0f - e->sfx_mix) + shaped * e->sfx_mix;
                } break;
                case PX_SFX_LOFI: {
                    float y = bq_run(&e->sfx_f2, bq_run(&e->sfx_f1, x));
                    y = px_tanh(y * e->sfx_c) * (1.5f / (1.0f + e->sfx_c * 0.2f));
                    x = x + (y - x) * e->sfx_mix;
                } break;
                default: {                                 /* square synth */
                    float a = px_abs(x);
                    e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    float b = bq_run(&e->sfx_f1, x), s;
#ifdef PX_SFX_SYNTH_HQ_LIVE
                    if (sfx_t == PX_SFX_SYNTH_HQ) {
                        b = bq_run(&e->sfx_f2, b);
                        const int on = e->sfx_env > (e->sfx_track.gate ? 0.001f : 0.002f);
                        (void)on;                      /* v49: the shared tracker's note (its own instance: SFX and SYN can both play) */
                        pxt_layer_run(&pxs_lay, &pxs_trk, x, pxt_run(e, &pxs_trk, x));
                        pxt_drive(&e->sfx_track, &pxs_lay, sr);
                        const float sq = synth_hq_pulse(&e->sfx_track, 4, 0.5f);
                        const float sub = synth_hq_pulse(&e->sfx_track, 2, 0.5f);
                        s = (sq + sub * e->sfx_d) * e->sfx_env;
                    } else
#endif
                    {
                        const float h = e->sfx_env * 0.1f;
                        if (e->sfx_prev <= 0.0f && b > h) {
                            e->sfx_prev = 1.0f; e->sfx_ff = -e->sfx_ff;
                        } else if (e->sfx_prev > 0.0f && b < -h) e->sfx_prev = -1.0f;
                        const float sq = e->sfx_prev * e->sfx_env, sub = e->sfx_ff * e->sfx_env;
                        s = sq + sub * e->sfx_d;
                    }
                    float t = e->sfx_env * (2.0f + e->sfx_c * 30.0f); if (t > 1.0f) t = 1.0f;
                    float f = 2.0f * PI_F * (120.0f + t * t * (300.0f + e->sfx_c * 3500.0f)) / sr;
                    float hp = s - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                    e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                    x = x * (1.0f - e->sfx_mix) + e->sfx_lo * 2.0f * e->sfx_mix;
                } break;
                }
            }
        break;
        case PX_B_BST:
            /* ---- v75: PRE / BOOST ---- */
            switch (bst_t) {
            case PX_BST_RANGEMASTER: x = pxp_rm_tick(&e->rm, x); break;
            case PX_BST_EPBOOST: x = pxp_ep_tick(&e->ep, x); break;
            case PX_BST_MAXIMIZER: x = pxp_bbe_tick(&e->bbe, x); break;
            case PX_BST_ENHANCE: x = pxe_tick(&e->enh, x); break;
            case PX_BST_FUZZFACE: x = pxp_ff_tick(&e->ff, x); break;
            case PX_BST_ACOUSTIC: x = ap_tick(&e->ap, x); break;
            case PX_BST_SIGNATURE: x = signature_tick(&e->signature, x); break;
            default: x = pxp_vw_tick(&e->vw, x); break;
            }
        break;
        case PX_B_EQ:
            /* ---- eq ---- */
            if (p->on[PX_B_EQ]) for (int c = 0; c < e->eq_n; c++) x = bq_run(&e->eq[c], x);
        break;
        case PX_B_MOD:
            /* ---- modulation ---- */
            if (p->on[PX_B_MOD]) {
                e->mod_ph = wrap1(e->mod_ph + e->mod_inc);
                float l = lfo_sin(e->mod_ph);
                switch (mod_t) {
                case PX_MOD_CHORUS: case PX_MOD_DIMENSION: {
                    float tri = lfo_tri(e->mod_ph), wet;
                    e->mod_buf[e->mod_w] = x;
                    if (mod_t == PX_MOD_CHORUS) wet = mod_tap(e, e->mod_base + e->mod_sweep * (0.5f + 0.5f * tri));
                    else wet = 0.5f * (mod_tap(e, e->mod_base + e->mod_sweep * tri) + mod_tap(e, e->mod_base - e->mod_sweep * tri));
                    wet = bq_run(&e->mod_lp, wet);
                    if (++e->mod_w >= 2048) e->mod_w = 0;
                    x = x * (1.0f - 0.5f * e->mod_mix) + wet * 0.5f * e->mod_mix;
                } break;
                case PX_MOD_FLANGER: case PX_MOD_VIBRATO: {
                    float d = e->mod_base + e->mod_sweep * (0.5f + 0.5f * (mod_t == PX_MOD_FLANGER ? lfo_tri(e->mod_ph) : l));
                    float wet = mod_tap(e, d);
                    e->mod_buf[e->mod_w] = x + wet * e->mod_fb;
                    if (++e->mod_w >= 2048) e->mod_w = 0;
                    x = (mod_t == PX_MOD_VIBRATO) ? wet : x * (1.0f - 0.5f * e->mod_mix) + wet * 0.5f * e->mod_mix;
                } break;
                case PX_MOD_PHASE90: case PX_MOD_STONE: {
                    float u = 0.5f + 0.5f * l;
                    float fc = e->ph_lo + (e->ph_hi - e->ph_lo) * u * u;
                    float w = PI_F * fc / sr, a = (1.0f - w) / (1.0f + w);
                    float s = x + e->ph_fb * e->mod_fb;
                    for (int i = 0; i < 4; i++) { float y = -a * s + e->ph_z[i]; e->ph_z[i] = s + a * y; s = y; }
                    e->ph_fb = s;
                    x = 0.5f * (x + s);
                } break;
                case PX_MOD_UNIVIBE: {
                    float tg = 0.5f + 0.5f * l;
                    e->uv_lamp += (tg - e->uv_lamp) * (tg > e->uv_lamp ? e->uv_up : e->uv_dn);
                    float fc0 = 60.0f + e->uv_lamp * e->mod_depth * 1200.0f, s = x;
                    for (int i = 0; i < 4; i++) {
                        float fc = fc0 * e->uv_k[i]; if (fc > sr * 0.45f) fc = sr * 0.45f;
                        float w = PI_F * fc / sr, a = (1.0f - w) / (1.0f + w);
                        float y = -a * s + e->ph_z[i]; e->ph_z[i] = s + a * y; s = y;
                    }
                    x = x * (1.0f - e->mod_mix) + s * e->mod_mix;
                } break;
                case PX_MOD_OPTOTREM: {
                    float tg = 1.0f - e->mod_depth * (0.5f + 0.5f * l);
                    e->trem_g += (tg - e->trem_g) * (tg < e->trem_g ? e->trem_att : e->trem_rel);
                    x *= e->trem_g;
                } break;
                case PX_MOD_BIASTREM: {
                    float sq = l > 0.0f ? 1.0f : -1.0f, sh = l + (sq - l) * e->mod_shape;
                    float g = 1.0f - e->mod_depth * (0.5f + 0.5f * sh);
                    x = px_tanh(x * g * 1.2f) * 0.85f;
                } break;
                case PX_MOD_HARMTREM: {
                    float lo = bq_run(&e->mod_lp, x), hi = x - lo;
                    float m = 0.5f + 0.5f * l;
                    x = lo * (1.0f - e->mod_depth * m) + hi * (1.0f - e->mod_depth * (1.0f - m));
                    x *= 1.0f + 0.5f * e->mod_depth;
                } break;
                case PX_MOD_ROTARY: {
                    e->rot_hs += (e->rot_ht - e->rot_hs) * e->rot_hk;
                    e->rot_ds += (e->rot_dt - e->rot_ds) * e->rot_dk;
                    e->rot_h = wrap1(e->rot_h + e->rot_hs / sr);
                    e->rot_d = wrap1(e->rot_d + e->rot_ds / sr);
                    float lo = bq_run(&e->mod_lp, x), hi = x - lo;
                    e->mod_buf[e->mod_w] = hi;
                    float hs = lfo_sin(e->rot_h);
                    float horn = mod_tap(e, (0.001f + 0.00044f * e->mod_depth * hs) * sr);
                    if (++e->mod_w >= 2048) e->mod_w = 0;
                    float hc = lfo_sin(wrap1(e->rot_h + 0.25f));
                    horn *= 1.0f - 0.35f * e->mod_depth * (0.5f + 0.5f * hc);
                    lo *= 1.0f - 0.25f * e->mod_depth * (0.5f + 0.5f * lfo_sin(e->rot_d));
                    x = lo * (1.5f - e->rot_bal) + horn * (0.5f + e->rot_bal);
                } break;
                default: break;
                }
            }
        break;
        default: break;
        }
        xb[k] = x * strim;
        }
    faded:
        if (fading) {
            const float dg = (fz - fa) / (float)n;
            float g = fa;
            for (int k = 0; k < n; k++) { g += dg; xb[k] = dryb[k] + (xb[k] - dryb[k]) * g; }
        }
        if (px_clock) { const uint32_t d = px_clock() - pt0; e->pf[sblk] += d; e->pfs[sblk] += ((float)d - e->pfs[sblk]) * 0.0625f; }
    }
    if (e->g_eq_n) bq_chain(e->g_eq, e->g_eq_n, xb, n);   /* v75: the global output EQ */
    for (int k = 0; k < n; k++) {
        float x = xb[k];
        { float y = x - e->odc_x1 + 0.9990f * e->odc_y1; e->odc_x1 = x; e->odc_y1 = y; x = y; }
        x *= pg;
        if (e->g_lim_c > 0.0f) {                       /* v75: the global limiter: the gain falls to the ceiling at once (a third of the way a sample) and comes back over ~2 ms */
            const float a = px_abs(x), tg = a > e->g_lim_c ? e->g_lim_c / a : 1.0f;
            e->g_lim_g += (tg - e->g_lim_g) * (tg < e->g_lim_g ? 0.35f : 0.0004f);
            x *= e->g_lim_g;
        }
        {   /* v36: soft limiter instead of a hard clip: transparent up to 0.8, then rounds off smoothly to 1.2 */
            float a = px_abs(x);
            if (a > 0.8f) { float y = 0.8f + 0.4f * px_tanh((a - 0.8f) * 2.5f); x = x > 0.0f ? y : -y; }
        }
        out[k] = x;
    }
    }
}
void px_engine_process(px_engine *e, const float *in, float *out, int n) { px_engine_part(e, in, out, n, PX_PART_ALL); }
#include "px_synth_adapter.h"                           /* Synth2: Felucca ANALOG and LOFI through the mono tracker (last: its names are Felucca's) */
static void px_s2_set_q(int type, const uint8_t *q) { px_s2_set(&px_s2_st, type, q); }
