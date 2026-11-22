/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — POLY SHIFTER (v75): a polyphonic pitch shifter on a phase vocoder, for chords and any interval.
 *
 * The method is the one published by J. Laroche and M. Dolson, "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects"
 * (WASPAA 1999), written here from that paper and the standard phase vocoder (Flanagan & Golden 1966, Portnoff 1976): the short-time spectrum is analysed,
 * its sinusoidal PEAKS are found, each peak and the bins around it (its region of influence) are moved together to the shifted frequency and rotated by the
 * phase the shifted sinusoid needs to continue from the last frame, and the frames are overlap-added. Keeping each region rigid keeps the window's shape
 * around every partial, so a chord's notes each stay one clean voice (the delay-line shifters of WHAMMY / HARMONIZER give the chord a beating
 * roughness). Nothing is resampled and nothing is tracked in time: one note or six.
 *
 *   frame 1024 samples (23 ms at 44.1 kHz, bins of 43 Hz), hop 256 (4x overlap), periodic Hann analysis and synthesis windows
 *   latency PV_LAT = 768 samples (17.4 ms): the wet voice and the dry signal are both delayed by it, so they stay together
 *   one real FFT of 1024 per frame each way, as a complex FFT of 512 (the usual packing)
 *   a TRANSIENT (the frame's energy jumps) drops the phase memory: the new frame starts from its own phases, so a pick keeps its attack
 * Everything is float, no table in flash: the twiddles and the window are made once (pv_init). RAM: about 25 KB (px_pv), part of the engine.
 * Host test: tests/engine/pv_test.c. */
#ifndef PX_PV_H
#define PX_PV_H

#define PV_N   1024
#define PV_M   (PV_N / 2)
#define PV_H   (PV_N / 4)
#define PV_NB  (PV_M + 1)                              /* bins 0 .. 512 */
#define PV_LAT (PV_N - PV_H)                           /* input to output, in samples */
#define PV_MAXPK 128
#define PV_TWO_PI 6.28318530718f

typedef struct {
    float in[PV_N];                                    /* the last PV_N input samples, by time modulo PV_N (also the delayed dry) */
    float ola[PV_N];                                   /* the overlap-add output, by time modulo PV_N */
    float X[2 * PV_NB], Y[2 * PV_NB];                  /* spectra (re, im interleaved): analysis, synthesis; Y is also the forward FFT's work array */
    float tab[PV_N];                                   /* cos, sin of 2 pi k / PV_N, k < PV_M */
    float win[PV_M + 1];                               /* the Hann window's first half (+ its middle sample) */
    int pos, cnt, inited, npk;
    int pk_bin[PV_MAXPK];                              /* the last frame's peaks: bin, the spectrum there, the synthesis phase */
    float pk_re[PV_MAXPK], pk_im[PV_MAXPK], pk_th[PV_MAXPK];
    float r, rt, mix, thr, energy;                     /* the frequency ratio now / wanted, the wet share, the transient ratio, the last frame's energy */
    float pw[PV_NB];                                   /* the frame's scratch (not on the stack: the audio interrupt's is small) */
    int pk[PV_MAXPK], nb[PV_MAXPK];
    float nre[PV_MAXPK], nim[PV_MAXPK], nth[PV_MAXPK];
} px_pv;

#endif /* PX_PV_H: the state; the functions follow, for px_dsp.c (it includes this again after <string.h> and <math.h>, with PX_PV_IMPL) */
#if defined(PX_PV_IMPL) && !defined(PX_PV_FUNCS)
#define PX_PV_FUNCS

static inline float pv_atan2(float y, float x)         /* |error| < 1e-5 rad */
{
    const float ax = x < 0.0f ? -x : x, ay = y < 0.0f ? -y : y, mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx < 1e-30f) return 0.0f;
    const float a = mn / mx, s = a * a;
    float r = ((((-0.0117212f * s + 0.05265332f) * s - 0.11643287f) * s + 0.19354346f) * s - 0.33262347f) * s + 0.99997726f;
    r *= a;
    if (ay > ax) r = 1.5707963f - r;
    if (x < 0.0f) r = 3.1415927f - r;
    return y < 0.0f ? -r : r;
}
static inline float pv_wrap(float a)                   /* to [-pi, pi] */
{
    while (a > 3.1415927f) a -= PV_TWO_PI;
    while (a < -3.1415927f) a += PV_TWO_PI;
    return a;
}
static inline float pv_hann(const px_pv *v, int i) { return v->win[i <= PV_M ? i : PV_N - i]; }

