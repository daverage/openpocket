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
        for (int w = 0; w < 20; w++) { const float f = f0_of(y, 4410 + w * 882, 800, 80, 260); if (f > 0) { const float r = f / 110.0f; if (fabsf(cents(r, 1.0f)) < 60) seen[0]++; else if (fabsf(cents(r, 1.2599f)) < 60) seen[1]++; else if (fabsf(cents(r, 1.4983f)) < 60) seen[2]++; } }
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
        string(x, N_, NOTE, ST, 1, 0.5f, 0.99999f, 0); render(&p, x, y, N_, 64);
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
            string(x, N_, H, S2, 2, 0.5f, 0.9998f, 0); px_preset_t p; preset_of(&p, q); render(&p, x, y, N_, 64);
            const float a = f0_of(y, (int)(1.2f * FS_), 4096, 38.0f, 80.0f), b = f0_of(y, (int)(2.7f * FS_), 4096, 230.0f, 460.0f);
            CHECK(a > 0 && b > 0, "a low then a high note: each in its own range (%.1f, %.1f Hz)", a, b);
        }
    }
    g_type = PX_SYN_ARCADE;
}
/* ENSEMBLE (Stage A): Felucca ANALOG's SOFT PAD / STRINGS / BRASS patches and two variations, played by the mono tracker (docs/research/synth2/SYNTH2-ENSEMBLE.md) */
static void ensemble_suite(void)
{
    g_type = PX_SYN_ENSEMBLE;
    static const float NOTES[3] = {82.41f, 164.81f, 329.63f}, ST[1] = {0.1f};
    for (int b = 0; b < PX_ENS_DEFINED; b++) for (int k = 0; k < 3; k++) {
        const uint8_t q[5] = {(uint8_t)(b * 10 + 5), 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
        string(x, N_, &NOTES[k], ST, 1, 0.5f, 0.99997f, 0); render(&p, x, y, N_, 64);
        const float got = f0_of(y, (int)(1.0f * FS_), 4096, NOTES[k] * 0.7f, NOTES[k] * 1.4f);
        CHECK(got > 0 && fabsf(cents(got, NOTES[k])) < 25.0f, "ENSEMBLE %s on %.1f Hz: pitch within 25 cents (%.1f)", PX_ENSEMBLE_PATCHES[b].name, NOTES[k], got);
    }
    {   /* the patch identity: slots 0..4 are final, 5..9 and 100 play slot 4 until filled; names read from the table */
        CHECK(px_ens_slot(0) == 0 && px_ens_slot(9) == 0 && px_ens_slot(10) == 1 && px_ens_slot(49) == 4 && px_ens_slot(50) == 4 && px_ens_slot(100) == 4, "ENSEMBLE slots: 10 values each, reserved slots play the last defined");
        char buf[8]; px_param_text(PX_B_SYN, PX_SYN_ENSEMBLE, 0, 10, buf); CHECK(!strcmp(buf, "STRINGS"), "ENSEMBLE patch 10 reads STRINGS (%s)", buf);
        px_param_text(PX_B_SYN, PX_SYN_ENSEMBLE, 0, 20, buf); CHECK(!strcmp(buf, "BRASS"), "ENSEMBLE patch 20 reads BRASS (%s)", buf);
        CHECK(px_nparams(PX_B_SYN, PX_SYN_ENSEMBLE) == 5 && !strcmp(px_type_name(PX_B_SYN, PX_SYN_ENSEMBLE), "ENSEMBLE"), "ENSEMBLE has five controls");
    }
    {   /* ATTACK and RELEASE: longer at 100 than at 0 (STRINGS, E3 for 1.2 s then silence) */
        static const float H[1] = {164.81f}; string(x, N_, H, ST, 1, 0.5f, 0.99990f, 0); memset(x + (int)(1.3f * FS_), 0, sizeof(float) * (N_ - (int)(1.3f * FS_)));
        float t_att[2], t_rel[2];
        for (int j = 0; j < 2; j++) {
            const uint8_t qa[5] = {10, 50, (uint8_t)(j ? 100 : 0), 50, 50}, qr[5] = {10, 50, 50, (uint8_t)(j ? 100 : 0), 50};
            px_preset_t p; preset_of(&p, qa); render(&p, x, y, N_, 64);
            float pk = 0; for (int i = 0; i < (int)(1.2f * FS_); i++) if (fabsf(y[i]) > pk) pk = fabsf(y[i]);
            int i = 0; while (i < N_ && fabsf(y[i]) < 0.5f * pk) i++; t_att[j] = (float)i / FS_;
            preset_of(&p, qr); render(&p, x, y, N_, 64);
            pk = 0; for (int i2 = 0; i2 < (int)(1.2f * FS_); i2++) if (fabsf(y[i2]) > pk) pk = fabsf(y[i2]);
            int e0 = (int)(1.3f * FS_), r = e0; while (r < N_ - 512 && rms(y, r, 512) > 0.1f * pk * 0.7f) r += 256; t_rel[j] = (float)(r - e0) / FS_;
        }
        printf("ENSEMBLE STRINGS: time to half level ATTACK 0 %.3f s, 100 %.3f s; release to -20 dB RELEASE 0 %.3f s, 100 %.3f s\n", t_att[0], t_att[1], t_rel[0], t_rel[1]);
        CHECK(t_att[1] - 0.1f > 1.6f * (t_att[0] - 0.1f), "ATTACK 100 is slower than 0 (from the pick at 0.1 s)");
        CHECK(t_rel[1] > 1.5f * t_rel[0], "RELEASE 100 is longer than 0");
    }
    {   /* a slide, a repicked note, a release (SOFTPAD, STRINGS, BRASS) */
        for (int b = 0; b < 3; b++) {
            const uint8_t q[5] = {(uint8_t)(b * 10 + 5), 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
            static const float H[1] = {164.81f}; string(x, 44100 * 3, H, ST, 1, 0.5f, 0.99999f, 196.0f); render(&p, x, y, 44100 * 3, 64);
            const float f2 = f0_of(y, (int)(1.5f * FS_), 4096, 120, 260), w2 = 164.81f + 31.19f * 1.5f / 3.0f;
            CHECK(f2 > 0 && fabsf(cents(f2, w2)) < 50.0f, "ENSEMBLE %s follows a slide (%.1f Hz, want %.1f)", PX_ENSEMBLE_PATCHES[b].name, f2, w2);
            static const float H2[2] = {110.0f, 110.0f}, ST2[2] = {0.1f, 1.0f}; string(x, 44100 * 2, H2, ST2, 2, 0.5f, 0.9999f, 0); memset(x + 44100 * 2, 0, sizeof(float) * (N_ - 44100 * 2));
            render(&p, x, y, N_, 64);
            const float mid = rms(y, (int)(0.8f * FS_), 4096), soon = rms(y, (int)(2.3f * FS_), 4096), late = rms(y, (int)(4.8f * FS_), 4096);
            CHECK(mid > 0.005f, "ENSEMBLE %s sounds while the guitar plays (%.4f)", PX_ENSEMBLE_PATCHES[b].name, mid);
            /* the pad and strings release is long by design (Felucca REL 90-95: a time constant of 1.4-1.7 s): the tail must keep falling, and RELEASE 0 must end it */
            CHECK(late < 0.5f * soon && late < 0.35f * mid, "ENSEMBLE %s's tail keeps falling after the guitar stops (%.4f, %.4f, %.4f)", PX_ENSEMBLE_PATCHES[b].name, mid, soon, late);
            const uint8_t q0[5] = {(uint8_t)(b * 10 + 5), 50, 50, 0, 50}; px_preset_t p0; preset_of(&p0, q0); render(&p0, x, y, N_, 64);
            const float end0 = rms(y, (int)(4.8f * FS_), 4096), mid0 = rms(y, (int)(0.8f * FS_), 4096);
            CHECK(end0 < 0.05f * mid0, "ENSEMBLE %s with RELEASE 0 has ended 2.8 s after the guitar (%.5f against %.4f): no stuck note", PX_ENSEMBLE_PATCHES[b].name, end0, mid0);
        }
    }
    {   /* the same five bytes, the same sound, and the call size does not matter */
        static const float H[2] = {146.83f, 196.0f}, ST2[2] = {0.1f, 1.2f}; const uint8_t q[5] = {30, 60, 40, 55, 50}; px_preset_t p; preset_of(&p, q);
        string(x, N_, H, ST2, 2, 0.4f, 0.99995f, 0); render(&p, x, y, N_, 64); render(&p, x, y2, N_, 64);
        CHECK(!memcmp(y, y2, sizeof y), "ENSEMBLE: the same settings and input give the same output");
        const int n = (N_ / 64) * 64; render(&p, x, y2, n, 48);
        float d = 0; for (int k = 0; k < n; k++) { const float a = fabsf(y[k] - y2[k]); if (a > d) d = a; }
        CHECK(d == 0.0f, "ENSEMBLE: the output does not depend on the call size (%g)", d);
    }
    {   /* every patch (and the reserved slots), the macros at 0 / 50 / 100: finite, bounded, silent on silence */
        static const float H[2] = {98.0f, 196.0f}, ST2[2] = {0.05f, 0.8f}; static const uint8_t V[3] = {0, 50, 100}; float peak_all = 0; int n = 0;
        string(x, 44100, H, ST2, 2, 0.7f, 0.99995f, 0);
        for (int pa = 0; pa < 10; pa++) for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++) {
            const uint8_t q[5] = {(uint8_t)(pa * 10 + 5), V[a], V[b], V[c], V[d]}; px_preset_t p; preset_of(&p, q);
            render(&p, x, y, 44100, 64);
            int ok = 1; float pk = 0; for (int i = 0; i < 44100; i++) { if (y[i] != y[i]) ok = 0; if (fabsf(y[i]) > pk) pk = fabsf(y[i]); }
            if (!ok || pk > 2.0f) { bad++; printf("FAIL ENSEMBLE patch %d macros %d %d %d %d: %s peak %.2f\n", pa, V[a], V[b], V[c], V[d], ok ? "too loud" : "NaN", pk); }
            if (pk > peak_all) peak_all = pk; n++;
            static float z[8192]; memset(z, 0, sizeof z); render(&p, z, y2, 8192, 64);
            for (int i = 0; i < 8192; i++) if (fabsf(y2[i]) > 1e-6f) { bad++; printf("FAIL ENSEMBLE patch %d: not silent on silence\n", pa); break; }
        }
        printf("ENSEMBLE: %d patch/macro combinations: all finite, loudest peak %.2f\n", n, peak_all);
    }
    {   /* fix pass 2: a steady note does not swell and fade (the detuned oscillators' beating): the peak level swings less than 5 dB */
        for (int b = 0; b < PX_ENS_DEFINED; b++) for (int k = 0; k < 3; k++) {
            const uint8_t q[5] = {(uint8_t)(b * 10 + 5), 50, 50, 50, 50}; px_preset_t p; preset_of(&p, q);
            string(x, N_, &NOTES[k], ST, 1, 0.5f, 0.99999f, 0); render(&p, x, y, N_, 64);
            float lo = 1e9f, hi = 0; for (int i = 44100; i + 441 < 4 * 44100; i += 441) { float pk = 0; for (int j = 0; j < 441; j++) if (fabsf(y[i + j]) > pk) pk = fabsf(y[i + j]); if (pk < lo) lo = pk; if (pk > hi) hi = pk; }
            CHECK(20.0f * log10f(hi / (lo + 1e-9f)) < 5.0f, "ENSEMBLE %s on %.0f Hz: no beating swell (%.1f dB)", PX_ENSEMBLE_PATCHES[b].name, NOTES[k], 20.0f * log10f(hi / (lo + 1e-9f)));
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
    g_type = PX_SYN_ANALOG;
}
int main(void)
{
    analog_suite();
    arcade_suite();
    pass1_suite();
    routing_split_suite();
    ensemble_suite();
    printf("%s: %d problems\n", bad ? "FAILED" : "analog + arcade + ensemble synth ok", bad);
    return bad != 0;
}
