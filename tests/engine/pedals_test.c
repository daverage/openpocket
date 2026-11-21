/* SFX RANGEMASTER (engine/px_pedals.h) through the whole engine. The expected numbers come from the FULL nodal simulator of the same circuit (tools/research/pedals/rm_circuit.py,
 * checked against ElectroSmash's analysis: 38 dB at full volume, input corner about 2.6 kHz, bias Vbe 0.30 V / 0.2 mA / collector 7.1 V), not from this implementation.
 * Level: one engine unit = 2 V of pickup EMF, engine out = 0.048 per collector volt (so engine gain = circuit gain - 20.35 dB).
 *  - catalogue, silence, no DC, polarity (not inverted)
 *  - small-signal response of a single coil into the stage (the loading by the 10-12 kOhm input is part of the circuit)
 *  - level behaviour: clean when small, second harmonic grows, the gain falls (the stage compresses), asymmetric
 *  - PICKUP: a humbucker into the same stage has less treble; VOLUME is an attenuator after the clipping (its distortion does not change with it)
 *  - bounded, finite, no click on switching or a knob jump */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 2 };
static float x[N], y[N], z[N], ya[N], yb[N], yc[N];
static void preset(px_preset_t *p, int on, int volume, int pickup)
{
    px_preset_factory(p, 7); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_SFX] = (uint8_t)on; p->type[PX_B_SFX] = PX_SFX_RANGEMASTER; p->p[PX_B_SFX][0] = (uint8_t)volume; p->p[PX_B_SFX][1] = (uint8_t)pickup;
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
static void sine(float *o, int n, float hz, float amp) { for (int i = 0; i < n; i++) o[i] = amp * sinf(6.2831853f * hz * (float)i / FS); }
/* gain in dB of a sine through the stage (engine units), measured over whole cycles in the last second */
static float gain_db(float hz, float emf_mv, int volume, int pickup, float *h2, float *h3)
{
    const float amp = emf_mv * 1e-3f / PXP_EMF_PER_UNIT; px_preset_t p; sine(x, N, hz, amp); preset(&p, 1, volume, pickup); render(&p, x, y, N);
    const int cyc = (int)(44100.0f / hz), n = (int)((float)(44100 / cyc) * (float)cyc * 0 + 44100);
    const float a1 = mag(y + 44100, n, hz);
    if (h2) *h2 = 20.0f * log10f(mag(y + 44100, n, 2 * hz) / a1 + 1e-9f);
    if (h3) *h3 = 20.0f * log10f(mag(y + 44100, n, 3 * hz) / a1 + 1e-9f);
    return 20.0f * log10f(a1 / amp);
}
int main(void)
{
    px_preset_t p, off; char b[8];
    CHECK(px_type_retired(PX_B_SFX, PX_SFX_RANGEMASTER) == 0 && !strcmp(px_type_name(PX_B_SFX, PX_SFX_RANGEMASTER), "RANGEMASTER"), "RANGEMASTER is a live type");
    CHECK(px_nparams(PX_B_SFX, PX_SFX_RANGEMASTER) == 2 && !strcmp(px_param_name(PX_B_SFX, PX_SFX_RANGEMASTER, 0), "VOLUME") && !strcmp(px_param_name(PX_B_SFX, PX_SFX_RANGEMASTER, 1), "PICKUP"), "VOLUME and PICKUP");
    CHECK(px_param_text(PX_B_SFX, PX_SFX_RANGEMASTER, 1, 0, b) == 1 && !strcmp(b, "SINGLE") && px_param_text(PX_B_SFX, PX_SFX_RANGEMASTER, 1, 100, b) == 1 && !strcmp(b, "HUMBKR"), "PICKUP reads SINGLE .. HUMBKR: '%s'", b);
    /* silence, DC */
    preset(&p, 1, 100, 0); memset(x, 0, sizeof x); render(&p, x, y, N);
    { float m = 0; for (int i = N / 2; i < N; i++) m = fmaxf(m, fabsf(y[i])); CHECK(m < 1e-6f, "silence in, silence out (%.2g)", m); }
    sine(x, N, 1000.0f, 0.05f); render(&p, x, y, N);
    { double s = 0; for (int i = 44100; i < N; i++) s += y[i]; s /= 44100.0; CHECK(fabs(s) < 0.003, "no DC at 100 mV EMF (%.5f)", s); }
    /* polarity restored: at 1 kHz the full simulator's (inverting) output is -135 deg from the EMF, so the engine's is +45 deg, (the default circuit rate has no half-band delay) */
    { double xr = 0, xi = 0, yr = 0, yi = 0; for (int i = 44100; i < N; i++) { const double w = 6.283185307179586 * 1000.0 * i / FS; xr += x[i] * cos(w); xi -= x[i] * sin(w); yr += y[i] * cos(w); yi -= y[i] * sin(w); }
      double ph = (atan2(yi, yr) - atan2(xi, xr)) * 180.0 / 3.141592653589793 + (PXP_OS == 2 ? 19.0 * 1000.0 / 44100.0 * 360.0 : 0.0); while (ph > 180) ph -= 360; while (ph < -180) ph += 360;
      CHECK(fabs(ph - 45.0) < 25.0, "polarity restored: phase %.0f deg at 1 kHz after removing the half-band delay (reference +45)", ph); }
    /* small signal, single coil (6k, 2.5 H), volume max: MNA reference dB (circuit gain) - 20.35 */
    { const float f[] = {100, 300, 1000, 2600, 5000, 10000}, ref[] = {7.9f, 17.8f, 30.5f, 27.6f, 20.6f, 13.9f};
      for (int k = 0; k < 6; k++) { const float g = gain_db(f[k], 1.0f, 100, 0, 0, 0) + 20.35f;
        CHECK(fabsf(g - ref[k]) < 1.6f, "single coil, %.0f Hz: circuit gain %.1f dB against the reference %.1f dB", f[k], g, ref[k]); } }
    /* level behaviour at 1 kHz */
    { float h2a, h2b, h2c, h3c; const float ga = gain_db(1000, 3.0f, 100, 0, &h2a, 0), gb = gain_db(1000, 30.0f, 100, 0, &h2b, 0), gc = gain_db(1000, 300.0f, 100, 0, &h2c, &h3c);
      CHECK(h2a < -40.0f, "3 mV: clean (H2 %.1f dB)", h2a);
      CHECK(h2b > h2a + 10.0f && h2c > h2b + 10.0f, "the second harmonic grows with level: %.1f, %.1f, %.1f dB", h2a, h2b, h2c);
      CHECK(h2c > h3c + 3.0f, "even order dominates at 300 mV (H2 %.1f, H3 %.1f dB)", h2c, h3c);
      CHECK(ga - gc > 4.0f, "the stage compresses: gain %.1f dB at 3 mV, %.1f dB at 300 mV", ga, gc); }
    /* PICKUP: a humbucker's inductance and resistance against the 12 kOhm input */
    { const float s5 = gain_db(5000, 1.0f, 100, 0, 0, 0), h5 = gain_db(5000, 1.0f, 100, 100, 0, 0); CHECK(s5 - h5 > 4.0f, "a humbucker has less 5 kHz into the stage (single coil %.1f, humbucker %.1f dB)", s5, h5); }
    /* VOLUME: an attenuator after the clipping */
    { float h2a, h2b; const float g100 = gain_db(1000, 100.0f, 100, 0, &h2a, 0), g50 = gain_db(1000, 100.0f, 50, 0, &h2b, 0);
      CHECK(g100 - g50 > 14.0f && g100 - g50 < 26.0f, "VOLUME 50 is about -21 dB on a 10k log pot (%.1f dB)", g100 - g50);
      CHECK(fabsf(h2a - h2b) < 1.0f, "the volume does not change the distortion (H2 %.1f / %.1f dB)", h2a, h2b); }
    /* the level at the pedal: guitar peaks 0.14 engine units (280 mV EMF) */
    sine(x, N, 440.0f, 0.14f); preset(&p, 1, 100, 0); render(&p, x, y, N);
    { float pk = 0; for (int i = 44100; i < N; i++) pk = fmaxf(pk, fabsf(y[i])); CHECK(pk > 0.05f && pk < 0.5f, "a hard-played note comes out at a usable level (peak %.3f)", pk); }
    /* bounded, finite */
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f; preset(&p, 1, 100, 50); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 1.0f, "a full-scale square: finite and bounded (peak %.3f)", pk); }
    { unsigned r = 3u; for (int i = 0; i < N; i++) { r = r * 1664525u + 1013904223u; x[i] = ((float)(r >> 9) / 4194304.0f - 1.0f) * 0.5f; } render(&p, x, y, N);
      int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 1.0f, "loud noise: finite and bounded (peak %.3f)", pk); }
    /* no click on switching or a knob jump */
    sine(x, N, 220.0f, 0.1f);
    {
        px_preset_t pa, pb; preset(&pa, 1, 80, 20); preset(&pb, 0, 80, 20); render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "switching RANGEMASTER off and on mid-note: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
        preset(&pa, 1, 30, 0); preset(&pb, 1, 100, 100); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "a knob jump mid-note (VOLUME 30 -> 100, PICKUP 0 -> 100) glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("PEDALS ok: 0 problems\n"); return 0;
}
