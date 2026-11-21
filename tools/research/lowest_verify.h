/* Host-only harmonic checks and continuous phase refinement for Lowest Sub. */
#include "../../engine/px_lowest_evidence.h"

/* Test low-note hypotheses before the broad peak picker's ranking and octave pruning.
 * The fundamental and adjacent harmonic spacings must agree with the same note. */
static float lv_low_candidate(const lowest_verify *state, float maximum)
{
    const int offsets[] = {0, 12, 19, 24, 28};
    float best = 0.0f, result = 0.0f;
    for (int bin = 0; bin < 12; ++bin) {
        const float root = pxr_test.hz[bin];
        const int fundamental = lv_support(state, bin, root, maximum);
        if (fundamental < 0) continue;
        const float estimate = state->frequency[fundamental];
        const float error = px_abs(estimate / root - 1.0f);
        if (error > 0.028f) continue;
        float score = fminf(state->magnitude[fundamental] / maximum, 0.6f);
        int votes = 1, odd = 0;
        for (int h = 2; h <= 5; ++h) {
            const int matched = lv_support(state, bin + offsets[h - 1], estimate * h, maximum);
            if (matched < 0) continue;
            score += fminf(state->magnitude[matched] / maximum, 0.6f);
            ++votes;
            if (h & 1) ++odd;
        }
        if (votes < 3 || !odd) continue;
        score *= 1.0f - 15.0f * error;
        if (score <= best) continue;
        best = score;
        result = root;
    }
    return result;
}

/* An incumbent needs less evidence than a new low note. Read the faster bank
 * for its fundamental and a corroborating partial so the slow probe cannot
 * retain a note solely from its own filter tail. */
static int lv_audible_low(const lowest_verify *fast, const lowest_verify *slow,
                          float pitch, float fast_maximum, float slow_maximum)
{
    const int bin = lv_bin(pitch);
    const int fundamental = lv_support(slow, bin, pitch, 0.60f * slow_maximum);
    if (fundamental < 0) return 0;
    const float estimate = slow->frequency[fundamental];
    if (lv_support(fast, bin, estimate, 0.70f * fast_maximum) < 0) return 0;
    return lv_support(fast, bin + 12, 2.0f * estimate, 0.70f * fast_maximum) >= 0 ||
           lv_support(fast, bin + 19, 3.0f * estimate, 0.70f * fast_maximum) >= 0;
}

static float lv_refine(const lowest_verify *state, float pitch, float maximum)
{
    const int bin = lv_bin(pitch);
    const int fundamental = lv_support(state, bin, pitch, maximum);
    if (fundamental < 0) return 0.0f;
    float total = state->frequency[fundamental] * state->magnitude[fundamental];
    float weight = state->magnitude[fundamental];
    const int offsets[] = {12, 19};
    for (int h = 2; h <= 3; ++h) {
        const int i = lv_support(state, bin + offsets[h - 2], pitch * h, maximum);
        if (i < 0 || px_abs(state->frequency[i] / h - state->frequency[fundamental]) > 0.012f * pitch) continue;
        const float w = state->magnitude[i] / (float)h;
        weight += w;
        total += w * state->frequency[i] / (float)h;
    }
    return total / weight;
}

static int lv_shared(const px_track *track, float pitch, float partial)
{
    for (int n = 0; n < track->nnotes; ++n) {
        const float other = track->notes[n];
        if (px_abs(other - pitch) < 0.033f * pitch) continue;
        const int order = (int)(partial / other + 0.5f);
        if (order < 1 || order > 8) continue;
        if (px_abs(partial - order * other) < fmaxf(7.0f, 0.024f * partial)) return 1;
    }
    return 0;
}

/* Use only partials that are unlikely to be explained by another sounding note.
 * If the fundamental is masked, two agreeing harmonics can still refine the pitch. */
static float lv_refine_exclusive(const lowest_verify *state, const px_track *track, float pitch, float maximum)
{
    const int bin = lv_bin(pitch);
    const int offsets[] = {0, 12, 19, 24};
    float total = 0.0f, weight = 0.0f, anchor = 0.0f;
    int votes = 0, fundamental = 0;
    for (int h = 1; h <= 4; ++h) {
        const float partial = pitch * h;
        const int i = lv_support(state, bin + offsets[h - 1], partial, maximum);
        if (i < 0 || lv_shared(track, pitch, state->frequency[i])) continue;
        const float estimate = state->frequency[i] / h;
        if (votes && px_abs(estimate - anchor) > 0.012f * pitch) continue;
        if (!votes) anchor = estimate;
        const float w = state->magnitude[i] / h;
        total += estimate * w;
        weight += w;
        ++votes;
        if (h == 1) fundamental = 1;
    }
    return fundamental || votes >= 2 ? total / weight : 0.0f;
}
