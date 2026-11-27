/* Per-patch HUSH: independent noise interval and wanted upper harmonics; bypass scope. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 4 };
static float x[N], ya[N], yb[N];
static px_engine e;
static void gate(px_preset_t *p, int type, int on)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_GATE] = (uint8_t)on; p->type[PX_B_GATE] = (uint8_t)type;
    p->p[PX_B_GATE][0] = 50; p->p[PX_B_GATE][1] = 50; p->p[PX_B_GATE][2] = 0; p->p[PX_B_GATE][3] = 50; p->p[PX_B_GATE][4] = 50;
}
static void run(int hush, const px_preset_t *p, float *out)
{
    px_global_t g; px_global_default(&g); px_preset_t q = *p; q.p[PX_B_GATE][3] = (uint8_t)hush;
    px_engine_init(&e, FS); px_engine_set(&e, &q); px_engine_globals(&e, &g);
    for (int at = 0; at + 64 <= N; at += 64) px_engine_process(&e, x + at, out + at, 64);
}
/* energy above hz (a one-pole high-pass of the difference, enough to compare) */
static float hf(const float *s, int a, int b)
{
    double q = 0; float p1 = 0, lp = 0; (void)p1;
    for (int i = a; i < b; i++) { lp += 0.45f * (s[i] - lp); const float h = s[i] - lp; q += (double)h * h; }
    return (float)sqrt(q / (b - a));
}
int main(void)
{
    px_preset_t P; gate(&P, PX_GATE_SUPPRESSOR, 1); P.p[PX_B_GATE][0] = 0;
    unsigned s = 21u;
    for (int i = 0; i < N; i++) {                       /* a note (a pluck: 220 Hz and its harmonics, decaying over about a second) plus hiss at -52 dBFS */
        const float t = (float)i / FS; float v = 0;
        for (int h = 1; h <= 14; h++) v += sinf(6.2831853f * 220.0f * (float)h * t) / (float)h;
        s = s * 1664525u + 1013904223u;
        x[i] = (t < 1.0f ? 0.25f * v * expf(-t * 3.0f) : 0.0f) + 0.0025f * ((float)(s >> 9) / 8388608.0f - 1.0f);
    }
    run(0, &P, ya); { static float z[N]; px_preset_t Q = P; run(0, &Q, z); CHECK(!memcmp(ya, z, sizeof ya), "HUSH 0: repeatable"); }
    run(100, &P, yb);
    const float n0 = hf(ya, 132300, 176384), n1 = hf(yb, 132300, 176384);                 /* the tail, 1 to 1.5 s */
    const float a0 = hf(ya, 2000, 8000), a1 = hf(yb, 2000, 8000);                    /* the attack, while it is loud */
    CHECK(n1 < n0 * 0.5f, "HUSH 100: the hiss of the tail falls by over 6 dB (%.5f -> %.5f, %.1f dB)", n0, n1, 20.0f * log10f(n1 / n0));
    CHECK(fabsf(20.0f * log10f(a1 / a0)) < 1.5f, "HUSH 100: the loud note keeps its top (%.1f dB)", 20.0f * log10f(a1 / a0));
    /* only the smart gate: the plain gate and the gate off */
    { px_preset_t G; gate(&G, PX_GATE_NORMAL, 1); run(0, &G, ya); run(100, &G, yb); CHECK(!memcmp(ya, yb, sizeof ya), "the plain gate is untouched by HUSH");
      gate(&G, PX_GATE_SUPPRESSOR, 0); run(0, &G, ya); run(100, &G, yb); CHECK(!memcmp(ya, yb, sizeof ya), "a gate that is off is untouched by HUSH"); }
    /* A quiet sustained HF sinusoid is musical energy, not a learned hiss floor. */
    {
        px_preset_t q; gate(&q, PX_GATE_SUPPRESSOR, 1); q.p[0][0] = 0; q.p[0][3] = 100;
        px_engine_init(&e, FS); px_engine_set(&e, &q);
        float g, h;
        for (int k = 0; k < 44100*3; ++k)
            suppressor_detect(&e, 0.003f*sinf(6.2831853f*8000.0f*k/FS), &g, &h);
        CHECK(e.suppressor.hf_gain > 0.999f, "a sustained quiet upper harmonic is not learned as hiss");
    }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("HUSH ok: 0 problems\n"); return 0;
}
