/* PITCH: POLY SHIFTER (v75, engine/px_pv.h): a phase-vocoder pitch shifter for chords.
 *  - the catalogue: SHIFT (semitones), FINE (cents), MIX, ATTACK
 *  - a tone lands on the shifted frequency (+12, -12, +7, +50 cents) and the original is gone; a chord's three notes each land, with nothing between them
 *  - no shift is the input, PV_LAT samples late and at its level (so MIX can blend dry and wet without a comb): a tone, a decaying chord
 *  - bounded and finite on silence, noise and clicks at every shift; the level of a shifted tone */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 3 };
static float x[N], y[N];
static px_engine eng;
static void shifter(px_preset_t *p, int shift01, int fine, int mix, int attack)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_PITCH] = 1; p->type[PX_B_PITCH] = PX_PIT_POLYSHIFT;
    p->p[PX_B_PITCH][0] = (uint8_t)shift01; p->p[PX_B_PITCH][1] = (uint8_t)fine; p->p[PX_B_PITCH][2] = (uint8_t)mix; p->p[PX_B_PITCH][3] = (uint8_t)attack;
}
static int sem(int st) { return (int)((float)(st + 12) / 24.0f * 100.0f + 0.5f); }      /* SHIFT 0..100 for st semitones */
static void render(const px_preset_t *p, int n)
{
    px_engine_init(&eng, FS); px_engine_set(&eng, p);
    for (int at = 0; at + 64 <= n; at += 64) px_engine_process(&eng, x + at, y + at, 64);
}
static float mag(const float *s, int n, float hz)
{
    double re = 0, im = 0; for (int i = 0; i < n; i++) { const double w = 6.283185307179586 * hz * i / FS; re += s[i] * cos(w); im += s[i] * sin(w); }
    return (float)(sqrt(re * re + im * im) * 2.0 / n);
}
static float rms(const float *s, int a, int b) { double e = 0; for (int i = a; i < b; i++) e += (double)s[i] * s[i]; return (float)sqrt(e / (b - a)); }
/* the share of the output's energy within 2 % of hz (over the last two seconds) */
static float share(float hz)
{
    const float tot = rms(y, N - 88200, N); float e = 0;
    for (float f = hz * 0.98f; f <= hz * 1.02f; f += hz * 0.004f) { const float m = mag(y + N - 88200, 88200, f); e = fmaxf(e, m); }
    return e / (tot * 1.41421356f + 1e-9f);
}
static void sines(const float *hz, int n, float amp) { for (int i = 0; i < N; i++) { float v = 0; for (int k = 0; k < n; k++) v += amp * sinf(6.2831853f * hz[k] * (float)i / FS + (float)k); x[i] = v; } }
int main(void)
{
    char b[16]; px_preset_t p;
    CHECK(PX_PIT_COUNT == 7 && !strcmp(px_type_name(PX_B_PITCH, PX_PIT_POLYSHIFT), "POLY SHIFTER") && px_nparams(PX_B_PITCH, PX_PIT_POLYSHIFT) == 4 &&
          !strcmp(px_param_name(PX_B_PITCH, PX_PIT_POLYSHIFT, 0), "SHIFT") && !strcmp(px_param_name(PX_B_PITCH, PX_PIT_POLYSHIFT, 1), "FINE") &&
          !strcmp(px_param_name(PX_B_PITCH, PX_PIT_POLYSHIFT, 2), "MIX") && !strcmp(px_param_name(PX_B_PITCH, PX_PIT_POLYSHIFT, 3), "ATTACK"), "POLY SHIFTER: the catalogue");
    CHECK(px_param_text(PX_B_PITCH, PX_PIT_POLYSHIFT, 0, 0, b) == 1 && !strcmp(b, "-12") && px_param_text(PX_B_PITCH, PX_PIT_POLYSHIFT, 0, 50, b) == 1 && !strcmp(b, "0") &&
          px_param_text(PX_B_PITCH, PX_PIT_POLYSHIFT, 0, 100, b) == 1 && !strcmp(b, "+12"), "SHIFT reads -12 .. 0 .. +12 ('%s')", b);
    CHECK(px_param_text(PX_B_PITCH, PX_PIT_POLYSHIFT, 1, 0, b) == 1 && !strcmp(b, "-50") && px_param_text(PX_B_PITCH, PX_PIT_POLYSHIFT, 1, 100, b) == 1 && !strcmp(b, "+50"), "FINE reads -50 .. +50 cents ('%s')", b);

    /* a tone */
    { const struct { int st, fine; float hz; const char *name; } T[] = {{12, 50, 880.0f, "+12"}, {-12, 50, 220.0f, "-12"}, {7, 50, 659.255f, "+7"}, {5, 50, 587.330f, "+5"},
                                   {0, 100, 452.893f, "+50 cents"}, {-5, 50, 329.628f, "-5"}};
      const float f0[1] = {440.0f};
      for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
          sines(f0, 1, 0.1f); shifter(&p, sem(T[i].st), T[i].fine, 100, 50); render(&p, N);
          const float s = share(T[i].hz), o = share(440.0f);
          CHECK(s > 0.85f && (T[i].st == 0 || o < 0.05f), "a 440 Hz tone %s: %.0f %% of the energy at %.1f Hz, %.1f %% left at 440 Hz", T[i].name, s * 100.0f, T[i].hz, o * 100.0f);
      } }

    /* a chord of partials at least 4 bins (170 Hz) apart, which a 1024-point frame resolves (a low chord's partials sit closer and blur: see the header): up an octave */
    { const float ch[3] = {250.0f, 450.0f, 700.0f}; sines(ch, 3, 0.05f); shifter(&p, sem(12), 50, 100, 50); render(&p, N);
      const float tot = rms(y, N - 88200, N) * 1.41421356f; float in = 0;
      for (int k = 0; k < 3; k++) { const float s = share(ch[k] * 2.0f); in += s * s; CHECK(share(ch[k] * 2.0f) > 0.25f, "chord +12: note %d lands at %.1f Hz (%.0f %% of the energy)", k, ch[k] * 2.0f, share(ch[k] * 2.0f) * 100.0f); }
      CHECK(in > 0.35f && share(250.0f) < 0.05f && share(450.0f) < 0.05f, "chord +12: the three notes carry the energy (%.2f), the originals are gone", in);
      (void)tot; }
    { const float ch[3] = {250.0f, 450.0f, 700.0f}; sines(ch, 3, 0.05f); shifter(&p, sem(-7), 50, 100, 50); render(&p, N);
      CHECK(share(250.0f * 0.667420f) > 0.2f && share(450.0f * 0.667420f) > 0.2f && share(700.0f * 0.667420f) > 0.2f, "chord -7 (a fifth down): each note lands (%.2f %.2f %.2f)",
            share(250.0f * 0.667420f), share(450.0f * 0.667420f), share(700.0f * 0.667420f)); }

    /* no shift: the input, PV_LAT late and at its level; MIX 0 the same */
    { const float ch[3] = {250.0f, 450.0f, 700.0f}; sines(ch, 3, 0.05f);
      for (int k = 0; k < 2; k++) {
          shifter(&p, 50, 50, k ? 0 : 100, 50); render(&p, N);
          float best = 0; int lag = 0;
          for (int l = PV_LAT - 40; l <= PV_LAT + 40; l++) { double c = 0, a = 0, d = 0; for (int i = 66150; i < N - 100; i++) { c += (double)x[i - l] * y[i]; a += (double)x[i - l] * x[i - l]; d += (double)y[i] * y[i]; } const float r = (float)(c / sqrt(a * d)); if (r > best) { best = r; lag = l; } }
          const float g = 20.0f * log10f(rms(y, 66150, N - 100) / rms(x, 66150, N - 100));
          CHECK(best > (k ? 0.999f : 0.95f) && abs(lag - PV_LAT) <= 4 && fabsf(g) < (k ? 0.1f : 2.0f), "no shift, MIX %d: the input %d samples late (%d), correlation %.4f, gain %.2f dB", k ? 0 : 100, lag, PV_LAT, best, g);
      } }
    /* a decaying chord with a noisy pick: still the input, still late */
    { unsigned s = 3u; for (int i = 0; i < N; i++) { const float t = (float)(i % 22050) / FS, env = expf(-t * 4.0f); s = s * 1664525u + 1013904223u;
          x[i] = env * 0.08f * (sinf(6.2831853f * 250.0f * (float)i / FS) + 0.7f * sinf(6.2831853f * 450.0f * (float)i / FS) + 0.5f * sinf(6.2831853f * 700.0f * (float)i / FS)) + ((float)(s >> 9) / 8388608.0f - 1.0f) * 0.002f; }
      shifter(&p, 50, 50, 100, 50); render(&p, N);
      double c = 0, a = 0, d = 0; for (int i = 66150; i < N - 100; i++) { c += (double)x[i - PV_LAT] * y[i]; a += (double)x[i - PV_LAT] * x[i - PV_LAT]; d += (double)y[i] * y[i]; }
      CHECK(c / sqrt(a * d) > 0.97, "no shift on picked, decaying chords: correlation with the late input %.4f", c / sqrt(a * d)); }

    /* level of a shifted tone: within 2 dB of the input (the bins move whole, so the frequency sits up to half a bin off their shape: up to about 2 dB) */
    { const float f0[1] = {330.0f}; sines(f0, 1, 0.1f);
      for (int st = -12; st <= 12; st += 3) { shifter(&p, sem(st), 50, 100, 50); render(&p, N);
          const float g = 20.0f * log10f(rms(y, N - 88200, N) / rms(x, N - 88200, N)); CHECK(fabsf(g) < 2.0f, "level at %+d semitones: %.2f dB", st, g); } }

    /* bounded and finite */
    { unsigned s = 9u;
      for (int shift = 0; shift <= 100; shift += 25) for (int sig = 0; sig < 3; sig++) {
          for (int i = 0; i < N; i++) { s = s * 1664525u + 1013904223u; const float nz = ((float)(s >> 9) / 8388608.0f - 1.0f); x[i] = sig == 0 ? 0.0f : sig == 1 ? 0.5f * nz : (i % 5000 == 0 ? 0.9f : 0.0f); }
          shifter(&p, shift, shift, 100, shift); render(&p, N);
          float mx = 0; int fin = 1; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; if (fabsf(y[i]) > mx) mx = fabsf(y[i]); }
          CHECK(fin && mx < (sig == 1 ? 3.0f : 1.5f), "bounded and finite: SHIFT %d on %s (peak %.2f)", shift, sig == 0 ? "silence" : sig == 1 ? "noise" : "clicks", mx); } }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("POLY SHIFTER ok: 0 problems\n"); return 0;
}
