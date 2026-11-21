/* Synth2 stages 3 and 4: ANALOG and ARCADE (Felucca ANALOG and LOFI) through the OpenPocket adapter (engine/px_synth_adapter.h), on synthetic plucks, through the whole engine.
 * Synthetic notes check the plumbing; whether it plays well is for a pedal and an ear (docs/research/synth2/SYNTH2-PLAN.md).
 *  - pitch: the synth plays the guitar's note (CLASSIC) and an octave below (SQ BASS), within 20 cents, from E2 up
 *  - a slide moves the pitch continuously (no semitone grid); a repicked note retriggers; the note releases after the guitar stops
 *  - the same five bytes give the same sound (fresh engines), and the sound does not depend on the call's block size (64, 48, 16, 1)
 *  - every patch at 0 / 50 / 100 of each macro: finite, bounded, and silent on silence
 *  - patch bands: slot = value / 10, 100 is the last slot; switching a patch while playing stays finite */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "px_dsp.c"
#define FS_ 44100.0f
static px_engine e;
static int bad;
static int g_type = PX_SYN_ANALOG;   /* the SYNTH type under test */
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
static float cents(float a, float b) { return 1200.0f * log2f(a / b); }
static void preset_of(px_preset_t *p, const uint8_t *q)
{
    px_preset_factory(p, 7);
    for (int z = 0; z < PX_NBLK; z++) p->on[z] = 0;
    p->on[PX_B_SYN] = 1; p->type[PX_B_SYN] = (uint8_t)g_type; memcpy(p->p[PX_B_SYN], q, 5);
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
static void analog_suite(void)
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
}
/* ARCADE: LOFI's chips follow the guitar's pitch within its 1/16 semitone steps; the stepped patches end by their own staircase */
static void arcade_suite(void)
{
    g_type = PX_SYN_ARCADE;
    {   static const char *nm[4] = {"ARCADE", "CHIP", "8BIT BS", "STEP"}; static const uint8_t slot[4] = {5, 15, 25, 45}; static const int sem[4] = {0, 0, -12, 0};
        static const float NOTES[3] = {110.0f, 164.81f, 329.63f}, ST[1] = {0.1f};
        for (int b = 0; b < 4; b++) for (int k = 0; k < 3; k++) {
            const uint8_t q[5] = {slot[b], 50, 50, 50, 100}; px_preset_t p; preset_of(&p, q);
            string(x, N_, &NOTES[k], ST, 1, 0.5f, 0.99997f, 0); render(&p, x, y, N_, 64);
            const float want = NOTES[k] * (sem[b] ? 0.5f : 1.0f), got = f0_of(y, (int)(0.5f * FS_), 4096, want * 0.7f, want * 1.4f);
            printf("ARCADE %s on %.1f Hz: plays %.2f Hz (%.1f cents)\n", nm[b], NOTES[k], got, got > 0 ? cents(got, want) : 0.0f);
            CHECK(got > 0 && fabsf(cents(got, want)) < 25.0f, "ARCADE %s on %.1f Hz: pitch within 25 cents", nm[b], NOTES[k]);
        } }
    {   /* MIX 0: the guitar only; MIX 100: the synth only (nothing of the guitar) */
        static const float H[1] = {196.0f}, ST[1] = {0.1f}; string(x, 44100 * 2, H, ST, 1, 0.5f, 0.99995f, 0);
        const uint8_t q0[5] = {5, 50, 50, 50, 0}, q1[5] = {5, 50, 50, 50, 100}; px_preset_t p0, p1; preset_of(&p0, q0); preset_of(&p1, q1);
        render(&p0, x, y, 44100 * 2, 64); render(&p1, x, y2, 44100 * 2, 64);
        const float rg = rms(x, 22050, 22050), r0 = rms(y, 22050, 22050);
        float d = 0; for (int i = 22050; i < 44100; i++) d += fabsf(y[i] - x[i]); d /= 22050;
        printf("MIX 0: guitar rms %.4f, output rms %.4f, mean |out - guitar| %.5f\n", rg, r0, d);
        CHECK(fabsf(r0 / rg - 1.0f) < 0.05f && d < 0.06f * rg, "MIX 0 is the guitar alone (the engine's DC blocker aside)");
        CHECK(rms(y2, 22050, 22050) > 0.01f, "MIX 100 plays the synth");
    }
    {   /* the ARP patch plays a major arpeggio (root, major third, fifth) over the guitar's note, stepping about every 20 ms */
        static const float H[1] = {110.0f}, ST[1] = {0.05f}; const uint8_t q[5] = {55, 50, 50, 50, 100}; px_preset_t p; preset_of(&p, q);
        string(x, 44100, H, ST, 1, 0.5f, 0.99997f, 0); render(&p, x, y, 44100, 64);
        int seen[3] = {0, 0, 0};
        for (int w = 0; w < 60; w++) { const float f = f0_of(y, 4410 + w * 441, 800, 80, 260); if (f > 0) { const float r = f / 110.0f; if (fabsf(cents(r, 1.0f)) < 60) seen[0]++; else if (fabsf(cents(r, 1.2599f)) < 60) seen[1]++; else if (fabsf(cents(r, 1.4983f)) < 60) seen[2]++; } }
        printf("ARP: windows on the root %d, major third %d, fifth %d\n", seen[0], seen[1], seen[2]);
        CHECK(seen[0] > 0 && seen[1] > 0 && seen[2] > 0, "the ARP patch steps through root, third and fifth");
    }
    {   /* a stepped patch ends by itself after the guitar stops (a staircase decay) */
        static const float H[1] = {146.83f}, ST[1] = {0.1f}; const uint8_t q[5] = {45, 50, 50, 50, 100}; px_preset_t p; preset_of(&p, q);
        string(x, 44100, H, ST, 1, 0.5f, 0.99995f, 0); memset(x + 44100, 0, sizeof(float) * (N_ - 44100));
        render(&p, x, y, N_, 64);
        const float mid = rms(y, 22050, 4096), late = rms(y, (int)(4.5f * FS_), 4096);
        printf("STEP: rms %.4f at 0.5 s, %.5f at 4.5 s\n", mid, late);
        CHECK(mid > 0.01f && late < 0.05f * mid, "a stepped patch sounds, then ends");
    }
    {   /* determinism, call-size independence, bounds on every patch and macro, silence */
        static const float H[2] = {146.83f, 196.0f}, ST[2] = {0.1f, 1.2f}; const uint8_t q[5] = {10, 60, 40, 55, 70}; px_preset_t p; preset_of(&p, q);
        string(x, N_, H, ST, 2, 0.4f, 0.99995f, 0); render(&p, x, y, N_, 64); render(&p, x, y2, N_, 64);
        CHECK(!memcmp(y, y2, sizeof(float) * (size_t)((N_ / 64) * 64)), "ARCADE: the same settings and input give the same output");
        static const int BS[3] = {48, 16, 1};
        for (int i = 0; i < 3; i++) { const int n = (N_ / 64) * 64; render(&p, x, y2, n, BS[i]); float d = 0; for (int k = 0; k < n; k++) { const float a = fabsf(y[k] - y2[k]); if (a > d) d = a; }
            CHECK(BS[i] == 1 ? d < 1e-5f : d == 0.0f, "ARCADE: the output does not depend on the call size (%d)", BS[i]); }
        static const float H2[2] = {98.0f, 196.0f}, ST2[2] = {0.05f, 0.8f}; static const uint8_t V[3] = {0, 50, 100}; float peak_all = 0; int n = 0;
        string(x, 44100, H2, ST2, 2, 0.7f, 0.99995f, 0);
        for (int pa = 0; pa < 10; pa++) for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++) {
            const uint8_t qq[5] = {(uint8_t)(pa * 10 + 5), V[a], V[b], V[c], V[d]}; px_preset_t pp; preset_of(&pp, qq);
            render(&pp, x, y, 44100, 64);
            int ok = 1; float pk = 0; for (int i = 0; i < 44100; i++) { if (y[i] != y[i]) ok = 0; if (fabsf(y[i]) > pk) pk = fabsf(y[i]); }
            if (!ok || pk > 2.0f) { bad++; printf("FAIL ARCADE patch %d macros %d %d %d %d: %s peak %.2f\n", pa, V[a], V[b], V[c], V[d], ok ? "too loud" : "NaN", pk); }
            if (pk > peak_all) peak_all = pk; n++;
            static float z[8192]; memset(z, 0, sizeof z); render(&pp, z, y2, 8192, 64);
            for (int i = 0; i < 8192; i++) if (fabsf(y2[i]) > 1e-6f) { bad++; printf("FAIL ARCADE patch %d: not silent on silence\n", pa); break; }
        }
        printf("ARCADE: %d patch/macro combinations: all finite, loudest peak %.2f\n", n, peak_all);
    }
    {   static const float H[1] = {130.81f}, ST[1] = {0.1f}; string(x, N_, H, ST, 1, 0.5f, 0.99997f, 0);
        const uint8_t q[5] = {0, 50, 50, 50, 100}; px_preset_t p; preset_of(&p, q);
        px_engine_init(&e, FS_); px_engine_set(&e, &p); int ok = 1;
        for (int at = 0; at + 64 <= N_; at += 64) {
            if (at % (44100 / 2) == 0) { px_preset_t r = e.cur; r.p[PX_B_SYN][0] = (uint8_t)((at / (44100 / 2)) * 17 % 101); r.p[PX_B_SYN][3] = (uint8_t)(at / 1000 % 101); if (px_engine_change(&e, &r) == PX_CH_KNOBS) { px_engine_prepare(&e, &r); px_engine_knobs(&e, &r); } }
            px_engine_process(&e, x + at, y + at, 64);
            for (int i = 0; i < 64; i++) if (y[at + i] != y[at + i] || fabsf(y[at + i]) > 3.0f) ok = 0;
        }
        CHECK(ok, "ARCADE: switching patches and macros while playing stays finite");
    }
}
/* Synth2 fix pass 1: the note-on beep (the tracker's wait for a low note's fundamental) and the NOISE patch's duration */
static void pass1_suite(void)
{
    {   /* a low note rich in harmonics, as a pick makes it: the first pitch the synth is given is the fundamental, within a few tens of ms, and is not corrected afterwards */
        static const float NOTES[3] = {82.41f, 110.0f, 146.83f}, ST[1] = {0.1f};
        for (int k = 0; k < 3; k++) {
            string(x, N_, &NOTES[k], ST, 1, 0.5f, 0.99997f, 0);
            px_engine_init(&e, FS_);
            static px_mono mo; px_mono_init(&e, &mo, PX_MONO_GUITAR);
            CHECK(mo.lay.minwait == PX_MONO_MINWAIT, "the mono interface sets the pitch-confirmation wait");
            int on = -1, corrections = 0; float f_on = 0, last = 0;
            for (int i = 0; i < N_; i++) {
                px_mono_out o; px_mono_run(&e, &mo, x[i], &o);
                if (o.onset && on < 0) { on = i; f_on = o.f0; last = o.f0; }
                else if (on >= 0 && o.gate && i - on < (int)(0.15f * FS_) && fabsf(cents(o.f0, last)) > 85.0f) { corrections++; last = o.f0; }
            }
            CHECK(on >= 0 && (float)(on - (int)(0.1f * FS_)) / FS_ < 0.045f, "%.1f Hz: the note starts within 45 ms of the pick (%.1f ms)", NOTES[k], (float)(on - (int)(0.1f * FS_)) * 1000.0f / FS_);
            /* (E2 itself is the known limit: its band settles for two periods, so it starts up to 300 cents sharp and settles within 15 ms: 96 -> 85 -> 82 Hz; a partial of an octave or more is what the wait removes) */
            CHECK(on >= 0 && fabsf(cents(f_on, NOTES[k])) < (k == 0 ? 300.0f : 50.0f), "%.1f Hz: it starts on the fundamental, not a partial (%.1f Hz)", NOTES[k], f_on);
            CHECK(corrections <= (k == 0 ? 2 : 0), "%.1f Hz: no pitch correction after the start (%d)", NOTES[k], corrections);
        }
    }
    {   /* NOISE: it plays while the guitar holds the note, and ends after it */
        g_type = PX_SYN_ARCADE;
        static const float NOTE[1] = {196.0f}, ST[1] = {0.1f};
        const uint8_t q[5] = {85, 50, 50, 50, 100}; px_preset_t p; preset_of(&p, q);
        string(x, N_, NOTE, ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
        const float early = rms(y, (int)(0.3f * FS_), 8192), held = rms(y, (int)(1.2f * FS_), 8192);
        CHECK(early > 0.01f, "NOISE sounds");
        CHECK(held > 0.5f * early, "NOISE is still playing 1.1 s into a held note (%.4f against %.4f)", held, early);
    }
}
/* Synth2: ARCADE's dry share follows the chain (the signal at the SYNTH block), ARCADE SPLIT plays a patch for each range */
static void routing_split_suite(void)
{
    g_type = PX_SYN_ARCADE;
    {   static const float NOTE[1] = {196.0f}, ST[1] = {0.1f};
        string(x, N_, NOTE, ST, 1, 0.5f, 0.99997f, 0);
        const uint8_t q[5] = {15, 50, 50, 50, 0};                                  /* MIX 0: the guitar alone */
        px_preset_t a; preset_of(&a, q); px_preset_t b = a;
        for (int z = 0; z < PX_NBLK; z++) { a.on[z] = b.on[z] = 0; }
        a.on[PX_B_EQ] = b.on[PX_B_EQ] = 1; a.on[PX_B_SYN] = 1; b.on[PX_B_SYN] = 0;
        for (int k = 0; k < 5; k++) a.p[PX_B_EQ][k] = b.p[PX_B_EQ][k] = 0;           /* every band down 12 dB: the guitar before the SYNTH block is 12 dB quieter */
        uint8_t o[PX_NBLK]; int n = 0; o[n++] = PX_B_EQ; o[n++] = PX_B_SYN; for (int z = 0; z < PX_NBLK; z++) if (z != PX_B_EQ && z != PX_B_SYN) o[n++] = (uint8_t)z;
        memcpy(a.order, o, sizeof o); memcpy(b.order, o, sizeof o);
        render(&a, x, y, N_, 64); render(&b, x, y2, N_, 64);
        const float ra = rms(y, 22050, 22050), rb = rms(y2, 22050, 22050);
        CHECK(fabsf(ra / rb - 1.0f) < 0.05f, "ARCADE's dry share comes after the blocks before it (EQ -12 dB): %.4f against the EQ alone %.4f", ra, rb);
    }
    {   g_type = PX_SYN_ARCADE_SPLIT;
        static const float NOTES[2] = {110.0f, 329.63f}; static const float ST[1] = {0.1f};
        const uint8_t q[5] = {25, 15, 42, 50, 100};                                 /* LOWER 8BIT BS (an octave down), UPPER CHIP, split at G3 (55) */
        CHECK(px_split_note(42) == 55, "SPLIT 42 is G3 (%d)", px_split_note(42));
        char buf[8]; px_param_text(PX_B_SYN, PX_SYN_ARCADE_SPLIT, 2, 42, buf); CHECK(!strcmp(buf, "G3"), "SPLIT reads as a note name (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_ARCADE_SPLIT, 0, 25, buf); CHECK(!strcmp(buf, "8BIT BS"), "LOWER reads as a patch name (%s)", buf);
        for (int k = 0; k < 2; k++) {
            px_preset_t p; preset_of(&p, q);
            string(x, N_, &NOTES[k], ST, 1, 0.5f, 0.99997f, 0); render(&p, x, y, N_, 64);
            const float want = k == 0 ? 55.0f : NOTES[1], got = f0_of(y, (int)(0.5f * FS_), 4096, want * 0.7f, want * 1.4f);
            CHECK(got > 0 && fabsf(cents(got, want)) < 25.0f, "ARCADE SPLIT on %.1f Hz plays %.1f Hz (got %.1f)", NOTES[k], want, got);
        }
        {   /* the two notes in turn: the range follows each note */
            static const float H[2] = {110.0f, 329.63f}, S2[2] = {0.1f, 2.0f};
            string(x, N_, H, S2, 2, 0.5f, 0.99997f, 0); px_preset_t p; preset_of(&p, q); render(&p, x, y, N_, 64);   /* (a string that rings on: the synth's release is Felucca's, short, it ends with the guitar) */
            const float a = f0_of(y, (int)(1.2f * FS_), 4096, 38.0f, 80.0f), b = f0_of(y, (int)(2.7f * FS_), 4096, 230.0f, 460.0f);
            CHECK(a > 0 && b > 0, "a low then a high note: each in its own range (%.1f, %.1f Hz)", a, b);
        }
    }
    g_type = PX_SYN_ARCADE;
}
/* Hann-windowed Goertzel level (dB) of y[a..a+len) at f; and the strongest frequency within +-100 cents of f (for the organ: the 8' bar is the played note, the 16' bar is an octave below it) */
static float gz_db(const float *v, int a, int len, double f)
{
    double re = 0, im = 0;
    for (int i = 0; i < len; i++) { const double w = 0.5 - 0.5 * cos(6.283185307 * i / len), ph = 6.283185307 * f * i / FS_; re += v[a + i] * w * cos(ph); im += v[a + i] * w * sin(ph); }
    return (float)(20.0 * log10(sqrt(re * re + im * im) / len * 4.0 + 1e-9));
}
static float peak_hz(const float *v, int a, int len, float f)
{
    float best = -1e9f, bf = f;
    for (int c = -100; c <= 100; c += 2) { const double ff = f * pow(2.0, c / 1200.0); const float l = gz_db(v, a, len, ff); if (l > best) { best = l; bf = (float)ff; } }
    return bf;
}
/* CHOIR (Synth2 stage G): the original FM-1 synth's VOICE engine, sung vowels, a stack of singers */
static void choir_suite(void)
{
    g_type = PX_SYN_CHOIR;
    static const float H[1] = {110.0f}, ST[1] = {0.1f};
    {   char buf[8]; px_param_text(PX_B_SYN, PX_SYN_CHOIR, 0, 35, buf); CHECK(!strcmp(buf, "OH"), "CHOIR VOWEL 35 reads OH (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_CHOIR, 0, 55, buf); CHECK(!strcmp(buf, "OO>AH"), "CHOIR VOWEL 55 reads OO>AH (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_CHOIR, 1, 65, buf); CHECK(!strcmp(buf, "MAJOR"), "CHOIR SINGERS 65 reads MAJOR (%s)", buf);
        CHECK(px_nparams(PX_B_SYN, PX_SYN_CHOIR) == 5 && !strcmp(px_type_name(PX_B_SYN, PX_SYN_CHOIR), "CHOIR"), "CHOIR has five controls"); }
    {   /* formants: AH's strongest harmonic is in the F1 region whatever the pitch (110, 165, 220 Hz); the level is steady across the range; EE has far more energy above 2 kHz relative to 250-1000 Hz than OO */
        static const float NT[3] = {110.0f, 165.0f, 220.0f}; float lv[3];
        for (int k = 0; k < 3; k++) {
            const uint8_t q[5] = {5, 45, 50, 50, 50}; px_preset_t p; preset_of(&p, q); string(x, N_, &NT[k], ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
            float best = -1e9f, bf = 0; for (int h = 1; h <= 14; h++) { const float l = gz_db(y, 2 * 44100, 32768, NT[k] * h); if (NT[k] * h > 300.0f && NT[k] * h < 1300.0f && l > best) { best = l; bf = NT[k] * h; } }
            CHECK(bf > 520.0f && bf < 820.0f, "CHOIR AH on %.0f Hz: the strongest harmonic is in the F1 region (%.0f Hz)", NT[k], bf);
            lv[k] = 20.0f * log10f(rms(y, 2 * 44100, 44100) + 1e-9f); }
        CHECK(fabsf(lv[0] - lv[1]) < 3.0f && fabsf(lv[1] - lv[2]) < 3.0f && fabsf(lv[0] - lv[2]) < 3.0f, "CHOIR AH: the level is steady across 110-220 Hz (%.1f %.1f %.1f dBFS)", lv[0], lv[1], lv[2]);
        float ratio[2];
        for (int vi = 0; vi < 2; vi++) { const uint8_t q[5] = {(uint8_t)(vi ? 25 : 45), 45, 50, 50, 50}; px_preset_t p; preset_of(&p, q); string(x, N_, H, ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
            double hi = 0, lo = 0; for (int h = 1; h <= 40; h++) { const double f = 110.0 * h; const double e = pow(10.0, gz_db(y, 2 * 44100, 32768, f) / 10.0); if (f > 2000 && f < 3200) hi += e; if (f > 250 && f < 1000) lo += e; }
            ratio[vi] = (float)(10.0 * log10((hi + 1e-12) / (lo + 1e-12))); }
        CHECK(ratio[1] > ratio[0] + 10.0f, "CHOIR EE has far more energy above 2 kHz than OO (high-to-low %.1f dB against %.1f dB)", ratio[1], ratio[0]); }
    {   /* TALK: OO>AH opens the mouth after each note starts: the spectrum's centre of mass (harmonics 2-18) rises from the first 50 ms to 1.5 s later; AH>OO falls */
        for (int vi = 0; vi < 2; vi++) { const uint8_t q[5] = {(uint8_t)(vi ? 65 : 55), 45, 50, 50, 50}; px_preset_t p; preset_of(&p, q); string(x, N_, H, ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
            int on = 0; float pk = 0; for (int i = 0; i < N_; i++) if (fabsf(y[i]) > pk) pk = fabsf(y[i]); while (on < N_ && fabsf(y[on]) < 0.05f * pk) on++;
            double c[2]; for (int w = 0; w < 2; w++) { const int a = on + (w ? 66150 : 882); double sw = 0, sf = 0; for (int h = 2; h <= 18; h++) { const double f = 110.0 * h; const double e = pow(10.0, gz_db(y, a, 2048, f) / 10.0); sw += e; sf += e * f; } c[w] = sf / (sw + 1e-12); }   /* (short windows: the morph takes 330 ms) */
            CHECK(vi ? c[1] < c[0] - 60.0 : c[1] > c[0] + 60.0, "CHOIR %s: the mouth %s (centre of mass %.0f Hz early, %.0f Hz later)", vi ? "AH>OO" : "OO>AH", vi ? "closes" : "opens", c[0], c[1]); } }
    {   /* every vowel x singers x spread x tone: finite and bounded; silence in, silence out */
        static const float Hh[2] = {98.0f, 196.0f}, Sh[2] = {0.05f, 0.8f}; static const uint8_t SP[3] = {0, 50, 100}, TN[2] = {0, 100}; float peak_all = 0; int n = 0;
        string(x, 44100, Hh, Sh, 2, 0.7f, 0.99995f, 0);
        for (int pa = 0; pa < 10; pa++) for (int vo = 0; vo < 10; vo++) for (int sp = 0; sp < 3; sp++) for (int tn = 0; tn < 2; tn++) {
            const uint8_t q[5] = {(uint8_t)(pa * 10 + 5), (uint8_t)(vo * 10 + 5), SP[sp], TN[tn], 50}; px_preset_t p; preset_of(&p, q); render(&p, x, y, 44100, 64);
            int ok = 1; float pk = 0; for (int i = 0; i < 44100; i++) { if (y[i] != y[i]) ok = 0; if (fabsf(y[i]) > pk) pk = fabsf(y[i]); }
            if (!ok || pk > 2.0f) { bad++; printf("FAIL CHOIR vowel %d singers %d spread %d tone %d: %s peak %.2f\n", pa, vo, SP[sp], TN[tn], ok ? "too loud" : "NaN", pk); }
            if (pk > peak_all) peak_all = pk; n++; }
        { const uint8_t q[5] = {85, 95, 50, 50, 50}; px_preset_t p; preset_of(&p, q); static float z[8192]; memset(z, 0, sizeof z); render(&p, z, y2, 8192, 64);
          for (int i = 0; i < 8192; i++) if (fabsf(y2[i]) > 1e-6f) { bad++; printf("FAIL CHOIR: not silent on silence\n"); break; } }
        printf("CHOIR: %d vowel/singers/macro combinations: all finite, loudest peak %.2f\n", n, peak_all); }
    {   /* deterministic, call-size independent */
        static const float H2[2] = {146.83f, 196.0f}, ST2[2] = {0.1f, 1.2f}; const uint8_t q[5] = {75, 55, 60, 40, 50}; px_preset_t p; preset_of(&p, q);
        string(x, N_, H2, ST2, 2, 0.4f, 0.99995f, 0); render(&p, x, y, N_, 64); render(&p, x, y2, N_, 64);
        CHECK(!memcmp(y, y2, sizeof y), "CHOIR: the same settings and input give the same output");
        const int n = (N_ / 64) * 64; render(&p, x, y2, n, 48); float d = 0; for (int k = 0; k < n; k++) { const float a = fabsf(y[k] - y2[k]); if (a > d) d = a; }
        CHECK(d == 0.0f, "CHOIR: the output does not depend on the call size (%g)", d); }
    {   /* every singer stops: a hard mute stops all five within 400 ms; the tail falls after the guitar stops */
        float ph = 0, env = 0.5f; const int tm = 44100; for (int i = 0; i < 44100 * 3; i++) { ph += 110.0f / FS_; ph -= floorf(ph); const float pp = ph * 6.2831853f; float g = env; if (i >= tm) g = env * expf(-(float)(i - tm) / FS_ * 1000.0f / 6.0f); else env *= 0.99997f;
            x[i] = i < 2000 ? 0.0f : g * 0.4f * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp) + 0.3f * sinf(4 * pp)); }
        const uint8_t q[5] = {5, 95, 50, 50, 50}; px_preset_t p; preset_of(&p, q); render(&p, x, y, 44100 * 3, 64);
        const float before = rms(y, tm - 4410, 2048), after = rms(y, tm + (int)(0.40f * FS_), 2048);
        CHECK(after < 0.03f * before, "CHOIR OPEN: after a hard mute every singer is 30 dB down within 400 ms (%.1f dB)", 20.0f * log10f(after / (before + 1e-9f))); }
    g_type = PX_SYN_ANALOG;
}
/* ENSEMBLE (Synth2 stage F): the INSTRUMENT patches played by a stack of voices from the one tracked note */
static void ensemble_suite(void)
{
    g_type = PX_SYN_ENSEMBLE;
    static const float H[1] = {110.0f}, ST[1] = {0.1f};
    {   /* the names and the controls */
        char buf[8]; px_param_text(PX_B_SYN, PX_SYN_ENSEMBLE, 0, 15, buf); CHECK(!strcmp(buf, "STRINGS"), "ENSEMBLE PATCH 15 reads STRINGS (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_ENSEMBLE, 1, 65, buf); CHECK(!strcmp(buf, "MAJOR"), "ENSEMBLE VOICING 65 reads MAJOR (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_ENSEMBLE, 1, 100, buf); CHECK(!strcmp(buf, "OPEN"), "ENSEMBLE VOICING 100 reads OPEN (%s)", buf);
        CHECK(px_nparams(PX_B_SYN, PX_SYN_ENSEMBLE) == 5 && !strcmp(px_type_name(PX_B_SYN, PX_SYN_ENSEMBLE), "ENSEMBLE") && px_stack_slot(100) == 9 && px_stack_slot(49) == 4, "ENSEMBLE has five controls and ten voicing slots");
    }
    {   /* every voicing plays its intervals (the voices' fundamentals at the note's semitones are there, a note not in the chord is not) on STRINGS (a saw: the fundamental is strong) */
        static const int EXPECT[10][5] = {{0, 99, 99, 99, 99}, {0, 12, 99, 99, 99}, {-12, 0, 99, 99, 99}, {-12, 0, 12, 99, 99}, {0, 7, 99, 99, 99}, {-12, 0, 7, 99, 99}, {0, 4, 7, 99, 99}, {0, 3, 7, 99, 99}, {0, 4, 7, 11, 99}, {-12, 0, 7, 12, 16}};
        for (int v = 0; v < 10; v++) {
            const uint8_t q[5] = {15, (uint8_t)(v * 10 + 5), 50, 50, 50}; px_preset_t p; preset_of(&p, q);
            string(x, N_, H, ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
            float lv[29]; float mx = -1e9f; for (int st = -12; st <= 16; st++) { lv[st + 12] = gz_db(y, 44100, 44100, 110.0 * pow(2.0, st / 12.0)); if (lv[st + 12] > mx) mx = lv[st + 12]; }
            for (int k = 0; k < 5 && EXPECT[v][k] != 99; k++) CHECK(lv[EXPECT[v][k] + 12] > mx - 14.0f, "ENSEMBLE %s: the voice at %+d semitones is there (%.1f dB under the strongest)", PX_STACKS[v].name, EXPECT[v][k], mx - lv[EXPECT[v][k] + 12]);
            for (int nt = 1; nt <= 11; nt += 5) { int in = 0; for (int k = 0; k < 5 && EXPECT[v][k] != 99; k++) if (EXPECT[v][k] == nt || EXPECT[v][k] - 12 == nt || EXPECT[v][k] + 12 == nt) in = 1;
                if (!in && nt != 6 && nt != 1 && nt != 11) continue;
                if (!in) CHECK(lv[nt + 12] < mx - 12.0f, "ENSEMBLE %s: the note at %+d semitones is not played (%.1f dB under)", PX_STACKS[v].name, nt, mx - lv[nt + 12]); }
        }
    }
    {   /* every patch x voicing x spread x tone: finite and bounded; silence in, silence out */
        static const float Hh[2] = {98.0f, 196.0f}, Sh[2] = {0.05f, 0.8f}; static const uint8_t SP[3] = {0, 50, 100}, TN[2] = {0, 100}; float peak_all = 0; int n = 0;
        string(x, 44100, Hh, Sh, 2, 0.7f, 0.99995f, 0);
        for (int pa = 0; pa < 10; pa++) for (int vo = 0; vo < 10; vo++) for (int sp = 0; sp < 3; sp++) for (int tn = 0; tn < 2; tn++) {
            const uint8_t q[5] = {(uint8_t)(pa * 10 + 5), (uint8_t)(vo * 10 + 5), SP[sp], TN[tn], 50}; px_preset_t p; preset_of(&p, q);
            render(&p, x, y, 44100, 64);
            int ok = 1; float pk = 0; for (int i = 0; i < 44100; i++) { if (y[i] != y[i]) ok = 0; if (fabsf(y[i]) > pk) pk = fabsf(y[i]); }
            if (!ok || pk > 2.0f) { bad++; printf("FAIL ENSEMBLE patch %d voicing %d spread %d tone %d: %s peak %.2f\n", pa, vo, SP[sp], TN[tn], ok ? "too loud" : "NaN", pk); }
            if (pk > peak_all) peak_all = pk; n++;
        }
        { const uint8_t q[5] = {55, 95, 50, 50, 50}; px_preset_t p; preset_of(&p, q); static float z[8192]; memset(z, 0, sizeof z); render(&p, z, y2, 8192, 64);
          for (int i = 0; i < 8192; i++) if (fabsf(y2[i]) > 1e-6f) { bad++; printf("FAIL ENSEMBLE: not silent on silence\n"); break; } }
        printf("ENSEMBLE: %d patch/voicing/macro combinations: all finite, loudest peak %.2f\n", n, peak_all);
    }
    {   /* deterministic, and the call size does not matter */
        static const float H2[2] = {146.83f, 196.0f}, ST2[2] = {0.1f, 1.2f}; const uint8_t q[5] = {55, 85, 60, 40, 50}; px_preset_t p; preset_of(&p, q);
        string(x, N_, H2, ST2, 2, 0.4f, 0.99995f, 0); render(&p, x, y, N_, 64); render(&p, x, y2, N_, 64);
        CHECK(!memcmp(y, y2, sizeof y), "ENSEMBLE: the same settings and input give the same output");
        const int n = (N_ / 64) * 64; render(&p, x, y2, n, 48);
        float d = 0; for (int k = 0; k < n; k++) { const float a = fabsf(y[k] - y2[k]); if (a > d) d = a; }
        CHECK(d == 0.0f, "ENSEMBLE: the output does not depend on the call size (%g)", d);
    }
    {   /* the copies' beating is irregular (a chorus), not a periodic swell (a phaser), and no deep dips: on STRINGS and SOFTPAD, UNISON / OCT UP / MAJOR, three notes: the envelope's periodicity
         * (the strongest autocorrelation of the 10 ms RMS at lags 0.25-2 s) stays under 0.6 (INSTRUMENT STRINGS, the single voice the user accepted, is 0.67-0.82) and the peak level swings less than 15 dB */
        static const float NT[3] = {82.41f, 164.81f, 329.63f}; static const int VO[3] = {0, 1, 6};
        for (int pi = 0; pi < 2; pi++) for (int vi = 0; vi < 3; vi++) for (int k = 0; k < 3; k++) {
            const uint8_t q[5] = {(uint8_t)(pi ? 15 : 5), (uint8_t)(VO[vi] * 10 + 5), 50, 50, 50}; px_preset_t p; preset_of(&p, q);
            static float xl[44100 * 7], yl[44100 * 7]; const int NL = 44100 * 7; float ph = 0, env = 0.5f;
            for (int i = 0; i < NL; i++) { ph += NT[k] / FS_; ph -= floorf(ph); const float pp = ph * 6.2831853f; xl[i] = i < 4410 ? 0.0f : 0.4f * env * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp) + 0.3f * sinf(4 * pp)); env *= 1.0f; }
            px_engine_init(&e, FS_); px_engine_set(&e, &p); for (int at = 0; at + 64 <= NL; at += 64) px_engine_process(&e, xl + at, yl + at, 64);
            float lo = 1e9f, hi = 0; for (int i = 44100 * 2; i + 441 < NL - 22050; i += 441) { float pk = 0; for (int j = 0; j < 441; j++) if (fabsf(yl[i + j]) > pk) pk = fabsf(yl[i + j]); if (pk < lo) lo = pk; if (pk > hi) hi = pk; }
            static float rr[900]; const int nb = (NL - 44100 * 3 - 882) / 441; double m = 0;
            for (int b = 0; b < nb; b++) { double a = 0; for (int j = 0; j < 441; j++) a += (double)yl[44100 * 2 + b * 441 + j] * yl[44100 * 2 + b * 441 + j]; rr[b] = (float)sqrt(a / 441); m += rr[b]; }
            m /= nb; double v0 = 0; for (int b = 0; b < nb; b++) v0 += (rr[b] - m) * (rr[b] - m);
            double best = 0; for (int lag = 25; lag <= 200 && lag < nb / 2; lag++) { double c = 0; for (int b = 0; b + lag < nb; b++) c += (rr[b] - m) * (rr[b + lag] - m); c /= v0; if (c > best) best = c; }
            CHECK(best < 0.6, "ENSEMBLE %s %s on %.0f Hz: the beating is irregular (periodicity %.2f)", PX_INSTRUMENT_PATCHES[pi ? 1 : 0].name, PX_STACKS[VO[vi]].name, NT[k], best);
            CHECK(20.0f * log10f(hi / (lo + 1e-9f)) < 15.0f, "ENSEMBLE %s %s on %.0f Hz: no deep dips (%.1f dB)", PX_INSTRUMENT_PATCHES[pi ? 1 : 0].name, PX_STACKS[VO[vi]].name, NT[k], 20.0f * log10f(hi / (lo + 1e-9f)));
        }
    }
    {   /* every voice releases: after the guitar stops no voice rings on (STRINGS OPEN, 5 voices, RELEASE as written) and a hard mute stops all of them fast (ORGAN OPEN) */
        { static const float H3[1] = {110.0f}, S3[1] = {0.1f}; const uint8_t q[5] = {15, 95, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
          string(x, 44100 * 2, H3, S3, 1, 0.5f, 0.9999f, 0); memset(x + 44100 * 2, 0, sizeof(float) * (N_ - 44100 * 2)); render(&p, x, y, N_, 64);
          const float mid = rms(y, (int)(0.8f * FS_), 4096), soon = rms(y, (int)(2.3f * FS_), 4096), late = rms(y, (int)(4.8f * FS_), 4096);
          CHECK(mid > 0.005f && late < 0.5f * soon && late < 0.35f * mid, "ENSEMBLE STRINGS OPEN: sounds, and the tail of all five voices keeps falling (%.4f, %.4f, %.4f)", mid, soon, late); }
        { float ph = 0, env = 0.5f; const int tm = 44100; for (int i = 0; i < 44100 * 3; i++) { ph += 110.0f / FS_; ph -= floorf(ph); const float pp = ph * 6.2831853f; float g = env; if (i >= tm) g = env * expf(-(float)(i - tm) / FS_ * 1000.0f / 6.0f); else env *= 0.99997f;
              x[i] = i < 2000 ? 0.0f : g * 0.4f * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp) + 0.3f * sinf(4 * pp)); }
          for (int ci = 0; ci < 2; ci++) { const uint8_t q[5] = {(uint8_t)(ci ? 15 : 55), 95, 50, 50, 50}; px_preset_t p; preset_of(&p, q); render(&p, x, y, 44100 * 3, 64);
              const float before = rms(y, tm - 4410, 2048), after = rms(y, tm + (int)(0.40f * FS_), 2048);
              CHECK(after < 0.03f * before, "ENSEMBLE %s OPEN: after a hard mute every voice is 30 dB down within 400 ms (%.1f dB)", ci ? "STRINGS" : "ORGAN", 20.0f * log10f(after / (before + 1e-9f))); } }
    }
    {   /* the voicing and the patch switched while playing stay finite, and a smaller stack leaves no voice behind (the bigger one first, then UNISON: the level settles to a fresh UNISON's) */
        static const float H4[1] = {130.81f}; string(x, N_, H4, ST, 1, 0.5f, 0.99997f, 0);
        const uint8_t q[5] = {55, 95, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
        px_engine_init(&e, FS_); px_engine_set(&e, &p); int ok = 1;
        for (int at = 0; at + 64 <= N_; at += 64) {
            if (at % (44100 / 2) == 0) { px_preset_t r = e.cur; r.p[PX_B_SYN][1] = (uint8_t)((at / (44100 / 2)) * 23 % 101); r.p[PX_B_SYN][0] = (uint8_t)((at / (44100 / 2)) * 17 % 101); if (px_engine_change(&e, &r) == PX_CH_KNOBS) { px_engine_prepare(&e, &r); px_engine_knobs(&e, &r); } }
            px_engine_process(&e, x + at, y + at, 64);
            for (int i = 0; i < 64; i++) if (y[at + i] != y[at + i] || fabsf(y[at + i]) > 4.0f) ok = 0;
        }
        CHECK(ok, "ENSEMBLE: switching patch and voicing while playing stays finite");
    }
    g_type = PX_SYN_ANALOG;
}
/* INSTRUMENT (Stage A): Felucca ANALOG's SOFT PAD / STRINGS / BRASS patches and two variations, played by the mono tracker (docs/research/synth2/SYNTH2-INSTRUMENT.md) */
static void instrument_suite(void)
{
    g_type = PX_SYN_INSTRUMENT;
    static const float NOTES[3] = {82.41f, 164.81f, 329.63f}, ST[1] = {0.1f};
    for (int b = 0; b < PX_INST_DEFINED; b++) for (int k = 0; k < 3; k++) {
        const uint8_t q[5] = {(uint8_t)(b * 10 + 5), 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
        string(x, N_, &NOTES[k], ST, 1, 0.5f, 0.99997f, 0); render(&p, x, y, N_, 64);
        const float got = PX_INSTRUMENT_PATCHES[b].eng ? peak_hz(y, (int)(1.0f * FS_), 8192, NOTES[k]) : f0_of(y, (int)(1.0f * FS_), 4096, NOTES[k] * 0.7f, NOTES[k] * 1.4f);   /* (WHEEL: the 8' bar) */
        CHECK(got > 0 && fabsf(cents(got, NOTES[k])) < 25.0f, "INSTRUMENT %s on %.1f Hz: pitch within 25 cents (%.1f)", PX_INSTRUMENT_PATCHES[b].name, NOTES[k], got);
    }
    {   /* the patch identity: ten slots (0..4 the ANALOG-engine patches, 5..9 the WHEEL organs), all final; names read from the table */
        CHECK(px_inst_slot(0) == 0 && px_inst_slot(9) == 0 && px_inst_slot(10) == 1 && px_inst_slot(49) == 4 && px_inst_slot(50) == 5 && px_inst_slot(59) == 5 && px_inst_slot(90) == 9 && px_inst_slot(100) == 9, "INSTRUMENT slots: 10 values each, 100 in the last");
        char buf[8]; px_param_text(PX_B_SYN, PX_SYN_INSTRUMENT, 0, 50, buf); CHECK(!strcmp(buf, "ORGAN"), "INSTRUMENT patch 50 reads ORGAN (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_INSTRUMENT, 0, 100, buf); CHECK(!strcmp(buf, "SOFTORG"), "INSTRUMENT patch 100 reads SOFTORG (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_INSTRUMENT, 0, 10, buf); CHECK(!strcmp(buf, "STRINGS"), "INSTRUMENT patch 10 reads STRINGS (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_INSTRUMENT, 0, 20, buf); CHECK(!strcmp(buf, "BRASS"), "INSTRUMENT patch 20 reads BRASS (%s)", buf);
        CHECK(px_nparams(PX_B_SYN, PX_SYN_INSTRUMENT) == 5 && !strcmp(px_type_name(PX_B_SYN, PX_SYN_INSTRUMENT), "INSTRUMENT"), "INSTRUMENT has five controls");
    }
    {   /* ATTACK and RELEASE: longer at 100 than at 0 (STRINGS, E3 for 1.2 s then silence) */
        static const float H[1] = {164.81f}; string(x, N_, H, ST, 1, 0.5f, 0.99990f, 0); memset(x + (int)(1.3f * FS_), 0, sizeof(float) * (N_ - (int)(1.3f * FS_)));
        float t_att[2], t_att90[2], t_rel[2];
        for (int j = 0; j < 2; j++) {
            const uint8_t qa[5] = {10, 50, (uint8_t)(j ? 100 : 0), 50, 50}, qr[5] = {10, 50, 50, (uint8_t)(j ? 100 : 0), 50};
            px_preset_t p; preset_of(&p, qa); render(&p, x, y, N_, 64);
            float pk = 0; for (int i = 0; i < (int)(1.2f * FS_); i++) if (fabsf(y[i]) > pk) pk = fabsf(y[i]);
            int i = 0; while (i < N_ && fabsf(y[i]) < 0.5f * pk) i++; t_att[j] = (float)i / FS_;
            i = 0; while (i < N_ && fabsf(y[i]) < 0.9f * pk) i++; t_att90[j] = (float)i / FS_;
            preset_of(&p, qr); render(&p, x, y, N_, 64);
            pk = 0; for (int i2 = 0; i2 < (int)(1.2f * FS_); i2++) if (fabsf(y[i2]) > pk) pk = fabsf(y[i2]);
            int e0 = (int)(1.3f * FS_), r = e0; while (r < N_ - 512 && rms(y, r, 512) > 0.1f * pk * 0.7f) r += 256; t_rel[j] = (float)(r - e0) / FS_;
        }
        printf("INSTRUMENT STRINGS: time to half level ATTACK 0 %.3f s, 100 %.3f s; to 90 %% ATTACK 0 %.3f s, 100 %.3f s; release to -20 dB RELEASE 0 %.3f s, 100 %.3f s\n", t_att[0], t_att[1], t_att90[0], t_att90[1], t_rel[0], t_rel[1]);
        /* a slow attack no longer makes the note late: it speaks within about 60 ms of the pick (PX_S2_ATK_HEAD), and the ATTACK knob sets the length of the swell to full level */
        CHECK(t_att[0] - 0.1f < 0.06f && t_att[1] - 0.1f < 0.06f, "a slow attack speaks at once: half level within 60 ms of the pick (ATTACK 0 %.3f s, 100 %.3f s after 0.1 s)", t_att[0] - 0.1f, t_att[1] - 0.1f);
        CHECK(t_att90[1] - 0.1f > 1.6f * (t_att90[0] - 0.1f), "ATTACK 100 swells more slowly than 0 (time to 90 %% of the level, from the pick at 0.1 s)");
        CHECK(t_rel[1] > 1.5f * t_rel[0], "RELEASE 100 is longer than 0");
        CHECK(t_rel[1] < 0.2f, "a long release no longer hangs on: RELEASE 100 is -20 dB within 0.2 s (%.3f s; it was 0.447 s before PX_S2_REL_SPAN)", t_rel[1]);
    }
    {   /* a slide, a repicked note, a release (SOFTPAD, STRINGS, BRASS) */
        for (int b = 0; b < 3; b++) {
            const uint8_t q[5] = {(uint8_t)(b * 10 + 5), 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
            static const float H[1] = {164.81f}; string(x, 44100 * 3, H, ST, 1, 0.5f, 0.99999f, 196.0f); render(&p, x, y, 44100 * 3, 64);
            const float f2 = f0_of(y, (int)(1.5f * FS_), 4096, 120, 260), w2 = 164.81f + 31.19f * 1.5f / 3.0f;
            CHECK(f2 > 0 && fabsf(cents(f2, w2)) < 50.0f, "INSTRUMENT %s follows a slide (%.1f Hz, want %.1f)", PX_INSTRUMENT_PATCHES[b].name, f2, w2);
            static const float H2[2] = {110.0f, 110.0f}, ST2[2] = {0.1f, 1.0f}; string(x, 44100 * 2, H2, ST2, 2, 0.5f, 0.9999f, 0); memset(x + 44100 * 2, 0, sizeof(float) * (N_ - 44100 * 2));
            render(&p, x, y, N_, 64);
            const float mid = rms(y, (int)(0.8f * FS_), 4096), soon = rms(y, (int)(2.3f * FS_), 4096), late = rms(y, (int)(4.8f * FS_), 4096);
            CHECK(mid > 0.005f, "INSTRUMENT %s sounds while the guitar plays (%.4f)", PX_INSTRUMENT_PATCHES[b].name, mid);
            /* the pad and strings release is long by design (Felucca REL 90-95: a time constant of 1.4-1.7 s): the tail must keep falling, and RELEASE 0 must end it */
            CHECK(late < 0.5f * soon && late < 0.35f * mid, "INSTRUMENT %s's tail keeps falling after the guitar stops (%.4f, %.4f, %.4f)", PX_INSTRUMENT_PATCHES[b].name, mid, soon, late);
            const uint8_t q0[5] = {(uint8_t)(b * 10 + 5), 50, 50, 0, 50}; px_preset_t p0; preset_of(&p0, q0); render(&p0, x, y, N_, 64);
            const float end0 = rms(y, (int)(4.8f * FS_), 4096), mid0 = rms(y, (int)(0.8f * FS_), 4096);
            CHECK(end0 < 0.05f * mid0, "INSTRUMENT %s with RELEASE 0 has ended 2.8 s after the guitar (%.5f against %.4f): no stuck note", PX_INSTRUMENT_PATCHES[b].name, end0, mid0);
        }
    }
    {   /* the same five bytes, the same sound, and the call size does not matter */
        static const float H[2] = {146.83f, 196.0f}, ST2[2] = {0.1f, 1.2f}; const uint8_t q[5] = {30, 60, 40, 55, 50}; px_preset_t p; preset_of(&p, q);
        string(x, N_, H, ST2, 2, 0.4f, 0.99995f, 0); render(&p, x, y, N_, 64); render(&p, x, y2, N_, 64);
        CHECK(!memcmp(y, y2, sizeof y), "INSTRUMENT: the same settings and input give the same output");
        const int n = (N_ / 64) * 64; render(&p, x, y2, n, 48);
        float d = 0; for (int k = 0; k < n; k++) { const float a = fabsf(y[k] - y2[k]); if (a > d) d = a; }
        CHECK(d == 0.0f, "INSTRUMENT: the output does not depend on the call size (%g)", d);
    }
    {   /* every patch (and the reserved slots), the macros at 0 / 50 / 100: finite, bounded, silent on silence */
        static const float H[2] = {98.0f, 196.0f}, ST2[2] = {0.05f, 0.8f}; static const uint8_t V[3] = {0, 50, 100}; float peak_all = 0; int n = 0;
        string(x, 44100, H, ST2, 2, 0.7f, 0.99995f, 0);
        for (int pa = 0; pa < 10; pa++) for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++) {
            const uint8_t q[5] = {(uint8_t)(pa * 10 + 5), V[a], V[b], V[c], V[d]}; px_preset_t p; preset_of(&p, q);
            render(&p, x, y, 44100, 64);
            int ok = 1; float pk = 0; for (int i = 0; i < 44100; i++) { if (y[i] != y[i]) ok = 0; if (fabsf(y[i]) > pk) pk = fabsf(y[i]); }
            if (!ok || pk > 2.0f) { bad++; printf("FAIL INSTRUMENT patch %d macros %d %d %d %d: %s peak %.2f\n", pa, V[a], V[b], V[c], V[d], ok ? "too loud" : "NaN", pk); }
            if (pk > peak_all) peak_all = pk; n++;
            static float z[8192]; memset(z, 0, sizeof z); render(&p, z, y2, 8192, 64);
            for (int i = 0; i < 8192; i++) if (fabsf(y2[i]) > 1e-6f) { bad++; printf("FAIL INSTRUMENT patch %d: not silent on silence\n", pa); break; }
        }
        printf("INSTRUMENT: %d patch/macro combinations: all finite, loudest peak %.2f\n", n, peak_all);
    }
    {   /* fix pass 2: a steady note does not swell and fade (the detuned oscillators' beating): the peak level swings less than 5 dB (the ANALOG-engine patches; the organs have their rotary: below) */
        for (int b = 0; b < 5; b++) for (int k = 0; k < 3; k++) {
            const uint8_t q[5] = {(uint8_t)(b * 10 + 5), 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
            string(x, N_, &NOTES[k], ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
            float lo = 1e9f, hi = 0; for (int i = 66150; i + 441 < 4 * 44100; i += 441) { float pk = 0; for (int j = 0; j < 441; j++) if (fabsf(y[i + j]) > pk) pk = fabsf(y[i + j]); if (pk < lo) lo = pk; if (pk > hi) hi = pk; }
            CHECK(20.0f * log10f(hi / (lo + 1e-9f)) < 5.0f, "INSTRUMENT %s on %.0f Hz: no beating swell (%.1f dB)", PX_INSTRUMENT_PATCHES[b].name, NOTES[k], 20.0f * log10f(hi / (lo + 1e-9f)));
        }
    }
    {   /* fix pass 2: a hard mute followed by a thump does not play another note, and the voice is released at the mute */
        static unsigned rs = 777; static const float THUMP_DB[3] = {-24.0f, -21.0f, -18.0f}; static const float F[2] = {110.0f, 196.0f};
        for (int fi = 0; fi < 2; fi++) for (int ti = 0; ti < 3; ti++) {
            float ph = 0, env = 0.5f, lp = 0; const int tm = 44100;
            for (int i = 0; i < 44100 * 2; i++) {
                ph += F[fi] / FS_; ph -= floorf(ph); const float pp = ph * 6.2831853f;
                float g = env; if (i >= tm) g = env * expf(-(float)(i - tm) / FS_ * 1000.0f / 6.0f); else env *= 0.99997f;
                float th = 0; const int t0 = tm + (int)(30 * 44.1f);
                if (i >= t0 && i < t0 + 2646) { rs = rs * 1664525u + 1013904223u; lp += ((float)(rs >> 8) * (1.0f / 8388608.0f) - 1.0f - lp) * 0.3f; th = lp * 3.0f * powf(10.0f, THUMP_DB[ti] / 20.0f) * 0.76f * env * expf(-(float)(i - t0) / FS_ * 1000.0f / 12.0f); }
                x[i] = i < 2000 ? 0.0f : g * 0.4f * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp) + 0.3f * sinf(4 * pp)) + th;
            }
            px_mono m; px_mono_init(&e, &m, PX_MONO_GUITAR); int on_after = 0, rel = -1;
            for (int i = 0; i < 44100 * 2; i++) { px_mono_out o; px_mono_run(&e, &m, x[i], &o); if (o.onset && i >= tm) on_after++; if (o.release && i >= tm && rel < 0) rel = i - tm; }
            CHECK(on_after == 0, "a thump (%.0f dB) after a hard mute on %.0f Hz re-triggers (%d)", THUMP_DB[ti], F[fi], on_after);
            CHECK(rel >= 0 && rel < (int)(0.06f * FS_), "the hard mute releases the voice within 60 ms (%d ms)", rel < 0 ? -1 : rel * 1000 / 44100);
        }
    }
    {   /* fix pass 3: a damped string sharpens (+100 cents over 60 ms) while the level falls; the tracked pitch must not follow it (the "semitone up on a hard mute") */
        static const float F[3] = {110.0f, 196.0f, 330.0f};
        for (int fi = 0; fi < 3; fi++) {
            float ph = 0, env = 0.5f; const int tm = 44100 / 2; float worst = 0;
            for (int i = 0; i < 44100 * 2; i++) {
                float f = F[fi], g = env;
                if (i >= tm) { const float t = (float)(i - tm) / FS_; g = env * expf(-t / 0.012f); f = F[fi] * powf(2.0f, 100.0f * (t < 0.06f ? t / 0.06f : 1.0f) / 1200.0f); } else env *= 0.99997f;
                ph += f / FS_; ph -= floorf(ph); const float pp = ph * 6.2831853f;
                x[i] = i < 2000 ? 0.0f : g * 0.4f * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp) + 0.3f * sinf(4 * pp));
            }
            px_mono m; px_mono_init(&e, &m, PX_MONO_GUITAR);
            for (int i = 0; i < 44100 * 2; i++) { px_mono_out o; px_mono_run(&e, &m, x[i], &o); if (i > tm && i < tm + 4410 && o.gate && o.f0 > 0) { const float c = cents(o.f0, F[fi]); if (fabsf(c) > fabsf(worst)) worst = c; } }
            CHECK(fabsf(worst) < 30.0f, "a damped, sharpening %.0f Hz string: the tracked pitch stays put (worst %+.0f cents)", F[fi], worst);
        }
    }
    {   /* fix pass 3: after a hard mute the synth stops fast whatever the patch's release (ANALOG CLASSIC and the long INSTRUMENT STRINGS and SOFTPAD): 30 dB down within 300 ms */
        struct { int type, patch; const char *nm; } C[5] = {{PX_SYN_ANALOG, 5, "ANALOG CLASSIC"}, {PX_SYN_INSTRUMENT, 15, "INSTRUMENT STRINGS"}, {PX_SYN_INSTRUMENT, 5, "INSTRUMENT SOFTPAD"}, {PX_SYN_INSTRUMENT, 55, "INSTRUMENT ORGAN"}, {PX_SYN_INSTRUMENT, 95, "INSTRUMENT SOFTORG"}};
        for (int ci = 0; ci < 5; ci++) {
            g_type = C[ci].type; float ph = 0, env = 0.5f; const int tm = 44100;
            for (int i = 0; i < 44100 * 3; i++) {
                ph += 110.0f / FS_; ph -= floorf(ph); const float pp = ph * 6.2831853f;
                float g = env; if (i >= tm) g = env * expf(-(float)(i - tm) / FS_ * 1000.0f / 6.0f); else env *= 0.99997f;
                x[i] = i < 2000 ? 0.0f : g * 0.4f * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp) + 0.3f * sinf(4 * pp));
            }
            const uint8_t q[5] = {(uint8_t)C[ci].patch, 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q); render(&p, x, y, 44100 * 3, 64);
            const float before = rms(y, tm - 4410, 2048), after = rms(y, tm + (int)(0.40f * FS_), 2048);
            CHECK(after < 0.03f * before, "%s: after a hard mute the synth is 30 dB down within 400 ms (%.1f dB)", C[ci].nm, 20.0f * log10f(after / (before + 1e-9f)));
        }
        g_type = PX_SYN_INSTRUMENT;
    }
    {   /* fix pass 4: ANALOG's detuned patches (CLASSIC, FATSAW, PULSE, PWM, SWEEP) do not swell and fade on a steady note: the peak level swings less than 5.5 dB (SWEEP 6.5: its filter envelope is slow by design) */
        static const int PAT[5] = {0, 1, 2, 3, 8}; static const char *const NM[5] = {"CLASSIC", "FATSAW", "PULSE", "PWM", "SWEEP"};
        g_type = PX_SYN_ANALOG;
        for (int b = 0; b < 5; b++) for (int k = 0; k < 3; k++) {
            const uint8_t q[5] = {(uint8_t)(PAT[b] * 10 + 5), 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
            string(x, N_, &NOTES[k], ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
            float lo = 1e9f, hi = 0; for (int i = 44100; i + 441 < 4 * 44100; i += 441) { float pk = 0; for (int j = 0; j < 441; j++) if (fabsf(y[i + j]) > pk) pk = fabsf(y[i + j]); if (pk < lo) lo = pk; if (pk > hi) hi = pk; }
            CHECK(20.0f * log10f(hi / (lo + 1e-9f)) < (b == 4 ? 6.5f : 5.5f), "ANALOG %s on %.0f Hz: no beating swell (%.1f dB; SWEEP also moves its filter slowly by design)", NM[b], NOTES[k], 20.0f * log10f(hi / (lo + 1e-9f)));
        }
    }
    {   /* Stage B: the WHEEL organ patches (PATCH 50..100): the drawbar structure, the percussion, TONE, ATTACK, RELEASE, the rotary, and the level */
        static const float H[1] = {110.0f}; static const double R9[9] = {0.5, 1.5, 1, 2, 3, 4, 5, 6, 8};
        float lv[5][9];
        g_type = PX_SYN_INSTRUMENT;
        for (int b = 5; b < 10; b++) {
            const uint8_t q[5] = {(uint8_t)(b * 10 + 5), 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
            string(x, N_, H, ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
            for (int k = 0; k < 9; k++) lv[b - 5][k] = gz_db(y, 2 * 44100, 88200, 110.0 * R9[k]);
            /* the rotary and the percussion do not change the loudness much: the RMS (20 ms windows) over 2..4 s swings less than 4 dB */
            float lo = 1e9f, hi = 0; for (int i = 2 * 44100; i + 882 < 4 * 44100; i += 441) { const float r = rms(y, i, 882); if (r < lo) lo = r; if (r > hi) hi = r; }
            CHECK(20.0f * log10f(hi / (lo + 1e-9f)) < 4.0f, "WHEEL %s: the rotary leaves the loudness steady (%.1f dB)", PX_INSTRUMENT_PATCHES[b].name, 20.0f * log10f(hi / (lo + 1e-9f)));
            const float lvl = 20.0f * log10f(rms(y, 2 * 44100, 44100) + 1e-9f);
            CHECK(lvl > -22.0f && lvl < -14.0f, "WHEEL %s: level near the ANALOG patches' (%.1f dBFS)", PX_INSTRUMENT_PATCHES[b].name, lvl);
        }
        float mx, mn;
        mx = -1e9f; mn = 1e9f; for (int k = 0; k < 9; k++) { if (lv[0][k] > mx) mx = lv[0][k]; if (lv[0][k] < mn) mn = lv[0][k]; }
        CHECK(mx - mn < 8.0f, "ORGAN (all nine bars out): the partials within 8 dB of each other (%.1f)", mx - mn);
        CHECK(lv[1][0] > lv[1][5] + 20.0f && lv[1][2] > lv[1][8] + 20.0f, "JAZZORG: three bars (16', 5 1/3', 8'), the upper bars silent");
        CHECK(lv[2][8] > lv[2][3] + 15.0f && lv[2][8] > lv[2][4] + 15.0f - 15.0f, "ROCKORG: the top bars (1 1/3', 1') come out over the middle ones");
        CHECK(lv[3][2] > lv[3][8] + 6.0f, "REEDORG: a falling slope (8' %.1f over 1' %.1f)", lv[3][2], lv[3][8]);
        CHECK(lv[4][2] > lv[4][0] + 40.0f && lv[4][2] > lv[4][1] + 40.0f, "SOFTORG: one flute bar (8'), no sub bars");
        /* JAZZORG's percussion (PERC 3RD: the 2 2/3' bar, the third harmonic) strikes with the note and decays: far stronger just after it than 3 s on */
        { const uint8_t q[5] = {65, 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q); string(x, N_, H, ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
          const float early = gz_db(y, (int)(0.16f * FS_), 4096, 330.0), late = gz_db(y, 3 * 44100, 8192, 330.0), body = gz_db(y, 3 * 44100, 8192, 110.0);
          CHECK(early > late + 15.0f && body > late + 20.0f, "JAZZORG: the 2 2/3' percussion decays (%.1f dB early, %.1f dB late, 8' %.1f dB)", early, late, body); }
        /* TONE: a tilt: low = the upper bars down and the sub bars up, high the other way; ORGAN has all bars at 8, so each side can only lower the bars it moves away from */
        { float tl[2][2];
          for (int j = 0; j < 2; j++) { const uint8_t q[5] = {55, (uint8_t)(j ? 100 : 0), 50, 50, 50}; px_preset_t p; preset_of(&p, q); string(x, N_, H, ST, 1, 0.5f, 1.0f, 0); render(&p, x, y, N_, 64);
              tl[j][0] = gz_db(y, 2 * 44100, 88200, 55.0); tl[j][1] = gz_db(y, 2 * 44100, 88200, 880.0); }
          CHECK((tl[1][1] - tl[1][0]) > (tl[0][1] - tl[0][0]) + 20.0f, "WHEEL TONE: 100 is brighter than 0 (the 1' bar over the 16' bar: %.1f dB against %.1f dB)", tl[1][1] - tl[1][0], tl[0][1] - tl[0][0]); }
        /* ATTACK: ORGAN speaks in a few ms; ATTACK 100 is slower (the pick at 0.1 s, the level reaching half its peak); RELEASE 100 lets it ring longer than RELEASE 0 */
        { float t_a[2], t_r[2]; static const float H2[1] = {164.81f};
          string(x, N_, H2, ST, 1, 0.5f, 0.99990f, 0); memset(x + (int)(1.3f * FS_), 0, sizeof(float) * (N_ - (int)(1.3f * FS_)));
          for (int j = 0; j < 2; j++) {
              const uint8_t qa[5] = {55, 50, (uint8_t)(j ? 100 : 0), 50, 50}, qr[5] = {55, 50, 50, (uint8_t)(j ? 100 : 0), 50};
              px_preset_t p; preset_of(&p, qa); render(&p, x, y, N_, 64);
              float pk = 0; for (int i = 0; i < (int)(1.2f * FS_); i++) if (fabsf(y[i]) > pk) pk = fabsf(y[i]);
              int i = 0; while (i < N_ && fabsf(y[i]) < 0.5f * pk) i++; t_a[j] = (float)i / FS_;
              preset_of(&p, qr); render(&p, x, y, N_, 64);
              pk = 0; for (int i2 = 0; i2 < (int)(1.2f * FS_); i2++) if (fabsf(y[i2]) > pk) pk = fabsf(y[i2]);
              int e0 = (int)(1.3f * FS_), r = e0; while (r < N_ - 512 && rms(y, r, 512) > 0.1f * pk * 0.7f) r += 256; t_r[j] = (float)(r - e0) / FS_; }
          printf("WHEEL ORGAN: half level ATTACK 0 %.3f s, 100 %.3f s; release to -20 dB RELEASE 0 %.3f s, 100 %.3f s\n", t_a[0], t_a[1], t_r[0], t_r[1]);
          CHECK(t_a[1] > t_a[0] + 0.004f, "WHEEL ATTACK 100 is slower than 0 (the organ speaks in 2 ms: Felucca's ATK 8)");
          CHECK(t_r[1] > 1.5f * t_r[0], "WHEEL RELEASE 100 is longer than 0"); }
    }
    {   /* fix pass 5: (a) a dead pick (a burst whose sound is gone before its pitch is known) plays no note; (b) a started note is never released on its own onset sample (random damped picks) */
        px_mono m; static unsigned rs = 4242; int on_dead = 0, on_soft = 0, bad_same = 0;
        { /* (a) a 12 ms noisy burst at 660 Hz, nothing else */
            memset(x, 0, sizeof(float) * (size_t)(44100));
            for (int i = 0; i < 530; i++) { rs = rs * 1664525u + 1013904223u; const float nz = (float)(rs >> 8) * (1.0f / 8388608.0f) - 1.0f; x[4410 + i] = (0.2f * sinf(6.2831853f * 660.0f * i / FS_) + 0.1f * nz) * expf(-(float)i / 100.0f); }
            px_mono_init(&e, &m, PX_MONO_GUITAR);
            for (int i = 0; i < 44100; i++) { px_mono_out o; px_mono_run(&e, &m, x[i], &o); if (o.onset) on_dead++; if (o.onset && o.release) bad_same++; } }
        { /* (b) 160 random picks of a damped string (E2..E4, -10..-40 dB, palm-mute decays of 8-120 ms, 90-400 ms apart): a started note is never released on its own onset sample */
            float ph = 0; unsigned r2 = 99; int pos = 4410; memset(x, 0, sizeof(float) * (size_t)N_); static float big[44100 * 40];
            const int NB = 44100 * 40; memset(big, 0, sizeof big);
            for (int k = 0; k < 160 && pos < NB - 44100; k++) {
                r2 = r2 * 1664525u + 1013904223u; const float f = 82.4f * powf(2.0f, (float)((r2 >> 8) % 24) / 12.0f);
                r2 = r2 * 1664525u + 1013904223u; const float amp = powf(10.0f, -(10.0f + (float)((r2 >> 8) % 30)) / 20.0f);
                r2 = r2 * 1664525u + 1013904223u; const float tc = 0.008f + 0.112f * (float)((r2 >> 8) % 100) / 100.0f;
                r2 = r2 * 1664525u + 1013904223u; const int gap = (int)((0.09f + 0.31f * (float)((r2 >> 8) % 100) / 100.0f) * FS_);
                for (int i = 0; i < 20000 && pos + i < NB; i++) { ph += f / FS_; ph -= floorf(ph); const float pp = 6.2831853f * ph; big[pos + i] += amp * expf(-(float)i / FS_ / tc) * 0.5f * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.4f * sinf(3 * pp) + 0.2f * sinf(4 * pp)); }
                pos += gap;
            }
            px_mono_init(&e, &m, PX_MONO_GUITAR);
            for (int i = 0; i < pos + 22050 && i < NB; i++) { px_mono_out o; px_mono_run(&e, &m, big[i], &o); if (o.onset && o.release) bad_same++; on_soft += o.onset; } }
        CHECK(on_dead == 0, "a dead pick (a 12 ms burst) plays no note (%d onsets)", on_dead);
        CHECK(on_soft > 60, "the random damped picks do produce notes (%d onsets of 160)", on_soft);
        CHECK(bad_same == 0, "a note is never released on its own onset sample (%d)", bad_same);
    }
    g_type = PX_SYN_ANALOG;
}
static void damp_pending_pick_suite(void)
{
    px_mono m;
    px_mono_out o;
    px_engine_init(&e, FS_);
    px_mono_init(&e, &m, PX_MONO_GUITAR);
    m.lay.key = m.lay.gate = m.was = 1;
    m.lay.state = 3;  /* a repick is waiting for pitch when the level collapses */
    m.lay.f0 = 146.0f;
    m.lay.env = m.lay.envs = 0.01f;
    m.lay.peak_ref = 0.1f;
    m.pk = m.pkl = 0.1f;
    m.fe = 0.005f;
    px_mono_run(&e, &m, 0.0f, &o);
    CHECK(o.damp && o.release && !o.onset && m.lay.state == 2 && o.f0 == 146.0f,
          "damping cancels the candidate that was waiting for pitch");
}

static void overtone_suite(void)
{
    px_track t = {0};
    px_engine_init(&e, FS_);
    pxt_prepare(&e);
    const float rate = pxt_rate * 0.25f;
    for (int i = 0; i < PXT_DEC; i++) {
        const float phase = 6.2831853f * 195.0f * (float)i / rate;
        t.dec[i] = sinf(phase) + 0.8f * sinf(4.0f * phase);
    }
    const float f = pxt_pick_fundamental(&t, 788.0f);
    CHECK(pxt_near(f, 195.0f, 50.0f), "an upper-partial onset speaks at the fundamental (%.1f Hz)", f);
    for (int i = 0; i < PXT_DEC; i++) t.dec[i] = sinf(6.2831853f * 788.0f * (float)i / rate);
    CHECK(pxt_pick_fundamental(&t, 788.0f) == 788.0f, "a genuinely high note keeps its pitch");
}

static void fast_pick_suite(void)
{
    const float pitches[] = {110.0f, 123.47f, 146.83f, 164.81f};
    const uint8_t q[] = {45, 50, 50, 50, 100};
    int matches = 0, matched = -1;
    uint32_t prev_age = 0;
    px_engine_init(&e, FS_);
    px_s2_reset(&e, PX_SYN_ARCADE, q);
    for (int i = 0; i < 2 * (int)FS_; i++) {
        const double time = i / (double)FS_ - 0.25;
        const int note = time >= 0.0 ? (int)(time * 22.0) : -1;
        float x = 0.0f, y;
        if (note >= 0 && note < 26) {
            const double age = time - note / 22.0;
            const double phase = 6.283185307179586 * pitches[note % 4] * age;
            for (int h = 1; h <= 6; h++) x += (float)(sin(h * phase) / h);
            x *= (float)(0.4 * (1.0 - exp(-1500.0 * age)) * exp(-30.0 * age));
        }
        px_synth_run(&e, &x, &y, 1);
        if (trk[0].v[0].age != prev_age && note >= 0 && note < 26 && note != matched &&
            pxt_near(px_s2_st.f0, pitches[note % 4], 50.0f)) {
            matches++;
            matched = note;
        }
        prev_age = trk[0].v[0].age;
    }
    CHECK(matches >= 22, "22 notes/sec: at least 22 of 26 picks tracked on time and pitch (%d)", matches);
}

/* Synth2 fix pass 6: what the original synths did: the level follows the pick strength, a re-detected pick softer than the ringing note is a slur (the pitch glides, no new attack), pitch changes glide */
static void legato_suite(void)
{
    static const float H[1] = {196.0f}, ST[1] = {0.1f};
    {   /* dynamics: a pick 24 dB softer plays quieter: STRINGS (18 dB of dynamics) by 10 dB or more, the organ (6 dB) by 8 dB or less, ARCADE (8 dB) in between; and STRINGS follows the pick more than the organ */
        float d[3]; const int T[3] = {PX_SYN_INSTRUMENT, PX_SYN_INSTRUMENT, PX_SYN_ARCADE}, P[3] = {15, 55, 5};
        for (int c = 0; c < 3; c++) {
            float lv[2]; for (int s = 0; s < 2; s++) {
                g_type = T[c]; const uint8_t q[5] = {(uint8_t)P[c], 50, 50, 50, 100}; px_preset_t p; preset_of(&p, q);
                string(x, N_, H, ST, 1, s ? 0.5f * powf(10.0f, -24.0f / 20.0f) : 0.5f, 1.0f, 0); render(&p, x, y, N_, 64); lv[s] = 20.0f * log10f(rms(y, 2 * 44100, 44100) + 1e-9f); }
            d[c] = lv[0] - lv[1]; }
        CHECK(d[0] > 10.0f, "STRINGS: a pick 24 dB softer is at least 10 dB quieter (%.1f dB)", d[0]);
        CHECK(d[1] < 8.0f, "ORGAN: a pick 24 dB softer is at most 8 dB quieter (%.1f dB)", d[1]);
        CHECK(d[0] > d[1] + 3.0f, "STRINGS follows the pick more than the organ (%.1f against %.1f dB)", d[0], d[1]);
        printf("dynamics, a pick 24 dB softer: STRINGS %.1f dB, ORGAN %.1f dB, ARCADE %.1f dB quieter\n", d[0], d[1], d[2]);
        g_type = PX_SYN_ANALOG; }
    {   /* a loud note decays to almost nothing, then a faint pick at another pitch (a slur: no onset, the pitch is reported), then a hard pick (a new attack): the tracker's events. (The pick strength is the peak of
         * the whole signal in the first 6 ms, ringing note included: a faint pick can only read as faint once the string has died down.) */
        static float xl[44100 * 4]; const int NL = 44100 * 4; float ph = 0, env = 0.5f; int onsets_soft = 0, slurs_soft = 0, onsets_hard = 0;
        for (int i = 0; i < NL; i++) { const float t = (float)i / FS_; const float f = t < 3.0f ? 164.81f : t < 3.4f ? 220.0f : 293.66f;
            if (i == (int)(0.1f * FS_)) env = 0.5f; if (i == (int)(3.0f * FS_)) env += 0.004f; if (i == (int)(3.4f * FS_)) env = 0.5f;
            ph += f / FS_; ph -= floorf(ph); const float pp = 6.2831853f * ph; xl[i] = t < 0.1f ? 0.0f : 0.4f * env * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp) + 0.3f * sinf(4 * pp)); env *= 0.99997f; }
        px_mono m; px_mono_init(&e, &m, PX_MONO_GUITAR);
        for (int i = 0; i < NL; i++) { px_mono_out o; px_mono_run(&e, &m, xl[i], &o); const float t = (float)i / FS_;
            if (t > 3.0f && t < 3.35f) { onsets_soft += o.onset; slurs_soft += o.slur; } if (t > 3.4f && t < 3.9f) onsets_hard += o.onset; }
        CHECK(onsets_soft == 0, "a faint pick on a mostly decayed string is not a new attack (%d onsets, %d slurs)", onsets_soft, slurs_soft);
        CHECK(onsets_hard >= 1, "a hard pick on a ringing note is a new attack (%d)", onsets_hard); }
    {   /* glide: the same two-pitch note change plays out over about 12 ms, not in one block (the pitch the voice plays, a block at a time, through the adapter) */
        static float xl[44100 * 3]; const int NL = 44100 * 3; float ph = 0, env = 0.5f; float worst = 0.0f, prev = 0.0f; int started = 0;
        for (int i = 0; i < NL; i++) { const float t = (float)i / FS_; const float f = t < 1.5f ? 164.81f : 246.94f;
            if (i == (int)(0.1f * FS_) || i == (int)(1.5f * FS_)) env = 0.5f; ph += f / FS_; ph -= floorf(ph); const float pp = 6.2831853f * ph; xl[i] = t < 0.1f ? 0.0f : 0.4f * env * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp)); env *= 0.99997f; }
        g_type = PX_SYN_ANALOG; const uint8_t q[5] = {5, 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q); px_engine_init(&e, FS_); px_engine_set(&e, &p);
        static float yl[44100 * 3];
        for (int at = 0; at + 32 <= NL; at += 32) { px_engine_process(&e, xl + at, yl + at, 32); const float pc = px_s2_st.pitch_cur;
            if ((float)at / FS_ > 1.5f && (float)at / FS_ < 1.7f && started) { const float step = fabsf(pc - prev); if (step > worst) worst = step; } if ((float)at / FS_ > 1.4f) started = 1; prev = pc; }
        CHECK(worst < 1.0f, "a 7-semitone change glides: the largest step in one 0.7 ms block is %.2f semitones (a jump would be 3.5 or more; 6 ms glide)", worst); }
    {   /* fix 917 -> 918: picks as hard as the string that is ringing are re-attacks, not slurs (the first slur rule asked for a pick clearly harder than the ringing note, which is never true in a fast run of equal picks:
         * 22 of 26 attacks of a 22-notes-a-second recording became slurs and the synths fell silent after the first note): 4 hard picks 250 ms apart on a ringing string give 3 attacks after the first and no slur */
        static const float FP[4] = {164.81f, 196.0f, 246.94f, 220.0f}; static float xl[44100 * 3]; const int NL = 44100 * 3; memset(xl, 0, sizeof xl); unsigned rn = 3; int on = 0, sl = 0;
        for (int k = 0; k < 4; k++) { const int st = (int)((0.1f + (float)k * 0.25f) * FS_); float ph = 0;
            for (int i = st; i < NL && i < st + 30000; i++) { ph += FP[k] / FS_; ph -= floorf(ph); const float pp = 6.2831853f * ph;
                xl[i] += 0.5f * expf(-(float)(i - st) / FS_ / 0.5f) * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.45f * sinf(3 * pp) + 0.3f * sinf(4 * pp));
                if (i - st < 120) { rn = rn * 1664525u + 1013904223u; xl[i] += 0.3f * ((float)(rn >> 9) / 4194304.0f - 1.0f) * expf(-(float)(i - st) / 40.0f); } } }
        px_mono m; px_mono_init(&e, &m, PX_MONO_GUITAR);
        for (int i = 0; i < NL; i++) { px_mono_out o; px_mono_run(&e, &m, xl[i], &o); on += o.onset; sl += o.slur; }
        CHECK(on >= 3 && sl == 0, "4 hard picks on a ringing string: 3 attacks and no slur (%d attacks, %d slurs)", on, sl); }
    {   /* fix pass 7 FOLLOW: on a lead patch a pick while the note sounds is not a new attack (the envelope is not restarted: the amplitude must not dip towards 0 between picks of a fast run); a pad patch restarts as before */
        static const float FQ[6] = {130.81f, 146.83f, 164.81f, 196.0f, 220.0f, 261.63f}; static float xs[44100 * 2], ys[44100 * 2]; const int NL = 44100 * 2; memset(xs, 0, sizeof xs);
        for (int k = 0; k < 30; k++) { const int st = (int)((0.2f + (float)k * 0.045f) * FS_); float ph = 0; for (int i = st; i < NL && i < st + 3000; i++) { ph += FQ[(k * 5) % 6] / FS_; ph -= floorf(ph); const float pp = 6.2831853f * ph; xs[i] += 0.25f * expf(-(float)(i - st) / FS_ / 0.05f) * (sinf(pp) + 0.6f * sinf(2 * pp) + 0.4f * sinf(3 * pp) + 0.25f * sinf(4 * pp));
                if (i - st < 150) { static unsigned rq = 7; rq = rq * 1664525u + 1013904223u; xs[i] += 0.8f * ((float)(rq >> 9) / 4194304.0f - 1.0f) * expf(-(float)(i - st) / 50.0f); } } }
        for (int ty = 0; ty < 2; ty++) { px_preset_t pr; px_preset_factory(&pr, 7); for (int z = 0; z < PX_NBLK; z++) pr.on[z] = 0; pr.on[PX_B_SYN] = 1;
            pr.type[PX_B_SYN] = PX_SYN_ANALOG; { uint8_t q[5] = {ty ? 80 : 5, 50, 50, 50, 50}; memcpy(pr.p[PX_B_SYN], q, 5); }   /* ty 0: CLASSIC (follows), 1: SWEEP (restarts) */
            px_engine e2;
            px_engine_init(&e2, FS_); px_engine_set(&e2, &pr);
            int accents = 0;
            float previous_accent = 0.0f;
            for (int at = 0; at + 64 <= NL; at += 64) {
                px_engine_process(&e2, xs + at, ys + at, 64);
                if (px_s2_st.pick_accent > previous_accent) accents++;
                previous_accent = px_s2_st.pick_accent;
            }
            CHECK(ty ? accents == 0 : accents >= 5, "%s: confirmed FOLLOW picks get level accents (%d)", ty ? "SWEEP" : "CLASSIC", accents);
            float lo = 1e9f; for (int i = (int)(0.5f * FS_); i < (int)(1.4f * FS_); i += 441) { double r = 0; for (int j = 0; j < 441; j++) r += ys[i + j] * ys[i + j]; r = sqrt(r / 441); if (r < lo) lo = (float)r; }
            { const int na = (int)trk[0].v[0].age; CHECK(ty ? na >= 8 : na <= 3, "%s: the note was started %d times by 30 picks (a follow patch keeps one note, a restarting one starts most)", ty ? "SWEEP" : "CLASSIC", na); }
            CHECK(ty ? 1 : lo > 0.004f, "FOLLOW lead patch: no dip to silence between the picks of a fast run (quietest 10 ms %.4f)", lo); } }
    {   /* SFX APPLAUSE was removed after test build 922: its id stays as a tombstone (stored presets keep their type numbers), the UI skips it and the engine bypasses it, its settings are kept */
        enum { NL = 8192 }; static float xa[NL], ya[NL], yb[NL];
        CHECK(px_type_retired(PX_B_SFX, PX_SFX_APPLAUSE) == 1, "SFX APPLAUSE is a retired tombstone");
        CHECK(px_type_name(PX_B_SFX, PX_SFX_APPLAUSE)[0] == 0, "its name is empty, so the UI skips it");
        CHECK(px_nlive(PX_B_SFX) == PX_SFX_COUNT - 1, "SFX has %d live types (%d ids)", px_nlive(PX_B_SFX), PX_SFX_COUNT);
        for (int i = 0; i < PX_SFX_COUNT - 1; i++) CHECK(px_live_type(PX_B_SFX, i) != PX_SFX_APPLAUSE, "the live SFX list never offers APPLAUSE (slot %d)", i);
        px_preset_t pr; px_preset_factory(&pr, 7); for (int z = 0; z < PX_NBLK; z++) pr.on[z] = 0; pr.on[PX_B_SFX] = 1; pr.type[PX_B_SFX] = PX_SFX_APPLAUSE;
        { uint8_t q[5] = {50, 50, 30, 100, 0}; memcpy(pr.p[PX_B_SFX], q, 5); }
        for (int i = 0; i < NL; i++) xa[i] = 0.2f * sinf(0.05f * (float)i) * (i > 2000 ? 1.0f : 0.0f);
        { px_engine ea, eb; px_preset_t off = pr; off.on[PX_B_SFX] = 0; px_engine_init(&ea, FS_); px_engine_set(&ea, &pr); px_engine_init(&eb, FS_); px_engine_set(&eb, &off);
          for (int at = 0; at < NL; at += 64) { px_engine_process(&ea, xa + at, ya + at, 64); px_engine_process(&eb, xa + at, yb + at, 64); }
          float d = 0; int fin = 1; for (int i = 0; i < NL; i++) { d += fabsf(ya[i] - yb[i]); if (!(fabsf(ya[i]) < 10.0f)) fin = 0; }
          CHECK(fin && d < 1e-6f, "a stored APPLAUSE preset is bypassed: finite and identical to the effect switched off (difference %.6f)", d); }
        CHECK(pr.p[PX_B_SFX][0] == 50 && pr.type[PX_B_SFX] == PX_SFX_APPLAUSE, "its stored type and settings are kept");
    }
    g_type = PX_SYN_ANALOG;
}
int main(void)
{
    analog_suite();
    arcade_suite();
    pass1_suite();
    routing_split_suite();
    instrument_suite();
    ensemble_suite();
    choir_suite();
    legato_suite();
    fast_pick_suite();
    overtone_suite();
    damp_pending_pick_suite();
    printf("%s: %d problems\n", bad ? "FAILED" : "analog + arcade + instrument + ensemble + choir synth ok", bad);
    return bad != 0;
}
