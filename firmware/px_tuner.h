/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the tuner's measurement (v75), host-testable (tests/tunersim.c).
 *
 *   tn_capture   (the audio interrupt) the input low-passed and decimated by 4 (11 025 Hz), into a ring
 *   tuner_estimate  (the main loop) the pitch: YIN (A. de Cheveigne and H. Kawahara, 2002), 25 Hz .. 1.5 kHz
 *   tn_smooth    the readings steadied: a median of the last five, a log-domain average, a hold across a few missed readings
 *   tn_note      the note and the cents for a frequency against a reference A
 *
 * v75 against v42..v74: the range reaches down to 25 Hz (a 5-string bass's low B is 31 Hz; it was 61 Hz), up to 1.5 kHz (1.1 kHz);
 * the decimation is a 24-tap windowed-sinc low-pass (it was the mean of four samples, which lets the highs fold back); the DC is taken out; the search
 * stops at the first dip, so a note costs about its own period. The caller defines TN_MEM (a section for the buffers, or nothing) and TN_READY. */
#pragma once
#include <stdint.h>
#include <math.h>

#define TN_N    2048u                                   /* the ring at 11 025 Hz (186 ms) */
#define TN_W    840u                                    /* YIN's window (76 ms) */
#define TN_TMIN 7u                                      /* 1 575 Hz */
#define TN_TMAX 420u                                    /* 26 Hz */
#define TN_FIR  24u
static float tn_buf[TN_N] TN_MEM;
static volatile uint32_t tn_w;
static float tn_lin[TN_W + TN_TMAX] TN_MEM;             /* the newest W + TMAX samples in order */
static float tn_d[TN_TMAX + 2u] TN_MEM;
static float tn_fir[TN_FIR];
static float tn_hist[32];
static uint32_t tn_hp, tn_ph;
static uint8_t tn_fir_ready;

static void tn_fir_init(void)                           /* a Hann-windowed sinc, cut-off 4.4 kHz of 44.1 kHz, the sum 1 */
{
    float s = 0.0f;
    for (uint32_t j = 0; j < TN_FIR; j++) {
        const float t = (float)j - 0.5f * (float)(TN_FIR - 1u), a = 6.2831853f * 0.10f * t;
        const float sinc = (t > -1e-6f && t < 1e-6f) ? 1.0f : sinf(a) / a;
        tn_fir[j] = sinc * (0.5f - 0.5f * cosf(6.2831853f * ((float)j + 0.5f) / (float)TN_FIR));
        s += tn_fir[j];
    }
    for (uint32_t j = 0; j < TN_FIR; j++) tn_fir[j] /= s;
    tn_fir_ready = 1;
}
static void tn_capture(const float *x, uint32_t n)      /* (the audio interrupt) */
{
    if (!TN_READY) return;
    if (!tn_fir_ready) tn_fir_init();
    uint32_t w = tn_w, hp = tn_hp, ph = tn_ph;
    for (uint32_t i = 0; i < n; i++) {
        tn_hist[hp] = x[i]; hp = (hp + 1u) & 31u;
        if (++ph >= 4u) {
            ph = 0;
            float y = 0.0f;
            for (uint32_t j = 0; j < TN_FIR; j++) y += tn_fir[j] * tn_hist[(hp - 1u - j) & 31u];
            tn_buf[w] = y;
            w = (w + 1u) & (TN_N - 1u);
        }
    }
    tn_hp = hp; tn_ph = ph; tn_w = w;
}
/* the fundamental's own frequency near f (Hz), from how far its phase turns between two windows of the newest samples: YIN finds the period all the
 * partials share, and a string's partials are stretched (inharmonicity: the 8th is up to 5 cents high in a plain string, more in a wound one), which
 * pulls that period 2 cents off the fundamental. Two Hann-windowed sums of the signal against e^{-j w0 n} (w0 at f), a window apart in time S, give the
 * component's phase advance w S; the turn beyond w0 S, over S, is the frequency's error. The window is five periods (the partials are four bins apart
 * or more: they do not lean on it), at least 76 ms. Right to within +-(fs / 2S) = 10 .. 21 Hz of f, which YIN's own error is far inside. */
static float tn_atan2(float y, float x)                 /* |error| < 1e-5 rad (the pedal's libm has no atan2f) */
{
    const float ax = x < 0.0f ? -x : x, ay = y < 0.0f ? -y : y, mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx < 1e-30f) return 0.0f;
    const float a = mn / mx, q = a * a;
    float r = ((((-0.0117212f * q + 0.05265332f) * q - 0.11643287f) * q + 0.19354346f) * q - 0.33262347f) * q + 0.99997726f;
    r *= a;
    if (ay > ax) r = 1.5707963f - r;
    if (x < 0.0f) r = 3.1415927f - r;
    return y < 0.0f ? -r : r;
}
static float tn_refine(float f)
{
    const float fs = 11025.0f;
    uint32_t M = (uint32_t)(5.0f * fs / f);
    if (M < TN_W) M = TN_W;
    if (M > 1536u) M = 1536u;
    const uint32_t S = M / 3u, L = M + S, w = tn_w;
    float mean = 0.0f;
    for (uint32_t i = 0; i < L; i++) mean += tn_buf[(w + TN_N - L + i) & (TN_N - 1u)];
    mean /= (float)L;
    const float om = 6.2831853f * f / fs, cr = cosf(om), ci = -sinf(om), th = 6.2831853f / (float)M, c2 = 2.0f * cosf(th);
    float a1r = 0, a1i = 0, a2r = 0, a2i = 0, rr = 1.0f, ri = 0.0f, c0 = 1.0f, c1 = cosf(th);   /* the rotation e^{-j w0 i}, the Hann window by its recurrence */
    for (uint32_t i = 0; i < M; i++) {
        const float win = 0.5f - 0.5f * c0;
        const float x1 = (tn_buf[(w + TN_N - L + i) & (TN_N - 1u)] - mean) * win, x2 = (tn_buf[(w + TN_N - L + S + i) & (TN_N - 1u)] - mean) * win;
        a1r += x1 * rr; a1i += x1 * ri; a2r += x2 * rr; a2i += x2 * ri;
        const float nr = rr * cr - ri * ci, ni = rr * ci + ri * cr; rr = nr; ri = ni;
        const float c3 = c2 * c1 - c0; c0 = c1; c1 = c3;
    }
    const float pr = a2r * a1r + a2i * a1i, pi = a2i * a1r - a2r * a1i;        /* A2 conj(A1) */
    if (pr * pr + pi * pi < 1e-20f) return f;
    float d = tn_atan2(pi, pr) - om * (float)S;
    while (d > 3.1415927f) d -= 6.2831853f;
    while (d < -3.1415927f) d += 6.2831853f;
    const float fr = f + d / (float)S * fs / 6.2831853f;
    return (fr > f * 0.98f && fr < f * 1.02f) ? fr : f;                         /* (within 35 cents: else keep YIN's) */
}
/* the pitch in Hz, or 0 (silence, noise, nothing clear) */
static float tuner_estimate(void)
{
    if (!TN_READY) return 0.0f;
    const uint32_t W = TN_W, TMAX = TN_TMAX, TMIN = TN_TMIN, L = W + TMAX, w = tn_w;
    float e = 0.0f, mean = 0.0f;
    for (uint32_t i = 0; i < L; i++) { const float v = tn_buf[(w + TN_N - L + i) & (TN_N - 1u)]; tn_lin[i] = v; mean += v; }
    mean /= (float)L;
    for (uint32_t i = 0; i < L; i++) { tn_lin[i] -= mean; e += tn_lin[i] * tn_lin[i]; }
    if (e < (float)L * 1.0e-6f) return 0.0f;           /* under -60 dBFS: nothing to tune */
    const float *x = tn_lin;
    float run = 0.0f, best_v = 1.0f;
    uint32_t best = 0, gmin = 0, found = 0, last = 0;
    tn_d[0] = 1.0f;
    for (uint32_t t = 1; t <= TMAX; t++) {
        float d = 0.0f;
        for (uint32_t i = 0; i < W; i++) { const float v = x[i] - x[i + t]; d += v * v; }
        run += d;
        const float c = run > 0.0f ? d * (float)t / run : 1.0f;
        tn_d[t] = c; last = t;
        if (t < TMIN) continue;
        if (!found) {
            if (c < 0.15f) { found = 1; best = t; best_v = c; }     /* the first dip under the threshold ... */
            else if (!gmin || c < tn_d[gmin]) gmin = t;
        } else if (c < best_v) { best = t; best_v = c; }             /* ... followed down to its bottom, then stop */
        else break;
    }
    if (!found) {                                       /* no clear dip: the lowest point, if it is low enough */
        if (!gmin || tn_d[gmin] > 0.35f) return 0.0f;
        best = gmin;
    }
    if (best + 1u > last || best < 2u) return 0.0f;    /* (a neighbour each side for the interpolation) */
    const float a = tn_d[best - 1u], b = tn_d[best], c3 = tn_d[best + 1u], den = a - 2.0f * b + c3;
    const float tf = (float)best + ((den > 1e-9f || den < -1e-9f) ? 0.5f * (a - c3) / den : 0.0f);
    return tn_refine(11025.0f / tf);
}

