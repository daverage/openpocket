/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * Opt-in target experiment, using the causal complex resonator method described by
 * Zhou et al. (doi:10.1155/2009/729494). Candidate policy benchmarked against the
 * external GPLv3 reference listed in docs/POLY-TRACKER-RESONATOR-BENCH.md.
 * This fixed-size implementation shares OpenPocket's existing decimation path.
 */
#ifndef PX_RESONATOR_TEST_H
#define PX_RESONATOR_TEST_H

#define PXR_LOW 23
#define PXR_BANDS 66
#define PXR_HOP 32
#ifndef PXR_FIRST_MIDI
#define PXR_FIRST_MIDI PXR_LOW
#endif
#if PXR_FIRST_MIDI < PXR_LOW || PXR_FIRST_MIDI >= PXR_LOW + PXR_BANDS
#error "PXR_FIRST_MIDI outside resonator bank"
#endif

static struct {
    px_erb_complex coeff[PXR_BANDS], state[PXR_BANDS];
    float hz[PXR_BANDS];
    uint8_t age[PXR_BANDS];
    unsigned hop;
} pxr_test PX_LO(pxr_test);

static void pxr_reset(float sample_rate)
{
    memset(&pxr_test, 0, sizeof pxr_test);
    const float rate = 0.25f * sample_rate;
    const float decay = expf(-1.0f / (0.0226644f * rate));
    for (int i = PXR_FIRST_MIDI - PXR_LOW; i < PXR_BANDS; ++i) {
        const float hz = 440.0f * expf((i + PXR_LOW - 69) * 0.057762265f);
        const float phase = 2.0f * PI_F * hz / rate;
        pxr_test.hz[i] = hz;
        pxr_test.coeff[i] = (px_erb_complex){decay * cosf(phase), decay * sinf(phase)};
    }
}

static __attribute__((noinline)) void pxr_pick(px_track *out)
{
    float energy[PXR_BANDS], maximum = 0.0f;
    const int start = PXR_FIRST_MIDI - PXR_LOW;
    for (int i = start; i < PXR_BANDS; ++i) {
        const px_erb_complex z = pxr_test.state[i];
        energy[i] = z.r * z.r + z.i * z.i;
        if (energy[i] > maximum) maximum = energy[i];
    }
    const float threshold = maximum * 0.15f;
    int peaks[PXT_NOTES], count = 0;
    if (maximum >= 1e-10f) for (int i = start; i < PXR_BANDS; ++i) {
        if (energy[i] <= threshold ||
            (i > start && energy[i] <= energy[i - 1]) ||
            (i + 1 < PXR_BANDS && energy[i] <= energy[i + 1])) continue;
        int at = count;
        while (at > 0 && energy[i] > energy[peaks[at - 1]]) --at;
        if (at == PXT_NOTES) continue;
        if (count < PXT_NOTES) ++count;
        for (int j = count - 1; j > at; --j) peaks[j] = peaks[j - 1];
        peaks[at] = i;
    }
    uint8_t present[PXR_BANDS] = {0};
    out->mono = 0.0f;
    out->conf = 0.0f;
    out->level = maximum;
    out->onset = 0.0f;
    out->nnotes = 0;
    for (int k = 0; k < count; ++k) {
        const int bin = peaks[k];
        int octave = 0;
        for (int j = 0; j < k; ++j) if (peaks[j] == bin - 12) octave = 1;
        if (octave) continue;
        present[bin] = 1;
        if (pxr_test.age[bin] < 3) ++pxr_test.age[bin];
        if (k > 0 && pxr_test.age[bin] < 3) continue;
        if (k == 0) {
            out->mono = pxr_test.hz[bin];     /* diagnostic mono audition uses the strongest note */
            out->conf = 1.0f;
        }
        int j = out->nnotes++;
        while (j > 0 && out->notes[j - 1] > pxr_test.hz[bin]) {
            out->notes[j] = out->notes[j - 1];
            out->sal[j] = out->sal[j - 1];
            --j;
        }
        out->notes[j] = pxr_test.hz[bin];
        out->sal[j] = energy[bin] / maximum;
    }
    for (int i = start; i < PXR_BANDS; ++i) if (!present[i]) pxr_test.age[i] = 0;
}

static __attribute__((noinline)) int pxr_run(px_track *out, float input)
{
    float x;
    if (!pxt_decimate(out, input, &x)) return 0;
    for (int i = PXR_FIRST_MIDI - PXR_LOW; i < PXR_BANDS; ++i) {
        const px_erb_complex c = pxr_test.coeff[i], z = pxr_test.state[i];
        pxr_test.state[i] = (px_erb_complex){x + c.r * z.r - c.i * z.i, c.i * z.r + c.r * z.i};
    }
    if (++pxr_test.hop < PXR_HOP) return 0;
    pxr_test.hop = 0;
    pxr_pick(out);
    return 1;
}
#endif
