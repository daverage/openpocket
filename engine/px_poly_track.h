/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * v58: the two-note polyphonic tracker and allocator, independent of any
 * oscillator. The quarter-rate resonator bank (px_resonator_test.h) proposes
 * semitone candidates; phase differences between reports refine each
 * candidate's frequency; the allocator keeps two note identities with their
 * own pitch, power, sustain level and gain envelope. A renderer reads
 * pxp_note::frequency and ::gain, and the event bits for oscillator resets.
 */
#ifndef PX_POLY_TRACK_H
#define PX_POLY_TRACK_H

enum { PXP_NOTES = 2 };
enum { PXP_ACQUIRED = 1u, PXP_RESTRUCK = 2u };   /* pxp_note::events, cleared by the renderer */

typedef struct {
    float frequency, target, gain, sustain, power;
    float pending, note;            /* candidate being confirmed; accepted semitone bin */
    unsigned stable, missing, held, repick, events;
} pxp_note;

typedef struct {
    pxp_note note[PXP_NOTES];
    px_erb_complex previous[PXR_BANDS];
    float inv_rate, report_rate, excitation_scale, dip;
    float env;
    int ready;
} pxp_tracker;

typedef struct {
    float note, hz, power;
} pxp_candidate;

static pxr_bank pxp_bank PX_LO(pxp_bank);
static pxp_tracker pxp;

static void pxp_reset(px_engine *e, int configure)
{
    if (configure) {
        erb_prepare(e);
        pxr_reset_bank(&pxp_bank, e->sr);
    } else {
        memset(pxp_bank.state, 0, sizeof pxp_bank.state);
        memset(pxp_bank.age, 0, sizeof pxp_bank.age);
        pxp_bank.hop = 0;
    }
    memset(&pxa_trk, 0, sizeof pxa_trk);
    memset(&pxp, 0, sizeof pxp);
    pxp.inv_rate = 1.0f / e->sr;
    pxp.report_rate = e->sr / (4 * PXR_HOP);
    pxp.excitation_scale = 2.0f * onepole_k(e->sr * 0.25f, 0.0226644f);
    pxp.dip = onepole_k(e->sr, 0.0018f);
}

/* v59: the poly tracker's own candidate picker. The shared pxr_pick (Lowest
 * Octave HQ) keeps the six strongest local maxima above 15% of the peak
 * energy; on GuitarSet dyads the weaker note of a pair is often a peak below
 * that line, and a harmonic is the strongest band in about a quarter of the
 * frames. Rank local maxima by harmonic sum instead: a band's amplitude plus
 * its 2nd..5th harmonic bands, weighted by PXP_G per harmonic. The best sum
 * is the first note. Further notes must reach PXP_RHO of its sum, counting
 * bands already claimed as another note's harmonics at PXP_SHARE, and a band
 * that is itself such a harmonic needs PXP_BETA of the first note's
 * amplitude. Notes picked in the previous report get PXP_BONUS. As before, a
 * second note must persist for PXP_AGE reports before it is offered. */
#ifdef PXP_TUNE
static float PXP_ALPHA = 0.15f, PXP_RHO = 0.20f, PXP_G = 0.50f, PXP_BETA = 1.00f, PXP_SHARE = 0.35f,
             PXP_BONUS = 0.0f;
static int PXP_MAXN = 2, PXP_AGE = 3, PXP_OLD = 0;
static float PXP_ACQ = 0.01f, PXP_HGUARD = 0.68f, PXP_MATCH = 0.058f;
static unsigned PXP_CONFIRM_N = 3, PXP_HOLD_N = 4;
#else
#define PXP_ACQ 0.01f  /* v57: 0.20, which dropped weak true notes */
#define PXP_HGUARD 0.68f
#define PXP_MATCH 0.058f
#define PXP_CONFIRM_N 3u
#define PXP_HOLD_N 4u
#define PXP_ALPHA 0.15f
#define PXP_RHO 0.20f
#define PXP_G 0.50f
#define PXP_BETA 1.00f
#define PXP_SHARE 0.35f
#define PXP_BONUS 0.0f
#define PXP_MAXN 2
#define PXP_AGE 3
#endif
enum { PXP_HARMONICS = 5, PXP_PICKS = 3 };
static const uint8_t pxp_offset[PXP_HARMONICS] = {0, 12, 19, 24, 28};

