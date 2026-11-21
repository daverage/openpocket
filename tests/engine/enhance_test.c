/* SFX ENHANCE v3 (engine/px_enhance.h) through the whole engine: WARMTH (even harmonics of the 80-400 Hz band, a full-wave rectifier) and PRESENCE (an Aphex-style exciter on the
 * high band), blended by TONE, BETTER the amount, LOUDER a clean 0..+12 dB boost.
 *  - catalogue: a live type with BETTER / LOUDER / TONE, readable text; APPLAUSE's tombstone is untouched
 *  - unity (BETTER 0, LOUDER 0), silence, finite and bounded on full-scale / above-full-scale input
 *  - WARMTH raises the second harmonic of a note by a fixed share whatever the playing level (no sensitivity setting), changes nothing above 1.5 kHz and not the fundamental
 *  - PRESENCE lifts the high band, by about the same amount at a level 20 dB lower, and changes nothing below 400 Hz
 *  - chords: the rectifier's difference tones below 100 Hz are held down
 *  - no click on switching or on a knob jump */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 2 };
static float x[N], y[N], z[N], ya[N], yb[N], yc[N];
static void preset(px_preset_t *p, int on, int better, int louder, int tone)
{
    px_preset_factory(p, 7); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_SFX] = (uint8_t)on; p->type[PX_B_SFX] = PX_SFX_ENHANCE; p->p[PX_B_SFX][0] = (uint8_t)better; p->p[PX_B_SFX][1] = (uint8_t)louder; p->p[PX_B_SFX][2] = (uint8_t)tone;
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
/* a plucked-string stand-in: 196 Hz and 15 harmonics (1/h falling) with a decay and a click of noise at the start, to give the high band something to work on */
static void pluck(float *o, int n, float amp)
{
    unsigned r = 7u; for (int i = 0; i < n; i++) { const float t = (float)(i % 22050) / FS, env = expf(-t * 4.0f); float v = 0;
        for (int h = 1; h <= 20; h++) v += sinf(6.2831853f * 196.0f * (float)h * (float)i / FS) / (float)h;
        r = r * 1664525u + 1013904223u; const float nz = ((float)(r >> 9) / 4194304.0f - 1.0f) * expf(-t * 60.0f) * 0.6f;
        o[i] = amp * 0.35f * (v * env + nz); }
}
static float band_db(const float *s, int n, float lo, float hi)   /* energy in a band, dB (DFT sum over bins) */
{
    double e = 0; for (float f = lo; f < hi; f += 4.0f) { const float m = mag(s, n, f); e += (double)m * m; } return 10.0f * log10f((float)e + 1e-20f);
}
int main(void)
{
    px_preset_t p, off;
    /* catalogue */
    CHECK(px_type_retired(PX_B_SFX, PX_SFX_ENHANCE) == 0 && !strcmp(px_type_name(PX_B_SFX, PX_SFX_ENHANCE), "ENHANCE"), "ENHANCE is a live type");
    CHECK(px_nparams(PX_B_SFX, PX_SFX_ENHANCE) == 3 && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 0), "BETTER") && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 1), "LOUDER") && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 2), "TONE"), "three controls: BETTER, LOUDER, TONE");
    { char b[8]; CHECK(px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 1, 100, b) == 1 && !strcmp(b, "+12"), "LOUDER 100 reads +12: '%s'", b);
      CHECK(px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 2, 0, b) == 1 && !strcmp(b, "WARM") && px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 2, 50, b) == 1 && !strcmp(b, "BOTH") && px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 2, 100, b) == 1 && !strcmp(b, "BRIGHT"), "TONE reads WARM / BOTH / BRIGHT"); }
    CHECK(px_nlive(PX_B_SFX) == PX_SFX_COUNT - 1 && px_type_retired(PX_B_SFX, PX_SFX_APPLAUSE) == 1, "APPLAUSE is still the one tombstone");
    /* unity and silence */
    sine(x, N, 110.0f, 0.14f); preset(&p, 1, 0, 0, 50); off = p; off.on[PX_B_SFX] = 0; render(&p, x, y, N); render(&off, x, z, N);
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "BETTER 0, LOUDER 0 is the dry signal (%.2g)", m); }
    preset(&p, 1, 100, 0, 50); memset(x, 0, sizeof x); render(&p, x, y, N);
    { float m = 0; for (int i = N / 2; i < N; i++) m = fmaxf(m, fabsf(y[i])); CHECK(m < 1e-7f, "silence in, silence out (%.2g)", m); }
    /* WARMTH: a fixed share of second harmonic whatever the level; nothing above, the fundamental untouched */
    float h2[2];
    for (int a = 0; a < 2; a++) {
        sine(x, N, 110.0f, a ? 0.014f : 0.14f); preset(&p, 1, 100, 0, 0); render(&p, x, y, N);
        const float f1 = mag(y + 44100, 44100, 110.0f), f2 = mag(y + 44100, 44100, 220.0f); h2[a] = 20.0f * log10f(f2 / f1);
        CHECK(fabsf(20.0f * log10f(f1 / (a ? 0.014f : 0.14f))) < 0.4f, "WARMTH leaves the fundamental alone (amp %.3f: %.2f dB)", a ? 0.014f : 0.14f, 20.0f * log10f(f1 / (a ? 0.014f : 0.14f)));
        CHECK(h2[a] > -16.0f && h2[a] < -11.0f, "WARMTH at BETTER 100: second harmonic %.1f dB re the note (-16..-11), amp %.3f", h2[a], a ? 0.014f : 0.14f);
    }
    CHECK(fabsf(h2[0] - h2[1]) < 0.6f, "WARMTH is level independent: %.1f dB at 0.14, %.1f dB 20 dB lower", h2[0], h2[1]);
    { float mix[N]; sine(x, N, 110.0f, 0.1f); for (int i = 0; i < N; i++) mix[i] = x[i] + 0.02f * sinf(6.2831853f * 4000.0f * (float)i / FS);
      preset(&p, 1, 100, 0, 0); render(&p, mix, y, N); const float d = 20.0f * log10f(mag(y + 44100, 44100, 4000.0f) / 0.02f);
      CHECK(fabsf(d) < 0.3f, "WARMTH changes nothing at 4 kHz (%.2f dB)", d); }
    /* PRESENCE: lifts the high band, by about the same at a level 20 dB lower, nothing below 400 Hz */
    float lift[2];
    for (int a = 0; a < 2; a++) {
        pluck(x, N, a ? 0.03f : 0.3f); preset(&p, 1, 100, 0, 100); render(&p, x, y, N);
        lift[a] = band_db(y + 22050, 22050, 3200.0f, 8000.0f) - band_db(x + 22050, 22050, 3200.0f, 8000.0f);
        const float low = band_db(y + 22050, 22050, 80.0f, 400.0f) - band_db(x + 22050, 22050, 80.0f, 400.0f);
        CHECK(fabsf(low) < 0.3f, "PRESENCE changes nothing below 400 Hz (%.2f dB, amp %.2f)", low, a ? 0.03f : 0.3f);
    }
    printf("PRESENCE lift 3.2-8 kHz at BETTER 100: %+.2f dB at 0.3, %+.2f dB at 0.03\n", lift[0], lift[1]);
    CHECK(lift[0] > 1.0f && lift[0] < 8.0f && fabsf(lift[0] - lift[1]) < 1.5f, "PRESENCE lifts the high band %+.1f dB, level independent within 1.5 dB (%+.1f dB 20 dB lower)", lift[0], lift[1]);
    /* TONE blends: warmth-only has no HF lift, presence-only has no extra second harmonic */
    { sine(x, N, 110.0f, 0.14f); preset(&p, 1, 100, 0, 100); render(&p, x, y, N); const float d = 20.0f * log10f(mag(y + 44100, 44100, 220.0f) / mag(y + 44100, 44100, 110.0f)); CHECK(d < -60.0f, "TONE 100 (presence only) adds no second harmonic to a pure low note (%.1f dB)", d); }
    /* LOUDER */
    sine(x, N, 110.0f, 0.1f); preset(&p, 1, 0, 100, 50); render(&p, x, y, N);
    { const float db = 20.0f * log10f(mag(y + 44100, 44100, 110.0f) / 0.1f); CHECK(fabsf(db - 12.0f) < 0.05f, "LOUDER 100 is +12 dB on a clean signal (%.3f dB)", db); }
    /* bounded and finite */
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f;
    preset(&p, 1, 100, 0, 50); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 2.2f, "a full-scale square at BETTER 100: finite and bounded (peak %.2f)", pk); }
    sine(x, N, 82.0f, 3.0f); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 6.0f, "3x over full scale: finite, the added part scales with the input (peak %.2f)", pk); }
    { unsigned r = 3u; for (int i = 0; i < N; i++) { r = r * 1664525u + 1013904223u; x[i] = ((float)(r >> 9) / 4194304.0f - 1.0f); } render(&p, x, y, N);
      int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 3.2f, "full-scale noise: finite and bounded (peak %.2f)", pk); }
    /* chords: the rectifier's difference tones below 100 Hz are held down (196/247 Hz: a 51 Hz difference) */
    { for (int i = 0; i < N; i++) x[i] = 0.07f * sinf(6.2831853f * 196.0f * (float)i / FS) + 0.07f * sinf(6.2831853f * 247.0f * (float)i / FS);
      preset(&p, 1, 100, 0, 0); render(&p, x, y, N); const float sub = mag(y + 44100, 44100, 51.0f), h2 = mag(y + 44100, 44100, 392.0f);
      CHECK(sub < 0.35f * h2, "a 196/247 Hz chord: the 51 Hz difference tone is held down (100 Hz four-pole) (%.4f against the second harmonic %.4f)", sub, h2); }
    /* no click on switching or a knob jump: the largest sample step stays within a few per cent of the steady renders' */
    sine(x, N, 220.0f, 0.3f);
    {
        px_preset_t pa, pb; preset(&pa, 1, 100, 50, 50); preset(&pb, 0, 100, 50, 50); render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.15f * fmaxf(sa, sb), "switching ENHANCE off and on mid-note: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
        preset(&pa, 1, 10, 0, 0); preset(&pb, 1, 100, 100, 100); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.15f * fmaxf(sa, sb), "a knob jump mid-note (BETTER 10 -> 100, TONE 0 -> 100, LOUDER 0 -> +12 dB) glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    /* a stored retired APPLAUSE stays bypassed */
    preset(&p, 1, 50, 50, 50); p.type[PX_B_SFX] = PX_SFX_APPLAUSE; off = p; off.on[PX_B_SFX] = 0; sine(x, N, 196.0f, 0.2f); render(&p, x, y, N); render(&off, x, z, N);
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "a stored APPLAUSE is still bypassed (%.2g)", m); }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("ENHANCE ok: 0 problems\n"); return 0;
}
