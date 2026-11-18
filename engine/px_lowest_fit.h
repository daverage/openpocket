/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * Causal harmonic fit for low notes. Shares the engine's packed 128-point FFT.
 */
enum {
    LF_SIZE = 256, LF_HALF = LF_SIZE / 2, LF_WINDOW = 176,
    LF_DECIMATE = 16, LF_HOP = 16, LF_HARMONICS = 8,
    LF_FIRST = 23, LF_LAST = 60, LF_TUNINGS = 3, LF_GROUP = 8,
    LF_SCAN_STAGES = (LF_LAST - LF_FIRST + LF_GROUP) / LF_GROUP,
    LF_RESIDUAL_STAGE = LF_SCAN_STAGES + 1,
    LF_FINAL_STAGE = 2 * LF_SCAN_STAGES + 1,
    LF_HISTORY = LF_WINDOW + LF_HOP
};

typedef struct {
    px_erb_complex partial[LF_HARMONICS];
    float pitch, bin, score;
} lf_match;

typedef struct {
    float history[LF_HISTORY], taper[LF_WINDOW / 2];
    float even[LF_HALF], odd[LF_HALF];
    px_erb_complex odd_rotation[LF_HALF / 4];
    float roots[LF_LAST - LF_FIRST + 1], window_sum, sample_sum;
    unsigned at, frame_at, phase, hop;
    float energy;
    lf_match first, second, held;
    float anchor, held_hz, held_gain;
    float first_hz, second_hz, first_gain, second_gain, first_root, second_root, first_share;
} lowest_fit;

static void lf_init(lowest_fit *fit, float rate)
{
    if (!fft_ready) fft_init();
    for (int i = 0; i < LF_WINDOW / 2; ++i) {
        const float taper = 0.5f - 0.5f * cosf(2.0f * PI_F * i / (LF_WINDOW - 1));
        fit->taper[i] = taper;
        fit->window_sum += 2.0f * taper;
    }
    for (int i = 1; i < LF_HALF / 2; i += 2) {
        const float phase = -2.0f * PI_F * i / LF_SIZE;
        fit->odd_rotation[i / 2] = (px_erb_complex){cosf(phase), sinf(phase)};
    }
    for (int i = LF_FIRST; i <= LF_LAST; ++i)
        fit->roots[i - LF_FIRST] = 440.0f * expf((i - 69) * 0.057762265f) * (LF_SIZE * LF_DECIMATE / rate);
}

static float lf_taper(const lowest_fit *fit, int i)
{
    return fit->taper[i < LF_WINDOW / 2 ? i : LF_WINDOW - 1 - i];
}

static float *lf_time_slot(lowest_fit *fit, int i)
{
    const int centered = (i - LF_WINDOW / 2 + LF_SIZE) & (LF_SIZE - 1);
    float *out = centered & 1 ? fit->odd : fit->even;
    return &out[centered / 2];
}

static float lf_window(lowest_fit *fit)
{
    memset(fit->even, 0, sizeof fit->even);
    memset(fit->odd, 0, sizeof fit->odd);
    float energy = 0.0f;
    int at = fit->frame_at;
    for (int i = 0; i < LF_WINDOW; ++i) {
        const float x = fit->history[at];
        if (++at == LF_HISTORY) at = 0;
        const float tapered = x * lf_taper(fit, i);
        *lf_time_slot(fit, i) = tapered;
        energy += x * tapered;
    }
    return energy;
}

/* Combine the even/odd transforms. Store their upper bins in reverse order
 * so each butterfly can overwrite the 128-point results it just read. */
static void lf_spectrum(lowest_fit *fit)
{
    rfft128(fit->even);
    rfft128(fit->odd);
    const float middle_r = fit->even[1], middle_i = -fit->odd[1];
    fit->even[0] += fit->odd[0];
    fit->odd[0] = middle_r;
    fit->odd[1] = middle_i;
    for (int k = 1; k < LF_HALF / 2; ++k) {
        float *a = &fit->even[k * 2], *b = &fit->odd[k * 2];
        const px_erb_complex w = k & 1 ? fit->odd_rotation[k / 2]
                                       : (px_erb_complex){rft_c[k / 2], -rft_s[k / 2]};
        const float real = w.r * b[0] - w.i * b[1];
        const float imag = w.r * b[1] + w.i * b[0];
        b[0] = a[0] - real;
        b[1] = imag - a[1];
        a[0] += real;
        a[1] += imag;
    }
}

