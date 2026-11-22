/* Host experiment: the 24-band POLY of isod89/pocket-synth (GPL-3.0, firmware/apps/synth/sy_engine.c) against the
 * engine's POLY OCTAVE HQ, scored with the harness of octave_banks.c. Not firmware.
 *   cc -O2 -Iengine -Ifirmware tools/research/pocket_poly_bench.c firmware/px_math.c -lm -o build/pocket_poly_bench
 *   build/pocket_poly_bench [IN.wav ...]        DOWN=1 for the octave down (flip-flop), default the octave up (rectified)
 * POCKET24: the guitar at 22.05 kHz through 24 band-passes (Q 7, a sixth of an octave from 80 Hz); each band normalised by
 * its own level (an "AGC") then the octave voice: DOWN a flip-flop on the band's zero crossings (a square at f/2), UP the
 * band rectified; scaled by the band's level. Reimplemented from its source, the filter and constants as written there. */
#define ERB_HOST_EXPERIMENTS 1
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static px_engine E;
static float ratio = 2.0f;
enum { NB = 24 };
typedef struct { float b0, b2, a1, a2, z1, z2, en, inv, prev, ff; } pbq;
static void pocket24(const float *x, float *y, int n, int down)
{
    pbq b[NB]; memset(b, 0, sizeof b);
    for (int k = 0; k < NB; k++) {
        const float fc = 80.0f * powf(2.0f, k / 6.0f), w = 6.28318531f * fc / 22050.0f, al = sinf(w) / 14.0f, a0 = 1 + al;
        b[k].b0 = al / a0; b[k].b2 = -al / a0; b[k].a1 = -2 * cosf(w) / a0; b[k].a2 = (1 - al) / a0;
    }
    float *p = calloc(n / 2 + 2, sizeof(float));
    for (int k = 0; k < NB; k++) {
        pbq *f = &b[k]; float en = 0, inv = 0, prev = 0, ff = 0, z1 = 0, z2 = 0;
        for (int i = 0; i < n / 2; i++) {
            const float xx = 0.5f * (x[2 * i] + x[2 * i + 1]), yy = f->b0 * xx + z1;
            z1 = -f->a1 * yy + z2; z2 = f->b2 * xx - f->a2 * yy;
            const float a = yy < 0 ? -yy : yy;
            en += (a - en) * (a > en ? 0.04f : 0.004f);
            if (!(i & 7)) inv = en > 1e-5f ? 1.0f / (en * 1.6f) : 0.0f;
            if (yy >= 0 && prev < 0) ff = ff > 0 ? -1.0f : 1.0f;
            prev = yy;
            if (inv == 0) continue;
            p[i] += (down ? ff : (2.0f * a * inv - 0.9f)) * en;
        }
    }
    float last = 0;
    for (int j = 0; j < n; j++) { const float p1 = p[j >> 1]; y[j] = 8.0f * ((j & 1) ? p1 : 0.5f * ((j ? p[(j >> 1) - 1] : last) + p1)); }
    free(p);
}
static void render(int which, const float *x, float *y, int n)
{
    if (which == 1) { pocket24(x, y, n, ratio == 0.5f); return; }
    px_erb_octave state = {0};
    for (int t = 0; t < n; t++) y[t] = erb_octaves(&E, &state, x[t], ratio == 2.0f, ratio == 0.5f, ERB_FULL);
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + 1e-9 * t.tv_nsec; }

