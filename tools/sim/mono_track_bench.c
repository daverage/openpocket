/* OpenPocket Synth2, stage 2: the retained mono tracker (engine/px_track.h: pxt_run + the note layer) on a DI file, as the
 * removed MONO SYNTH used it (v73 rc_syn / syn_run, same constants), for tools/research/mono_compare.py (candidate eng_v73).
 * Usage: mono_track_bench IN.wav OUT.csv   (any WAV tools/research reads: PCM16/24/32 or float, mono or stereo, 44.1 kHz)
 * One row per 64 samples: t, voice (the pitch it plays while its gain is above 0.1, else 0), gain.
 *   cc -O2 -Iengine -Ifirmware tools/sim/mono_track_bench.c firmware/px_math.c -lm -o build/mono_v73
 * Build flags as the pedal's engine: -fno-builtin -ffp-contract=off for bit-exact comparison. -DPXT_FMIN=38.0f for the low floor. */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "w");
    if (!in || !out) return 1;
    unsigned char h[12];
    if (fread(h, 1, 12, in) != 12) return 1;
    int ch = 1, bits = 16, fmt = 1; unsigned dsz = 0;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, in) != 8) return 1;
        const unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { unsigned char b[16]; if (fread(b, 1, 16, in) != 16) return 1; fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; bits = b[14] | b[15] << 8; fseek(in, (long)sz - 16, SEEK_CUR); }
        else if (!memcmp(c, "data", 4)) { dsz = sz; break; }
        else fseek(in, (long)sz + (sz & 1), SEEK_CUR);
    }
    const int bps = bits / 8; const long m = dsz / (bps * ch);
    static px_engine e;
    px_engine_init(&e, 44100.0f);
    static px_track trk; static pxt_layer lay;
    if (getenv("FMIN")) pxt_set_range(&trk, (float)atof(getenv("FMIN")));   /* FMIN=38: the bass range, the same tracker */
    pxt_reset(&e, &trk, &lay);
    const float sr = e.sr, ka = onepole_k(sr, 0.002f), kr = onepole_k(sr, 0.06f), glk = onepole_k(sr, 0.012f);
    float kg = 0.0f, f0 = 0.0f, f0t = 0.0f, dc_x1 = 0, dc_y1 = 0;
    fprintf(out, "t,voice,gain\n");
    for (long i = 0; i < m; i++) {
        double a = 0;
        for (int c = 0; c < ch; c++) {
            if (fmt == 3) { float v; if (fread(&v, 4, 1, in) != 1) v = 0; a += v; }
            else if (bits == 16) { int16_t v; if (fread(&v, 2, 1, in) != 1) v = 0; a += v / 32768.0; }
            else if (bits == 24) { unsigned char b[3]; if (fread(b, 1, 3, in) != 3) memset(b, 0, 3); int v = b[0] | b[1] << 8 | b[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF; a += v / 8388608.0; }
            else { int32_t v; if (fread(&v, 4, 1, in) != 1) v = 0; a += v / 2147483648.0; }
        }
        const float dn = (i & 1) ? 1e-15f : -1e-15f;
        float x = (float)(a / ch) + dn;
        { float y = x - dc_x1 + 0.9995f * dc_y1; dc_x1 = x; dc_y1 = y; x = y; }       /* the engine's DC blocker */
        pxt_layer_run(&lay, &trk, x, pxt_run(&e, &trk, x));
        const int on = lay.gate;
        if (on) {
            f0t = lay.f0;
            const float r = f0 > 0.0f ? f0t / f0 : 0.0f;
            if (lay.onset || r < 0.944f || r > 1.059f) f0 = f0t;
        }
        kg += ((on ? 1.0f : 0.0f) - kg) * (on ? ka : kr);
        if (kg < 1e-6f) kg = 0.0f;
        f0 += (f0t - f0) * glk;
        if ((i + 1) % 64 == 0) fprintf(out, "%.5f,%.3f,%.4f\n", (i + 1) / 44100.0, kg > 0.1f ? f0 : 0.0f, kg);
    }
    fclose(in);
    return fclose(out) != 0;
}
