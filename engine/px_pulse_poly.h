/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * The two oscillator voices share one analyzer and one ladder, but keep
 * independent note identities, pitch, phase, and envelopes.
 */
typedef struct {
    float frequency, target, phase, gain, sustain, power;
    float pending;
    unsigned stable, missing, held, repick;
#ifdef PX_POLY_TEST
    float note, phase2, color, triangle;
    uint32_t noise;
#endif
} px_poly_voice;

typedef struct {
    px_poly_voice voice[2];
    px_erb_complex previous[PXR_BANDS];
    float inv_rate, report_rate, excitation_scale, dip;
    float env;
    int ready;
} px_pulse_poly;

static pxr_bank pulse_bank PX_LO(pulse_bank);
static px_pulse_poly pulse_poly;

static void pulse_poly_reset(px_engine *e, int configure)
{
    if (configure) {
        erb_prepare(e);
        pxr_reset_bank(&pulse_bank, e->sr);
    } else {
        memset(pulse_bank.state, 0, sizeof pulse_bank.state);
        memset(pulse_bank.age, 0, sizeof pulse_bank.age);
        pulse_bank.hop = 0;
    }
    memset(&pxa_trk, 0, sizeof pxa_trk);
    memset(&pulse_poly, 0, sizeof pulse_poly);
    pulse_poly.inv_rate = 1.0f / e->sr;
    pulse_poly.report_rate = e->sr / (4 * PXR_HOP);
    pulse_poly.excitation_scale = 2.0f * onepole_k(e->sr * 0.25f, 0.0226644f);
    pulse_poly.dip = onepole_k(e->sr, 0.0018f);
    memset(e->syn_lad, 0, sizeof e->syn_lad);
    e->syn_lfo[0] = 0.0f;
}

typedef struct {
    float note, hz, power;
} px_poly_candidate;

enum { PULSE_CONFIRM = 3, PULSE_HOLD = 4 };

/* Unwrap each tracked partial relative to the resonator's nominal pitch.
 * Reject estimates outside a nearby interval before smoothing the bend. */
static float pulse_poly_frequency(int bin, float tolerance)
{
    const float note = pulse_bank.hz[bin];
    const px_erb_complex x = pulse_bank.state[bin], p = pulse_poly.previous[bin];
    if (!pulse_poly.ready || p.r * p.r + p.i * p.i < 1e-8f) return note;
    const float rate = pulse_poly.report_rate;
    const float phase = pxt_atan2(x.i * p.r - x.r * p.i, x.r * p.r + x.i * p.i);
    const float base = phase * (rate / (2.0f * PI_F));
    const float hz = base + rate * (float)(int)((note - base) / rate + 0.5f);
    return px_abs(hz - note) < tolerance * note ? hz : note;
}

static float pulse_poly_sustain(float power)
{
    const float amp = sqrtf(power) * pulse_poly.excitation_scale;
    return amp > 0.14f ? 1.0f : 0.30f + 5.0f * amp;
}

static int pulse_poly_nearest(const px_poly_candidate *candidates, int count,
                              const px_poly_voice *voice, int avoid)
{
    if (!voice->held) return -1;
    int match = -1;
    float distance = 0.058f * voice->target;
    for (int i = 0; i < count; ++i) {
        const float next = px_abs(candidates[i].hz - voice->target);
        if (i != avoid && next < distance) { match = i; distance = next; }
    }
    return match;
}

static int pulse_poly_harmonic(float hz, float other)
{
    return px_abs(hz - 2.0f * other) < 0.045f * other ||
           px_abs(hz - 3.0f * other) < 0.065f * other ||
           px_abs(hz - 4.0f * other) < 0.085f * other;
}

static float pulse_poly_power(int bin)
{
    const px_erb_complex z = pulse_bank.state[bin];
    return z.r * z.r + z.i * z.i;
}

/* The lowest bass fundamentals often miss the peak shortlist. Look for
 * agreement between their even and odd harmonics, with root-band power
 * as supporting evidence. Use the higher partials for pitch refinement. */