/* ---- test signals and scores ---- */
static void tones(float *x, int n, const float *f0, int nf, float vib, int start)
{
    memset(x, 0, sizeof(float) * n);
    for (int s = 0; s < nf; s++) for (int h = 1; h <= 10; h++) { double ph = s + h;
        for (int i = start; i < n; i++) {
            const double f = f0[s] * h * (1.0 + 0.0002 * h * h) * pow(2.0, vib / 1200.0 * sin(2 * M_PI * 5.5 * i / 44100.0));
            ph += 2 * M_PI * f / 44100.0; x[i] += (float)(0.08 / h * exp(-(i - start) / (3.0 * 44100.0)) * sin(ph)); } }
}
static float target(const float *y, int n, const float *f0, int nf)
{
    const int a = n / 4, L = 16384; double tot = 0, hit = 0;
    for (int b = 1; b < L / 2 && b * 44100.0 / L < 6000; b++) {
        const double fb = b * 44100.0 / L; int want = 0;
        for (int s = 0; s < nf; s++) for (int h = 1; h <= 10; h++) { const double f = ratio * f0[s] * h * (1.0 + 0.0002 * h * h); if (fabs(fb - f) <= 2.0 * 44100.0 / L) want = 1; }
        double re = 0, im = 0; const double w = 2 * M_PI * b / L;
        for (int i = 0; i < L; i += 1) { const double hw = 0.5 - 0.5 * cos(2 * M_PI * i / L); re += y[a + i] * hw * cos(w * i); im -= y[a + i] * hw * sin(w * i); }
        const double p = re * re + im * im; tot += p; if (want) hit += p;
    }
    return (float)(100.0 * hit / (tot + 1e-30));
}
static float *levels(const float *y, int n, int *m)
{
    const int W = 441; *m = n / W; float *l = malloc(sizeof(float) * *m);
    for (int j = 0; j < *m; j++) { double s = 0; for (int i = 0; i < W; i++) s += (double)y[j * W + i] * y[j * W + i]; l[j] = (float)(10 * log10(s / W + 1e-20)); }
    return l;
}
static float wobble(const float *x, const float *y, int n)
{
    int m; float *lx = levels(x, n, &m), *ly = levels(y, n, &m), top = -200;
    for (int j = 0; j < m; j++) if (lx[j] > top) top = lx[j];
    double acc = 0; int cnt = 0;
    for (int j = 9; j < m - 9; j++) {
        if (lx[j] < top - 30 || ly[j] < -90) continue;
        double avg = 0; for (int q = -7; q <= 7; q++) avg += ly[j + q] - lx[j + q - 1]; avg /= 15;
        const double d = ly[j] - lx[j - 1] - avg; acc += d * d; cnt++;
    }
    free(lx); free(ly); return cnt ? (float)sqrt(acc / cnt) : 0.0f;
}
/* v64: the pick click: the wet output's energy above 2.5 kHz in the 20 ms after each pick of the dry input, against
 * its average above 2.5 kHz (dB; lower is cleaner) */
