/* Synth2 choir evaluation: CHOIR AAH through the tracker and the adapter on steady notes. Writes OUTDIR/voice_<f>.f32 (4 s) and prints the level. usage: voice_eval OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
#define N (44100*4)
static float x[N], y[N];
int main(int argc, char **argv)
{
    static const float F[4] = {110.0f, 165.0f, 220.0f, 330.0f};
    for (int k = 0; k < 4; k++) {
        float ph = 0, env = 0.5f;
        for (int i = 0; i < N; i++) { ph += F[k] / 44100.f; ph -= floorf(ph); const float p = ph * 6.2831853f; x[i] = i < 4410 ? 0 : 0.4f * env * (sinf(p) + .6f * sinf(2 * p) + .45f * sinf(3 * p) + .3f * sinf(4 * p)); env *= 0.999995f; }
        px_preset_t pr; px_preset_factory(&pr, 7); for (int z = 0; z < PX_NBLK; z++) pr.on[z] = 0; pr.on[PX_B_SYN] = 1; pr.type[PX_B_SYN] = PX_SYN_INSTRUMENT;
        { const uint8_t q[5] = {45, 50, 50, 50, 50}; memcpy(pr.p[PX_B_SYN], q, 5); }
        px_engine e; px_engine_init(&e, 44100.f); px_engine_set(&e, &pr);
        for (int at = 0; at + 64 <= N; at += 64) px_engine_process(&e, x + at, y + at, 64);
        double r = 0; for (int i = 2 * 44100; i < 3 * 44100; i++) r += (double)y[i] * y[i];
        printf("%6.1f Hz: rms %.1f dBFS\n", F[k], 10 * log10(r / 44100 + 1e-12));
        char nm[512]; snprintf(nm, sizeof nm, "%s/voice_%d.f32", argv[1], (int)F[k]); FILE *fo = fopen(nm, "wb"); fwrite(y, 4, N, fo); fclose(fo);
    }
    return 0;
}
