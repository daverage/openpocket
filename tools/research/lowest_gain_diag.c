/* Synth2 fix pass 1: LOWEST OCTAVE HQ alone (every other block off): output peak, RMS, wet-over-dry RMS and the time the soft limiter works (> 0.8).
 *   cc -O2 -Iengine -Ifirmware -fno-builtin -ffp-contract=off -DPX_LOWEST_HQ_GAIN=2.0f tools/research/lowest_gain_diag.c firmware/px_math.c -lm -o build/lg
 * Usage: lg IN.wav [blend 0..100] */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float *rd(const char *p, long *n)
{
    FILE *in = fopen(p, "rb"); if (!in) return 0;
    unsigned char h[12]; if (fread(h, 1, 12, in) != 12) return 0;
    int ch = 1, bits = 16, fmt = 1; unsigned dsz = 0;
    for (;;) { unsigned char c[8]; if (fread(c, 1, 8, in) != 8) return 0;
        const unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { unsigned char b[16]; if (fread(b, 1, 16, in) != 16) return 0; fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; bits = b[14] | b[15] << 8; fseek(in, (long)sz - 16, SEEK_CUR); }
        else if (!memcmp(c, "data", 4)) { dsz = sz; break; } else fseek(in, (long)sz + (sz & 1), SEEK_CUR); }
    const int bps = bits / 8; *n = dsz / (bps * ch); float *x = malloc(sizeof(float) * (*n));
    for (long i = 0; i < *n; i++) { double a = 0;
        for (int c = 0; c < ch; c++) {
            if (fmt == 3) { float v; if (fread(&v, 4, 1, in) != 1) v = 0; a += v; }
            else if (bits == 16) { int16_t v; if (fread(&v, 2, 1, in) != 1) v = 0; a += v / 32768.0; }
            else if (bits == 24) { unsigned char b[3]; if (fread(b, 1, 3, in) != 3) memset(b, 0, 3); int v = b[0] | b[1] << 8 | b[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF; a += v / 8388608.0; }
            else { int32_t v; if (fread(&v, 4, 1, in) != 1) v = 0; a += v / 2147483648.0; } }
        x[i] = (float)(a / ch); }
    fclose(in); return x;
}
int main(int argc, char **argv)
{
    long n; float *in = rd(argv[1], &n); if (!in) return 1;
    const int blend = argc > 2 ? atoi(argv[2]) : 50;
    static px_engine e; px_engine_init(&e, 44100.0f);
    px_preset_t p; px_preset_factory(&p, 0);
    for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
    p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = PX_PIT_LOWEST_HQ; p.p[PX_B_PITCH][0] = 0; p.p[PX_B_PITCH][1] = (uint8_t)blend; p.p[PX_B_PITCH][2] = 0;
    px_engine_set(&e, &p);
    float *out = malloc(sizeof(float) * n);
    for (long i = 0; i < n; i += PX_MAX_BLOCK) { int b = (int)(n - i); if (b > PX_MAX_BLOCK) b = PX_MAX_BLOCK; px_engine_process(&e, in + i, out + i, b); }
    double so = 0, si = 0; float pk = 0, pi = 0; long lim = 0;
    for (long i = 0; i < n; i++) { so += out[i] * out[i]; si += in[i] * in[i]; const float a = fabsf(out[i]); if (a > pk) pk = a; if (a > 0.8f) lim++; if (fabsf(in[i]) > pi) pi = fabsf(in[i]); }
    printf("in peak %.3f rms %.4f | out peak %.3f rms %.4f (%+.1f dB vs in) | > 0.8: %.3f %%\n", pi, sqrt(si / n), pk, sqrt(so / n), 10 * log10(so / si), 100.0 * lim / n);
    return 0;
}