/* ---- steadying the readings ---- */
typedef struct { float lg[5]; uint8_t n, miss; float out; } tn_sm_t;      /* the last readings in octaves from 440 Hz, how many, missed in a row, what is shown (Hz) */
static float tn_log2(float hz) { return logf(hz) * 1.44269504f; }
/* hz: this reading (0: none). Returns the frequency to show, or 0. A median of the last five readings, averaged with what was shown (in the log
 * domain, so a cent is a cent everywhere); a jump of over 60 cents starts again at once; a gap of three missed readings (0.3 s) clears it. */
static float tn_smooth(tn_sm_t *s, float hz)
{
    if (hz <= 0.0f) {
        if (++s->miss >= 3u) { s->n = 0; s->out = 0.0f; }
        return s->out;
    }
    s->miss = 0;
    const float l = tn_log2(hz);
    if (s->n && s->out > 0.0f) {
        const float d = (l - tn_log2(s->out)) * 1200.0f;
        if (d > 60.0f || d < -60.0f) { s->n = 0; s->out = 0.0f; }          /* another note */
    }
    if (s->n < 5u) s->lg[s->n++] = l;
    else { for (uint32_t i = 0; i < 4u; i++) s->lg[i] = s->lg[i + 1u]; s->lg[4] = l; }
    float t[5]; for (uint32_t i = 0; i < s->n; i++) t[i] = s->lg[i];
    for (uint32_t i = 1; i < s->n; i++) { const float v = t[i]; uint32_t j = i; while (j && t[j - 1u] > v) { t[j] = t[j - 1u]; j--; } t[j] = v; }
    const float med = t[s->n / 2u];
    const float prev = s->out > 0.0f ? tn_log2(s->out) : med;
    s->out = expf((prev + 0.5f * (med - prev)) * 0.69314718f);
    return s->out;
}
/* the note (0 = C .. 11 = B) and the cents for a frequency, against a reference A (Hz); note -1 for none */
static void tn_note(float hz, float ref, int *note, float *cents)
{
    *note = -1; *cents = 0.0f;
    if (hz <= 20.0f) return;
    const float st = 12.0f * tn_log2(hz / ref);
    const int r = (int)(st + (st >= 0 ? 0.5f : -0.5f));
    *cents = (st - (float)r) * 100.0f;
    *note = ((r % 12) + 12 + 9) % 12;
}
