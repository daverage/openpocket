/* POLY OCTAVE HQ: the -1 octave's level against RANGE. RANGE thins the octave-down out (the bank follows fewer notes); a single low E was 7 dB quieter at RANGE 1 than at 100 (a chord 13 dB), which made the -1
 * 'very quiet' once RANGE was engaged. It now gets the make-up LOWEST OCTAVE HQ has ((100 / RANGE)^0.196). Checks: the level holds within 2 dB across RANGE 1..99; RANGE 0 and 100 are unchanged
 * (the make-up must not leak into them: an `else` once re-bound to the new statement and gave RANGE 100 the LOWEST branch, +9 dB); LOWEST OCTAVE HQ is untouched. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
static float in[88200], out[88200];
static void pluck(float *o, int n, const float *f, int nf)
{
    for (int i = 0; i < n; i++) { float v = 0; const float env = expf(-(float)(i % 44100) / 44100.0f * 2.0f);
        for (int k = 0; k < nf; k++) for (int h = 1; h <= 6; h++) v += sinf(6.2831853f * f[k] * (float)h * (float)i / 44100.0f) / (float)h;
        o[i] = 0.06f * v * env / (float)nf * 1.5f; }
}
static float level(int type, int range, const float *f, int nf)
{
    px_preset_t p; px_preset_factory(&p, 7); for (int k = 0; k < PX_NBLK; k++) p.on[k] = 0;
    p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = (uint8_t)type; p.p[PX_B_PITCH][0] = 0; p.p[PX_B_PITCH][1] = 50; p.p[PX_B_PITCH][2] = 0; p.p[PX_B_PITCH][3] = (uint8_t)range; p.p[PX_B_PITCH][4] = 0;
    static px_engine e; px_engine_init(&e, 44100.0f); px_engine_set(&e, &p); pluck(in, 88200, f, nf);
    for (int at = 0; at + 64 <= 88200; at += 64) px_engine_process(&e, in + at, out + at, 64);
    double s = 0; for (int i = 22050; i < 88200; i++) s += (double)out[i] * out[i]; return (float)(10.0 * log10(s / 66150.0 + 1e-20));
}
int main(void)
{
    const float one[1] = {82.41f}, chord[3] = {82.41f, 110.0f, 146.83f};
    const float full = level(PX_PIT_POLYOCT_HQ, 100, one, 1);
    printf("  POLY OCT HQ, E2, -1 octave: RANGE 100 %.1f dB", full);
    for (int r = 1; r < 100; r += (r < 10 ? 3 : 20)) { const float l = level(PX_PIT_POLYOCT_HQ, r, one, 1); printf(", RANGE %d %.1f", r, l); CHECK(fabsf(l - full) < 2.0f, "the -1 holds its level at RANGE %d: %.1f dB against %.1f dB at RANGE 100", r, l, full); }
    printf("\n");
    CHECK(full > -40.0f && full < -33.0f, "RANGE 100 is unchanged (%.1f dB; the LOWEST branch leaking in gave -27.4)", full);
    { const float l0 = level(PX_PIT_POLYOCT_HQ, 0, one, 1); CHECK(l0 > -40.0f && l0 < -33.0f, "RANGE 0 is unchanged (%.1f dB)", l0); }
    { const float c1 = level(PX_PIT_POLYOCT_HQ, 1, chord, 3), c100 = level(PX_PIT_POLYOCT_HQ, 100, chord, 3); CHECK(c1 > c100 - 8.0f, "a chord at RANGE 1 is no longer 13 dB down (%.1f dB against %.1f dB)", c1, c100); }
    { const float lo = level(PX_PIT_LOWEST_HQ, 25, one, 1); CHECK(lo > -27.0f && lo < -20.0f, "LOWEST OCTAVE HQ is unchanged (%.1f dB at RANGE 25)", lo); }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("POLY RANGE ok: 0 problems\n"); return 0;
}