static int pulse_poly_bass(float maximum, px_poly_candidate result[2])
{
    float score[12] = {0};
    px_poly_candidate bass[12];
    int best = -1;
    const float floor = 0.13f * maximum;
    if (maximum <= 1e-8f) return 0;
    for (int bin = PXR_FIRST_MIDI - PXR_LOW; bin < 12; ++bin) {
        const float root = pulse_poly_power(bin), even = pulse_poly_power(bin + 12);
        const float odd = pulse_poly_power(bin + 19);
        if (root < 0.032f * maximum || even < floor || odd < floor) continue;
        const float note = pulse_bank.hz[bin];
        const float twice = 0.5f * pulse_poly_frequency(bin + 12, 0.09f);
        const float third = (1.0f / 3.0f) * pulse_poly_frequency(bin + 19, 0.09f);
        if (px_abs(twice - third) > 0.055f * note || px_abs(third - note) > 0.035f * note) continue;
        const float support = even < odd ? even : odd;
        score[bin] = support + 0.20f * (even + odd) + 0.12f * root;
        bass[bin] = (px_poly_candidate){note, 0.25f * twice + 0.75f * third, 0.60f * (even + odd)};
        if (best < 0 || score[bin] > score[best]) best = bin;
    }
    if (best < 0) return 0;
    result[0] = bass[best];
    int second = -1;
    float floor2 = 0.30f * score[best];
    for (int bin = PXR_FIRST_MIDI - PXR_LOW; bin < 12; ++bin) {
        if (bin >= best - 1 && bin <= best + 1) continue;
        if (score[bin] <= floor2) continue;
        second = bin;
        floor2 = score[bin];
    }
    if (second < 0) return 1;
    result[1] = bass[second];
    return 2;
}

