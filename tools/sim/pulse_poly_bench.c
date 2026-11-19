/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * Render the poly synths from mono 44.1 kHz float32 on stdin.
 * Usage: pulse_poly_bench [trace.csv|-] [pulse|saw|arcade|fm [five knobs]]
 *        pulse_poly_bench --fm-oscillator HZ RATIO capped|uncapped
 */
#define PX_PULSE_TEST 1
#define PX_POLY_TEST 1
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>

static int fm_oscillator(int argc, char **argv)
{
    if (argc != 5) return 2;
    char *end;
    const long hz = strtol(argv[2], &end, 10);
    if (end == argv[2] || *end || hz < 1 || hz > 1600) return 2;
    const long ratio = strtol(argv[3], &end, 10);
    if (end == argv[3] || *end || ratio < 1 || ratio > 5) return 2;
    const int capped = !strcmp(argv[4], "capped");
    if (!capped && strcmp(argv[4], "uncapped")) return 2;
    enum { RATE = 44100 };
    const float step = (float)hz / RATE;
    const float index = capped ? poly_fm_index(step, step * (float)ratio, 5.0f) : 5.0f;
    fprintf(stderr, "%.9g\n", index);
    /* Exact phase bins let the FFT separate foldback from drift. */
    for (long i = 0; i < RATE; ++i) {
        const float carrier = (float)(i * hz % RATE) / RATE;
        const float modulator = (float)(i * hz * ratio % RATE) / RATE;
        const float out = poly_fm_carrier(carrier, modulator, index);
        if (fwrite(&out, sizeof out, 1, stdout) != 1) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--fm-oscillator")) return fm_oscillator(argc, argv);
    if (argc > 3 && argc != 8) return 2;
    int type = PX_SYN_PULSE_POLY;
    if (argc >= 3) {
        static const char *const names[] = {"pulse", "saw", "arcade", "fm"};
        unsigned i;
        for (i = 0; i < sizeof names / sizeof names[0]; ++i)
            if (!strcmp(argv[2], names[i])) break;
        if (i == sizeof names / sizeof names[0]) return 2;
        type += (int)i;
    }

    static px_engine engine;
    px_preset_t preset;
    px_engine_init(&engine, 44100.0f);
    px_preset_factory(&preset, 0);
    memset(preset.on, 0, sizeof preset.on);
    px_order_default(preset.order);
    preset.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    preset.on[PX_B_SYN] = 1;
    preset.type[PX_B_SYN] = (uint8_t)type;
    for (int k = 0; k < PX_NPAR; ++k) {
        long value = 50;
        if (argc == 8) {
            char *end;
            const char *arg = argv[3 + k];
            value = strtol(arg, &end, 10);
            if (end == arg || *end || value < 0 || value > 100) return 2;
        }
        preset.p[PX_B_SYN][k] = (uint8_t)value;
    }
    px_engine_set(&engine, &preset);

    FILE *trace = NULL;
    if (argc >= 2 && strcmp(argv[1], "-")) {
        trace = fopen(argv[1], "w");
        if (!trace) return 1;
    }
    if (trace) {
        fprintf(trace, "t,nn");
        for (int i = 0; i < PXT_NOTES; ++i) fprintf(trace, ",n%d", i + 1);
        fprintf(trace, ",v1,v2,gain1,gain2,held1,held2,repick1,repick2\n");
    }
    float input[PX_MAX_BLOCK], output[PX_MAX_BLOCK];
    size_t offset = 0, n;
    while ((n = fread(input, sizeof *input, PX_MAX_BLOCK, stdin))) {
        px_engine_process(&engine, input, output, (int)n);
        if (fwrite(output, sizeof *output, n, stdout) != n) return 1;
        offset += n;
        if (trace && offset % (4 * PXR_HOP) == 0) {
            fprintf(trace, "%.8f,%d", (double)offset / engine.sr, pxa_trk.nnotes);
            for (int i = 0; i < PXT_NOTES; ++i)
                fprintf(trace, ",%.5f", i < pxa_trk.nnotes ? pxa_trk.notes[i] : 0.0f);
            const pxp_note *a = &pxp.note[0], *b = &pxp.note[1];
            fprintf(trace, ",%.5f,%.5f,%.6f,%.6f,%u,%u,%u,%u\n",
                    a->frequency, b->frequency, a->gain, b->gain, a->held, b->held, a->repick, b->repick);
        }
    }
    return ferror(stdin) || (trace && fclose(trace) != 0);
}
