/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * v66 (docs/POLY-SYNTH-DESIGN.md): a second note while the first still sounds needs 6 reports, never starts on a
 * harmonic of the held note or within 7.5% of its pitch (a bend); the quieter free voice takes a new note first.
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
#ifdef PX_POLY_EVICT_TEST
    unsigned age;
#ifdef PXP_EVICT_FAST_RELEASE
    unsigned evicted;
#endif
#endif
#ifdef PXP_TUNE
    unsigned acquisitions;                             /* (host benches: each start of a note) */
#endif
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
    int rank;                                          /* v66: 0 the picker's first note, 1 its second, 9 a bass rescue */
} pxp_candidate;

#ifndef PXP_DECAY
#ifdef PXP_TUNE
static float PXP_DECAY = 0.0226644f;
#else
#define PXP_DECAY 0.0226644f                           /* the poly bank's time constant (s); v66: tunable */
#endif
#endif
static pxr_bank pxp_bank PX_LO(pxp_bank);
static pxp_tracker pxp;

static void pxp_reset(px_engine *e, int configure)
{
    if (configure) {
        erb_prepare(e);
        pxr_reset_bank_decay(&pxp_bank, e->sr, PXP_DECAY);
    } else {
        memset(pxp_bank.state, 0, sizeof pxp_bank.state);
        memset(pxp_bank.age, 0, sizeof pxp_bank.age);
        pxp_bank.hop = 0;
    }
    memset(&pxa_trk, 0, sizeof pxa_trk);
    memset(&pxp, 0, sizeof pxp);
    pxp.inv_rate = 1.0f / e->sr;
    pxp.report_rate = e->sr / (4 * PXR_HOP);
    pxp.excitation_scale = 2.0f * onepole_k(e->sr * 0.25f, PXP_DECAY);
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
static float PXP_NEAR = 0.075f, PXP_HREL = 1000.0f, PXP_ENVMIN = 0.0012f, PXP_SUBFIX = 0.0f, PXP_ACQ2 = 0.0f;
static int PXP_BYRANK = 0, PXP_QUIET = 1; static unsigned PXP_CONFIRM2 = 6; static float PXP_SECOND = 0.5f;
static unsigned PXP_CONFIRM_N = 3, PXP_HOLD_N = 4;
#else
#ifndef PXP_NEAR
#define PXP_NEAR 0.075f
#endif
#ifndef PXP_HREL
#define PXP_HREL 1000.0f
#endif
#ifndef PXP_ENVMIN
#define PXP_ENVMIN 0.0012f
#endif
#ifndef PXP_SUBFIX
#define PXP_SUBFIX 0.0f
#endif
#ifndef PXP_ACQ2
#define PXP_ACQ2 0.0f
#endif
#ifndef PXP_CONFIRM2
#define PXP_CONFIRM2 6u
#endif
#ifndef PXP_QUIET
#define PXP_QUIET 1
#endif
#ifndef PXP_SECOND
#define PXP_SECOND 0.5f
#endif
#ifndef PXP_BYRANK
#define PXP_BYRANK 0
#endif
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
static int8_t pxp_rank[PXR_BANDS];                     /* v66: each band's place in this report's picks, -1 not picked */

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
    memset(pxp_rank, -1, sizeof pxp_rank);
    for (int k = 0; k < count; ++k) pxp_rank[picks[k]] = (int8_t)k;
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

#ifdef PX_POLY_EVICT_TEST
#ifndef PXP_EVICT_MS
#define PXP_EVICT_MS 100
#endif
#ifndef PXP_EVICT_RATIO
#define PXP_EVICT_RATIO 1000.0f
#endif
#ifndef PXP_EVICT_TOL
#define PXP_EVICT_TOL 0.021f
#endif
static int pxp_onset_harmonic(float hz, float root)
{
    return pxp_harmonic(hz, root) ||
           px_abs(hz - 5.0f * root) < 0.105f * root ||
           px_abs(hz - 6.0f * root) < 0.125f * root;
}
#endif

static inline int pxp_blocks_new_harmonic(float hz, float root)
{
#if defined(PX_POLY_EVICT_TEST) && defined(PXP_EXTEND_GUARD)
    return pxp_onset_harmonic(hz, root);
#else
    return pxp_harmonic(hz, root);
#endif
}

static float pxp_power(int bin)
{
    const px_erb_complex z = pxp_bank.state[bin];
    return z.r * z.r + z.i * z.i;
}

#ifdef PX_POLY_OCTAVE_EVIDENCE_TEST
#ifndef PXP_OCT_ODD_LIMIT
#define PXP_OCT_ODD_LIMIT 0.15f
#endif
#ifndef PXP_OCT_SIXTH_FLOOR
#define PXP_OCT_SIXTH_FLOOR 0.015f
#endif
#ifndef PXP_OCT_ROOT_RATIO
#define PXP_OCT_ROOT_RATIO 0.55f
#endif
/* Compare the even partials that support an independent octave with the
 * odd partial that supports only the root. Use powers from the same report. */
static int pxp_octave_evidence(int root)
{
    if (root + 31 >= PXR_BANDS) return 0;
    const float octave = pxp_power(root + 12);
    return octave > PXP_OCT_ROOT_RATIO * pxp_power(root) &&
           pxp_power(root + 19) < PXP_OCT_ODD_LIMIT * octave &&
           pxp_power(root + 24) > 0.04f * octave &&
           pxp_power(root + 31) > PXP_OCT_SIXTH_FLOOR * octave;
}

static int pxp_has_octave_evidence(float root, float upper)
{
    if (px_abs(upper - 2.0f * root) >= 0.075f * root) return 0;
    int bin = PXR_FIRST_MIDI - PXR_LOW;
    while (bin + 1 < PXR_BANDS && pxp_bank.hz[bin] < 0.973f * root) ++bin;
    return pxp_octave_evidence(bin);
}
#endif

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
        bass[bin] = (pxp_candidate){note, 0.25f * twice + 0.75f * third, 0.60f * (even + odd), 9};
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
    if (pxp.env > PXP_ENVMIN) {
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
            /* v66: a candidate that is the second harmonic of a note still building up: its octave below holds
             * PXP_SUBFIX of its energy, and so does that note's third harmonic (a fifth above the candidate) */
            int at = bin;
            if (PXP_SUBFIX > 0.0f && bin - 12 >= PXR_FIRST_MIDI - PXR_LOW && bin + 7 < PXR_BANDS) {
                const float e = pxp_power(bin);
                if (pxp_power(bin - 12) > PXP_SUBFIX * e && pxp_power(bin + 7) > PXP_SUBFIX * e) at = bin - 12;
            }
            int dup = 0;
            for (int j = 0; j < count; ++j) dup |= candidates[j].note == pxp_bank.hz[at];
            if (dup) continue;
            candidates[count++] = (pxp_candidate){pxp_bank.hz[at], pxp_frequency(at, 0.046f),
                                                  track->sal[i] * track->level, pxp_rank[bin] < 0 ? 9 : pxp_rank[bin]};
        }
    }
