/* Host cost of Synth2's blocks relative to the protected effects: ns per 64-frame engine call, mean and worst, on a guitar loop.
 * A host measurement (this machine's CPU), NOT the pedal's: use the ratios as a rough guide only; the pedal's load is read from its own counters.
 *   cc -O2 -fno-builtin -ffp-contract=off -Iengine tools/sim/synth_cost.c firmware/px_math.c -lm -o build/synth_cost && build/synth_cost assets/loops/riff.wav */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "../../engine/px_dsp.c"
static float *wav16(const char *fn, int *n)
{
    FILE *f = fopen(fn, "rb"); if (!f) return 0; unsigned char h[12]; if (fread(h, 1, 12, f) != 12) return 0;
    for (;;) { unsigned char c[8]; if (fread(c, 1, 8, f) != 8) return 0; unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "data", 4)) { short *s = malloc(sz); if (fread(s, 1, sz, f) != sz) return 0; float *x = malloc(sizeof(float) * (sz / 2)); for (unsigned i = 0; i < sz / 2; i++) x[i] = s[i] / 32768.0f; *n = (int)(sz / 2); return x; }
        fseek(f, (long)sz + (sz & 1), SEEK_CUR); }
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static void bench(const char *name, px_preset_t *p, const float *x, int n)
{
    static px_engine e; px_engine_init(&e, 44100.0f); px_engine_set(&e, p);
    static float y[64]; double sum = 0, worst = 0; int calls = 0;
    for (int r = 0; r < 3; r++) for (int at = 0; at + 64 <= n; at += 64) {
        const double t0 = now(); px_engine_process(&e, x + at, y, 64); const double d = now() - t0;
        if (r) { sum += d; calls++; if (d > worst) worst = d; }
    }
    printf("%-22s mean %7.0f ns  worst %8.0f ns per 64 frames  (%.2f %% of the 1451 us a call lasts on this CPU)\n", name, sum / calls, worst, 100.0 * (sum / calls) / 1451247.0);
}
int main(int argc, char **argv)
{
    int n; float *x = wav16(argc > 1 ? argv[1] : "assets/loops/riff.wav", &n); if (!x) return 1;
    px_preset_t p;
    px_preset_factory(&p, 7); for (int z = 0; z < PX_NBLK; z++) p.on[z] = 0; bench("all blocks off", &p, x, n);
    p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = PX_PIT_POLYOCT_HQ; p.p[PX_B_PITCH][0] = 50; p.p[PX_B_PITCH][1] = 50; bench("POLY OCTAVE HQ", &p, x, n);
    p.on[PX_B_PITCH] = 0; p.on[PX_B_SYN] = 1; p.type[PX_B_SYN] = PX_SYN_OCT_FUZZ; bench("POLY OCT FUZZ", &p, x, n);
    {   /* the mono tracker alone (px_mono.h), per 64 frames */
        static px_engine e; static px_mono m; px_engine_init(&e, 44100.0f); px_mono_init(&e, &m, PX_MONO_GUITAR);
        double sum = 0; int calls = 0; px_mono_out o;
        for (int r = 0; r < 3; r++) for (int at = 0; at + 64 <= n; at += 64) { const double t0 = now(); for (int k = 0; k < 64; k++) px_mono_run(&e, &m, x[at + k], &o); if (r) { sum += now() - t0; calls++; } }
        printf("%-22s mean %7.0f ns per 64 frames  (the tracker alone)\n", "mono tracker", sum / calls);
    }
    p.type[PX_B_SYN] = PX_SYN_ANALOG;
    static const uint8_t pat[10] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90}; static const char *nm[10] = {"CLASSIC", "FATSAW", "PULSE", "PWM", "SQ BASS", "SUB", "MOOG", "ACID", "SWEEP", "SYNC"};
    for (int i = 0; i < 10; i++) { char b[40]; snprintf(b, sizeof b, "ANALOG %s", nm[i]); p.p[PX_B_SYN][0] = pat[i]; for (int k = 1; k < 5; k++) p.p[PX_B_SYN][k] = 50; bench(b, &p, x, n); }
    return 0;
}
