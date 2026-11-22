/* v76: ACOUSTIC PREAMP (BST) and ACOUSTIC DRIVE (SFX): the catalogue, each control's effect on the spectrum, the attack dip, unity levels, the dry blend's alignment,
 * aliasing of the clipper, bounded and silent output, no click on a knob jump, and both together. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 2 };
static float x[N], y[N];
static void pre(px_preset_t *p, int lc, int dq, int body, int nt, int lv)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_BST] = 1; p->type[PX_B_BST] = PX_BST_ACOUSTIC;
    p->p[PX_B_BST][0] = (uint8_t)lc; p->p[PX_B_BST][1] = (uint8_t)dq; p->p[PX_B_BST][2] = (uint8_t)body; p->p[PX_B_BST][3] = (uint8_t)nt; p->p[PX_B_BST][4] = (uint8_t)lv;
}
static void drv(px_preset_t *p, int dr, int bl, int pz, int tn, int lv)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_SFX] = 1; p->type[PX_B_SFX] = PX_SFX_ACOUSTIC;
    p->p[PX_B_SFX][0] = (uint8_t)dr; p->p[PX_B_SFX][1] = (uint8_t)bl; p->p[PX_B_SFX][2] = (uint8_t)pz; p->p[PX_B_SFX][3] = (uint8_t)tn; p->p[PX_B_SFX][4] = (uint8_t)lv;
}
static void render(const px_preset_t *p, const float *in, float *out, int n)
{
    static px_engine e; px_engine_init(&e, FS); px_engine_set(&e, p);
    for (int at = 0; at + 64 <= n; at += 64) px_engine_process(&e, in + at, out + at, 64);
}
static float mag(const float *s, int n, float hz)
{
    double re = 0, im = 0; for (int i = 0; i < n; i++) { const double w = 6.283185307179586 * hz * i / FS; re += s[i] * cos(w); im += s[i] * sin(w); }
    return (float)(sqrt(re * re + im * im) * 2.0 / n);
}
static float gain_db(const px_preset_t *p, float hz, float amp)
{
    for (int i = 0; i < 44100; i++) x[i] = amp * sinf(6.2831853f * hz * (float)i / FS);
    render(p, x, y, 44100);
    return 20.0f * log10f(mag(y + 22050, 22050, hz) / amp + 1e-9f);
}
int main(void)
{
    px_preset_t p, q;
    CHECK(!strcmp(px_type_name(PX_B_BST, PX_BST_ACOUSTIC), "ACOUSTIC PRE") && px_nparams(PX_B_BST, PX_BST_ACOUSTIC) == 5, "preamp catalogue");
    CHECK(!strcmp(px_type_name(PX_B_SFX, PX_SFX_ACOUSTIC), "ACOUSTIC DRIVE") && px_nparams(PX_B_SFX, PX_SFX_ACOUSTIC) == 5 && !px_type_retired(PX_B_SFX, PX_SFX_ACOUSTIC), "drive catalogue");
    /* ---- PREAMP ---- */
    pre(&p, 55, 0, 50, 0, 50);
    { const float g1k = gain_db(&p, 1000, 0.05f), g40 = gain_db(&p, 40, 0.05f), g100 = gain_db(&p, 100, 0.05f); printf("preamp flat-ish: 1 kHz %.2f, 100 Hz %.2f, 40 Hz %.2f\n", g1k, g100, g40);
      CHECK(fabsf(g1k) < 1.5f, "unity at 1 kHz: %.2f", g1k); CHECK(g40 < g1k - 5.0f, "LOW CUT takes the 40 Hz: %.2f", g40); }
    pre(&p, 0, 0, 50, 0, 50); const float lo0 = gain_db(&p, 40, 0.05f); pre(&p, 100, 0, 50, 0, 50); const float lo1 = gain_db(&p, 100, 0.05f);
    CHECK(lo0 > -3.0f && lo1 < -3.0f, "LOW CUT range: 40 Hz at min %.1f dB, 100 Hz at max %.1f dB", lo0, lo1);
    pre(&p, 55, 0, 50, 0, 50); const float q0 = gain_db(&p, 2600, 0.05f), t0 = gain_db(&p, 9000, 0.05f); pre(&p, 55, 100, 50, 0, 50); const float q1 = gain_db(&p, 2600, 0.05f), t1 = gain_db(&p, 9000, 0.05f), m1 = gain_db(&p, 200, 0.05f);
    CHECK(q1 < q0 - 4.0f && t1 < t0 - 2.5f && fabsf(m1) < 1.0f, "DEQUACK: 2.6 kHz %.1f -> %.1f, 9 kHz %.1f -> %.1f, 200 Hz %.1f", q0, q1, t0, t1, m1);
    pre(&p, 55, 0, 0, 0, 50); const float bc = gain_db(&p, 300, 0.05f); pre(&p, 0, 0, 100, 0, 50); const float bl = gain_db(&p, 60, 0.05f), ba = gain_db(&p, 12000, 0.05f);
    CHECK(bc < -6.0f && bl > 5.0f && ba > 3.0f, "BODY: box %.1f dB, low %.1f dB, air %.1f dB", bc, bl, ba);
    pre(&p, 55, 0, 50, 50, 50); const float nf = 80.0f * powf(50.0f, 0.5f), ng = gain_db(&p, nf, 0.05f), nn = gain_db(&p, nf * 1.5f, 0.05f);
    CHECK(ng < -15.0f && fabsf(nn) < 1.5f, "NOTCH at %.0f Hz: %.1f dB, a fifth up %.1f dB", nf, ng, nn);
    pre(&p, 55, 0, 50, 0, 0); const float lm = gain_db(&p, 1000, 0.05f); pre(&p, 55, 0, 50, 0, 100); const float lp = gain_db(&p, 1000, 0.05f);
    CHECK(fabsf(lm + 12.0f) < 1.0f && fabsf(lp - 12.0f) < 1.0f, "LEVEL -12 .. +12 dB: %.1f, %.1f", lm, lp);
    {   /* the attack dip: a 2.9 kHz tone starting suddenly loses more in its first 15 ms than the same tone later, with DEQUACK up, than with it down */
        for (int i = 0; i < N; i++) x[i] = (i < 22050 ? 0.0f : 0.08f) * sinf(6.2831853f * 2900.0f * (float)i / FS);
        float ratio[2];
        for (int k = 0; k < 2; k++) { pre(&p, 20, k ? 100 : 0, 50, 0, 50); render(&p, x, y, N); ratio[k] = mag(y + 22050, 660, 2900.0f) / (mag(y + 40000, 4000, 2900.0f) + 1e-9f); }
        printf("attack against sustain at 2.9 kHz: DEQUACK 0 %.2f, 100 %.2f\n", ratio[0], ratio[1]);
        CHECK(ratio[1] < ratio[0] * 0.9f, "the attack dip: %.2f against %.2f", ratio[1], ratio[0]);
    }
    /* ---- DRIVE ---- */
    drv(&p, 50, 0, 50, 50, 50); const float d0 = gain_db(&p, 200, 0.1f), d1 = gain_db(&p, 4000, 0.1f);
    CHECK(fabsf(d0) < 1.5f && fabsf(d1) < 1.5f, "BLEND 0 is the dry guitar (delayed, level kept): %.2f dB at 200 Hz, %.2f dB at 4 kHz", d0, d1);
    {   /* the dry blend is in step with the wet: blend 50 with the clipper nearly linear (DRIVE 0, small input) keeps the level at all frequencies (no comb) */
        drv(&p, 0, 50, 0, 100, 50); float mn = 99, mx = -99;
        for (float f = 200; f < 6000; f *= 1.4f) { const float g = gain_db(&p, f, 0.02f); if (g < mn) mn = g; if (g > mx) mx = g; }
        printf("blend 50, DRIVE 0, 200 Hz .. 6 kHz: %.1f .. %.1f dB\n", mn, mx);
        CHECK(mn > -3.0f && mx < 3.0f, "no comb in the blend: %.1f .. %.1f dB", mn, mx);
    }
    {   /* harmonics rise with DRIVE; PIEZO takes the low end before the clipper */
        float h[2];
        for (int k = 0; k < 2; k++) { drv(&p, k ? 90 : 10, 100, 50, 80, 50); for (int i = 0; i < N; i++) x[i] = 0.1f * sinf(6.2831853f * 440.0f * (float)i / FS); render(&p, x, y, N);
            h[k] = mag(y + 44100, 44100, 1320.0f) / mag(y + 44100, 44100, 440.0f); }
        CHECK(h[1] > h[0] * 3.0f, "DRIVE adds harmonics: 3rd/1st %.3f -> %.3f", h[0], h[1]);
        drv(&p, 60, 100, 100, 50, 50); const float z60 = gain_db(&p, 60, 0.02f), z1k = gain_db(&p, 1000, 0.02f); drv(&p, 60, 100, 0, 50, 50); const float y60 = gain_db(&p, 60, 0.02f);
        CHECK(z60 < y60 - 4.0f && z60 < z1k - 8.0f, "PIEZO takes the lows: 60 Hz %.1f (PIEZO 100) against %.1f (0), 1 kHz %.1f", z60, y60, z1k);
    }
    {   /* aliasing: a 5 kHz sine hard driven: the fold-back of its 8th harmonic (40 kHz -> 4.1 kHz) against the fundamental */
        drv(&p, 100, 100, 0, 100, 50); for (int i = 0; i < N; i++) x[i] = 0.3f * sinf(6.2831853f * 5000.0f * (float)i / FS); render(&p, x, y, N);
        const float a = 20.0f * log10f(mag(y + 44100, 44100, 4100.0f) / mag(y + 44100, 44100, 5000.0f) + 1e-9f);
        printf("alias at 4.1 kHz against the 5 kHz fundamental (PX_AD_OS %d): %.1f dB\n", PX_AD_OS, a);
        CHECK(a < -40.0f, "aliasing %.1f dB", a);
    }
    /* ---- both: bounded, silent, no click, stacked ---- */
    for (int k = 0; k < 2; k++) {
        if (k == 0) pre(&p, 100, 100, 100, 100, 100); else drv(&p, 100, 100, 100, 100, 100);
        for (int i = 0; i < N; i++) x[i] = 0.9f * sinf(6.2831853f * 110.0f * (float)i / FS); render(&p, x, y, N);
        float mx = 0; int fin = 1; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; if (fabsf(y[i]) > mx) mx = fabsf(y[i]); }
        CHECK(fin && mx < 20.0f, "%s everything up: finite, peak %.2f", k ? "drive" : "preamp", mx);
        memset(x, 0, sizeof x); render(&p, x, y, N); mx = 0; for (int i = 22050; i < N; i++) if (fabsf(y[i]) > mx) mx = fabsf(y[i]);
        CHECK(mx < 1e-3f, "%s: silence in, %.5f out", k ? "drive" : "preamp", mx);
    }
    {   px_preset_t a, b; for (int k = 0; k < 2; k++) {
            if (k == 0) { pre(&a, 20, 0, 50, 0, 50); pre(&b, 90, 100, 0, 70, 50); } else { drv(&a, 10, 20, 0, 50, 50); drv(&b, 100, 100, 100, 20, 50); }
            static px_engine e; for (int i = 0; i < N; i++) x[i] = 0.08f * sinf(6.2831853f * 196.0f * (float)i / FS);
            px_engine_init(&e, FS); px_engine_set(&e, &a);
            for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 / 64 * 64) px_engine_set(&e, &b); px_engine_process(&e, x + at, y + at, 64); }
            float ref = 0, jmp = 0; for (int i = 4000; i < N; i++) { const float d = fabsf(y[i] - y[i - 1]); if (i < 44000) { if (d > ref) ref = d; } else if (i < 50000 && d > jmp) jmp = d; }
            CHECK(jmp < ref * 3.0f + 0.2f, "%s: no click on a knob jump: step %.3f against %.3f", k ? "drive" : "preamp", jmp, ref); } }
    {   pre(&p, 55, 40, 50, 0, 50); q = p; q.on[PX_B_SFX] = 1; q.type[PX_B_SFX] = PX_SFX_ACOUSTIC; q.p[PX_B_SFX][0] = 40; q.p[PX_B_SFX][1] = 40; q.p[PX_B_SFX][2] = 50; q.p[PX_B_SFX][3] = 50; q.p[PX_B_SFX][4] = 50;
        for (int i = 0; i < N; i++) x[i] = 0.1f * sinf(6.2831853f * 330.0f * (float)i / FS); render(&q, x, y, N);
        float mx = 0; int fin = 1; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; if (fabsf(y[i]) > mx) mx = fabsf(y[i]); }
        CHECK(fin && mx > 0.01f && mx < 2.0f, "preamp and drive stacked: finite, peak %.2f", mx); }
    printf(bad ? "acoustic_test: %d FAILED\n" : "acoustic_test: ok\n", bad);
    return bad != 0;
}
