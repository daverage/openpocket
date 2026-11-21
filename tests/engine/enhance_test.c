/* SFX ENHANCE (engine/px_enhance.h): the NE570-inspired harmonic enhancer, through the whole engine.
 *  - catalogue: a live type with BETTER / LOUDER; LOUDER shows dB; APPLAUSE's tombstone is still skipped
 *  - unity: BETTER 0, LOUDER 0 is the dry signal; silence stays silence (denormal/DC state)
 *  - harmonics: even over odd, at the level of hard playing at the pedal (peak 0.14) the added second harmonic is audible (1..3 %), and it falls with the playing level (an amplitude law)
 *  - the fundamental is not changed (< 0.2 dB) at BETTER 100; LOUDER 100 is +12 dB on the clean signal
 *  - bounded and finite for a full-scale square and for input above full scale
 *  - switching the effect on/off or moving the knobs mid-note makes no click (the difference to the steady render stays small)
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
static void preset(px_preset_t *p, int on, int better, int louder)
{
    px_preset_factory(p, 7); for (int z = 0; z < PX_NBLK; z++) p->on[z] = 0;
    p->on[PX_B_SFX] = (uint8_t)on; p->type[PX_B_SFX] = PX_SFX_ENHANCE; p->p[PX_B_SFX][0] = (uint8_t)better; p->p[PX_B_SFX][1] = (uint8_t)louder;
}
static void render(const px_preset_t *p, const float *x, float *y, int n)
{
    static px_engine e; px_engine_init(&e, FS); px_engine_set(&e, p);
    for (int at = 0; at + 64 <= n; at += 64) px_engine_process(&e, x + at, y + at, 64);
}
static float mag(const float *y, int n, float hz)      /* DFT magnitude at hz over n samples (integer cycles chosen by the caller) */
{
    double re = 0, im = 0; for (int i = 0; i < n; i++) { const double w = 6.283185307179586 * hz * i / FS; re += y[i] * cos(w); im += y[i] * sin(w); }
    return (float)(sqrt(re * re + im * im) * 2.0 / n);
}
enum { N = 44100 * 2 }; static float x[N], y[N], d[N], z[N];
int main(void)
{
    px_preset_t p, off;
    /* catalogue */
    CHECK(px_type_retired(PX_B_SFX, PX_SFX_ENHANCE) == 0, "ENHANCE is a live type");
    CHECK(strcmp(px_type_name(PX_B_SFX, PX_SFX_ENHANCE), "ENHANCE") == 0, "its name is ENHANCE");
    CHECK(px_nparams(PX_B_SFX, PX_SFX_ENHANCE) == 2 && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 0), "BETTER") && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 1), "LOUDER"), "two controls: BETTER, LOUDER");
    { char b[8]; CHECK(px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 1, 100, b) == 1 && !strcmp(b, "+12"), "LOUDER 100 reads +12 (dB): '%s'", b); CHECK(px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 1, 0, b) == 1 && !strcmp(b, "0"), "LOUDER 0 reads 0: '%s'", b); }
    CHECK(px_nlive(PX_B_SFX) == PX_SFX_COUNT - 1 && px_type_retired(PX_B_SFX, PX_SFX_APPLAUSE) == 1, "APPLAUSE is still the one tombstone");
    /* unity and silence */
    for (int i = 0; i < N; i++) x[i] = 0.14f * sinf(6.2831853f * 110.0f * (float)i / FS);
    preset(&p, 1, 0, 0); off = p; off.on[PX_B_SFX] = 0; render(&p, x, y, N); render(&off, x, z, N);
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "BETTER 0, LOUDER 0 is the dry signal (max difference %.2g)", m); }
    preset(&p, 1, 100, 0); memset(x, 0, sizeof x); render(&p, x, y, N);
    { float m = 0; for (int i = N / 2; i < N; i++) m = fmaxf(m, fabsf(y[i])); CHECK(m < 1e-7f, "silence in, silence out (%.2g)", m); }
    /* harmonics and the fundamental at the level of hard playing at the pedal (0.14) and 20 dB below it */
    const float amp[3] = {0.14f, 0.014f, 0.5f}; float h2[3], h3[3], fund[3];
    for (int a = 0; a < 3; a++) {
        for (int i = 0; i < N; i++) x[i] = amp[a] * sinf(6.2831853f * 110.0f * (float)i / FS);
        preset(&p, 1, 100, 0); render(&p, x, y, N); const float *t = y + 44100; const int n = 44100;     /* the last second, whole cycles */
        fund[a] = mag(t, n, 110.0f); h2[a] = 100.0f * mag(t, n, 220.0f) / fund[a]; h3[a] = 100.0f * mag(t, n, 330.0f) / fund[a];
        printf("amp %.3f: H2 %.3f %%  H3 %.3f %%  fundamental change %.3f dB\n", amp[a], h2[a], h3[a], 20.0f * log10f(fund[a] / amp[a]));
        CHECK(fabsf(20.0f * log10f(fund[a] / amp[a])) < 0.2f, "the fundamental is not changed at BETTER 100 (amp %.3f)", amp[a]);
        CHECK(h2[a] > h3[a], "even harmonics lead (amp %.3f: H2 %.3f H3 %.3f)", amp[a], h2[a], h3[a]);
    }
    CHECK(h2[0] > 1.0f && h2[0] < 3.0f, "hard playing at the pedal (peak 0.14) adds 1..3 %% second harmonic at BETTER 100 (%.3f %%)", h2[0]);
    CHECK(h2[1] < h2[0] / 7.0f && h2[1] > h2[0] / 14.0f, "20 dB less playing level gives about 10 times less H2 (%.3f %% against %.3f %%)", h2[1], h2[0]);
    /* LOUDER */
    for (int i = 0; i < N; i++) x[i] = 0.1f * sinf(6.2831853f * 110.0f * (float)i / FS);
    preset(&p, 1, 0, 100); render(&p, x, y, N);
    { const float db = 20.0f * log10f(mag(y + 44100, 44100, 110.0f) / 0.1f); CHECK(fabsf(db - 12.0f) < 0.05f, "LOUDER 100 is +12 dB on a clean signal (%.3f dB)", db); }
    /* bounded and finite: a full-scale square, and input above full scale */
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f;
    preset(&p, 1, 100, 100); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 4.2f, "a full-scale square at BETTER 100, LOUDER 100 stays finite and bounded (peak %.2f; dry x +12 dB = 3.98)", pk); }
    for (int i = 0; i < N; i++) x[i] = 3.0f * sinf(6.2831853f * 82.0f * (float)i / FS);
    preset(&p, 1, 100, 0); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 3.6f, "input above full scale: finite, the added part stays small (peak %.2f)", pk); }
    /* no click on switching or on a knob move: the largest sample-to-sample step of the render with the change must stay within a few percent of the steady renders' (their own slope) */
    for (int i = 0; i < N; i++) x[i] = 0.3f * sinf(6.2831853f * 220.0f * (float)i / FS);
    {
        static float ya[N], yb[N], yc[N];
        px_preset_t pa, pb; preset(&pa, 1, 100, 50); preset(&pb, 0, 100, 50);
        render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        const float ref = fmaxf(sa, sb);
        CHECK(worst < 1.15f * ref, "switching ENHANCE off and on mid-note: no click (largest step %.4f, steady renders %.4f / %.4f)", worst, sa, sb);
        preset(&pa, 1, 10, 0); preset(&pb, 1, 100, 100); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.15f * fmaxf(sa, sb), "a knob jump mid-note (BETTER 10 -> 100, LOUDER 0 -> +12 dB) glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    /* a stored retired APPLAUSE and an unknown type id stay bypassed (the new type did not shift anything) */
    preset(&p, 1, 50, 50); p.type[PX_B_SFX] = PX_SFX_APPLAUSE; off = p; off.on[PX_B_SFX] = 0;
    for (int i = 0; i < N; i++) x[i] = 0.2f * sinf(0.05f * (float)i); render(&p, x, y, N); render(&off, x, z, N);
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "a stored APPLAUSE is still bypassed (%.2g)", m); }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("ENHANCE ok: 0 problems\n"); return 0;
}