#ifdef PX_POLY_OCTAVE_EVIDENCE_TEST
    const int roots = count;
    for (int i = 0; i < roots && count < PXT_NOTES; ++i) {
        int bin = PXR_FIRST_MIDI - PXR_LOW;
        while (bin < PXR_BANDS && pxp_bank.hz[bin] < candidates[i].note) ++bin;
        if (!pxp_octave_evidence(bin)) continue;
        const int at = bin + 12;
        int duplicate = 0;
        for (int j = 0; j < count; ++j) duplicate |= candidates[j].note == pxp_bank.hz[at];
        if (!duplicate) candidates[count++] = (pxp_candidate){
            pxp_bank.hz[at], pxp_frequency(at, 0.032f), pxp_power(at), 9};
    }
#endif
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
#ifdef PX_POLY_EVICT_TEST
        if (v->age < 255) ++v->age;
#endif
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
#ifdef PX_POLY_FADE_TEST
        /* Phase refinement can cross the bin boundary without changing the
         * nominal candidate. Limit that step on low, quiet notes. */
        if (!v->repick && c->note < 110.0f && pxp.env < 0.01f
#if defined(PXP_FADE_SETTLE) && defined(PX_POLY_EVICT_TEST)
            && v->age >= PXP_FADE_SETTLE
#endif
           ) {
            const float step = 0.012f * v->target;
            const float delta = c->hz - v->target;
            v->target += delta < -step ? -step : delta > step ? step : delta;
        } else
