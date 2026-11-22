/* SPDX-License-Identifier: GPL-3.0-only
 * Loudness, peak and host cost of every live type of every block, on the bundled guitar loops (assets/loops, at the pedal's level x0.15).
 *   cc -O2 -Iengine tools/research/level_audit.c engine/px_dsp.c firmware/px_math.c -lm -o build/level_audit && build/level_audit [PARAM=50] > audit.csv
 * One block on at a time (the gate off, preset LEVEL 0 dB), all five settings at PARAM. Loudness = ITU-R BS.1770 (K-weighted, gated), as
 * tools/calib_levels.py. Columns: block, type id, name, mean dB over the five loops against the same loop with the block off, worst loop, best loop,
 * peak dBFS (worst loop), host ms to render one second of audio (a relative cost only: not the pedal's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "px_dsp.h"
#define SR 44100.0
static float *load(const char *path, int *n)
{
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 44) { fclose(f); fprintf(stderr, "%s: too short for a WAV\n", path); return 0; }
    unsigned char *b = malloc((size_t)sz); if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return 0; } fclose(f);
    if (memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); fprintf(stderr, "%s: not a RIFF WAVE file\n", path); return 0; }
    int ch = 1, bits = 16, fmt = 1; long p = 12, data = -1, dlen = 0;
    while (p + 8 <= sz) {
        long l = b[p + 4] | b[p + 5] << 8 | b[p + 6] << 16 | (long)b[p + 7] << 24;
        if (!memcmp(b + p, "fmt ", 4)) { fmt = b[p + 8] | b[p + 9] << 8; ch = b[p + 10] | b[p + 11] << 8; bits = b[p + 22] | b[p + 23] << 8; }
        if (!memcmp(b + p, "data", 4)) { data = p + 8; dlen = l; break; }
        p += 8 + l + (l & 1);
    }
    if (data < 0 || ch < 1 || (fmt != 1 && fmt != 3) || (bits != 16 && bits != 24 && bits != 32) || (fmt == 3 && bits != 32)) { free(b); fprintf(stderr, "%s: unsupported WAV (PCM 16/24/32 or float 32, data chunk required)\n", path); return 0; }
    if (dlen > sz - data) dlen = sz - data;                  /* a data chunk that claims more than the file holds */
    int fr = (int)(dlen / (ch * bits / 8)); float *o = malloc(sizeof(float) * (size_t)fr);
    for (int i = 0; i < fr; i++) {
        const unsigned char *s = b + data + (long)i * ch * bits / 8;
        if (fmt == 3 && bits == 32) { float v; memcpy(&v, s, 4); o[i] = v; }
        else if (bits == 16) o[i] = (short)(s[0] | s[1] << 8) / 32768.0f;
        else if (bits == 24) o[i] = (int)((s[0] << 8 | s[1] << 16 | (unsigned)s[2] << 24)) / 2147483648.0f;
        else o[i] = (int)(s[0] | s[1] << 8 | s[2] << 16 | (unsigned)s[3] << 24) / 2147483648.0f;
    }
    free(b); *n = fr; return o;
}
static void biquad(const double *b, const double *a, const float *x, double *y, int n)
{
    double z1 = 0, z2 = 0;
    for (int i = 0; i < n; i++) { double v = b[0] * x[i] + z1; z1 = b[1] * x[i] - a[1] * v + z2; z2 = b[2] * x[i] - a[2] * v; y[i] = v; }
}
static double lufs(const float *x, int n)
{
    double *y = malloc(sizeof(double) * (size_t)n), *w = malloc(sizeof(double) * (size_t)n);
    float *xf = malloc(sizeof(float) * (size_t)n);
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196, K = tan(M_PI * f0 / SR), Vh = pow(10, G / 20), Vb = pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
    double b1[3] = {(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0}, a1[3] = {1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0};
    biquad(b1, a1, x, y, n);
    f0 = 38.13547087602444; Q = 0.5003270373238773; K = tan(M_PI * f0 / SR); a0 = 1 + K / Q + K * K;
    double b2[3] = {1, -2, 1}, a2[3] = {1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0};
    for (int i = 0; i < n; i++) xf[i] = (float)y[i];
    biquad(b2, a2, xf, w, n);
    const int blk = (int)(0.4 * SR), hop = (int)(0.1 * SR), nb = (n - blk) / hop;
    double *z = malloc(sizeof(double) * (size_t)(nb > 0 ? nb : 1)); int m = 0;
    for (int i = 0; i < nb; i++) { double s = 0; for (int k = 0; k < blk; k++) s += w[i * hop + k] * w[i * hop + k]; z[i] = s / blk; }
    double acc = 0; int c = 0;
    for (int i = 0; i < nb; i++) if (-0.691 + 10 * log10(z[i] + 1e-20) > -70) { z[m++] = z[i]; acc += z[i]; c++; }
    double rel = c ? -0.691 + 10 * log10(acc / c) - 10 : -99, a = 0; int d = 0;
    for (int i = 0; i < m; i++) if (-0.691 + 10 * log10(z[i]) > rel) { a += z[i]; d++; }
    free(y); free(w); free(xf); free(z);
    return d ? -0.691 + 10 * log10(a / d) : -99;
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static px_engine eng;
static void render(const px_preset_t *p, const float *in, float *out, int n, double *sec)
{
    px_engine_init(&eng, (float)SR); px_engine_set(&eng, p);
    const double t0 = now();
    for (int at = 0; at < n; at += 64) { const int m = n - at < 64 ? n - at : 64; px_engine_process(&eng, in + at, out + at, m); }
    *sec += now() - t0;
}
/* the settings a type is measured at: every knob at PARAM (noon), except the ones that are a level by design, which are at their unity position (the boosts' LEVEL / GAIN / LOUDER: a boost that
 * is louder than the guitar at noon is meant to be) */
static int setting(int b, int t, int k, int param)
{
    if (b == PX_B_BST && t == PX_BST_EPBOOST && k == 0) return 0;          /* GAIN 0 = unity */
    if (b == PX_B_BST && t == PX_BST_VOXWAH && k == 2) return 0;           /* LEVEL 0 = the guitar's loudness */
    if (b == PX_B_BST && t == PX_BST_RANGEMASTER && k == 0) return 50;     /* VOLUME 50 = unity */
    if (b == PX_B_BST && t == PX_BST_ENHANCE && k == 1) return 0;          /* LOUDER 0 */
    if (b == PX_B_SYN && (t == PX_SYN_ARCADE || t == PX_SYN_ARCADE_SPLIT) && k == 4) return 100;   /* MIX 100: the synth alone (the guitar's share is not trimmed) */
    return param;
}
int main(int argc, char **argv)
{
    static const char *LOOPS[5] = {"arpeggio", "chords", "chug", "lead", "riff"};
    const int param = argc > 1 ? atoi(argv[1]) : 50;
    float *in[5], *off[5], *out; int len[5];
    for (int l = 0; l < 5; l++) {
        char path[128]; snprintf(path, sizeof path, "assets/loops/%s.wav", LOOPS[l]);
        in[l] = load(path, &len[l]); if (!in[l]) { fprintf(stderr, "no %s\n", path); return 1; }
        for (int i = 0; i < len[l]; i++) in[l][i] *= 0.15f;
        off[l] = malloc(sizeof(float) * (size_t)len[l]);
    }
    { int mx = 0; for (int l = 0; l < 5; l++) if (len[l] > mx) mx = len[l]; out = malloc(sizeof(float) * (size_t)mx); if (!out) { fprintf(stderr, "out of memory\n"); return 1; } }   /* one render buffer, as long as the longest loop */
    double dummy = 0, lo[5]; px_preset_t p;
    memset(&p, 0, sizeof p); px_order_default(p.order); p.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    for (int l = 0; l < 5; l++) { render(&p, in[l], off[l], len[l], &dummy); lo[l] = lufs(off[l], len[l]); }
    printf("block,type,name,mean_db,worst_db,best_db,peak_dbfs,ms_per_s\n");
    for (int b = 1; b < PX_NBLK; b++) {
        for (int t = 0; t < px_ntypes(b); t++) {
            if (px_type_retired(b, t) || !px_type_name(b, t)[0]) continue;
            memset(&p, 0, sizeof p); px_order_default(p.order); p.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
            p.on[b] = 1; p.type[b] = (uint8_t)t; for (int k = 0; k < PX_NPAR; k++) p.p[b][k] = (uint8_t)setting(b, t, k, param);
            double sec = 0, tot = 0, mean = 0, worst = 99, best = -99, peak = -99;
            for (int l = 0; l < 5; l++) {
                render(&p, in[l], out, len[l], &sec); tot += (double)len[l] / SR;
                const double d = lufs(out, len[l]) - lo[l]; mean += d / 5; if (d < worst) worst = d; if (d > best) best = d;
                float pk = 0; for (int i = 0; i < len[l]; i++) if (fabsf(out[i]) > pk) pk = fabsf(out[i]);
                if (20 * log10(pk + 1e-9) > peak) peak = 20 * log10(pk + 1e-9);
            }
            printf("%s,%d,%s,%.2f,%.2f,%.2f,%.1f,%.2f\n", px_block_label(b), t, px_type_name(b, t), mean, worst, best, peak, sec / tot * 1000.0);
            fflush(stdout);
        }
    }
    return 0;
}
