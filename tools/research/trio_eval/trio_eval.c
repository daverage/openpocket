/* Synth2 stage D: CHIP CHOIR through the tracker and the adapter on steady plucks. Writes build/trio_eval/note_<k>_tone<T>.f32 (1 s of the 3 s render, float32) and prints the level.
 * usage: trio_eval OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
#define N (44100*4)
static float x[N], y[N];
int main(int argc, char **argv)
{
    static const float F[5] = {82.41f, 164.81f, 329.63f, 659.26f, 987.77f}; static const int TONE[3] = {50, 25, 8};
    for (int ti = 0; ti < 3; ti++) for (int k = 0; k < 5; k++) {
        float f = F[k], ph = 0, env = 0.5f;
        for (int i = 0; i < N; i++) { ph += f / 44100.f; ph -= floorf(ph); const float p = ph * 6.2831853f; x[i] = i < 4410 ? 0 : 0.4f * env * (sinf(p) + .6f * sinf(2 * p) + .45f * sinf(3 * p) + .3f * sinf(4 * p)); env *= 0.99999f; }
        px_preset_t pr; px_preset_factory(&pr, 7); for (int z = 0; z < PX_NBLK; z++) pr.on[z] = 0; pr.on[PX_B_SYN] = 1; pr.type[PX_B_SYN] = PX_SYN_ENSEMBLE;
        { const uint8_t q[5] = {45, (uint8_t)TONE[ti], 50, 50, 50}; memcpy(pr.p[PX_B_SYN], q, 5); }
        px_engine e; px_engine_init(&e, 44100.f); px_engine_set(&e, &pr);
        for (int at = 0; at + 64 <= N; at += 64) px_engine_process(&e, x + at, y + at, 64);
        double r = 0; for (int i = 2 * 44100; i < 3 * 44100; i++) r += (double)y[i] * y[i];
        printf("TONE %3d  %7.2f Hz: rms %.1f dBFS\n", TONE[ti], f, 10 * log10(r / 44100 + 1e-12));
        char nm[512]; snprintf(nm, sizeof nm, "%s/note_%d_tone%d.f32", argv[1], k, TONE[ti]); FILE *fo = fopen(nm, "wb"); fwrite(y + 44100, 4, 44100 * 3, fo); fclose(fo);
    }
    return 0;
}
