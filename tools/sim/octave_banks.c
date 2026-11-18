/* Host experiment (v49): which filter bank gives a clean octave up? Not firmware.
 *   cc -O2 -Iengine -Ifirmware tools/sim/octave_banks.c firmware/px_math.c -lm -o build/octave_banks
 *   build/octave_banks [IN.wav ...]
 * Every bank does the same per band, out = Re(z^2 / |z|) (Thuillier's phase doubling, as POLY OCTAVE HQ and the MIT
 * Terrarium Poly Octave both do); only the bands differ:
 *   TODAY      POLY OCTAVE HQ itself (erb_octaves: 43 ERB-spaced bands 65 Hz .. 4 kHz, 1.5x ERB wide, half rate)
 *   TERRARIUM  80 bands, centre 480 * 2^(0.027 n) - 420 (60 Hz .. 2.1 kHz), width the harmonic mean of the spacings
 *              (schult/terrarium-poly-octave util/OctaveGenerator.h, MIT)
 *   TERR x1.5  the same centres, 1.5x wider (a faster low end)
 *   TERR 3k    TERRARIUM's spacing continued to 3.3 kHz (106 bands)
 * Scores: the share of octave-voice energy at exactly twice each partial (single notes, chords; +-10 cents); the level
 * wobble the octave adds over the input's (dB); the time the octave of an 82 Hz pluck takes to reach half its level. */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int n; px_erb_band b[128]; } bank_t;