static float pxp_salience(const float *amp, const uint8_t *claimed, int bin, float share)
{
    float sum = 0.0f, weight = 1.0f;
    for (int h = 0; h < PXP_HARMONICS; ++h, weight *= PXP_G) {
        const int at = bin + pxp_offset[h];
        if (at >= PXR_BANDS) break;
        sum += weight * (claimed[at] ? share * amp[at] : amp[at]);
    }
    return sum;
}

static __attribute__((noinline)) void pxp_pick(pxr_bank *bank, px_track *out)
{
    float energy[PXR_BANDS], amp[PXR_BANDS], maximum = 0.0f;
    uint8_t claimed[PXR_BANDS] = {0}, present[PXR_BANDS] = {0};
    const int start = PXR_FIRST_MIDI - PXR_LOW;
    for (int i = start; i < PXR_BANDS; ++i) {
        const px_erb_complex z = bank->state[i];
        energy[i] = z.r * z.r + z.i * z.i;
        amp[i] = sqrtf(energy[i]);
        if (energy[i] > maximum) maximum = energy[i];
    }
    out->mono = 0.0f;
    out->conf = 0.0f;
    out->level = maximum;
    out->onset = 0.0f;
    out->nnotes = 0;
    int picks[PXP_PICKS], count = 0;
    if (maximum >= 1e-10f) {
        const float floor = PXP_ALPHA * sqrtf(maximum);
        float first = 0.0f;
        while (count < PXP_MAXN) {
            int best = -1;
            float score = 0.0f;
            for (int i = start; i < PXR_BANDS; ++i) {
                if (amp[i] < floor || (i > start && amp[i] <= amp[i - 1]) ||
                    (i + 1 < PXR_BANDS && amp[i] <= amp[i + 1])) continue;
                int taken = 0;
                for (int j = 0; j < count; ++j) taken |= picks[j] == i;
                if (taken || (count && claimed[i] && amp[i] < PXP_BETA * amp[picks[0]])) continue;
                float s = pxp_salience(amp, claimed, i, count ? PXP_SHARE : 1.0f);
                if (bank->age[i]) s *= 1.0f + PXP_BONUS;
                if (s > score) { score = s; best = i; }
            }
            if (best < 0 || (count && score < PXP_RHO * first)) break;
            if (!count) first = score;
            picks[count++] = best;
            for (int h = 1; h < PXP_HARMONICS && best + pxp_offset[h] < PXR_BANDS; ++h)
                claimed[best + pxp_offset[h]] = 1;
        }
    }
    for (int k = 0; k < count; ++k) {
        const int bin = picks[k];
        present[bin] = 1;
        if (bank->age[bin] < PXP_AGE) ++bank->age[bin];
        if (k > 0 && bank->age[bin] < PXP_AGE) continue;
        if (k == 0) { out->mono = bank->hz[bin]; out->conf = 1.0f; }
        int j = out->nnotes++;
        while (j > 0 && out->notes[j - 1] > bank->hz[bin]) {
            out->notes[j] = out->notes[j - 1];
            out->sal[j] = out->sal[j - 1];
            --j;
        }
        out->notes[j] = bank->hz[bin];
        out->sal[j] = energy[bin] / maximum;
    }
    for (int i = start; i < PXR_BANDS; ++i) if (!present[i]) bank->age[i] = 0;
}

/* Unwrap each tracked partial relative to the resonator's nominal pitch.
 * Reject estimates outside a nearby interval before smoothing the bend. */
static float pxp_frequency(int bin, float tolerance)
{
    const float note = pxp_bank.hz[bin];
    const px_erb_complex x = pxp_bank.state[bin], p = pxp.previous[bin];
    if (!pxp.ready || p.r * p.r + p.i * p.i < 1e-8f) return note;
    const float rate = pxp.report_rate;
    const float phase = pxt_atan2(x.i * p.r - x.r * p.i, x.r * p.r + x.i * p.i);
    const float base = phase * (rate / (2.0f * PI_F));
    const float hz = base + rate * (float)(int)((note - base) / rate + 0.5f);
    return px_abs(hz - note) < tolerance * note ? hz : note;
}

