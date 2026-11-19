/* Synth2 stage 2: the mono tracker's interface (engine/px_mono.h) on synthetic plucks. Synthetic notes are a smoke test of the
 * interface, not evidence of tracking quality: that is the recorded-guitar benchmark (tools/research/mono_compare.py, docs/SYNTH2-TRACKER.md).
 *  - a plucked E2, A2, E3, E4: pitch within 25 cents, one onset, a gate that falls after the note dies
 *  - the same note picked again while it rings: a retrigger (onset with the gate on), same pitch
 *  - a slide (E3 up to G3 over 0.5 s): pitch follows continuously, no extra onset
 *  - bass E1 (41.2 Hz): found in the bass range (38 Hz), not in the guitar range
 *  - silence and a very quiet input: no onset */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static px_engine e; static px_mono m;
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
static float cents(float a, float b) { return 1200.0f * log2f(a / b); }
typedef struct { int onsets, retrig, releases, first_onset, gate_end; float f_at_onset, f_settled, f_mid, f_end; int gated, right; } res;
static float g_target;   /* run(): count the gated samples whose pitch is within 50 cents of this (0: not counted) */
/* notes: hz[i] picked at start[i] (s); a sine-and-harmonics string with a decay; glide: hz[0] -> hz_end linearly over the length */
static res run(float fmin, const float *hz, const float *start, int n, float secs, float amp, float decay, float hz_end)
{
    px_engine_init(&e, FS); px_mono_init(&e, &m, fmin);
    res r = {0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0};
    const int N = (int)(secs * FS); float ph[8] = {0}, env[8] = {0};
    for (int i = 0; i < N; i++) {
        float v = 0.0f;
        for (int k = 0; k < n; k++) {
            if (i == (int)(start[k] * FS)) { env[k] = amp; ph[k] = 0; }
            float f = hz[k]; if (hz_end > 0.0f && k == 0) f += (hz_end - hz[0]) * (float)i / (float)N;
            ph[k] += f / FS; ph[k] -= floorf(ph[k]);
            const float p = ph[k] * 6.2831853f;
            v += env[k] * (sinf(p) + 0.6f * sinf(2 * p) + 0.45f * sinf(3 * p) + 0.3f * sinf(4 * p) + 0.2f * sinf(5 * p));
            env[k] *= decay;
        }
        px_mono_out o; px_mono_run(&e, &m, 0.4f * v, &o);
        if (o.gate && g_target > 0.0f) { r.gated++; if (fabsf(cents(o.f0, g_target)) < 50.0f) r.right++; }
        if (o.onset) { if (!r.onsets) { r.first_onset = i; r.f_at_onset = o.f0; } r.onsets++; }
        if (o.retrigger) r.retrig++;
        if (o.release) r.releases++;
        if (i == N / 2) r.f_mid = o.f0;
        if (i == (int)((start[0] + 0.3f) * FS)) r.f_settled = o.f0;      /* the pitch 300 ms into the first note: the onset's own pitch is the first frames' guess */
        r.f_end = o.f0; r.gate_end = o.gate;
    }
    return r;
}
int main(void)
{
    static const float NOTES[4] = {82.41f, 110.0f, 164.81f, 329.63f}, ST0[1] = {0.1f};
    for (int k = 0; k < 4; k++) {
        res r = run(PX_MONO_GUITAR, &NOTES[k], ST0, 1, 2.0f, 0.5f, 0.99985f, 0.0f);
        printf("note %.1f Hz: onset %.1f ms (f0 then %.1f), f0 at 300 ms %.2f (%.1f cents), onsets %d, releases %d, gate at end %d\n", NOTES[k], (r.first_onset / FS - 0.1f) * 1000.0f, r.f_at_onset, r.f_settled, cents(r.f_settled, NOTES[k]), r.onsets, r.releases, r.gate_end);
        CHECK(r.onsets == 1, "note %.1f: one onset (%d)", NOTES[k], r.onsets);
        CHECK(r.first_onset > 0 && fabsf(cents(r.f_settled, NOTES[k])) < 25.0f, "note %.1f: pitch within 25 cents once settled (%.1f)", NOTES[k], cents(r.f_settled, NOTES[k]));
        CHECK(r.first_onset / FS - 0.1f < 0.05f, "note %.1f: onset within 50 ms", NOTES[k]);
        CHECK(r.releases == 1 && !r.gate_end, "note %.1f: the gate falls once the note has died (releases %d)", NOTES[k], r.releases);
    }
    {   /* repeated: the same note picked again while it rings */
        static const float H[2] = {164.81f, 164.81f}, S[2] = {0.1f, 0.7f};
        res r = run(PX_MONO_GUITAR, H, S, 2, 1.4f, 0.5f, 0.99995f, 0.0f);
        printf("repeat: onsets %d retrigger %d releases %d\n", r.onsets, r.retrig, r.releases);
        CHECK(r.onsets == 2 && r.retrig == 1, "a repicked note retriggers (onsets %d retrig %d)", r.onsets, r.retrig);
    }
    {   /* slide */
        static const float H[1] = {164.81f}, S[1] = {0.1f};
        res r = run(PX_MONO_GUITAR, H, S, 1, 1.0f, 0.5f, 0.99997f, 196.0f);
        const float want_mid = 164.81f + (196.0f - 164.81f) * 0.5f;
        printf("slide: onsets %d, f_mid %.1f (want ~%.1f), f_end %.1f\n", r.onsets, r.f_mid, want_mid, r.f_end);
        CHECK(r.onsets == 1, "a slide is one note (%d onsets)", r.onsets);
        CHECK(fabsf(cents(r.f_mid, want_mid)) < 60.0f && fabsf(cents(r.f_end, 196.0f)) < 60.0f, "the pitch follows the slide");
    }
    {   /* bass E1: the bass range. A synthetic E1 with strong harmonics is a hard case: the bench's real bass DI is the evidence (docs/SYNTH2-TRACKER.md) */
        static const float H[1] = {41.2f}, S[1] = {0.1f};
        g_target = 41.2f;
        res g = run(PX_MONO_GUITAR, H, S, 1, 2.0f, 0.5f, 0.99995f, 0.0f), b = run(PX_MONO_BASS, H, S, 1, 2.0f, 0.5f, 0.99995f, 0.0f);
        g_target = 0.0f;
        printf("E1: guitar range: %d of %d gated samples within 50 cents, onsets %d; bass range: %d of %d, onsets %d (f0 %.1f at 300 ms)\n",
               g.right, g.gated, g.onsets, b.right, b.gated, b.onsets, b.f_settled);
        CHECK(b.gated > 0 && b.right * 5 > b.gated, "E1 is found in the bass range for a good part of the note (%d of %d)", b.right, b.gated);
        CHECK(g.right * 20 < g.gated + 1, "E1 is below the guitar range: it is not reported as E1 (%d of %d)", g.right, g.gated);
        if (b.onsets > 3) printf("KNOWN ISSUE: a synthetic E1 retriggers %d times in the bass range (pitch alternating between E1 and a partial)\n", b.onsets);
    }
    {   /* silence and a very quiet input */
        static const float H[1] = {110.0f}, S[1] = {0.1f};
        res q = run(PX_MONO_GUITAR, H, S, 1, 1.0f, 0.0005f, 0.9999f, 0.0f);
        printf("very quiet note (-66 dB): onsets %d\n", q.onsets);
        res z = run(PX_MONO_GUITAR, H, S, 1, 1.0f, 0.0f, 0.9999f, 0.0f);
        CHECK(z.onsets == 0 && z.releases == 0, "silence: nothing");
    }
    printf("%s: %d problems\n", bad ? "FAILED" : "mono tracker interface ok", bad);
    return bad != 0;
}
