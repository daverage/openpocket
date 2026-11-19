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

typedef struct {
    px_erb_complex coeff[PXR_BANDS], state[PXR_BANDS];
    float hz[PXR_BANDS];
    uint8_t age[PXR_BANDS];
    unsigned hop;
} pxr_bank;

#if defined(PX_RESONATOR_TEST) || defined(PX_LOWEST_TEST)
static pxr_bank pxr_test PX_LO(pxr_test);
#endif

static void pxr_reset_bank_decay(pxr_bank *bank, float sample_rate, float seconds)
{
    memset(bank, 0, sizeof *bank);
    const float rate = 0.25f * sample_rate;
    const float decay = expf(-1.0f / (seconds * rate));
    for (int i = PXR_FIRST_MIDI - PXR_LOW; i < PXR_BANDS; ++i) {
        const float hz = 440.0f * expf((i + PXR_LOW - 69) * 0.057762265f);
        const float phase = 2.0f * PI_F * hz / rate;
        bank->hz[i] = hz;
        bank->coeff[i] = (px_erb_complex){decay * cosf(phase), decay * sinf(phase)};
    }
}
static void pxr_reset_bank(pxr_bank *bank, float sample_rate) { pxr_reset_bank_decay(bank, sample_rate, 0.0226644f); }

static __attribute__((noinline)) void pxr_pick(pxr_bank *bank, px_track *out)
{
    float energy[PXR_BANDS], maximum = 0.0f;
    const int start = PXR_FIRST_MIDI - PXR_LOW;
    for (int i = start; i < PXR_BANDS; ++i) {
        const px_erb_complex z = bank->state[i];
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
        if (bank->age[bin] < 3) ++bank->age[bin];
        if (k > 0 && bank->age[bin] < 3) continue;
        if (k == 0) {
            out->mono = bank->hz[bin];     /* diagnostic mono audition uses the strongest note */
            out->conf = 1.0f;
        }
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

/* v59: advance the bank by one input sample; 1 when a report is due (the caller picks). */
static __attribute__((noinline)) int pxr_advance(pxr_bank *bank, px_track *out, float input)
{
    float x;
    if (!pxt_decimate(out, input, &x)) return 0;
    for (int i = PXR_FIRST_MIDI - PXR_LOW; i < PXR_BANDS; ++i) {
        const px_erb_complex c = bank->coeff[i], z = bank->state[i];
        bank->state[i] = (px_erb_complex){x + c.r * z.r - c.i * z.i, c.i * z.r + c.r * z.i};
    }
    if (++bank->hop < PXR_HOP) return 0;
    bank->hop = 0;
    return 1;
}

static inline int pxr_run_bank(pxr_bank *bank, px_track *out, float input)
{
    if (!pxr_advance(bank, out, input)) return 0;
    pxr_pick(bank, out);
    return 1;
}
#if defined(PX_RESONATOR_TEST) || defined(PX_LOWEST_TEST)
static void pxr_reset(float rate) { pxr_reset_bank(&pxr_test, rate); }
static int pxr_run(px_track *out, float input) { return pxr_run_bank(&pxr_test, out, input); }
#endif
#endif