static float pxp_sustain(float power)
{
    const float amp = sqrtf(power) * pxp.excitation_scale;
    return amp > 0.14f ? 1.0f : 0.30f + 5.0f * amp;
}

static int pxp_nearest(const pxp_candidate *candidates, int count, const pxp_note *v, int avoid)
{
    if (!v->held) return -1;
    int match = -1;
    float distance = PXP_MATCH * v->target;
    for (int i = 0; i < count; ++i) {
        const float next = px_abs(candidates[i].hz - v->target);
        if (i != avoid && next < distance) { match = i; distance = next; }
    }
    return match;
}

static int pxp_harmonic(float hz, float other)
{
    return px_abs(hz - 2.0f * other) < 0.045f * other ||
           px_abs(hz - 3.0f * other) < 0.065f * other ||
           px_abs(hz - 4.0f * other) < 0.085f * other;
}

static float pxp_power(int bin)
{
    const px_erb_complex z = pxp_bank.state[bin];
    return z.r * z.r + z.i * z.i;
}

/* The lowest bass fundamentals often miss the peak shortlist. Look for
 * agreement between their even and odd harmonics, with root-band power
 * as supporting evidence. Use the higher partials for pitch refinement. */
static int pxp_bass(float maximum, pxp_candidate result[2])
{
    float score[12] = {0};
    pxp_candidate bass[12];
    int best = -1;
    const float floor = 0.13f * maximum;
    if (maximum <= 1e-8f) return 0;
    for (int bin = PXR_FIRST_MIDI - PXR_LOW; bin < 12; ++bin) {
        const float root = pxp_power(bin), even = pxp_power(bin + 12);
        const float odd = pxp_power(bin + 19);
        if (root < 0.032f * maximum || even < floor || odd < floor) continue;
        const float note = pxp_bank.hz[bin];
        const float twice = 0.5f * pxp_frequency(bin + 12, 0.09f);
        const float third = (1.0f / 3.0f) * pxp_frequency(bin + 19, 0.09f);
        if (px_abs(twice - third) > 0.055f * note || px_abs(third - note) > 0.035f * note) continue;
        const float support = even < odd ? even : odd;
        score[bin] = support + 0.20f * (even + odd) + 0.12f * root;
        bass[bin] = (pxp_candidate){note, 0.25f * twice + 0.75f * third, 0.60f * (even + odd)};
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

/* Gather this report's candidates: confirmed bass fundamentals first, then
 * the resonator's peaks that the bass notes do not already explain. */
static int pxp_candidates(const px_track *track, pxp_candidate *candidates)
{
    int count = 0;
    if (pxp.env > 0.0012f) {
        const int bass_count = pxp_bass(track->level, candidates);
        count = bass_count;
        int bin = PXR_FIRST_MIDI - PXR_LOW;
        for (int i = 0; i < track->nnotes; ++i) {
            const float note = track->notes[i];
            int explained = 0;
            for (int j = 0; j < bass_count; ++j) {
                const float hz = candidates[j].hz;
                if (px_abs(note - hz) < 0.047f * hz || pxp_harmonic(note, hz)) explained = 1;
            }
            if (explained) continue;
            while (bin < PXR_BANDS && pxp_bank.hz[bin] < note) ++bin;
            if (bin == PXR_BANDS || count == PXT_NOTES) break;
            candidates[count++] = (pxp_candidate){note, pxp_frequency(bin, 0.046f),
                                                  track->sal[i] * track->level};
        }
    }
    memcpy(pxp.previous, pxp_bank.state, sizeof pxp.previous);
    pxp.ready = 1;
    return count;
}

/* Keep each held note on its nearest candidate; start free notes on the
 * strongest unexplained candidate once it repeats for PXP_CONFIRM_N reports. */
static void pxp_allocate(const px_track *track, const pxp_candidate *candidates, int count)
{
    pxp_note *const voices = pxp.note;
    int matches[2] = {pxp_nearest(candidates, count, &voices[0], -1),
                      pxp_nearest(candidates, count, &voices[1], -1)};
    if (matches[0] >= 0 && matches[0] == matches[1]) {
        const float hz = candidates[matches[0]].hz;
        const int farther = px_abs(hz - voices[0].target) < px_abs(hz - voices[1].target);
        matches[farther] = pxp_nearest(candidates, count, &voices[farther], matches[1 - farther]);
    }
    unsigned used = 0;
    for (int i = 0; i < 2; ++i) {
        pxp_note *v = &voices[i];
        if (!v->held) continue;
        const int match = matches[i];
        if (match < 0) {
            if (++v->missing >= PXP_HOLD_N) { v->held = 0; v->repick = 0; }
            continue;
        }
        used |= 1u << match;
        const pxp_candidate *c = &candidates[match];
        /* Resonator power is absolute, so one string cannot retrigger the
         * other just by changing its share of the total input power. */
        if (!v->missing && c->power > 3.0f * v->power &&
            c->power > 0.12f * track->level && v->gain > 0.18f)
            v->repick = 1;
        v->power += 0.21f * (c->power - v->power);
        v->sustain = pxp_sustain(c->power);
        v->target = c->hz;
        v->note = c->note;
        v->missing = 0;
    }
    for (int i = 0; i < 2; ++i) {
        pxp_note *v = &voices[i];
        if (v->held) continue;
        int best = -1;
        float power = track->level * PXP_ACQ;
        for (int j = 0; j < count; ++j) {
            const pxp_candidate *c = &candidates[j];
            if ((used & (1u << j)) || c->power <= power) continue;
            const pxp_note *other = &voices[1 - i];
            if (other->held && pxp_harmonic(c->note, other->target) &&
                c->power < PXP_HGUARD * track->level) continue;
            best = j;
            power = c->power;
        }
        if (best < 0) { v->stable = 0; continue; }
        used |= 1u << best;
        const pxp_candidate *c = &candidates[best];
        if (v->pending == c->note) {
            if (v->stable < PXP_CONFIRM_N) ++v->stable;
        } else {
            v->pending = c->note;
            v->stable = 1;
        }
        if (v->stable >= PXP_CONFIRM_N && v->gain < 0.025f) {
            v->held = 1;
            v->missing = v->stable = 0;
            v->target = v->frequency = c->hz;
            v->power = c->power;
            v->sustain = pxp_sustain(c->power);
            v->note = c->note;
            v->events |= PXP_ACQUIRED;
        }
    }
}

static __attribute__((noinline)) void pxp_frame(const px_track *track)
{
    pxp_candidate candidates[PXT_NOTES];
#ifdef PXP_FRAME_HOOK
    PXP_FRAME_HOOK(track);                       /* host benches only: tools/sim/pulse_poly_bench.c */
#endif
    const int count = pxp_candidates(track, candidates);
    pxp_allocate(track, candidates, count);
}

/* Per input sample: level follower, bank, and on each report the allocator. */
static inline void pxp_input(float input, float env_coeff)
{
    pxp.env += env_coeff * (px_abs(input) - pxp.env);
    if (!pxr_advance(&pxp_bank, &pxa_trk, input)) return;
#ifdef PXP_TUNE
    if (PXP_OLD) pxr_pick(&pxp_bank, &pxa_trk); else
#endif
    pxp_pick(&pxp_bank, &pxa_trk);
    pxp_frame(&pxa_trk);
}

/* Per output sample: advance one note's gain envelope and pitch glide.
 * Returns the phase increment, or 0 when the note is silent. */
static inline float pxp_note_step(pxp_note *v, float attack, float release)
{
    if (v->repick && v->gain < 0.12f) {
        v->repick = 0;
        v->events |= PXP_RESTRUCK;
    }
    const float target = v->held && !v->repick ? v->sustain : 0.0f;
    const float k = v->repick || (!v->held && v->stable >= PXP_CONFIRM_N) ? pxp.dip :
                    target > v->gain ? attack : release;
    v->gain += k * (target - v->gain);
    if (v->gain < 1e-5f && !v->held) { v->gain = 0.0f; return 0.0f; }
    v->frequency += attack * (v->target - v->frequency);
    return v->frequency * pxp.inv_rate;
}
#endif
