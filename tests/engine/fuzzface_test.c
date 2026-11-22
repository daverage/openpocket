/* BST FUZZ FACE (v75): the circuit model in engine/px_pedals.h (pxp_ff_*).
 *  - the catalogue entry: five controls, the PICKUP / TRANSISTOR texts
 *  - the quiescent point: Q2's collector near 4.5 V, Q1's collector under 1 V (germanium, BIAS 50); BIAS moves it
 *  - the guitar-volume cleanup: the same note at a lower input level has fewer harmonics relative to the fundamental
 *  - FUZZ: more of it, more harmonics at the same input
 *  - bounded, finite, silent in silence; no click on a knob jump mid-note; both transistor sets */
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
static void ff(px_preset_t *p, int fz, int vol, int pk, int bias, int tr)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_BST] = 1; p->type[PX_B_BST] = PX_BST_FUZZFACE;
    p->p[PX_B_BST][0] = (uint8_t)fz; p->p[PX_B_BST][1] = (uint8_t)vol; p->p[PX_B_BST][2] = (uint8_t)pk; p->p[PX_B_BST][3] = (uint8_t)bias; p->p[PX_B_BST][4] = (uint8_t)tr;
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
static float harm_db(const px_preset_t *p, float amp)              /* the 2nd..5th harmonics' power against the fundamental, dB, for a 220 Hz sine */
{
    for (int i = 0; i < N; i++) x[i] = amp * sinf(6.2831853f * 220.0f * (float)i / FS);
    render(p, x, y, N);
    const float f1 = mag(y + 44100, 44100, 220.0f); float h = 0;
    for (int k = 2; k <= 5; k++) { const float m = mag(y + 44100, 44100, 220.0f * (float)k); h += m * m; }
    return 10.0f * log10f(h / (f1 * f1) + 1e-12f);
}
int main(void)
{
    px_preset_t p;
    CHECK(!strcmp(px_type_name(PX_B_BST, PX_BST_FUZZFACE), "FUZZ FACE") && PX_BST_FUZZFACE == 5, "the catalogue lists the FUZZ FACE sixth");
    CHECK(px_nparams(PX_B_BST, PX_BST_FUZZFACE) == 5, "five controls");
    /* the quiescent point */
    for (int tr = 0; tr < 2; tr++) for (int b = 0; b <= 100; b += 50) {
        px_ff r; pxp_ff_init(&r, FS, 0); pxp_ff_params(&r, tr, 0.5f, b * 0.01f, 0.0f); pxp_ff_dc(&r);
        CHECK(isfinite(r.x[0]) && isfinite(r.x[1]) && isfinite(r.x[2]) && isfinite(r.x[3]), "dc finite (tr %d bias %d)", tr, b);
        if (b == 50) CHECK(r.x[3] > 4.0f && r.x[3] < 5.0f, "Q2 collector %.2f V at BIAS 50 (tr %d)", r.x[3], tr);
        if (b == 50 && !tr) CHECK(r.x[1] > 0.4f && r.x[1] < 1.0f, "Q1 collector %.2f V (germanium)", r.x[1]);
        if (b == 0) { px_ff q; pxp_ff_init(&q, FS, 0); pxp_ff_params(&q, tr, 0.5f, 0.5f, 0.0f); pxp_ff_dc(&q); CHECK(r.x[3] < q.x[3] - 1.0f, "BIAS moves Q2's collector down (tr %d)", tr); }
    }
    /* the cleanup with the guitar's volume */
    ff(&p, 60, 50, 0, 50, 0);
    const float lo = harm_db(&p, 0.01f), hi = harm_db(&p, 0.15f);
    CHECK(lo < hi - 6.0f, "lower input = cleaner: %.1f dB against %.1f dB", lo, hi);
    /* FUZZ */
    ff(&p, 20, 50, 0, 50, 0); const float f20 = harm_db(&p, 0.01f);
    ff(&p, 90, 50, 0, 50, 0); const float f90 = harm_db(&p, 0.01f);
    CHECK(f90 > f20 + 3.0f, "more FUZZ, more harmonics: %.1f -> %.1f dB", f20, f90);
    {   /* v76: the FUZZ pot works across its travel at a guitar's level, and the guitar's volume cleans it up (softer at -12 dB, clean at -24 dB) */
        float t[4]; const int fz[4] = {0, 25, 50, 75};
        for (int k = 0; k < 4; k++) { ff(&p, fz[k], 50, 50, 50, 0); t[k] = harm_db(&p, 0.125f); }
        printf("THD at a guitar's level: FUZZ 0 %.1f, 25 %.1f, 50 %.1f, 75 %.1f dB\n", t[0], t[1], t[2], t[3]);
        CHECK(t[1] > t[0] + 2.0f && t[2] > t[1] + 0.5f && t[3] > t[2] - 0.2f && t[3] > t[0] + 5.0f, "FUZZ is not all at the top");
        ff(&p, 60, 50, 50, 50, 0); const float loud = harm_db(&p, 0.125f), half = harm_db(&p, 0.031f), quiet = harm_db(&p, 0.008f);
        CHECK(half < loud - 5.0f && quiet < loud - 30.0f, "the guitar volume cleans up: %.1f dB at full, %.1f dB at -12 dB, %.1f dB at -24 dB", loud, half, quiet);
    }
    /* bounded, silent, no click */
    ff(&p, 100, 100, 100, 50, 1);
    for (int i = 0; i < N; i++) x[i] = 0.9f * sinf(6.2831853f * 110.0f * (float)i / FS);
    render(&p, x, y, N); float mx = 0; int fin = 1; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; if (fabsf(y[i]) > mx) mx = fabsf(y[i]); }
    CHECK(fin && mx < 8.0f, "loud input, everything up, silicon: finite, peak %.2f", mx);
    memset(x, 0, sizeof x); ff(&p, 70, 50, 0, 50, 0); render(&p, x, y, N);
    mx = 0; for (int i = 22050; i < N; i++) if (fabsf(y[i]) > mx) mx = fabsf(y[i]);
    CHECK(mx < 1e-3f, "silence in, %.5f out", mx);
    {   /* a knob jump mid-note: sample-to-sample step stays near the note's own */
        static px_engine e; px_preset_t a, b; ff(&a, 20, 50, 0, 50, 0); ff(&b, 100, 50, 100, 30, 1);
        for (int i = 0; i < N; i++) x[i] = 0.08f * sinf(6.2831853f * 196.0f * (float)i / FS);
        px_engine_init(&e, FS); px_engine_set(&e, &a);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 / 64 * 64) px_engine_set(&e, &b); px_engine_process(&e, x + at, y + at, 64); }
        float ref = 0, jmp = 0; for (int i = 4000; i < N; i++) { const float d = fabsf(y[i] - y[i - 1]); if (i < 44000) { if (d > ref) ref = d; } else if (i < 50000 && d > jmp) jmp = d; }
        CHECK(jmp < ref * 3.0f + 0.2f, "no click on a knob jump: step %.3f against %.3f", jmp, ref);
    }
    printf(bad ? "fuzzface_test: %d FAILED\n" : "fuzzface_test: ok\n", bad);
    return bad != 0;
}
