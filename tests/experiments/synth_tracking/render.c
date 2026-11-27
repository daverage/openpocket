/* Render through the real engine and record the tracker output every 64 samples. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "px_dsp.c"
#include "ft_wav.h"

static px_engine engine;

int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    proto_mode = atoi(argv[1]);
    int voice = atoi(argv[2]);
    const uint8_t types[] = { PX_SYN_ANALOG, PX_SYN_CHOIR, PX_SYN_ENSEMBLE, PX_SYN_ENSEMBLE,
                              PX_SYN_ANALOG, PX_SYN_INSTRUMENT, PX_SYN_ENSEMBLE, 0 };
    const uint8_t patches[][5] = {
        {0, 50, 50, 50, 50},   /* CLASSIC */
        {5, 45, 50, 50, 50},   /* CHOIR AH */
        {15, 45, 50, 50, 50},  /* ENSEMBLE STRINGS */
        {55, 45, 50, 50, 50},  /* ENSEMBLE ORGAN */
        {25, 60, 45, 50, 50},  /* ANALOG LEAD */
        {65, 50, 50, 50, 50},  /* JAZZ ORGAN */
        {15, 15, 50, 50, 50},  /* STRING ENSEMB */
        {0},                   /* SFX Square reference */
    };
    if (voice < 0 || voice >= (int)(sizeof types / sizeof *types)) return 2;
    long count;
    float *input = ft_rd(argv[3], &count);
    FILE *out = fopen(argv[4], "wb");
    if (!input || !out) return 1;
    short *pcm = malloc(sizeof *pcm * (size_t)count);
    if (!pcm) return 1;

    px_preset_t p;
    px_preset_base(&p);
    memset(p.on, 0, sizeof p.on);
    p.valve_on = 0;
    const float rate = 44100.0f;
    if (voice == 7) {
        p.on[PX_B_SFX] = 1;
        p.type[PX_B_SFX] = PX_SFX_SYNTH;
        const uint8_t square[] = {60, 40, 55, 100, 0};
        memcpy(p.p[PX_B_SFX], square, sizeof square);
    } else {
        p.on[PX_B_SYN] = 1;
        p.type[PX_B_SYN] = types[voice];
        memcpy(p.p[PX_B_SYN], patches[voice], sizeof p.p[PX_B_SYN]);
    }
    px_engine_init(&engine, rate);
    bq_lowpass(&edge_filter, rate, 700.0f, 0.7f);
    edge_attack = onepole_k(rate, 0.002f);
    edge_release = onepole_k(rate, 0.15f);
    edge_age = (int)(0.025f * rate) + 1;
    px_engine_set(&engine, &p);
    for (long i = 0; i < count; i += 32) {
        const int n = count - i < 32 ? (int)(count - i) : 32;
        float y[32];
        px_engine_process(&engine, input + i, y, n);
        for (int j = 0; j < n; j++) {
            const float v = y[j] > 1.0f ? 1.0f : y[j] < -1.0f ? -1.0f : y[j];
            pcm[i + j] = (short)(v * 32767.0f);
        }
        if ((i & 63) == 32) {
            printf("%.6f %.4f %.0f %d\n", (i + n) / 44100.0, px_s2_st.f0,
                   px_s2_st.gate, proto_onsets);
        }
    }
    ft_wr16(out, pcm, count);
    free(pcm);
    free(input);
    return fclose(out) || ferror(stdout);
}
