/* The global settings (v75: px_global_t, px_engine_globals): outside the presets.
 *  - all at their defaults: the engine's output is bit for bit what it was
 *  - the input high-pass: 40 Hz cut, the notes above untouched, down to 25 Hz for a bass
 *  - the output EQ: each of the five bands moves its own range by its gain and leaves the rest
 *  - the limiter: a loud signal stays under its ceiling, a quiet one is not touched
 *  - bounded and finite at the extremes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 2 };
static float x[N], y[N], z[N];
static px_engine e1, e0;
static void flat(px_preset_t *p) { px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0; }
static void run(px_engine *e, const px_global_t *g, const float *in, float *out)
{
    px_preset_t p; flat(&p); px_engine_init(e, FS); px_engine_set(e, &p); if (g) px_engine_globals(e, g);
    for (int at = 0; at + 64 <= N; at += 64) px_engine_process(e, in + at, out + at, 64);
}
static float mag(const float *s, int n, float hz) { double re = 0, im = 0; for (int i = 0; i < n; i++) { const double w = 6.283185307179586 * hz * i / FS; re += s[i] * cos(w); im += s[i] * sin(w); } return (float)(sqrt(re * re + im * im) * 2.0 / n); }
static void sine(float hz, float amp) { for (int i = 0; i < N; i++) x[i] = amp * sinf(6.2831853f * hz * (float)i / FS); }
static float gain_db(const px_global_t *g, float hz)
{
    sine(hz, 0.1f); run(&e1, g, x, y); run(&e0, 0, x, z);
    return 20.0f * log10f(mag(y + 44100, 44100, hz) / (mag(z + 44100, 44100, hz) + 1e-9f) + 1e-9f);
}
int main(void)
{
    px_global_t g;
    /* defaults: nothing runs */
    { unsigned s = 5u; for (int i = 0; i < N; i++) { s = s * 1664525u + 1013904223u; x[i] = 0.3f * ((float)(s >> 9) / 8388608.0f - 1.0f); }
      px_global_default(&g); run(&e1, &g, x, y); run(&e0, 0, x, z);
      CHECK(!memcmp(y, z, sizeof y) && e1.g_hp_on == 0 && e1.g_eq_n == 0 && e1.g_lim_c == 0.0f, "default globals: the output is bit for bit the same, nothing running"); }
    /* the input high-pass */
    px_global_default(&g); g.hpf = 80;
    { const float a = gain_db(&g, 40.0f), b = gain_db(&g, 400.0f), c = gain_db(&g, 160.0f);
      CHECK(a < -9.0f && fabsf(b) < 0.1f && c > -3.5f && c < 0.0f, "HPF 80 Hz: 40 Hz %.1f dB, 160 Hz %.1f, 400 Hz %.2f", a, c, b); }
    g.hpf = 25;
    { const float a = gain_db(&g, 12.0f), b = gain_db(&g, 41.2f), c = gain_db(&g, 82.4f);
      CHECK(a < -5.0f && fabsf(b) < 1.5f && fabsf(c) < 0.2f, "HPF 25 Hz (a bass): 12 Hz %.1f dB, E1 %.2f, E2 %.2f", a, b, c); }
    g.hpf = 10; px_engine_globals(&e1, &g); CHECK(e1.glob.hpf == 20, "HPF below 20 Hz is clamped to 20");
    /* the output EQ: band i at +6 dB (36) or -12 dB (0) */
    { const float F[5] = {40.0f, 250.0f, 1000.0f, 3500.0f, 15000.0f};
      for (int i = 0; i < 5; i++) {
          px_global_default(&g); g.eq[i] = 36;
          const float at = gain_db(&g, F[i]);
          CHECK(at > 4.5f && at < 6.5f, "EQ band %d +6 dB: %.2f dB at %.0f Hz", i, at, F[i]);
          for (int j = 0; j < 5; j++) if (abs(i - j) >= 2) { const float far = gain_db(&g, F[j]); CHECK(fabsf(far) < 1.2f, "EQ band %d +6 dB: %.2f dB at %.0f Hz, two bands away", i, far, F[j]); }
          g.eq[i] = 0; const float cut = gain_db(&g, F[i]); CHECK(cut < -10.0f && cut > -13.0f, "EQ band %d -12 dB: %.2f dB", i, cut); } }
    /* the limiter: ceiling -4 dB (lim 8) = 0.631 */
    px_global_default(&g); g.lim = 8;
    { sine(1000.0f, 0.9f); run(&e1, &g, x, y);
      float pk = 0; for (int i = 44100; i < N; i++) if (fabsf(y[i]) > pk) pk = fabsf(y[i]);
      CHECK(pk < 0.631f * 1.03f && pk > 0.631f * 0.9f, "limiter -4 dB: a 0.9 sine peaks at %.3f (ceiling 0.631)", pk);
      sine(1000.0f, 0.2f); run(&e1, &g, x, y); run(&e0, 0, x, z);
      CHECK(fabsf(mag(y + 44100, 44100, 1000.0f) / mag(z + 44100, 44100, 1000.0f) - 1.0f) < 0.005f, "limiter: a quiet signal is untouched"); }
    /* extremes */
    { px_global_default(&g); g.hpf = 255; g.lim = 255; for (int i = 0; i < 5; i++) g.eq[i] = (i & 1) ? 255 : 0; sine(500.0f, 0.8f); run(&e1, &g, x, y);
      float mx = 0; int fin = 1; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; if (fabsf(y[i]) > mx) mx = fabsf(y[i]); }
      CHECK(fin && mx < 2.0f, "extreme values are clamped: finite, peak %.2f", mx); }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("GLOBAL SETTINGS ok: 0 problems\n"); return 0;
}
