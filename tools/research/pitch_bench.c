/* OpenPocket Synth, stage 5: one block alone over a WAV, for tools/research/low_bench.py.
 *   pitch_bench in.wav out.wav BLOCK(pitch|syn) TYPE P0 P1 P2 P3 P4 [LOW_SEL]
 * cc -O2 -Iengine tools/research/pitch_bench.c engine/px_dsp.c firmware/px_math.c -lm */
#include "px_dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static float *load(const char *fn, int *n, float *sr)
{
    FILE *f = fopen(fn, "rb"); if (!f) return 0;
    uint8_t h[44]; fread(h, 1, 12, f);
    uint16_t ch = 1, bits = 16; uint32_t rate = 44100, ds = 0;
    for (;;) { uint8_t c[8]; if (fread(c, 1, 8, f) != 8) break; uint32_t cs = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { uint8_t b[16]; fread(b, 1, 16, f); ch = b[2] | b[3] << 8; rate = b[4] | b[5] << 8 | b[6] << 16; bits = b[14]; fseek(f, cs - 16, SEEK_CUR); }
        else if (!memcmp(c, "data", 4)) { ds = cs; break; } else fseek(f, (cs + 1) & ~1u, SEEK_CUR); }
    if (bits != 16) { fclose(f); return 0; }
    int fr = ds / (2 * ch); int16_t *raw = malloc(ds); fread(raw, 1, ds, f); fclose(f);
    float *x = malloc(sizeof(float) * fr);
    for (int i = 0; i < fr; i++) { float a = 0; for (int c = 0; c < ch; c++) a += raw[i * ch + c] / 32768.0f; x[i] = a / ch; }
    free(raw); *n = fr; *sr = rate; return x;
}
int main(int argc, char **argv)
{
    if (argc < 10) { fprintf(stderr, "usage\n"); return 1; }
    int n; float sr; float *x = load(argv[1], &n, &sr); if (!x) { fprintf(stderr, "bad wav\n"); return 1; }
    static px_engine e; px_engine_init(&e, sr);
    px_preset_t p; px_preset_factory(&p, 0);
    for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
    int b = !strcmp(argv[3], "syn") ? PX_B_SYN : PX_B_PITCH;
    p.on[b] = 1; p.type[b] = (uint8_t)atoi(argv[4]);
    for (int k = 0; k < 5; k++) p.p[b][k] = (uint8_t)atoi(argv[5 + k]);
    px_engine_set(&e, &p);
    float *y = malloc(sizeof(float) * n);
    for (int i = 0; i < n; i += PX_MAX_BLOCK) {
        int m = n - i < PX_MAX_BLOCK ? n - i : PX_MAX_BLOCK;
        if (argc > 10) e.erb_pitch.low_sel = (float)atof(argv[10]);
        px_engine_process(&e, x + i, y + i, m);
    }
    FILE *f = fopen(argv[2], "wb"); fwrite(y, sizeof(float), n, f); fclose(f);
    return 0;
}
