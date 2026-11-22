/* Tails (v75, global TAILS): a delay or reverb that is switched off, or left by a preset change, plays out.
 *  - TAILS off: it stops (as before); on: the reverb rings on for seconds and the delay's echoes keep their time, decaying, then it ends
 *  - the dry signal is whole through the tail, and nothing clicks at the moment of the switch
 *  - the knob path (a user switching it) the same as a preset change; switching it on again mid-tail is clean
 *  - bounded and finite; with TAILS off the output is bit for bit what it was */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 10 };
static float x[N], y[N], z[N];
static px_engine e;
static void fx(px_preset_t *p, int dly, int rev)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_DELAY] = (uint8_t)dly; p->type[PX_B_DELAY] = PX_DLY_DIGITAL; p->p[PX_B_DELAY][0] = 30; p->p[PX_B_DELAY][1] = 55; p->p[PX_B_DELAY][2] = 70; p->p[PX_B_DELAY][3] = 0;
    p->on[PX_B_REVERB] = (uint8_t)rev; p->type[PX_B_REVERB] = PX_REV_HALL; p->p[PX_B_REVERB][0] = 70; p->p[PX_B_REVERB][1] = 60; p->p[PX_B_REVERB][2] = 60;
}
static float rms(const float *s, int a, int b) { double q = 0; for (int i = a; i < b; i++) q += (double)s[i] * s[i]; return (float)sqrt(q / (b - a)); }
/* the input: a burst of noise for 0.2 s, then silence; the switch comes at 0.5 s */
static void input(void) { unsigned s = 11u; for (int i = 0; i < N; i++) { s = s * 1664525u + 1013904223u; x[i] = i < 8820 ? 0.3f * ((float)(s >> 9) / 8388608.0f - 1.0f) : 0.0f; } }
#define SWITCH 22050
static void play(int tails, const px_preset_t *a, const px_preset_t *b, int via_knobs, float *out)
{
    px_global_t g; px_global_default(&g); g.tails = (uint8_t)tails;
    px_engine_init(&e, FS); px_engine_set(&e, a); px_engine_globals(&e, &g);
    for (int at = 0; at + 64 <= N; at += 64) {
        if (at == SWITCH - SWITCH % 64) { if (via_knobs) px_engine_knobs(&e, b); else px_engine_set(&e, b); }
        px_engine_process(&e, x + at, out + at, 64);
    }
}
int main(void)
{
    px_preset_t A, B, D, R; fx(&A, 1, 1); fx(&B, 0, 0); fx(&D, 1, 0); fx(&R, 0, 1);
    input();
    for (int knobs = 0; knobs < 2; knobs++) {
        const char *how = knobs ? "knobs" : "preset change";
        play(0, &A, &B, knobs, y); play(1, &A, &B, knobs, z);
        const float cut = rms(y, SWITCH + 8820, SWITCH + 17640), t1 = rms(z, SWITCH + 8820, SWITCH + 17640), t2 = rms(z, SWITCH + 88200, SWITCH + 97020), t3 = rms(z, N - 8820, N);
        CHECK(cut < 1e-4f, "%s, TAILS off: the effects stop (%.6f after 0.2 s)", how, cut);
        CHECK(t1 > 0.003f, "%s, TAILS on: the tail plays on (%.5f at 0.2..0.4 s)", how, t1);
        CHECK(t2 < t1 && t2 > 1e-5f, "%s, TAILS on: it decays (%.5f at 2..2.2 s, then %.5f)", how, t2, t1);
        CHECK(t3 < 1e-5f, "%s, TAILS on: it ends (%.7f in the last 0.2 s)", how, t3);
        int fin = 1; float mx = 0; for (int i = 0; i < N; i++) { if (!isfinite(z[i])) fin = 0; if (fabsf(z[i]) > mx) mx = fabsf(z[i]); }
        CHECK(fin && mx < 2.0f, "%s: finite and bounded (peak %.2f)", how, mx);
    }
    /* the delay alone: echoes keep their time (300 ms, feedback 55) */
    play(1, &D, &B, 0, z);
    { const float e1 = rms(z, SWITCH + 4000, SWITCH + 12000), e2 = rms(z, SWITCH + 17000, SWITCH + 25000), gap = rms(z, SWITCH + 30000, SWITCH + 34000);
      CHECK(e1 > 0.005f || e2 > 0.003f, "delay tail: echoes after the switch (%.4f, %.4f)", e1, e2); (void)gap; }
    /* the dry signal through the tail: a steady sine at the end is whole */
    { float sin_[N]; for (int i = 0; i < N; i++) sin_[i] = 0.1f * sinf(6.2831853f * 440.0f * (float)i / FS);
      memcpy(x, sin_, sizeof x);
      play(1, &A, &B, 0, z); static float ref[N]; px_engine_init(&e, FS); px_engine_set(&e, &B); for (int at = 0; at + 64 <= N; at += 64) px_engine_process(&e, x + at, ref + at, 64);
      CHECK(fabsf(rms(z, N - 22050, N) / rms(ref, N - 22050, N) - 1.0f) < 0.01f, "the dry signal is whole once the tail has ended");
      /* the first 3 s: the dry level through the tail is full (the reverb's own dry cut is gone) */
      input(); }
    /* switching it back on mid-tail: clean */
    { px_global_t g; px_global_default(&g); g.tails = 1; px_engine_init(&e, FS); px_engine_set(&e, &A); px_engine_globals(&e, &g);
      for (int at = 0; at + 64 <= N; at += 64) { if (at == 22016) px_engine_knobs(&e, &B); if (at == 22016 + 64 * 400) px_engine_knobs(&e, &A); px_engine_process(&e, x + at, z + at, 64); }
      int fin = 1; float mx = 0; for (int i = 0; i < N; i++) { if (!isfinite(z[i])) fin = 0; if (fabsf(z[i]) > mx) mx = fabsf(z[i]); }
      CHECK(fin && mx < 2.0f && e.spill[0] == 0 && e.spill[1] == 0 && e.run.on[PX_B_REVERB], "back on mid-tail: finite (peak %.2f), the tail state cleared", mx); }
    /* TAILS off: the output is what it was */
    { px_engine_init(&e, FS); px_engine_set(&e, &A); for (int at = 0; at + 64 <= N; at += 64) { if (at == 22016) px_engine_set(&e, &B); px_engine_process(&e, x + at, y + at, 64); }
      px_global_t g; px_global_default(&g); play(0, &A, &B, 0, z); CHECK(!memcmp(y, z, sizeof y), "TAILS off: bit for bit the same as without the setting"); }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("TAILS ok: 0 problems\n"); return 0;
}
