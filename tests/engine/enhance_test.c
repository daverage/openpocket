/* SFX ENHANCE v4 (engine/px_enhance.h) through the whole engine: a gain that follows a fast rectified copy of the signal (a compander cell, no threshold): even and odd harmonics with no clipping,
 * a subtle compressor, BETTER the amount, LOUDER a clean 0..+12 dB boost. Expectations come from the offline prototype (docs/ENHANCE.md), not from a real Better Box.
 *  - catalogue: a live type with BETTER / LOUDER, readable text; APPLAUSE's tombstone is untouched
 *  - unity (BETTER 0, LOUDER 0), silence, finite and bounded
 *  - no knee: quiet notes get harmonics and gain too; the second harmonic dominates the third (the half-wave half of the detector), gain falls as the level rises
 *  - no DC, no click on switching or on a knob jump */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 2 };
static float x[N], y[N], z[N], ya[N], yb[N], yc[N];
static void preset(px_preset_t *p, int on, int better, int louder)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_BST] = (uint8_t)on; p->type[PX_B_BST] = PX_BST_ENHANCE; p->p[PX_B_BST][0] = (uint8_t)better; p->p[PX_B_BST][1] = (uint8_t)louder;
}
static void render(const px_preset_t *p, const float *in, float *out, int n)
{
    static px_engine e; px_engine_init(&e, FS); px_engine_set(&e, p);
    for (int at = 0; at + 64 <= n; at += 64) px_engine_process(&e, in + at, out + at, 64);
}
static float mag(const float *s, int n, float hz)      /* DFT magnitude at hz over n samples (whole cycles chosen by the caller) */
{
    double re = 0, im = 0; for (int i = 0; i < n; i++) { const double w = 6.283185307179586 * hz * i / FS; re += s[i] * cos(w); im += s[i] * sin(w); }
    return (float)(sqrt(re * re + im * im) * 2.0 / n);
}
static void sine(float *o, int n, float hz, float amp) { for (int i = 0; i < n; i++) o[i] = amp * sinf(6.2831853f * hz * (float)i / FS); }
/* two notes (exact FFT bins 148 and 187 of 32768 at 44.1 kHz: 199 and 252 Hz): the energy that is NOT a harmonic, sum or difference of order <= 3 of the two, re the two notes (dB): the 'digital interference' */
#define FN 32768
static double fre[FN], fim[FN];
static float chord_junk(int better)
{
    px_preset_t p; px_preset_base(&p); for (int k = 0; k < PX_NBLK; k++) p.on[k] = 0;
    p.on[PX_B_BST] = 1; p.type[PX_B_BST] = PX_BST_ENHANCE; p.p[PX_B_BST][0] = (uint8_t)better; p.p[PX_B_BST][1] = 0;
    static float in[FN * 6], out[FN * 6]; for (int i = 0; i < FN * 6; i++) in[i] = 0.05f * (sinf(6.2831853f * 148.0f * (float)(i % FN) / FN) + sinf(6.2831853f * 187.0f * (float)(i % FN) / FN));
    static px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &p); for (int at = 0; at + 64 <= FN * 6; at += 64) px_engine_process(&e, in + at, out + at, 64);
    for (int i = 0; i < FN; i++) { fre[i] = out[FN * 5 + i]; fim[i] = 0; }
    for (int i = 1, j = 0; i < FN; i++) { int bit = FN >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { double t = fre[i]; fre[i] = fre[j]; fre[j] = t; t = fim[i]; fim[i] = fim[j]; fim[j] = t; } }
    for (int len = 2; len <= FN; len <<= 1) { const double a = -2 * 3.141592653589793 / len; for (int i = 0; i < FN; i += len) for (int j = 0; j < len / 2; j++) { const double wr = cos(a * j), wi = sin(a * j), ur = fre[i + j], ui = fim[i + j], vr = fre[i + j + len / 2] * wr - fim[i + j + len / 2] * wi, vi = fre[i + j + len / 2] * wi + fim[i + j + len / 2] * wr; fre[i + j] = ur + vr; fim[i + j] = ui + vi; fre[i + j + len / 2] = ur - vr; fim[i + j + len / 2] = ui - vi; } }
    static char rel[FN / 2]; memset(rel, 0, sizeof rel);
    for (int n1 = -3; n1 <= 3; n1++) for (int n2 = -3; n2 <= 3; n2++) { if (abs(n1) + abs(n2) > 3) continue; int b = n1 * 148 + n2 * 187; if (b < 0) b = -b; for (int d = -2; d <= 2; d++) if (b + d > 0 && b + d < FN / 2) rel[b + d] = 1; }
    double fund = 0, junk = 0; for (int b = 1; b < FN / 2; b++) { const double pw = fre[b] * fre[b] + fim[b] * fim[b]; if (b == 148 || b == 187) fund += pw; if (!rel[b]) junk += pw; }
    return (float)(10.0 * log10(junk / fund));
}
int main(void)
{
    px_preset_t p, off;
    char b[8];
    /* catalogue */
    CHECK(px_type_retired(PX_B_BST, PX_BST_ENHANCE) == 0 && !strcmp(px_type_name(PX_B_BST, PX_BST_ENHANCE), "ENHANCE"), "ENHANCE is a live type");
    CHECK(px_nparams(PX_B_BST, PX_BST_ENHANCE) == 2 && !strcmp(px_param_name(PX_B_BST, PX_BST_ENHANCE, 0), "BETTER") && !strcmp(px_param_name(PX_B_BST, PX_BST_ENHANCE, 1), "LOUDER"), "ENHANCE has BETTER and LOUDER");
    CHECK(px_param_text(PX_B_BST, PX_BST_ENHANCE, 1, 100, b) == 1 && !strcmp(b, "+12"), "LOUDER 100 reads +12: '%s'", b);
    CHECK(px_nlive(PX_B_SFX) == PX_SFX_COUNT - 5 && px_type_retired(PX_B_SFX, PX_SFX_APPLAUSE) == 1 && px_type_retired(PX_B_SFX, PX_SFX_ENHANCE) == 1, "APPLAUSE is still a tombstone (with ENHANCE and the three pedals that moved to BST in v75)");
    /* unity and silence */
    sine(x, N, 110.0f, 0.14f); preset(&p, 1, 0, 0); off = p; off.on[PX_B_BST] = 0; render(&p, x, y, N); render(&off, x, z, N);
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "BETTER 0, LOUDER 0 is the dry signal (%.2g)", m); }
    preset(&p, 1, 100, 0); memset(x, 0, sizeof x); render(&p, x, y, N);
    { float m = 0; for (int i = N / 2; i < N; i++) m = fmaxf(m, fabsf(y[i])); CHECK(m < 1e-7f, "silence in, silence out (%.2g)", m); }
    /* no knee: the harmonics and the gain follow the level at EVERY level (200 Hz, BETTER 100) */
    { const float lv[] = {0.003f, 0.01f, 0.03f, 0.08f, 0.2f}; float g[5], h2[5], h3[5];
      for (int k = 0; k < 5; k++) { sine(x, N, 200.0f, lv[k]); preset(&p, 1, 100, 0); render(&p, x, y, N);
        const float f1 = mag(y + 44100, 44100, 200.0f); g[k] = 20.0f * log10f(f1 / lv[k]); h2[k] = 20.0f * log10f(mag(y + 44100, 44100, 400.0f) / f1 + 1e-9f); h3[k] = 20.0f * log10f(mag(y + 44100, 44100, 600.0f) / f1 + 1e-9f);
        printf("  200 Hz amp %.3f: gain %+.2f dB  H2 %.1f dB  H3 %.1f dB\n", lv[k], g[k], h2[k], h3[k]); }
      CHECK(h2[0] > -40.0f && h2[1] > -34.0f, "quiet notes are processed too: H2 %.1f dB at 0.003 and %.1f dB at 0.01 (a knee would leave them clean)", h2[0], h2[1]);
      CHECK(h2[3] > -30.0f && h2[3] < -10.0f, "BETTER 100 makes a second harmonic of %.1f dB at guitar level (-30..-10)", h2[3]);
      CHECK(h3[3] < h2[3] - 4.0f, "the second harmonic dominates the third by >4 dB (%.1f vs %.1f dB)", h2[3], h3[3]);
      CHECK(g[0] > g[2] + 2.0f && g[2] > g[4] + 2.0f, "a subtle compander: gain %.1f dB at 0.003, %.1f at 0.03, %.1f at 0.2 (quiet notes raised, loud ones tamed)", g[0], g[2], g[4]);
      CHECK(g[4] > -14.0f && g[0] < 12.0f, "the gain stays within bounds (%.1f .. %.1f dB)", g[4], g[0]); }
    /* a chord: the intermodulation between the notes stays below -25 dB re the notes at BETTER 100 and does not get worse over the top of the knob (it was -23 dB, 3 dB under the second harmonic, before the knob's top was flattened) */
    { const float j70 = chord_junk(70), j85 = chord_junk(85), j100 = chord_junk(100); printf("  chord junk: BETTER 70 %.1f dB, 85 %.1f dB, 100 %.1f dB\n", j70, j85, j100);
      CHECK(j100 < -25.0f, "a chord at BETTER 100: the junk is below -25 dB (%.1f dB)", j100);
      CHECK(j100 < j70 + 1.5f && j85 < j70 + 1.5f, "the junk does not grow over the top of the knob (%.1f / %.1f / %.1f dB)", j70, j85, j100); }
    /* BETTER is a smooth amount: more BETTER, more second harmonic */
    { float prev = -200.0f; int ok = 1; sine(x, N, 200.0f, 0.08f);
      for (int bt = 10; bt <= 100; bt += 30) { preset(&p, 1, bt, 0); render(&p, x, y, N); const float h = 20.0f * log10f(mag(y + 44100, 44100, 400.0f) / mag(y + 44100, 44100, 200.0f)); if (h <= prev) ok = 0; prev = h; }
      CHECK(ok, "second harmonic rises with BETTER"); }
    /* no DC added */
    { sine(x, N, 200.0f, 0.1f); preset(&p, 1, 100, 0); render(&p, x, y, N); double s = 0; for (int i = 44100; i < N; i++) s += y[i]; s /= 44100.0; CHECK(fabs(s) < 0.0015, "no DC (%.5f)", s); }
    /* LOUDER */
    sine(x, N, 110.0f, 0.01f); preset(&p, 1, 0, 100); render(&p, x, y, N);
    { const float db = 20.0f * log10f(mag(y + 44100, 44100, 110.0f) / 0.01f); CHECK(fabsf(db - 12.0f) < 0.05f, "LOUDER 100 is +12 dB on a clean signal (%.3f dB)", db); }
    /* bounded and finite */
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f;
    preset(&p, 1, 100, 0); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 1.2f, "a full-scale square at BETTER 100: finite and bounded (peak %.2f)", pk); }
    sine(x, N, 82.0f, 3.0f); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 3.5f, "3x over full scale: finite and bounded (peak %.2f)", pk); }
    { unsigned r = 3u; for (int i = 0; i < N; i++) { r = r * 1664525u + 1013904223u; x[i] = ((float)(r >> 9) / 4194304.0f - 1.0f); } render(&p, x, y, N);
      int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 1.5f, "full-scale noise: finite and bounded (peak %.2f)", pk); }
    /* no click on switching or a knob jump: the largest sample step stays within a few per cent of the steady renders' */
    sine(x, N, 220.0f, 0.3f);
    {
        px_preset_t pa, pb; preset(&pa, 1, 100, 50); preset(&pb, 0, 100, 50); render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.15f * fmaxf(sa, sb), "switching ENHANCE off and on mid-note: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
        preset(&pa, 1, 10, 0); preset(&pb, 1, 100, 100); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.15f * fmaxf(sa, sb), "a knob jump mid-note (BETTER 10 -> 100, LOUDER 0 -> +12 dB) glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    /* a stored retired APPLAUSE stays bypassed */
    preset(&p, 1, 50, 50); p.on[PX_B_BST] = 0; p.on[PX_B_SFX] = 1; p.type[PX_B_SFX] = PX_SFX_APPLAUSE; off = p; off.on[PX_B_SFX] = 0; sine(x, N, 196.0f, 0.2f); render(&p, x, y, N); render(&off, x, z, N);
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "a stored APPLAUSE is still bypassed (%.2g)", m); }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("ENHANCE ok: 0 problems\n"); return 0;
}
