/* Synth2 stage 3: ANALOG (Felucca) through the OpenPocket adapter (engine/px_synth_adapter.h), on synthetic plucks, through the whole engine.
 * Synthetic notes check the plumbing; whether it plays well is for a pedal and an ear (docs/SYNTH2-PLAN.md).
 *  - pitch: the synth plays the guitar's note (CLASSIC) and an octave below (SQ BASS), within 20 cents, from E2 up
 *  - a slide moves the pitch continuously (no semitone grid); a repicked note retriggers; the note releases after the guitar stops
 *  - the same five bytes give the same sound (fresh engines), and the sound does not depend on the call's block size (64, 48, 16, 1)
 *  - every patch at 0 / 50 / 100 of each macro: finite, bounded, and silent on silence
 *  - patch bands: slot = value / 10, 100 is the last slot; switching a patch while playing stays finite */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
#define FS_ 44100.0f
static px_engine e;
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
static float cents(float a, float b) { return 1200.0f * log2f(a / b); }
static void preset_of(px_preset_t *p, const uint8_t *q)
{
    px_preset_factory(p, 7);
    for (int z = 0; z < PX_NBLK; z++) p->on[z] = 0;
    p->on[PX_B_SYN] = 1; p->type[PX_B_SYN] = PX_SYN_ANALOG; memcpy(p->p[PX_B_SYN], q, 5);
}
/* a plucked string: harmonic sines with a decay; notes (hz, start s); glide to hz_end for note 0 */
static void string(float *x, int N, const float *hz, const float *st, int n, float amp, float decay, float hz_end)
{
    float ph[4] = {0}, env[4] = {0};
    for (int i = 0; i < N; i++) {
        float v = 0;
        for (int k = 0; k < n; k++) {
            if (i == (int)(st[k] * FS_)) env[k] = amp;
            float f = hz[k]; if (hz_end > 0 && k == 0) f += (hz_end - hz[0]) * (float)i / (float)N;
            ph[k] += f / FS_; ph[k] -= floorf(ph[k]);
            const float p = ph[k] * 6.2831853f;
            v += env[k] * (sinf(p) + 0.6f * sinf(2 * p) + 0.45f * sinf(3 * p) + 0.3f * sinf(4 * p) + 0.2f * sinf(5 * p));
            env[k] *= decay;
        }
        x[i] = 0.4f * v;
    }
}
static void render(const px_preset_t *p, const float *x, float *y, int N, int bs)
{
    px_engine_init(&e, FS_); px_engine_set(&e, p);
    for (int at = 0; at + bs <= N; at += bs) px_engine_process(&e, x + at, y + at, bs);
}
/* the fundamental of y[a..a+len): the autocorrelation peak between lo and hi Hz, refined; 0 when there is none */
static float f0_of(const float *y, int a, int len, float lo, float hi)
{
    double best = 0, e0 = 0; int bl = 0;
    for (int i = 0; i < len; i++) e0 += (double)y[a + i] * y[a + i];
    if (e0 < 1e-9) return 0;
    const int l0 = (int)(FS_ / hi), l1 = (int)(FS_ / lo);
    for (int l = l0; l <= l1 && l < len / 2; l++) {
        double c = 0; for (int i = 0; i + l < len; i++) c += (double)y[a + i] * y[a + i + l];
        c /= e0; if (c > best) { best = c; bl = l; }
    }
    return bl ? FS_ / (float)bl : 0;
}
static float rms(const float *y, int a, int len) { double s = 0; for (int i = 0; i < len; i++) s += (double)y[a + i] * y[a + i]; return (float)sqrt(s / len); }
#define N_ (44100 * 5)
static float x[N_], y[N_], y2[N_];
int main(void)
{
    { const char *nm[2] = {"CLASSIC", "SQ BASS"}; const uint8_t slot[2] = {0, 40}; const int sem[2] = {0, -12};
      static const float NOTES[3] = {82.41f, 164.81f, 329.63f}, ST[1] = {0.1f};
      for (int b = 0; b < 2; b++) for (int k = 0; k < 3; k++) {
          const uint8_t q[5] = {slot[b], 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
          string(x, N_, &NOTES[k], ST, 1, 0.5f, 0.99997f, 0); render(&p, x, y, N_, 64);
          const float want = NOTES[k] * (sem[b] ? 0.5f : 1.0f), got = f0_of(y, (int)(0.6f * FS_), 4096, want * 0.7f, want * 1.4f);
          printf("%s on %.1f Hz: plays %.2f Hz (want %.2f, %.1f cents)\n", nm[b], NOTES[k], got, want, got > 0 ? cents(got, want) : 0.0f);
          CHECK(got > 0 && fabsf(cents(got, want)) < 20.0f, "%s on %.1f Hz: pitch within 20 cents", nm[b], NOTES[k]);
      } }
    {   /* a slide: E3 to G3 over 3 s: the output pitch rises through the middle values */
        static const float H[1] = {164.81f}, ST[1] = {0.1f}; const uint8_t q[5] = {0, 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
        string(x, 44100 * 3, H, ST, 1, 0.5f, 0.99999f, 196.0f); render(&p, x, y, 44100 * 3, 64);
        const float f1 = f0_of(y, (int)(0.5f * FS_), 4096, 120, 260), f2 = f0_of(y, (int)(1.5f * FS_), 4096, 120, 260), f3 = f0_of(y, (int)(2.6f * FS_), 4096, 120, 260);
        const float w1 = 164.81f + 31.19f * 0.5f / 3.0f, w2 = 164.81f + 31.19f * 1.5f / 3.0f, w3 = 164.81f + 31.19f * 2.6f / 3.0f;
        printf("slide: %.1f %.1f %.1f Hz (want %.1f %.1f %.1f)\n", f1, f2, f3, w1, w2, w3);
        CHECK(f1 > 0 && f2 > f1 && f3 > f2, "the pitch rises with the slide");
        CHECK(fabsf(cents(f2, w2)) < 40.0f && fabsf(cents(f3, w3)) < 40.0f, "and follows it (no semitone grid)");
    }
    {   /* a repicked note retriggers; the note releases after the guitar stops */
        static const float H[2] = {110.0f, 110.0f}, ST[2] = {0.1f, 1.0f}; const uint8_t q[5] = {0, 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
        string(x, 44100 * 2, H, ST, 2, 0.5f, 0.9999f, 0); memset(x + 44100 * 2, 0, sizeof(float) * (N_ - 44100 * 2));
        render(&p, x, y, N_, 64);
        const float before = rms(y, (int)(0.9f * FS_), 2048), after = rms(y, (int)(1.15f * FS_), 2048), late = rms(y, (int)(4.5f * FS_), 4096), mid = rms(y, (int)(0.5f * FS_), 4096);
        printf("repick: rms %.4f before, %.4f after; at 0.5 s %.4f, at 4.5 s (the guitar stopped at 2 s) %.5f\n", before, after, mid, late);
        CHECK(mid > 0.01f, "the synth sounds while the guitar plays");
        CHECK(late < 0.05f * mid, "the synth releases after the guitar has stopped");
        CHECK(after > 1.2f * before, "the repicked note comes in again (the envelope restarts)");
    }
    {   /* determinism and block-size independence */
        static const float H[2] = {146.83f, 196.0f}, ST[2] = {0.1f, 1.2f}; const uint8_t q[5] = {10, 60, 40, 55, 50}; px_preset_t p; preset_of(&p, q);
        string(x, N_, H, ST, 2, 0.4f, 0.99995f, 0);
        render(&p, x, y, N_, 64); render(&p, x, y2, N_, 64);
        CHECK(!memcmp(y, y2, sizeof y), "the same settings and input give the same output");
        static const int BS[3] = {48, 16, 1};
        for (int i = 0; i < 3; i++) {
            const int n = (N_ / 64) * 64; render(&p, x, y2, n, BS[i]);   /* (a whole number of calls of 64, 48, 16 and 1) */
            float d = 0; for (int k = 0; k < n; k++) { const float a = fabsf(y[k] - y2[k]); if (a > d) d = a; }
            printf("call size %d: largest difference from 64: %g\n", BS[i], d);
            /* (the engine's anti-denormal offset alternates by the sample's place in the call, so one-sample calls differ in the last bits) */
            CHECK(BS[i] == 1 ? d < 1e-5f : d == 0.0f, "the output does not depend on the call size (%d)", BS[i]); }
    }
    {   /* every patch, the macros at 0 / 50 / 100: finite and bounded; silence in, silence out */
        static const float H[2] = {98.0f, 196.0f}, ST[2] = {0.05f, 0.8f}; static const uint8_t V[3] = {0, 50, 100}; float peak_all = 0; int n = 0;
        string(x, 44100, H, ST, 2, 0.7f, 0.99995f, 0);
        for (int pa = 0; pa < 10; pa++) for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++) {
            const uint8_t q[5] = {(uint8_t)(pa * 10 + 5), V[a], V[b], V[c], V[d]}; px_preset_t p; preset_of(&p, q);
            render(&p, x, y, 44100, 64);
            int ok = 1; float pk = 0; for (int i = 0; i < 44100; i++) { if (y[i] != y[i]) ok = 0; if (fabsf(y[i]) > pk) pk = fabsf(y[i]); }
            if (!ok || pk > 2.0f) { bad++; printf("FAIL patch %d macros %d %d %d %d: %s peak %.2f\n", pa, V[a], V[b], V[c], V[d], ok ? "too loud" : "NaN", pk); }
            if (pk > peak_all) peak_all = pk; n++;
            memset(y2, 0, sizeof(float) * 8192); static float z[8192]; memset(z, 0, sizeof z); render(&p, z, y2, 8192, 64);
            for (int i = 0; i < 8192; i++) if (y2[i] != 0.0f && fabsf(y2[i]) > 1e-6f) { bad++; printf("FAIL patch %d: not silent on silence\n", pa); break; }
        }
        printf("%d patch/macro combinations: all finite, loudest peak %.2f\n", n, peak_all);
    }
    {   /* patch bands and a patch switched while playing */
        CHECK(px_patch_slot(0) == 0 && px_patch_slot(9) == 0 && px_patch_slot(10) == 1 && px_patch_slot(99) == 9 && px_patch_slot(100) == 9, "patch bands: 10 values a slot, 100 in the last");
        static const float H[1] = {130.81f}, ST[1] = {0.1f}; string(x, N_, H, ST, 1, 0.5f, 0.99997f, 0);
        const uint8_t q[5] = {0, 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
        px_engine_init(&e, FS_); px_engine_set(&e, &p);
        int ok = 1;
        for (int at = 0; at + 64 <= N_; at += 64) {
            if (at % (44100 / 2) == 0) { px_preset_t r = e.cur; r.p[PX_B_SYN][0] = (uint8_t)((at / (44100 / 2)) * 17 % 101); r.p[PX_B_SYN][3] = (uint8_t)(at / 1000 % 101); if (px_engine_change(&e, &r) == PX_CH_KNOBS) { px_engine_prepare(&e, &r); px_engine_knobs(&e, &r); } }
            px_engine_process(&e, x + at, y + at, 64);
            for (int i = 0; i < 64; i++) if (y[at + i] != y[at + i] || fabsf(y[at + i]) > 3.0f) ok = 0;
        }
        CHECK(ok, "switching patches and envelope while playing stays finite");
    }
    printf("%s: %d problems\n", bad ? "FAILED" : "analog synth ok", bad);
    return bad != 0;
}
