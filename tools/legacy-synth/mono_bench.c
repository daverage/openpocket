/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * MONO SYNTH as the pedal plays it, for tools/research/track_eval.py and tools/research/mono_eval.py (v60).
 * Usage: mono_bench IN.wav OUT.csv   (44.1 kHz mono PCM16; the SYN block alone, MONO SYNTH, knobs at noon)
 * One row per 64 samples: t, voice (the pitch it plays while its gain is above 0.1, else 0), gain.
 *   cc -O2 -Iengine -Ifirmware tools/legacy-synth/mono_bench.c firmware/px_math.c -lm -o build/mono_old
 *   cc -O2 -DPX_MONO_LAYER=1 -Iengine -Ifirmware tools/legacy-synth/mono_bench.c firmware/px_math.c -lm -o build/mono_layer
 */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "w");
    if (!in || !out) return 1;
    unsigned char head[44];
    if (fread(head, 1, 44, in) != 44) return 1;
    /* find the data chunk (GuitarSet and the bundled loops have plain 44-byte headers, but be safe) */
    fseek(in, 12, SEEK_SET);
    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, in) != 8) return 1;
        const unsigned size = ch[4] | ch[5] << 8 | ch[6] << 16 | (unsigned)ch[7] << 24;
        if (!memcmp(ch, "data", 4)) break;
        fseek(in, (long)size + (size & 1), SEEK_CUR);
    }
    static px_engine engine;
    px_preset_t preset;
    px_engine_init(&engine, 44100.0f);
    px_preset_factory(&preset, 0);
    memset(preset.on, 0, sizeof preset.on);
    px_order_default(preset.order);
    preset.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    preset.on[PX_B_SYN] = 1;
    preset.type[PX_B_SYN] = PX_SYN_MONO;
    for (int k = 0; k < PX_NPAR; ++k) preset.p[PX_B_SYN][k] = 50;
    px_engine_set(&engine, &preset);
    fprintf(out, "t,voice,gain\n");
    enum { N = 64 };
    short pcm[N];
    float x[N], y[N];
    size_t got, at = 0;
    while ((got = fread(pcm, sizeof *pcm, N, in)) > 0) {
        for (size_t i = 0; i < got; ++i) x[i] = pcm[i] * (1.0f / 32768.0f);
        px_engine_process(&engine, x, y, (int)got);
        at += got;
        const float gain = engine.syn_kg;
        fprintf(out, "%.5f,%.3f,%.4f\n", at / 44100.0, gain > 0.1f ? engine.syn_f0 : 0.0f, gain);
    }
    fclose(in);
    return fclose(out) != 0;
}