static float click(const float *x, float *y, int n)
{
    static float hp[2]; double on = 0, all = 0; int non = 0, nall = 0, since = 1 << 30; float fast = 0, slow = 0, z = 0, z2 = 0;
    (void)hp;
    for (int i = 0; i < n; i++) {
        const float a = fabsf(x[i]); fast += (a - fast) * 0.0224f; slow += (a - slow) * 0.00057f;
        if (fast > 2.0f * slow && fast > 1e-3f && since > 4410) since = 0;
        const float h = y[i] - 1.68f * z + 0.71f * z2; z2 = z; z = y[i];   /* (a crude high-pass, ~2.5 kHz) */
        const double e = (double)h * h; all += e; nall++;
        if (since < 882) { on += e; non++; }
        since++;
    }
    return non && all > 0 ? (float)(10 * log10((on / non) / (all / nall) + 1e-20)) : 0.0f;
}
static float arrival(int which, float f)
{
    const int n = 44100, s0 = 4410; float *x = malloc(4 * n), *y = malloc(4 * n);
    memset(x, 0, 4 * n); for (int i = s0; i < n; i++) x[i] = 0.3f * sinf(2 * 3.14159265f * f * (i - s0) / 44100.0f);
    render(which, x, y, n);
    double ref = 0; for (int i = 22050; i < 22050 + 4410; i++) ref += (double)y[i] * y[i]; ref /= 4410;
    const int W = (int)(44100.0f / (ratio * f)) + 1;            /* one octave cycle */
    float t = -1;
    for (int i = s0; i + W < n; i += 16) { double e = 0; for (int q = 0; q < W; q++) e += (double)y[i + q] * y[i + q];
        if (sqrt(e / W) > 0.5 * sqrt(ref)) { t = (i + W / 2 - s0) * 1000.0f / 44100.0f; break; } }
    free(x); free(y); return t;
}
static float *wav_read(const char *fn, int *n)
{
    FILE *f = fopen(fn, "rb"); if (!f) return 0;
    unsigned char h[12]; if (fread(h, 1, 12, f) != 12) { fclose(f); return 0; }
    int ch = 1, bits = 16, fmt = 1; unsigned dsz = 0; int ok = 0;
    for (;;) { unsigned char c[8]; if (fread(c, 1, 8, f) != 8) break; unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { unsigned char b[16]; fread(b, 1, 16, f); fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; bits = b[14] | b[15] << 8; fseek(f, sz - 16, SEEK_CUR); }
        else if (!memcmp(c, "data", 4)) { dsz = sz; ok = 1; break; }
        else fseek(f, (sz + 1) & ~1u, SEEK_CUR); }
    if (!ok) { fclose(f); return 0; }
    int bps = bits / 8, m = dsz / (bps * ch); if (m > 44100 * 30) m = 44100 * 30;   /* (the first 30 s) */
    float *x = malloc(sizeof(float) * m);
    for (int i = 0; i < m; i++) { double a = 0; for (int c = 0; c < ch; c++) {
        if (fmt == 3) { float v; fread(&v, 4, 1, f); a += v; } else if (bits == 16) { int16_t v; fread(&v, 2, 1, f); a += v / 32768.0; }
        else if (bits == 24) { unsigned char b[3]; fread(b, 1, 3, f); int v = b[0] | b[1] << 8 | b[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF; a += v / 8388608.0; }
        else { int32_t v; fread(&v, 4, 1, f); a += v / 2147483648.0; } }
        x[i] = (float)(a / ch); }
    fclose(f); *n = m; return x;
}

int main(int argc, char **argv)
{
    px_engine_init(&E, 44100.0f); erb_prepare(&E);
    if (getenv("DOWN")) ratio = 0.5f;
    static const char *NM[2] = {"ENGINE NOW", "POCKET24"};
    printf("%-34s %14s %14s\n", ratio == 2.0f ? "OCTAVE UP" : "OCTAVE DOWN", NM[0], NM[1]);
    const int n = 3 * 44100; float *x = malloc(4 * n), *y = malloc(4 * n);
    double tgt[2] = {0}, lat[2] = {0}, latmax[2] = {0}, hi[2] = {0}, ch[2] = {0}; int nt = 0, nl = 0;
    for (int m = 40; m <= 76; m += 3) { const float f = 440.0f * powf(2.0f, (m - 69) / 12.0f); tones(x, n, &f, 1, 0, 0);
        for (int q = 0; q < 2; q++) { render(q, x, y, n); tgt[q] += target(y, n, &f, 1); } nt++; }
    for (int m = 38; m <= 52; m++) { const float f = 440.0f * powf(2.0f, (m - 69) / 12.0f);
        for (int q = 0; q < 2; q++) { const float a = arrival(q, f); lat[q] += a; if (a > latmax[q]) latmax[q] = a; } nl++; }
    for (int q = 0; q < 2; q++) hi[q] = arrival(q, 1500.0f);
    static const float C[3][3] = {{82.41f, 123.47f, 164.81f}, {110.0f, 164.81f, 220.0f}, {196.0f, 246.94f, 293.66f}};
    for (int c = 0; c < 3; c++) { tones(x, n, C[c], 3, 0, 0); for (int q = 0; q < 2; q++) { render(q, x, y, n); ch[q] += target(y, n, C[c], 3) / 3; } }
    printf("%-34s %13.1f%% %13.1f%%\n", "single notes E2..E5, on target", tgt[0] / nt, tgt[1] / nt);
    printf("%-34s %13.1f%% %13.1f%%\n", "chords E5 A5 G, on target", ch[0], ch[1]);
    printf("%-34s %6.1f /%6.1f %6.1f /%6.1f\n", "arrival D2..E3 mean / worst (ms)", lat[0] / nl, latmax[0], lat[1] / nl, latmax[1]);
    printf("%-34s %14.1f %14.1f\n", "arrival 1.5 kHz (ms)", hi[0], hi[1]);
    tones(x, n, C[0], 3, 0, 0); double t[2];
    for (int q = 0; q < 2; q++) { render(q, x, y, n); double t0 = now(); for (int r = 0; r < 20; r++) render(q, x, y, n); t[q] = (now() - t0) / 20 / n * 1e9; }
    printf("%-34s %11.0f ns %11.0f ns   (host time per sample; ratio %.2f)\n", "CPU on this host", t[0], t[1], t[1] / t[0]);
    for (int a = 1; a < argc; a++) {
        int m; float *r = wav_read(argv[a], &m); if (!r) continue;
        float *o = malloc(4 * m); const char *b = strrchr(argv[a], '/');
        printf("%-34.34s", b ? b + 1 : argv[a]);
        for (int q = 0; q < 2; q++) { render(q, r, o, m); printf(" %14.2f", wobble(r, o, m)); }
        printf("   (added wobble dB)\n"); free(r); free(o);
    }
    return 0;
}