static const float *lf_bin(const lowest_fit *fit, int bin)
{
    if (bin == LF_HALF / 2) return fit->odd;
    if (bin > LF_HALF / 2) return &fit->odd[2 * (LF_HALF - bin)];
    return &fit->even[2 * bin];
}

static px_erb_complex lf_interp(const lowest_fit *fit, float bin)
{
    const int k = (int)bin;
    const float f = bin - k;
    const float *a = lf_bin(fit, k - 1), *b = lf_bin(fit, k);
    const float *c = lf_bin(fit, k + 1), *d = lf_bin(fit, k + 2);
    const float wa = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
    const float wb = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
    const float wc = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
    const float wd = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
    return (px_erb_complex){wa * a[0] + wb * b[0] + wc * c[0] + wd * d[0],
                            wa * a[1] + wb * b[1] + wc * c[1] + wd * d[1]};
}

/* Partial energy near the stronger note's comb is too ambiguous to keep
 * the old pitch alive. This runs only for the previously confirmed note. */
static __attribute__((noinline)) void lf_keep(lowest_fit *fit, const lf_match *match, float floor)
{
    const float upper = fit->first.bin, inv = 1.0f / upper;
    int distinct = 0;
    float sum = 0.0f;
    for (int h = 0; h < LF_HARMONICS; ++h) {
        const px_erb_complex z = match->partial[h];
        const float power = z.r * z.r + z.i * z.i;
        if (power <= floor) continue;
        const float bin = match->bin * (h + 1);
        const int order = (int)(bin * inv + 0.5f);
        if (px_abs(bin - order * upper) < 1.1f) continue;
        ++distinct;
        sum += power;
    }
    if (distinct >= 2 && sum > fit->held.score) {
        fit->held = *match;
        fit->held.score = sum;
    }
}

static __attribute__((noinline)) void lf_pick(lowest_fit *fit, lf_match *best,
                                              int group, float excluded)
{
    static const float tuning[LF_TUNINGS] = {0.988514f, 1.0f, 1.011619f};
    static const float weights[LF_HARMONICS] = {1.0f, 1.0f, .88f, .76f, .66f, .56f, .51f, .46f};
    const float floor = 0.006f * fit->energy * fit->window_sum;
    const int begin = group * LF_GROUP;
    for (int note = begin; note < begin + LF_GROUP && note <= LF_LAST - LF_FIRST; ++note) {
        const float nominal = fit->roots[note];
        if (nominal == excluded) continue;
        const int test_held = excluded > 0.0f && nominal == fit->anchor;
        for (int t = 0; t < LF_TUNINGS; ++t) {
            lf_match match = {.pitch = nominal, .bin = nominal * tuning[t]};
            unsigned support = 0;
            int votes = 0, low_votes = 0;
            for (int h = 0; h < LF_HARMONICS; ++h) {
                const float bin = match.bin * (h + 1);
                if (bin >= LF_HALF - 2) break;
                const px_erb_complex z = lf_interp(fit, bin);
                const float power = z.r * z.r + z.i * z.i;
                match.partial[h] = z;
                match.score += weights[h] * power;
                if (power <= floor) continue;
                support |= 1u << h;
                ++votes;
                if (h < 4) ++low_votes;
            }
            const int valid = ((support & 1u) && votes >= 2) ||
                              (votes >= 3 && low_votes >= 2 && (support & ((1u << 2) | (1u << 4))));
            if (valid && match.score > best->score) *best = match;
            if (test_held) lf_keep(fit, &match, 0.09f * floor);
        }
    }
}

