/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * The Poly voicings (PULSE, and in PX_POLY_TEST SAW, ARCADE and FM) render
 * the two notes of the shared tracker (px_poly_track.h) through one ladder.
 * Each note keeps its own oscillator phase and colour envelope here.
 */
#include "px_poly_track.h"

typedef struct {
    float phase;
#ifdef PX_POLY_TEST
    float phase2, color, triangle;
    uint32_t noise;
#endif
} px_poly_voice;

static px_poly_voice poly_osc[PXP_NOTES];

static void pulse_poly_reset(px_engine *e, int configure)
{
    pxp_reset(e, configure);
    memset(poly_osc, 0, sizeof poly_osc);
    memset(e->syn_lad, 0, sizeof e->syn_lad);
    e->syn_lfo[0] = 0.0f;
}

#ifdef PX_POLY_TEST
/* The index is in radians. Keep the significant sidebands below 42% of
 * the sample rate as carrier frequency and modulator ratio rise. */
static inline float poly_fm_index(float step, float mod_step, float index)
{
    if (step + mod_step * (index + 2.0f) <= 0.42f) return index;
    const float limit = (0.42f - step) / mod_step - 2.0f;
    return limit > 0.0f ? limit : 0.0f;
}

static inline float poly_fm_carrier(float phase, float modulator, float index)
{
    return sin_hq(wrap1(phase + (index / (2.0f * PI_F)) * sin_hq(modulator)));
}

static inline float poly_fm_sample(px_poly_voice *v, float step, float ratio, float index)
{
    const float mod_step = step * ratio;
    v->phase2 = wrap1(v->phase2 + mod_step);
    index = poly_fm_index(step, mod_step, index);
    return poly_fm_carrier(v->phase, v->phase2, index);
}

/* In the experimental multi-voice build, leave the waveforms in flash
 * and share the RAM-resident voice and tracking loops. */
static __attribute__((noinline)) float poly_synth_oscillator(const px_engine *e, px_poly_voice *v,
                                                              const pxp_note *n, int type, float step)
{
    v->color += e->syn_fdk * -v->color;
    if (type == PX_SYN_FM_POLY) {
        v->phase = wrap1(v->phase + step);
        return poly_fm_sample(v, step, e->syn_mul, e->syn_fenv * (0.14f + 0.86f * v->color));
    }
    if (type == PX_SYN_SAW_POLY) {
        step *= e->syn_mul;
        const float first = step * (1.0f - e->syn_pwd);
        v->phase = wrap1(v->phase + first);
        const float a = syn_saw(v->phase, first);
        if (e->syn_pwd == 0.0f) return a;
        const float second = step * (1.0f + e->syn_pwd);
        v->phase2 = wrap1(v->phase2 + second);
        return 0.5f * (a + syn_saw(v->phase2, second));
    }

    /* Arcade WAVE's upper four positions use the accepted semitone bin. */
    const unsigned wave = (unsigned)e->syn_snap;
    if (wave >= 4) step = n->note * pxp.inv_rate;
    step *= e->syn_mul;
    const float phase = v->phase + step;
    v->phase = wrap1(phase);
    float sample;
    if ((wave & 3u) == 2u) {
        if (phase >= 1.0f) {
            v->noise ^= v->noise << 13;
            v->noise ^= v->noise >> 17;
            v->noise ^= v->noise << 5;
        }
        sample = (float)(v->noise >> 8) * (1.0f / 8388608.0f) - 1.0f;
    } else {
        const float width = (wave & 3u) == 3u ? 0.25f : 0.5f;
        sample = syn_saw(v->phase, step) - syn_saw(wrap1(v->phase + width), step);
        if ((wave & 3u) == 1u) {
            /* Integrate a symmetric band-limited pulse. The leak removes
             * the integration constant over a few oscillator cycles. */
            v->triangle += step * (4.0f * sample - 0.85f * v->triangle);
            sample = v->triangle * 0.83f;
        }
    }
    if (e->syn_pwd > 0.0f) {
        const float x = sample * e->syn_wave;
        sample = (float)(int)(x + (x < 0.0f ? -0.5f : 0.5f)) / e->syn_wave;
    }
    return sample * v->color;
}
#endif

/* Full-rate PolyBLEP phase. A releasing oscillator keeps its phase and
 * frequency until its gain reaches silence or the slot can be reused. */
static __attribute__((noinline)) float pulse_poly_voice_sample(int index, float width,
                                                               float attack, float release
#ifdef PX_POLY_TEST
                                                               , const px_engine *e, int type
#endif
                                                               )
{
    pxp_note *const n = &pxp.note[index];
    px_poly_voice *const v = &poly_osc[index];
    const float step = pxp_note_step(n, attack, release);
#ifdef PX_POLY_TEST
    if (n->events) {
        if (n->events & PXP_ACQUIRED) {
            v->phase2 = wrap1(v->phase + 0.25f);
            v->triangle = -lfo_tri(v->phase);
            v->noise = index == 0 ? 0x91e10da5u : 0xc2b2ae35u;
        }
        v->color = 1.0f;
    }
#endif
    n->events = 0;
    if (step == 0.0f) return 0.0f;
#ifdef PX_POLY_TEST
    if (type != PX_SYN_PULSE_POLY) return n->gain * poly_synth_oscillator(e, v, n, type, step);
#endif
    v->phase = wrap1(v->phase + step);
    return n->gain * (syn_saw(v->phase, step) - syn_saw(wrap1(v->phase + width), step));
}

static __attribute__((noinline)) void pulse_poly_block(px_engine *e, float *samples, int n, float trim)
{
    float hz = e->syn_cut;
#ifdef PX_POLY_TEST
    const int type = e->syn_type;
    if (type == PX_SYN_SAW_POLY) {
        float sweep = 0.0f;
        for (int i = 0; i < 2; ++i) {
            const float amount = poly_osc[i].color * pxp.note[i].gain;
            if (amount > sweep) sweep = amount;
        }
        hz *= 1.0f + e->syn_fenv * sweep;
    }
#endif
    const float cutoff = syn_ladder_coefficient(e, hz);
    for (int i = 0; i < n; ++i) {
        const float input = samples[i];
        pxp_input(input, e->syn_ea);
        float width = 0.5f;
#ifdef PX_POLY_TEST
        if (type == PX_SYN_PULSE_POLY)
#endif
        {
            e->syn_lfo[0] = wrap1(e->syn_lfo[0] + e->syn_linc[0]);
            width = e->syn_pw0 + e->syn_pwd * lfo_sin(e->syn_lfo[0]);
            width = width < 0.08f ? 0.08f : (width > 0.92f ? 0.92f : width);
        }
        float sum = 0.0f;
        for (int v = 0; v < PXP_NOTES; ++v) {
            sum += pulse_poly_voice_sample(v, width, e->syn_ka, e->syn_kr
#ifdef PX_POLY_TEST
                                           , e, type
#endif
                                           );
        }
#ifdef PX_POLY_TEST
        if (type == PX_SYN_ARCADE_POLY) {
            samples[i] = (input * e->syn_dry + e->syn_out * 0.5f * sum) * trim;
            continue;
        }
        if (type == PX_SYN_FM_POLY) {
            samples[i] = e->syn_out * 0.5f * sum * trim;
            continue;
        }
#endif
        const float dn = (i & 1) ? 1e-15f : -1e-15f;
        samples[i] = 0.29f * syn_ladder_step(e, sum * 0.48f + dn, cutoff) * trim;
    }
}
