/* Synth2 fix pass 1: LOWEST OCTAVE HQ's own gain (PX_LOWEST_HQ_GAIN). Prints the wet RMS (dry off) of a low-E pluck, an A-string note and a power chord, and
 * the same of POLY OCTAVE HQ; tests/run_tests.sh builds it with the gain 1.0 (the v73 level) and the shipped one and checks the ratio, and that POLY OCTAVE HQ did not move. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "px_dsp.c"
static float run(int type, const float *hz, int n)
{
    static px_engine e; static float x[44100 * 2], y[44100 * 2];
    px_engine_init(&e, 44100.0f);
    px_preset_t p; px_preset_factory(&p, 0);
    for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
    p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = (uint8_t)type; p.p[PX_B_PITCH][0] = 0; p.p[PX_B_PITCH][1] = 100; p.p[PX_B_PITCH][2] = 0; p.p[PX_B_PITCH][3] = 1;
    px_engine_set(&e, &p);
    float ph[3] = {0};
    for (int i = 0; i < 44100 * 2; i++) {
        float v = 0, env = expf(-(float)i / 44100.0f * 1.5f) * 0.3f;
        for (int k = 0; k < n; k++) { ph[k] += hz[k] / 44100.0f; ph[k] -= floorf(ph[k]); const float q = ph[k] * 6.2831853f; v += env * (sinf(q) + 0.5f * sinf(2 * q) + 0.3f * sinf(3 * q)); }
        x[i] = v;
    }
    for (int i = 0; i + 64 <= 44100 * 2; i += 64) px_engine_process(&e, x + i, y + i, 64);
    double s = 0; for (int i = 22050; i < 44100; i++) s += (double)y[i] * y[i];
    return (float)sqrt(s / 22050);
}
int main(void)
{
    const float e2[1] = {82.41f}, a2[1] = {110.0f}, pc[2] = {82.41f, 123.47f};
    printf("%.7f %.7f %.7f %.7f\n", run(PX_PIT_LOWEST_HQ, e2, 1), run(PX_PIT_LOWEST_HQ, a2, 1), run(PX_PIT_LOWEST_HQ, pc, 2), run(PX_PIT_POLYOCT_HQ, pc, 2));
    return 0;
}
