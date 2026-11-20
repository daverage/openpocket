/* Runs the REAL production mono tracker (engine/px_mono.h) over raw float32 mono samples on stdin.
 * argv[1] = fmin Hz (0 = guitar default, 38 = bass). stdout: one line per sample-block: t_seconds f0 gate */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static px_engine e; static px_mono m;
int main(int argc, char **argv)
{
    float fmin = argc > 1 ? (float)atof(argv[1]) : 0.0f;
    px_engine_init(&e, 44100.0f); px_mono_init(&e, &m, fmin);
    float x; long i = 0;
    while (fread(&x, 4, 1, stdin) == 1) {
        px_mono_out o; px_mono_run(&e, &m, x, &o);
        if ((i & 63) == 63) printf("%.5f %.3f %d\n", (i + 1) / 44100.0, o.f0, o.gate);
        i++;
    }
    return 0;
}
