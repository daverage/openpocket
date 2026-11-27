/* Host-only comparison driver. Build against a historical engine with
 * -DNOISE_BASELINE, or against the current engine. Input/output: mono float32. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "px_dsp.h"
static px_engine e;
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f)/4; rewind(f);
    float *x = malloc(n*4), *y = malloc(n*4);
    if (!x || !y || fread(x, 4, n, f) != (size_t)n) return 2; fclose(f);
    int model = atoi(argv[3]), threshold = atoi(argv[4]);
    px_preset_t p; px_preset_base(&p); memset(p.on, 0, sizeof p.on);
    p.on[0] = model >= 0; p.type[0] = model < 0 ? 0 : model;
    p.p[0][0] = threshold; p.p[0][1] = 50; p.p[0][3] = 0; p.p[0][4] = 0;
#ifdef NOISE_BASELINE
    p.p[0][2] = 50; p.p[0][3] = 50; p.p[0][4] = 100;
#else
    p.patch_volume = 50; p.p[0][2] = 0;
    if (model == 1) p.p[0][1] = 23;
#endif
    px_engine_init(&e, 44100); px_engine_set(&e, &p);
    clock_t start = clock();
    for (long at = 0; at < n; at += 64) {
        int m = n-at < 64 ? (int)(n-at) : 64; px_engine_process(&e, x+at, y+at, m);
    }
    double us = (double)(clock()-start)*1e6/CLOCKS_PER_SEC/n;
    f = fopen(argv[2], "wb"); if (!f || fwrite(y,4,n,f) != (size_t)n) return 2;
    fclose(f); printf("%.6f us/sample, engine %zu bytes\n", us, sizeof e);
    free(x); free(y); return 0;
}