static __attribute__((noinline)) void pulse_poly_frame(const px_track *track)
{
    px_poly_candidate candidates[PXT_NOTES];
    int count = 0;
    if (pulse_poly.env > 0.0012f) {
        const int bass_count = pulse_poly_bass(track->level, candidates);
        count = bass_count;
        int bin = PXR_FIRST_MIDI - PXR_LOW;
        for (int i = 0; i < track->nnotes; ++i) {
            const float note = track->notes[i];
            int explained = 0;
            for (int j = 0; j < bass_count; ++j) {
                const float hz = candidates[j].hz;
                if (px_abs(note - hz) < 0.047f * hz || pulse_poly_harmonic(note, hz)) explained = 1;
            }
            if (explained) continue;
            while (bin < PXR_BANDS && pulse_bank.hz[bin] < note) ++bin;
            if (bin == PXR_BANDS || count == PXT_NOTES) break;
            candidates[count++] = (px_poly_candidate){note, pulse_poly_frequency(bin, 0.046f),
                                                      track->sal[i] * track->level};
        }
    }
    memcpy(pulse_poly.previous, pulse_bank.state, sizeof pulse_poly.previous);
    pulse_poly.ready = 1;

    px_poly_voice *const voices = pulse_poly.voice;
    int matches[2] = {pulse_poly_nearest(candidates, count, &voices[0], -1),
                      pulse_poly_nearest(candidates, count, &voices[1], -1)};
    if (matches[0] >= 0 && matches[0] == matches[1]) {
        const float hz = candidates[matches[0]].hz;
        const int farther = px_abs(hz - voices[0].target) < px_abs(hz - voices[1].target);
        matches[farther] = pulse_poly_nearest(candidates, count, &voices[farther], matches[1 - farther]);
    }
    unsigned used = 0;
    for (int i = 0; i < 2; ++i) {
        px_poly_voice *v = &voices[i];
        if (!v->held) continue;
        const int match = matches[i];
        if (match < 0) {
            if (++v->missing >= PULSE_HOLD) { v->held = 0; v->repick = 0; }
            continue;
        }
        used |= 1u << match;
        const px_poly_candidate *c = &candidates[match];
        /* Resonator power is absolute, so one string cannot retrigger the
         * other just by changing its share of the total input power. */
        if (!v->missing && c->power > 3.0f * v->power &&
            c->power > 0.12f * track->level && v->gain > 0.18f)
            v->repick = 1;
        v->power += 0.21f * (c->power - v->power);
        v->sustain = pulse_poly_sustain(c->power);
        v->target = c->hz;
#ifdef PX_POLY_TEST
        v->note = c->note;
#endif
        v->missing = 0;
    }
    for (int i = 0; i < 2; ++i) {
        px_poly_voice *v = &voices[i];
        if (v->held) continue;
        int best = -1;
        float power = track->level * 0.20f;
        for (int j = 0; j < count; ++j) {
            const px_poly_candidate *c = &candidates[j];
            if ((used & (1u << j)) || c->power <= power) continue;
            const px_poly_voice *other = &voices[1 - i];
            if (other->held && pulse_poly_harmonic(c->note, other->target) &&
                c->power < 0.68f * track->level) continue;
            best = j;
            power = c->power;
        }
        if (best < 0) { v->stable = 0; continue; }
        used |= 1u << best;
        const px_poly_candidate *c = &candidates[best];
        if (v->pending == c->note) {
            if (v->stable < PULSE_CONFIRM) ++v->stable;
        } else {
            v->pending = c->note;
            v->stable = 1;
        }
        if (v->stable >= PULSE_CONFIRM && v->gain < 0.025f) {
            v->held = 1;
            v->missing = v->stable = 0;
            v->target = v->frequency = c->hz;
            v->power = c->power;
            v->sustain = pulse_poly_sustain(c->power);
#ifdef PX_POLY_TEST
            v->note = c->note;
            v->phase2 = wrap1(v->phase + 0.25f);
            v->color = 1.0f;
            v->triangle = -lfo_tri(v->phase);
            v->noise = i == 0 ? 0x91e10da5u : 0xc2b2ae35u;
#endif
        }
    }
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
                                                              int type, float step)
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
    if (wave >= 4) step = v->note * pulse_poly.inv_rate;
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
static __attribute__((noinline)) float pulse_poly_voice_sample(px_poly_voice *v, float width,
                                                               float attack, float release
#ifdef PX_POLY_TEST
                                                               , const px_engine *e, int type
#endif
                                                               )
{
    if (v->repick && v->gain < 0.12f) {
        v->repick = 0;
#ifdef PX_POLY_TEST
        v->color = 1.0f;
#endif
    }
    const float target = v->held && !v->repick ? v->sustain : 0.0f;
    const float k = v->repick || (!v->held && v->stable >= PULSE_CONFIRM) ? pulse_poly.dip :
                    target > v->gain ? attack : release;
    v->gain += k * (target - v->gain);
    if (v->gain < 1e-5f && !v->held) { v->gain = 0.0f; return 0.0f; }
    v->frequency += attack * (v->target - v->frequency);
    const float step = v->frequency * pulse_poly.inv_rate;
#ifdef PX_POLY_TEST
    if (type != PX_SYN_PULSE_POLY) return v->gain * poly_synth_oscillator(e, v, type, step);
#endif
    v->phase = wrap1(v->phase + step);
    return v->gain * (syn_saw(v->phase, step) - syn_saw(wrap1(v->phase + width), step));
}

static __attribute__((noinline)) void pulse_poly_block(px_engine *e, float *samples, int n, float trim)
{
    float hz = e->syn_cut;
#ifdef PX_POLY_TEST
    const int type = e->syn_type;
    if (type == PX_SYN_SAW_POLY) {
        float sweep = 0.0f;
        for (int i = 0; i < 2; ++i) {
            const float amount = pulse_poly.voice[i].color * pulse_poly.voice[i].gain;
            if (amount > sweep) sweep = amount;
        }
        hz *= 1.0f + e->syn_fenv * sweep;
    }
#endif
    const float cutoff = syn_ladder_coefficient(e, hz);
    for (int i = 0; i < n; ++i) {
        const float input = samples[i];
        pulse_poly.env += e->syn_ea * (px_abs(input) - pulse_poly.env);
        if (pxr_run_bank(&pulse_bank, &pxa_trk, input)) pulse_poly_frame(&pxa_trk);
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
        for (int v = 0; v < 2; ++v) {
            sum += pulse_poly_voice_sample(&pulse_poly.voice[v], width, e->syn_ka, e->syn_kr
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
