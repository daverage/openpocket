/* Integrated HUM: notch rejection, wanted-signal preservation, AUTO and finite bounds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 8 };
static float x[N], y[N];
static px_engine eng;
static void hum(px_preset_t *p, int on, int mains, int depth, int width, int harm)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_GATE] = (uint8_t)on; p->type[PX_B_GATE] = PX_GATE_SUPPRESSOR;
    p->p[PX_B_GATE][0] = 0; p->p[PX_B_GATE][2] = mains < 34 ? 26 : mains > 66 ? 52 : 78;
    p->p[PX_B_GATE][3] = 0;

}
static void render(const px_preset_t *p, const float *in, float *out, int n)
{
    px_engine_init(&eng, FS); px_engine_set(&eng, p);
    for (int at = 0; at + 64 <= n; at += 64) px_engine_process(&eng, in + at, out + at, 64);
}
static float mag(const float *s, int n, float hz)
{
    double re = 0, im = 0; for (int i = 0; i < n; i++) { const double w = 6.283185307179586 * hz * i / FS; re += s[i] * cos(w); im += s[i] * sin(w); }
    return (float)(sqrt(re * re + im * im) * 2.0 / n);
}
static void sine(float *o, int n, float hz, float amp) { for (int i = 0; i < n; i++) o[i] = amp * sinf(6.2831853f * hz * (float)i / FS); }
static float gain_db(const px_preset_t *p, float hz)        /* a sine through the engine, the last second, in dB against the same with the block off */
{
    px_preset_t off = *p; off.on[PX_B_GATE] = 0;
    sine(x, 44100 * 2, hz, 0.05f); render(p, x, y, 44100 * 2);
    static float z[N]; render(&off, x, z, 44100 * 2);
    return 20.0f * log10f(mag(y + 44100, 44100, hz) / (mag(z + 44100, 44100, hz) + 1e-9f) + 1e-9f);
}
static void mains_hum(float *o, int n, int mains, float amp)   /* a hum: the mains and its next harmonics, with a little noise */
{
    unsigned s = 12345u;
    for (int i = 0; i < n; i++) {
        float v = 0;
        for (int h = 1; h <= 3; h++) v += amp / (float)h * sinf(6.2831853f * (float)(mains * h) * (float)i / FS + (float)h);
        s = s * 1664525u + 1013904223u; v += ((float)(s >> 9) / 8388608.0f - 1.0f) * amp * 0.05f;
        o[i] = v;
    }
}
static void note(float *o, int n, float hz, float amp)          /* a plucked-string-like tone: harmonics 1..8 */
{
    for (int i = 0; i < n; i++) { float v = 0; for (int h = 1; h <= 8; h++) v += amp / (float)h * sinf(6.2831853f * hz * (float)h * (float)i / FS); o[i] = v; }
}
int main(void)
{
    char b[16]; px_preset_t p;
    CHECK(PX_EQ_COUNT == 3 && px_nparams(PX_B_GATE, PX_GATE_SUPPRESSOR) == 4, "integrated catalogue");
    CHECK(px_param_text(PX_B_GATE, PX_GATE_SUPPRESSOR, 2, 0, b) && !strcmp(b, "OFF"), "HUM off label");
    /* fixed mains: the notches, and what must pass (DEPTH 65 = about 27 dB, WIDTH 16 = 4 Hz, HARMONICS 50 = three notches) */
    hum(&p, 1, 0, 65, 16, 50);
    { const float h1 = gain_db(&p, 50), h2 = gain_db(&p, 100), h3 = gain_db(&p, 150), e = gain_db(&p, 82.4f), a = gain_db(&p, 110), m = gain_db(&p, 400), h4 = gain_db(&p, 200);
      CHECK(h1 < -22 && h2 < -22 && h3 < -22, "MAINS 50: 50 / 100 / 150 Hz cut by more than 22 dB (%.1f %.1f %.1f)", h1, h2, h3);
      CHECK(fabsf(e) < 0.6f && fabsf(a) < 0.6f && fabsf(m) < 0.1f && fabsf(h4) < 0.3f, "MAINS 50: a low E (%.2f dB), an A (%.2f), 400 Hz (%.2f) and 200 Hz (%.2f) pass", e, a, m, h4); }
    hum(&p, 1, 100, 65, 16, 50);
    { const float h1 = gain_db(&p, 60), h2 = gain_db(&p, 120), h3 = gain_db(&p, 180), e = gain_db(&p, 82.4f), a = gain_db(&p, 110), m = gain_db(&p, 400), h5 = gain_db(&p, 50);
      CHECK(h1 < -22 && h2 < -22 && h3 < -22, "MAINS 60: 60 / 120 / 180 Hz cut by more than 22 dB (%.1f %.1f %.1f)", h1, h2, h3);
      CHECK(fabsf(e) < 0.6f && fabsf(a) < 0.6f && fabsf(m) < 0.1f && fabsf(h5) < 3.0f, "MAINS 60: a low E (%.2f dB), an A (%.2f), 400 Hz (%.2f) pass; 50 Hz is mostly left (%.2f)", e, a, m, h5); }
    /* AUTO */
    hum(&p, 1, 50, 65, 16, 50); p.p[PX_B_GATE][0] = 100;
    for (int k = 0; k < 2; k++) {
        const int mains = k ? 50 : 60;
        mains_hum(x, N, mains, 0.02f);
        render(&p, x, y, N);
        const float cut = 20.0f * log10f((mag(y + N - 44100 * 2, 44100 * 2, (float)mains) + 1e-9f) / mag(x + N - 44100 * 2, 44100 * 2, (float)mains));
        CHECK(eng.hum_hz == mains && cut < -15.0f, "AUTO: a %d Hz hum settles on %d Hz (chose %d) and is cut by %.1f dB", mains, mains, eng.hum_hz, cut);
    }
    {   /* a hum that changes country: 50 Hz for 3 s, then 60 Hz */
        mains_hum(x, 44100 * 4, 50, 0.02f); mains_hum(x + 44100 * 4, 44100 * 4, 60, 0.02f);
        render(&p, x, y, N);
        const float cut = 20.0f * log10f((mag(y + N - 44100 * 2, 44100 * 2, 60.0f) + 1e-9f) / mag(x + N - 44100 * 2, 44100 * 2, 60.0f));
        CHECK(eng.hum_hz == 60 && cut < -15.0f, "AUTO: follows a change from 50 to 60 Hz (chose %d, %.1f dB)", eng.hum_hz, cut);
    }
    {   /* notes and no hum: the mains stays where it was */
        note(x, N, 82.41f, 0.1f); render(&p, x, y, N);
        CHECK((eng.hum_hz == 0 || eng.hum_hz == 50), "AUTO: a low E with no hum leaves it on 50 (chose %d)", eng.hum_hz);
        note(x, N, 110.0f, 0.1f); render(&p, x, y, N);
        CHECK((eng.hum_hz == 0 || eng.hum_hz == 50), "AUTO: an A with no hum leaves it on 50 (chose %d)", eng.hum_hz);
        note(x, N, 98.0f, 0.1f); render(&p, x, y, N);
        CHECK((eng.hum_hz == 0 || eng.hum_hz == 50), "AUTO: a G with no hum leaves it on 50 (chose %d)", eng.hum_hz);
    }
    /* extremes: bounded and finite */
    for (int m = 0; m <= 100; m += 50) for (int d = 0; d <= 100; d += 100) for (int w = 0; w <= 100; w += 100) for (int h = 0; h <= 100; h += 100) {
        hum(&p, 1, m, d, w, h); mains_hum(x, 44100, m > 66 ? 60 : 50, 0.5f); render(&p, x, y, 44100);
        float mx = 0; int fin = 1; for (int i = 0; i < 44100; i++) { if (!isfinite(y[i])) fin = 0; if (fabsf(y[i]) > mx) mx = fabsf(y[i]); }
        CHECK(fin && mx < 4.0f, "bounded and finite at MAINS %d DEPTH %d WIDTH %d HARMONICS %d (peak %.2f)", m, d, w, h, mx);
    }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("Integrated HUM ok: 0 problems\n"); return 0;
}