#endif
            v->target = c->hz;
        v->note = c->note;
        v->missing = 0;
    }
    /* v66: the quieter free voice first: a releasing voice must dip to silence before it can start again */
    const int quiet_first = PXP_QUIET && !voices[0].held && !voices[1].held && voices[1].gain < voices[0].gain;
    for (int n = 0; n < 2; ++n) {
        const int i = quiet_first ? 1 - n : n;
        pxp_note *v = &voices[i];
        if (v->held) continue;
        int best = -1, rank = 99;
        /* v66: the other voice holds a note that still carries PXP_SECOND of the level: this one is a second note
         * (stricter); a held note that has faded leaves the new one the lead (as fast as the first) */
        const int second = voices[1 - i].held && voices[1 - i].power >= PXP_SECOND * track->level;
        const float base_floor = track->level * PXP_ACQ;
        const float floor = second && PXP_ACQ2 > PXP_ACQ ? track->level * PXP_ACQ2 : base_floor;
        float power = base_floor;
        for (int j = 0; j < count; ++j) {
            const pxp_candidate *c = &candidates[j];
            const pxp_note *other = &voices[1 - i];
            float entry_floor = floor;
#if defined(PXP_LATE_ROOT_TEST) && defined(PX_POLY_EVICT_TEST)
            /* Low notes can carry less energy than the held upper note.
             * Keep the higher acquisition floor for candidates above it. */
            if (second && c->rank <= PXP_LATE_ROOT_TEST && c->note < other->note)
                entry_floor = base_floor;
#endif
            if ((used & (1u << j)) || c->power <= entry_floor) continue;
            int guard = other->held;
#ifdef PX_POLY_OCTAVE_EVIDENCE_TEST
            guard &= !pxp_has_octave_evidence(other->note, c->note);
#endif
            int nominal_octave = 0;
#if defined(PXP_NOMINAL_OCTAVE_TEST) && defined(PX_POLY_EVICT_TEST)
            /* A settled low note can wander within the resonator bandwidth.
             * Keep its octave subject to the harmonic guard through that drift. */
            nominal_octave = other->age >= (unsigned)(0.10f * pxp.report_rate) &&
                other->note < 110.0f && px_abs(c->note - 2.0f * other->note) < 0.01f * other->note;
#endif
            if (guard && (nominal_octave || pxp_blocks_new_harmonic(c->note, other->target)) &&
                c->power < PXP_HGUARD * track->level) continue;
            /* v66: a harmonic of the other held note needs PXP_HREL of its power; a pitch within PXP_NEAR of it is
             * that note bending, not a new one */
            if (guard && PXP_HREL > 0.0f && (nominal_octave || pxp_blocks_new_harmonic(c->hz, other->target)) &&
                c->power < PXP_HREL * other->power) continue;
            if (other->held && PXP_NEAR > 0.0f && px_abs(c->hz - other->target) < PXP_NEAR * other->target) continue;
            /* v66: the picker's order (harmonic sum) first, power within the same rank */
            if (PXP_BYRANK ? (c->rank > rank || (c->rank == rank && c->power <= power)) : c->power <= power) continue;
            best = j;
            power = c->power;
            rank = c->rank;
        }
        if (best < 0) { v->stable = 0; continue; }
        used |= 1u << best;
        const pxp_candidate *c = &candidates[best];
        const unsigned confirm = second && PXP_CONFIRM2 > PXP_CONFIRM_N ? PXP_CONFIRM2 : PXP_CONFIRM_N;
        if (v->pending == c->note) {
            if (v->stable < confirm) ++v->stable;
        } else {
            v->pending = c->note;
            v->stable = 1;
        }
        if (v->stable >= confirm && v->gain < 0.025f) {
            v->held = 1;
            v->missing = v->stable = 0;
            v->target = v->frequency = c->hz;
            v->power = c->power;
            v->sustain = pxp_sustain(c->power);
            v->note = c->note;
            v->events |= PXP_ACQUIRED;
#ifdef PX_POLY_EVICT_TEST
            v->age = 0;
#ifdef PXP_EVICT_FAST_RELEASE
            v->evicted = 0;
#endif
            pxp_note *other = &voices[1 - i];
            const float harmonic = c->hz * (int)(other->target / c->hz + 0.5f);
            if (other->held && other->age <= PXP_EVICT_MS * 0.001f * pxp.report_rate &&
#ifdef PX_POLY_OCTAVE_EVIDENCE_TEST
                !pxp_has_octave_evidence(c->note, other->note) &&
#endif
                other->power < PXP_EVICT_RATIO * c->power &&
                px_abs(other->target - harmonic) < PXP_EVICT_TOL * harmonic &&
                pxp_onset_harmonic(other->target, c->hz)) {
                other->held = other->repick = other->stable = 0;
#ifdef PXP_EVICT_FAST_RELEASE
                other->evicted = 1;
#endif
            }
#endif
#ifdef PXP_TUNE
            ++v->acquisitions;
#endif
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
    const float k =
#if defined(PX_POLY_EVICT_TEST) && defined(PXP_EVICT_FAST_RELEASE)
                    v->evicted ? pxp.dip :
#endif
                    v->repick || (!v->held && v->stable >= PXP_CONFIRM_N) ? pxp.dip :
                    target > v->gain ? attack : release;
    v->gain += k * (target - v->gain);
    if (v->gain < 1e-5f && !v->held) { v->gain = 0.0f; return 0.0f; }
    v->frequency += attack * (v->target - v->frequency);
    return v->frequency * pxp.inv_rate;
}
#endif
