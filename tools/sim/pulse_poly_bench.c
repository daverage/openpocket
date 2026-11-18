/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * Render the firmware PULSE POLY block from mono 44.1 kHz float32 on stdin.
 * Pass a CSV path to log the raw resonator candidates and audible voices.
 */
#define PX_PULSE_TEST 1
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc > 2) return 2;
    FILE *trace = argc == 2 ? fopen(argv[1], "w") : NULL;
    if (argc == 2 && !trace) return 1;

    static px_engine engine;
    px_preset_t preset;
    px_engine_init(&engine, 44100.0f);
    px_preset_factory(&preset, 0);
    memset(preset.on, 0, sizeof preset.on);
    px_order_default(preset.order);
    preset.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    preset.on[PX_B_SYN] = 1;
    preset.type[PX_B_SYN] = PX_SYN_PULSE_POLY;
    for (int k = 0; k < PX_NPAR; ++k) preset.p[PX_B_SYN][k] = 50;
    px_engine_set(&engine, &preset);

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
            const px_poly_voice *a = &pulse_poly.voice[0], *b = &pulse_poly.voice[1];
            fprintf(trace, ",%.5f,%.5f,%.6f,%.6f,%u,%u,%u,%u\n",
                    a->frequency, b->frequency, a->gain, b->gain, a->held, b->held, a->repick, b->repick);
        }
    }
    return ferror(stdin) || (trace && fclose(trace) != 0);
}