static float tcenter(int n) { return 480.0f * powf(2.0f, 0.027f * n) - 420.0f; }
static float LIN_STEP = 0, LAW_S0 = 0, LAW_C = 0, LAW_W = 0.8f;    /* LAW: spacing = S0 + C f (Hz), bands 0.8 x spacing wide */
static float LAWC[128];                                 /* > 0: evenly spaced bands this far apart (Hz), this wide */
static float center(int n)
{
    if (LAW_S0 > 0) { if (n < 0) return LAWC[0] - (LAW_S0 + LAW_C * LAWC[0]); return LAWC[n]; }
    return LIN_STEP > 0 ? 60.0f + LIN_STEP * n : tcenter(n);
}
static int law_bands(float s0, float c)  /* (width: LAW_W of the spacing) */
{
    LAW_S0 = s0; LAW_C = c; int n = 0; float f = 60.0f;
    while (f < 2100.0f && n < 127) { LAWC[n++] = f; f += s0 + c * f; }
    LAWC[n] = f; return n;
}
static void make_bank(bank_t *k, int nb, float widen)
{
    const double sr = 44100.0, pi = 3.14159265358979;
    k->n = nb;
    for (int n = 0; n < nb; n++) {
        const double f0 = center(n - 1), f1 = center(n), f2 = center(n + 1), a = f2 - f1, b = f1 - f0;
        const double bw = widen * 2.0 * a * b / (a + b), w0 = pi * bw / sr;
        const double a0 = 1 + sqrt(2.0) * sin(w0) / 2, g = (1 - cos(w0)) / (2 * a0), w1 = 2 * pi * f1 / sr;
        /* the same complex biquad form as erb_prepare's (Noga's transform of the cookbook low-pass) */
        const double c = cos(w1), s = sin(w1), c2 = cos(2 * w1), s2 = sin(2 * w1);
        const double A1 = -2 * cos(w0) / a0, A2 = (1 - sqrt(2.0) * sin(w0) / 2) / a0;
        k->b[n] = (px_erb_band){(float)g, {(float)(2 * g * c), (float)(2 * g * s)}, {(float)(g * c2), (float)(g * s2)},
                                {(float)(A1 * c), (float)(A1 * s)}, {(float)(A2 * c2), (float)(A2 * s2)}};
    }
}
static void make_v48(bank_t *k)
{
    const double sr = 44100.0, pi = 3.14159265358979, F = 1 + 0.00437 * 65, L = 1 + 0.00437 * 4000, st = pow(L / F, 1.0 / 42);
    double pos = F; k->n = 43;
    for (int i = 0; i < 43; i++, pos *= st) {
        const double c0 = (pos - 1) / 0.00437, sp = pos * (st - 1 / st) / (2 * 0.00437), om = pi * 1.5 * sp / sr;
        const double a0 = 1 / (1 + 0.70710678 * sin(om)), hs = sin(0.5 * om), b0 = a0 * hs * hs, ph = 2 * pi * c0 / sr;
        const double c = cos(ph), s = sin(ph), c2 = c * c - s * s, s2 = 2 * c * s, a1 = -2 * cos(om) * a0, a2 = 2 * a0 - 1;
        k->b[i] = (px_erb_band){(float)b0, {(float)(2 * b0 * c), (float)(2 * b0 * s)}, {(float)(b0 * c2), (float)(b0 * s2)},
                                {(float)(a1 * c), (float)(a1 * s)}, {(float)(a2 * c2), (float)(a2 * s2)}};
    }
}
static void run_bank(const bank_t *k, const float *x, float *y, int n)
{
    static px_erb_complex z1[128], z2[128];
    memset(z1, 0, sizeof z1); memset(z2, 0, sizeof z2);
    for (int t = 0; t < n; t++) {
        const float v = x[t]; float up = 0.0f;
        for (int i = 0; i < k->n; i++) {
            const px_erb_band *b = &k->b[i];
            const float re = b->b0 * v + z1[i].r, im = z1[i].i;
            z1[i].r = b->b1.r * v - (b->a1.r * re - b->a1.i * im) + z2[i].r;
            z1[i].i = b->b1.i * v - (b->a1.r * im + b->a1.i * re) + z2[i].i;
            z2[i].r = b->b2.r * v - (b->a2.r * re - b->a2.i * im);
            z2[i].i = b->b2.i * v - (b->a2.r * im + b->a2.i * re);
            const float p = re * re + im * im + 1e-24f;
            up += (re * re - im * im) * erb_inverse_root(p);
        }
        y[t] = up;
    }
}
static px_engine E;
static void run_today(const float *x, float *y, int n)
{
    static px_erb_octave st; memset(&st, 0, sizeof st);
    for (int t = 0; t < n; t++) y[t] = erb_octaves(&E, &st, x[t], 1.0f, 0.0f, ERB_FULL);
}
static bank_t BK[3];
static void render(int which, const float *x, float *y, int n) { if (which == 0) run_today(x, y, n); else run_bank(&BK[which - 1], x, y, n); }

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
        for (int s = 0; s < nf; s++) for (int h = 1; h <= 10; h++) { const double f = 2 * f0[s] * h * (1.0 + 0.0002 * h * h); if (fabs(1200 * log2(fb / f)) < 10) want = 1; }
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
static float arrival(int which, float f)
{
    const int n = 44100, s0 = 4410; float *x = malloc(4 * n), *y = malloc(4 * n);
    memset(x, 0, 4 * n); for (int i = s0; i < n; i++) x[i] = 0.3f * sinf(2 * 3.14159265f * f * (i - s0) / 44100.0f);
    render(which, x, y, n);
    double ref = 0; for (int i = 22050; i < 22050 + 4410; i++) ref += (double)y[i] * y[i]; ref /= 4410;
    const int W = (int)(44100.0f / (2 * f)) + 1;            /* one octave cycle */
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
    const float s1 = getenv("S1") ? atof(getenv("S1")) : 37.5f, c1 = getenv("C1") ? atof(getenv("C1")) : 0.04f, w1 = getenv("W1") ? atof(getenv("W1")) : 0.8f;
    const float s2 = getenv("S2") ? atof(getenv("S2")) : 50.0f, c2 = getenv("C2") ? atof(getenv("C2")) : 0.03f, w2 = getenv("W2") ? atof(getenv("W2")) : 0.8f;
    make_v48(&BK[0]);
    int n1 = law_bands(s1, c1); make_bank(&BK[1], n1, w1);
    int n2 = law_bands(s2, c2); make_bank(&BK[2], n2, w2);
    LAW_S0 = 0;
    static char l1[40], l2[40]; snprintf(l1, 40, "%.0f+%.3f w%.1f(%d)", s1, c1, w1, n1); snprintf(l2, 40, "%.0f+%.3f w%.1f(%d)", s2, c2, w2, n2);
    static const char *NM[4]; NM[0] = "ENGINE NOW"; NM[1] = "V48 BANK"; NM[2] = l1; NM[3] = l2;
    printf("%-34s", ""); for (int q = 0; q < 4; q++) printf(" %20s", NM[q]); printf("\n");
    const int n = 3 * 44100; float *x = malloc(4 * n), *y = malloc(4 * n);
    double tgt[4] = {0}, lat[4] = {0}, latmax[4] = {0}, hi[4] = {0}, ch[4] = {0}; int nt = 0, nl = 0;
    for (int m = 40; m <= 76; m += 3) {                      /* single notes E2 .. E5 */
        const float f = 440.0f * powf(2.0f, (m - 69) / 12.0f); tones(x, n, &f, 1, 0, 0);
        for (int q = 0; q < 4; q++) { render(q, x, y, n); tgt[q] += target(y, n, &f, 1); }
        nt++;
    }
    for (int m = 38; m <= 52; m++) {                          /* arrival: D2 .. E3 */
        const float f = 440.0f * powf(2.0f, (m - 69) / 12.0f);
        for (int q = 0; q < 4; q++) { const float a = arrival(q, f); lat[q] += a; if (a > latmax[q]) latmax[q] = a; }
        nl++;
    }
    for (int q = 0; q < 4; q++) hi[q] = arrival(q, 1500.0f);
    static const float C[3][3] = {{82.41f, 123.47f, 164.81f}, {110.0f, 164.81f, 220.0f}, {196.0f, 246.94f, 293.66f}};
    for (int c = 0; c < 3; c++) { tones(x, n, C[c], 3, 0, 0); for (int q = 0; q < 4; q++) { render(q, x, y, n); ch[q] += target(y, n, C[c], 3) / 3; } }
    printf("%-34s", "single notes E2..E5, on target"); for (int q = 0; q < 4; q++) printf(" %19.1f%%", tgt[q] / nt); printf("\n");
    printf("%-34s", "chords E5 A5 G, on target");       for (int q = 0; q < 4; q++) printf(" %19.1f%%", ch[q]); printf("\n");
    printf("%-34s", "arrival D2..E3 mean / worst (ms)"); for (int q = 0; q < 4; q++) printf(" %12.1f / %5.1f", lat[q] / nl, latmax[q]); printf("\n");
    printf("%-34s", "arrival 1.5 kHz (ms)");            for (int q = 0; q < 4; q++) printf(" %20.1f", hi[q]); printf("\n");
    for (int a = 1; a < argc; a++) {
        int m; float *r = wav_read(argv[a], &m); if (!r) continue;
        float *o = malloc(4 * m); const char *b = strrchr(argv[a], '/');
        printf("%-30.30s", b ? b + 1 : argv[a]);
        for (int q = 0; q < 4; q++) { render(q, r, o, m); printf(" %12.2f", wobble(r, o, m)); }
        printf("   (added wobble dB)\n"); free(r); free(o);
    }
    return 0;
}