static void lf_subtract(lowest_fit *fit, const lf_match *match)
{
    const float gain = 2.0f / fit->window_sum;
    for (int h = 0; h < LF_HARMONICS; ++h) {
        const px_erb_complex z = match->partial[h];
        if (z.r == 0.0f && z.i == 0.0f) continue;
        const float step = match->bin * (h + 1) / LF_SIZE;
        const float approx_c = sin_hq(step + 0.25f), approx_s = sin_hq(step);
        const float correction = 0.5f * (3.0f - approx_c * approx_c - approx_s * approx_s);
        const float c = approx_c * correction, s = approx_s * correction;
        const float cycle = -step * (LF_WINDOW / 2);
        const float phase = wrap1(cycle - (int)cycle);
        const float start_c = sin_hq(wrap1(phase + 0.25f)), start_s = sin_hq(phase);
        float r = gain * (z.r * start_c - z.i * start_s);
        float i = gain * (z.i * start_c + z.r * start_s);
        for (int n = 0; n < LF_WINDOW; ++n) {
            *lf_time_slot(fit, n) -= lf_taper(fit, n) * r;
            const float next = c * r - s * i;
            i = s * r + c * i;
            r = next;
        }
    }
}

static float lf_level(const lowest_fit *fit, const lf_match *match)
{
    static const float weight[] = {1.0f, 0.10f, 0.05f};
    float power = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const px_erb_complex z = match->partial[i];
        power += weight[i] * (z.r * z.r + z.i * z.i);
    }
    return (2.0f / fit->window_sum) * sqrtf(power);
}

static void lf_result(const lowest_fit *fit, const lf_match *match, float rate,
                      float *hz, float *gain, float *root)
{
    *hz = match->pitch * (rate / (LF_SIZE * LF_DECIMATE));
    *gain = lf_level(fit, match);
    const px_erb_complex z = match->partial[0];
    *root = 2.0f * sqrtf(z.r * z.r + z.i * z.i) / fit->window_sum;
}

/* Spread the FFTs and scans across the report interval. The history has an
 * extra hop so the residual pass can still read the same analysis frame. */
static __attribute__((noinline)) int lf_run(lowest_fit *fit, float x, float rate)
{
    fit->sample_sum += x;
    if (++fit->phase < LF_DECIMATE) return 0;
    fit->history[fit->at] = fit->sample_sum / LF_DECIMATE;
    if (++fit->at == LF_HISTORY) fit->at = 0;
    fit->sample_sum = 0.0f;
    fit->phase = 0;
    const unsigned stage = fit->hop = (fit->hop + 1) & (LF_HOP - 1);
    if (stage == 0) {
        fit->frame_at = fit->at + LF_HOP;
        if (fit->frame_at >= LF_HISTORY) fit->frame_at -= LF_HISTORY;
        fit->first = (lf_match){0};
        fit->second = (lf_match){0};
        fit->held = (lf_match){0};
        fit->energy = lf_window(fit);
        if (fit->energy >= 0.00025f) lf_spectrum(fit);
    } else if (fit->energy >= 0.00025f) {
        if (stage <= LF_SCAN_STAGES) {
            lf_pick(fit, &fit->first, (int)stage - 1, 0.0f);
        } else if (fit->first.pitch > 0.0f) {
            if (stage == LF_RESIDUAL_STAGE) {
                lf_window(fit);
                lf_subtract(fit, &fit->first);
                lf_spectrum(fit);
                fit->second.score = 0.04f * fit->energy * fit->window_sum;
            } else if (stage <= LF_FINAL_STAGE) {
                lf_pick(fit, &fit->second, (int)stage - LF_RESIDUAL_STAGE - 1, fit->first.pitch);
            }
        }
    }
    if (stage != LF_FINAL_STAGE) return 0;
    fit->first_share = fit->energy > 0.0f ? 2.0f * fit->first.score / (fit->energy * fit->window_sum) : 0.0f;
    lf_result(fit, &fit->first, rate, &fit->first_hz, &fit->first_gain, &fit->first_root);
    lf_result(fit, &fit->second, rate, &fit->second_hz, &fit->second_gain, &fit->second_root);
    if (fit->anchor == fit->first.pitch) fit->held = fit->first;
    if (fit->held.score >= 0.004f * fit->energy * fit->window_sum) {
        fit->held_hz = fit->held.pitch * (rate / (LF_SIZE * LF_DECIMATE));
        fit->held_gain = lf_level(fit, &fit->held);
    } else {
        fit->held_hz = fit->held_gain = 0.0f;
    }
    return 1;
}
