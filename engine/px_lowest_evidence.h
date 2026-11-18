/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * Resonator evidence used by the Lowest Sub selector.
 */
#ifndef PX_LOWEST_EVIDENCE_H
#define PX_LOWEST_EVIDENCE_H
typedef struct {
    px_erb_complex previous[PXR_BANDS], rotation[PXR_BANDS], smooth[PXR_BANDS];
    float magnitude[PXR_BANDS], frequency[PXR_BANDS];
    int ready;
} lowest_verify;

static void lv_init(lowest_verify *state, float rate)
{
    for (int i = 0; i < PXR_BANDS; ++i) {
        const float advance = 2.0f * PI_F * pxr_test.hz[i] * PXR_HOP / (rate * 0.25f);
        state->rotation[i] = (px_erb_complex){cosf(advance), sinf(advance)};
    }
}

static float lv_observe(lowest_verify *state, float rate, float smoothing)
{
    float maximum = 0.0f;
    const float scale = rate / (8.0f * PI_F * PXR_HOP);
    for (int i = 0; i < PXR_BANDS; ++i) {
        px_erb_complex z = pxr_test.state[i];
        const px_erb_complex previous = state->previous[i], rot = state->rotation[i];
        if (smoothing > 0.0f) {
            /* A pole applied at the report rate sharpens the existing resonator without
             * another sample-rate bank. Rotation keeps its passband centered on this bin. */
            const px_erb_complex prior = state->smooth[i];
            z = (px_erb_complex){
                (1.0f - smoothing) * z.r + smoothing * (prior.r * rot.r - prior.i * rot.i),
                (1.0f - smoothing) * z.i + smoothing * (prior.i * rot.r + prior.r * rot.i)
            };
            state->smooth[i] = z;
        }
        const float x = previous.r * rot.r - previous.i * rot.i;
        const float y = previous.r * rot.i + previous.i * rot.r;
        state->previous[i] = z;
        state->magnitude[i] = sqrtf(z.r * z.r + z.i * z.i);
        state->frequency[i] = 0.0f;
        if (state->ready && (x * x + y * y) > 1e-8f && state->magnitude[i] > 1e-4f)
            state->frequency[i] = pxr_test.hz[i] + scale * pxt_atan2(z.i * x - z.r * y, z.r * x + z.i * y);
        if (state->magnitude[i] > maximum) maximum = state->magnitude[i];
    }
    state->ready = 1;
    return maximum;
}

/* Search the nearest bin and its neighbors. A strong adjacent harmonic that does not
 * actually rotate near the expected frequency cannot confirm the hypothesis. */
static int lv_support(const lowest_verify *state, int bin, float expected, float maximum)
{
    int best = -1;
    float level = maximum * 0.10f;
    for (int i = bin - 1; i <= bin + 1; ++i) {
        if (i < 0 || i >= PXR_BANDS || state->magnitude[i] <= level) continue;
        if (px_abs(state->frequency[i] - expected) > 0.033f * expected) continue;
        best = i;
        level = state->magnitude[i];
    }
    return best;
}

static int lv_bin(float pitch)
{
    return (int)(69 - PXR_LOW + 17.31234f * logf(pitch / 440.0f) + 0.5f);
}

/* Keep the lowest observed note. Recover a lower octave only when there is actual
 * fundamental evidence plus both even and odd harmonics at the right frequencies. */
static float lv_select(const lowest_verify *state, const px_track *track, float maximum)
{
    float selected = track->nnotes ? track->notes[0] : 0.0f;
    if (selected <= 0.0f) return selected;
    for (int i = 0; i < track->nnotes; ++i) {
        const float lower = 0.5f * track->notes[i];
        if (lower >= selected || lower < pxr_test.hz[0] * 0.97f) continue;
        const int bin = lv_bin(lower);
        if (bin < 0 || bin >= PXR_BANDS) continue;
        if (lv_support(state, bin, lower, maximum) >= 0 &&
            lv_support(state, bin + 12, 2.0f * lower, maximum) >= 0 &&
            lv_support(state, bin + 19, 3.0f * lower, maximum) >= 0)
            selected = pxr_test.hz[bin];
    }
    return selected;
}

#endif