static inline void pv_reset(px_pv *v)
{
    memset(v->in, 0, sizeof v->in); memset(v->ola, 0, sizeof v->ola);
    v->pos = 0; v->cnt = 0; v->npk = 0; v->energy = 0.0f;
    v->r = v->rt;
}
static inline void pv_init(px_pv *v)
{
    for (int k = 0; k < PV_M; k++) { v->tab[2 * k] = cosf(PV_TWO_PI * (float)k / (float)PV_N); v->tab[2 * k + 1] = sinf(PV_TWO_PI * (float)k / (float)PV_N); }
    for (int i = 0; i <= PV_M; i++) v->win[i] = 0.5f - 0.5f * (i < PV_M ? v->tab[2 * i] : -1.0f);
    v->rt = 1.0f; v->mix = 1.0f; v->thr = 4.0f; v->inited = 1;
    pv_reset(v);
}
/* the 512-point complex FFT in place (interleaved re, im); inverse: conjugated twiddles, not scaled */
static inline void pv_cfft(float *z, const float *tab, int inverse)
{
    for (int i = 1, j = 0; i < PV_M; i++) {
        int bit = PV_M >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { const float tr = z[2 * i], ti = z[2 * i + 1]; z[2 * i] = z[2 * j]; z[2 * i + 1] = z[2 * j + 1]; z[2 * j] = tr; z[2 * j + 1] = ti; }
    }
    const float sg = inverse ? 1.0f : -1.0f;
    for (int len = 2; len <= PV_M; len <<= 1) {
        const int half = len >> 1, step = PV_M / len;
        for (int k = 0; k < half; k++) {
            const float wr = tab[4 * k * step], wi = sg * tab[4 * k * step + 1];
            for (int i = k; i < PV_M; i += len) {
                float *a = z + 2 * i, *b = z + 2 * (i + half);
                const float tr = b[0] * wr - b[1] * wi, ti = b[0] * wi + b[1] * wr;
                b[0] = a[0] - tr; b[1] = a[1] - ti; a[0] += tr; a[1] += ti;
            }
        }
    }
}
/* one frame: analysis of the last PV_N samples, the shift, synthesis, overlap-add */
static inline void pv_frame(px_pv *v)
{
    const int mask = PV_N - 1, start = (v->pos + 1) & mask;
    float *X = v->X, *Y = v->Y;
    /* analysis: windowed frame -> real FFT (as a 512-point complex one) -> X[0..512] */
    for (int i = 0; i < PV_N; i++) Y[i] = v->in[(start + i) & mask] * pv_hann(v, i);
    pv_cfft(Y, v->tab, 0);
    X[0] = Y[0] + Y[1]; X[1] = 0.0f; X[2 * PV_M] = Y[0] - Y[1]; X[2 * PV_M + 1] = 0.0f;
    for (int k = 1; k < PV_M; k++) {
        const float a = Y[2 * k], b = Y[2 * k + 1], c = Y[2 * (PV_M - k)], d = Y[2 * (PV_M - k) + 1];
        const float er = 0.5f * (a + c), ei = 0.5f * (b - d), or_ = 0.5f * (b + d), oi = -0.5f * (a - c);
        const float wc = v->tab[2 * k], ws = v->tab[2 * k + 1];
        X[2 * k] = er + wc * or_ + ws * oi; X[2 * k + 1] = ei + wc * oi - ws * or_;
    }
    for (int k = 1; k < PV_NB; k += 2) { X[2 * k] = -X[2 * k]; X[2 * k + 1] = -X[2 * k + 1]; }   /* the window's centre as the time origin: the phases of a partial's bins agree */
    /* the peaks: a bin above its two neighbours each side, and not far under the loudest */
    float *const pw = v->pw; float mx = 0.0f, en = 0.0f;
    for (int k = 0; k < PV_NB; k++) { pw[k] = X[2 * k] * X[2 * k] + X[2 * k + 1] * X[2 * k + 1]; en += pw[k]; if (pw[k] > mx) mx = pw[k]; }
    const int reset = v->energy > 1e-12f && en > v->thr * v->energy;           /* a transient: the phase memory goes */
    v->energy = en;
    const float floor_ = mx * 1e-4f + 1e-12f;
    int *const pk = v->pk; int n = 0;
    for (int k = 2; k < PV_M - 1 && n < PV_MAXPK; k++)
        if (pw[k] > floor_ && pw[k] > pw[k - 1] && pw[k] >= pw[k + 1] && pw[k] > pw[k - 2] && pw[k] >= pw[k + 2]) pk[n++] = k;
    memset(Y, 0, sizeof(float) * 2 * PV_NB);
    int *const nb = v->nb; float *const nre = v->nre, *const nim = v->nim, *const nth = v->nth; int npk = 0;
    int q = 0;                                                                 /* the previous peaks, walked in step */
    for (int j = 0; j < n; j++) {
        const int p = pk[j];
        while (q + 1 < v->npk && v->pk_bin[q + 1] <= p) q++;
        int m = -1;                                                            /* the nearest previous peak, within 2 bins */
        if (!reset && v->npk > 0) {
            int best = q, d = v->pk_bin[q] > p ? v->pk_bin[q] - p : p - v->pk_bin[q];
            if (q + 1 < v->npk) { const int d2 = v->pk_bin[q + 1] - p; if (d2 < d) { best = q + 1; d = d2; } }
            if (d <= 2) m = best;
        }
        const float phi = pv_atan2(X[2 * p + 1], X[2 * p]);
        float om, th_prev;
        if (m >= 0) {                                                          /* the partial's own frequency from the phase advance at its last bin */
            const int b = v->pk_bin[m];
            const float cr = X[2 * b] * v->pk_re[m] + X[2 * b + 1] * v->pk_im[m], ci = X[2 * b + 1] * v->pk_re[m] - X[2 * b] * v->pk_im[m];
            const float omb = PV_TWO_PI * (float)b / (float)PV_N, dev = pv_wrap(pv_atan2(ci, cr) - omb * (float)PV_H);
            om = omb + dev / (float)PV_H; th_prev = v->pk_th[m];
        } else { om = PV_TWO_PI * (float)p / (float)PV_N; th_prev = phi - om * (float)PV_H; }
        const float om2 = om * v->r;
        int p2 = (int)(om2 * (float)PV_N / PV_TWO_PI + 0.5f);
        if (p2 < 1) p2 = 1;
        if (p2 > PV_M - 1) p2 = PV_M - 1;
        const float th = th_prev + om2 * (float)PV_H, dth = th - phi, c = cosf(dth), s = sinf(dth);
        const int lo = j ? (pk[j - 1] + p) / 2 + 1 : 0, hi = j + 1 < n ? (p + pk[j + 1]) / 2 : PV_M;
        for (int k = lo; k <= hi; k++) {
            const int k2 = k + (p2 - p);
            if (k2 < 0 || k2 > PV_M) continue;
            Y[2 * k2] = X[2 * k] * c - X[2 * k + 1] * s; Y[2 * k2 + 1] = X[2 * k] * s + X[2 * k + 1] * c;
        }
        nb[npk] = p; nre[npk] = X[2 * p]; nim[npk] = X[2 * p + 1]; nth[npk] = th; npk++;
    }
    /* what the next frame needs */
    v->npk = npk;
    for (int j = 0; j < npk; j++) { v->pk_bin[j] = nb[j]; v->pk_re[j] = nre[j]; v->pk_im[j] = nim[j]; v->pk_th[j] = nth[j]; }
    /* synthesis: back to the frame's time origin, spectrum -> complex 512 -> inverse FFT -> windowed overlap-add */
    for (int k = 1; k < PV_NB; k += 2) { Y[2 * k] = -Y[2 * k]; Y[2 * k + 1] = -Y[2 * k + 1]; }
    for (int k = 0; k < PV_M; k++) {
        const float ar = Y[2 * k], ai = Y[2 * k + 1], cr = Y[2 * (PV_M - k)], ci = -Y[2 * (PV_M - k) + 1];     /* X[k], conj(X[M - k]) */
        const float er = 0.5f * (ar + cr), ei = 0.5f * (ai + ci), dr = ar - cr, di = ai - ci;
        const float wc = k ? v->tab[2 * k] : 1.0f, ws = k ? v->tab[2 * k + 1] : 0.0f;                        /* conj(W^k) = (wc, +ws) */
        const float fr = 0.5f * (dr * wc - di * ws), fi = 0.5f * (dr * ws + di * wc);                         /* Fo[k] */
        X[2 * k] = er - fi; X[2 * k + 1] = ei + fr;                                                          /* Z[k] = Fe + j Fo */
    }
    pv_cfft(X, v->tab, 1);
    const float scale = 1.0f / ((float)PV_M * 1.5f);                           /* the FFT's size, and the windows' overlap (sum of w^2 = 1.5) */
    for (int i = 0; i < PV_N; i++) v->ola[(start + i) & mask] += X[i] * scale * pv_hann(v, i);
}
/* one sample in; the wet voice and the dry signal out, both PV_LAT behind the input (the caller mixes) */
static inline void pv_tick(px_pv *v, float x, float *dry, float *wet)
{
    const int mask = PV_N - 1;
    v->in[v->pos] = x;
    if (++v->cnt >= PV_H) { v->cnt = 0; v->r += (v->rt - v->r) * 0.5f; pv_frame(v); }
    const int o = (v->pos - PV_LAT) & mask;
    *dry = v->in[o]; *wet = v->ola[o]; v->ola[o] = 0.0f;
    v->pos = (v->pos + 1) & mask;
}
#endif
