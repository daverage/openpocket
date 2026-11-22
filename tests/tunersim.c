/* The tuner's measurement (firmware/px_tuner.h, v75) on synthetic strings:
 *  - every note from a 5-string bass's low B (31 Hz) to 1.5 kHz reads, in tune within 1.5 cents (3 up to 1.5 kHz), detuned by -20 / +20 cents too,
 *    with a weak fundamental, with mains hum, with noise
 *  - silence and noise read nothing
 *  - the smoother: steady against jitter, quick on a new note, clear after a gap
 *  - tn_note: the note and the cents against a reference A from 415 to 466 Hz */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define TN_MEM
#define TN_READY 1
#include "../firmware/px_tuner.h"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
enum { N = 44100 };
static float buf[N];
static unsigned rs = 17u;
static float rnd(void) { rs = rs * 1664525u + 1013904223u; return (float)(rs >> 9) / 8388608.0f - 1.0f; }
static void string_(float f0, float weak, float hum, float noise)   /* a plucked string: inharmonic partials, a weak fundamental if asked, hum, noise */
{
    float ph[10]; for (int h = 0; h < 10; h++) ph[h] = rnd() * 3.14159f;
    for (int i = 0; i < N; i++) {
        float v = 0, t = (float)i / 44100.0f;
        for (int h = 1; h <= 8; h++) {
            const float f = f0 * (float)h * sqrtf(1.0f + 0.0001f * (float)(h * h));
            if (f > 20000.0f) break;
            v += (h == 1 ? weak : 1.0f / (float)h) * sinf(6.2831853f * f * t + ph[h]);
        }
        buf[i] = 0.1f * v * expf(-t * 0.5f) + hum * sinf(6.2831853f * 50.0f * t) + noise * rnd();
    }
}
static float read_now(void)
{
    memset(tn_buf, 0, sizeof tn_buf); tn_w = 0; tn_hp = tn_ph = 0; memset(tn_hist, 0, sizeof tn_hist);
    for (int k = 0; k + 128 <= N; k += 128) tn_capture(buf + k, 128);
    return tuner_estimate();
}
int main(void)
{
    const struct { const char *n; float hz; } NOTE[] = {{"B0", 30.87f}, {"E1", 41.20f}, {"A1", 55.0f}, {"B1", 61.74f}, {"E2", 82.41f}, {"A2", 110.0f}, {"D3", 146.83f}, {"G3", 196.0f},
        {"B3", 246.94f}, {"E4", 329.63f}, {"A4", 440.0f}, {"E5", 659.26f}, {"A5", 880.0f}, {"E6", 1318.5f}, {"G6", 1568.0f}};
    for (unsigned i = 0; i < sizeof NOTE / sizeof NOTE[0]; i++)
        for (int dc = -20; dc <= 20; dc += 20) {
            const float f = NOTE[i].hz * exp2f((float)dc / 1200.0f);
            string_(f, 1.0f, 0.0f, 0.0005f);
            const float hz = read_now(), cents = hz > 0 ? 1200.0f * log2f(hz / f) : 0;
            const float lim = NOTE[i].hz < 700.0f ? 1.5f : 3.0f;
            if (NOTE[i].hz > 1500.0f && dc > 0) continue;                  /* (above the range) */
            CHECK(hz > 0 && fabsf(cents) < lim, "%s %+d cents: reads %.2f Hz (%+.2f cents off, limit %.1f)", NOTE[i].n, dc, hz, cents, lim);
        }
    /* a weak fundamental (a low E on a small pickup), hum, noise */
    for (unsigned i = 0; i < 8; i++) {
        const float f = NOTE[i].hz;
        string_(f, 0.25f, 0.0f, 0.0005f); float hz = read_now(); float c = hz > 0 ? 1200.0f * log2f(hz / f) : 99;
        CHECK(fabsf(c) < 3.0f, "%s with a fundamental 12 dB down: %+.2f cents", NOTE[i].n, c);
        string_(f, 1.0f, 0.001f, 0.001f); hz = read_now(); c = hz > 0 ? 1200.0f * log2f(hz / f) : 99;
        CHECK(fabsf(c) < 3.0f, "%s with 50 Hz hum (-40 dB) and noise: %+.2f cents", NOTE[i].n, c);
    }
    /* silence and noise */
    memset(buf, 0, sizeof buf); CHECK(read_now() == 0.0f, "silence reads nothing");
    for (int i = 0; i < N; i++) buf[i] = 0.05f * rnd(); { const float hz = read_now(); CHECK(hz == 0.0f, "noise reads nothing (%.1f Hz)", hz); }

    /* the smoother */
    { tn_sm_t s; memset(&s, 0, sizeof s); float lo = 1e9f, hi = 0;
      for (int i = 0; i < 20; i++) { const float o = tn_smooth(&s, 440.0f * exp2f((float)((i % 3) - 1) * 4.0f / 1200.0f)); if (i >= 6) { if (o < lo) lo = o; if (o > hi) hi = o; } }
      CHECK(1200.0f * log2f(hi / lo) < 3.5f, "smoother: +-4 cents of jitter shows within 3.5 cents (%.2f)", 1200.0f * log2f(hi / lo));
      const float j = tn_smooth(&s, 440.0f * exp2f(2.0f / 12.0f));
      CHECK(fabsf(1200.0f * log2f(j / (440.0f * exp2f(2.0f / 12.0f)))) < 2.0f, "smoother: a note 2 semitones away is shown at once (%.1f Hz)", j);
      float o = 1; tn_smooth(&s, 0); tn_smooth(&s, 0); CHECK(tn_smooth(&s, 0) == 0.0f, "smoother: three missed readings clear it");
      (void)o; }
    /* the note and the cents against a reference */
    { int n; float c;
      tn_note(440.0f, 440.0f, &n, &c); CHECK(n == 9 && fabsf(c) < 0.01f, "A 440 against 440: A, %.3f", c);
      tn_note(432.0f, 432.0f, &n, &c); CHECK(n == 9 && fabsf(c) < 0.01f, "432 against 432: A");
      tn_note(440.0f, 432.0f, &n, &c); CHECK(n == 9 && fabsf(c - 31.77f) < 0.1f, "440 against 432: A +31.8 (%.2f)", c);
      tn_note(415.305f, 440.0f, &n, &c); CHECK(n == 8 && fabsf(c) < 0.01f, "G# 415.3 against A=440: G#, %.3f", c);
      tn_note(466.0f, 466.0f, &n, &c); CHECK(n == 9, "466 against 466: A");
      tn_note(82.41f, 440.0f, &n, &c); CHECK(n == 4 && fabsf(c) < 0.5f, "E2: E (%d), %.2f", n, c);
      tn_note(10.0f, 440.0f, &n, &c); CHECK(n == -1, "10 Hz: no note"); }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("TUNER ok: 0 problems\n"); return 0;
}
