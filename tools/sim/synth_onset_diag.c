/* Synth2 fix pass 1: the synth's first 100 ms of a note, patch by patch, with a clean synthetic pluck (the tracker has no wrong pitch to
 * offer: whatever is left is the oscillator, the envelope or the filter). Prints per patch the output RMS in 5 ms bins and the peak/steady ratio.
 *   cc -O2 -Iengine -Ifirmware -fno-builtin -ffp-contract=off tools/sim/synth_onset_diag.c firmware/px_math.c -lm -o build/synth_onset_diag */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    const float f = argc > 1 ? (float)atof(argv[1]) : 110.0f;
    static px_engine e; static float in[44100 * 2], out[44100 * 2];
    for (int ty = 0; ty < 2; ty++) for (int pa = 0; pa < 10; pa++) {
        px_engine_init(&e, 44100.0f);
        uint8_t q[5] = { (uint8_t)(pa * 10 + 5), 50, 50, 50, 50 };
        px_s2_reset(&e, ty ? PX_SYN_ARCADE : PX_SYN_ANALOG, q);
        const int t0 = 4410;
        for (int i = 0; i < 44100 * 2; i++) {
            float x = 0; if (i >= t0) { const float t = (float)(i - t0) / 44100.0f; const float env = 0.5f * expf(-t * 3.0f) * (1.0f - expf(-t * 1500.0f));
                for (int h = 1; h <= 6; h++) x += env * (1.0f / h) * sinf(6.2831853f * f * h * t); }
            in[i] = x * 0.4f;
        }
        px_synth_run(&e, in, out, 44100 * 2);
        printf("%s %d:", ty ? "ARCADE" : "ANALOG", pa);
        float peak = 0, st = 0; int pk_bin = 0;
        for (int b = 0; b < 24; b++) { double s = 0; for (int i = 0; i < 220; i++) { const float v = out[t0 + b * 220 + i]; s += v * v; } const float r = sqrtf((float)(s / 220));
            printf(" %.3f", r); if (r > peak) { peak = r; pk_bin = b; } }
        { double s = 0; for (int i = 0; i < 2205; i++) { const float v = out[t0 + 13230 + i]; s += v * v; } st = sqrtf((float)(s / 2205)); }
        printf("  | peak bin %d, peak/steady(300ms) %.2f\n", pk_bin, st > 1e-6f ? peak / st : 0.0f);
    }
    return 0;
}
