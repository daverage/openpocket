/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later.
 * v61: render a SYN type of the poly-test build (the ensembles) from mono 44.1 kHz float32 on stdin to stdout.
 * Usage: ensemble_bench TYPE_NAME [five knobs]       e.g. ensemble_bench "STRING ENSEMBLE" 50 50 100 50 0
 * BLK=PITCH or BLK=SFX renders that block's type instead of SYN's (v63: catalogue comparisons).
 * OCTAVE_VOICE=up or down isolates that voice in an organ, strings or choir.
 * TRIM_FROM=TYPE_NAME uses another type's calibrated block trim for A/B.
 * Prints the output RMS and peak to stderr.
 *   cc -O2 -Iengine -Ifirmware tools/legacy-synth/ensemble_bench.c firmware/px_math.c -lm -o build/ensemble_bench
 */
#define PX_PULSE_TEST 1
#define PX_POLY_TEST 1
#define PX_ENSEMBLE_TEST 1
#define PX_MONO_LAYER 1
#include "../../engine/px_dsp.c"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 7) return 2;
    int type = -1, trim_type = -1, blk = PX_B_SYN;
    const char *trim_from = getenv("TRIM_FROM");
    if (getenv("BLK") && !strcmp(getenv("BLK"), "PITCH")) blk = PX_B_PITCH;
    else if (getenv("BLK") && !strcmp(getenv("BLK"), "SFX")) blk = PX_B_SFX;
    for (int t = 0; t < px_ntypes(blk); ++t) {
        const char *name = px_type_name(blk, t);
        if (!strcmp(name, argv[1])) type = t;
        if (trim_from && !strcmp(name, trim_from)) trim_type = t;
    }
    if (type < 0) { fprintf(stderr, "no type %s\n", argv[1]); return 2; }
    if (trim_from && trim_type < 0) { fprintf(stderr, "no trim type %s\n", trim_from); return 2; }
    static px_engine engine;
    px_preset_t preset;
    px_engine_init(&engine, 44100.0f);
    px_preset_factory(&preset, 0);
    memset(preset.on, 0, sizeof preset.on);
    px_order_default(preset.order);
    preset.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    preset.on[blk] = 1;
    preset.type[blk] = (uint8_t)type;
    for (int k = 0; k < PX_NPAR; ++k) preset.p[blk][k] = argc == 7 ? (uint8_t)atoi(argv[2 + k]) : 50;
    px_engine_set(&engine, &preset);
    if (trim_type >= 0) {
        const float *trim_db = blk == PX_B_PITCH ? PIT_DB : blk == PX_B_SFX ? SFX_DB : SYN_DB;
        engine.btr[blk] = powf(10.0f, trim_db[trim_type] * 0.05f);
    }
    const char *octave = getenv("OCTAVE_VOICE");
    if (octave) {
        const int up = !strcmp(octave, "up");
        if (!up && strcmp(octave, "down")) return 2;
        const int organ = type == PX_SYN_ORGAN || type == PX_SYN_ORGAN_HQ ||
                          type == PX_SYN_CHURCH || type == PX_SYN_CHURCH_HQ;
        const int choir = type == PX_SYN_CHOIR || type == PX_SYN_CHOIR_HQ;
        const int strings = type == PX_SYN_STRINGS || type == PX_SYN_STRINGS_HQ;
        if (blk != PX_B_SYN || (!organ && !choir && (!strings || !up))) return 2;
        engine.syn_d8 = 0.0f;
        engine.syn_dry = 0.0f;
        engine.syn_plvl = 0.0f;
        engine.syn_lvl[2] = 0.0f;
        const int voice = organ ? up : !up;
        engine.syn_lvl[1 - voice] = 0.0f;
    }
    float in[PX_MAX_BLOCK], out[PX_MAX_BLOCK];
    double sum = 0.0, peak = 0.0;
    size_t total = 0, n;
    int bad = 0;
    while ((n = fread(in, sizeof *in, PX_MAX_BLOCK, stdin))) {
        px_engine_process(&engine, in, out, (int)n);
        for (size_t i = 0; i < n; ++i) {
            if (!isfinite(out[i])) bad = 1;
            sum += (double)out[i] * out[i];
            if (fabs(out[i]) > peak) peak = fabs(out[i]);
        }
        total += n;
        if (fwrite(out, sizeof *out, n, stdout) != n) return 1;
    }
    fprintf(stderr, "rms %.6f peak %.4f%s\n", sqrt(sum / (total ? total : 1)), peak, bad ? " NONFINITE" : "");
    return bad;
}
