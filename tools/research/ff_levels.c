/* SPDX-License-Identifier: GPL-3.0-only
 * The FUZZ FACE's loudness against the guitar alone over its knobs (the five bundled loops, BS.1770, helpers of level_audit.c): FUZZ x PICKUP x TRANSISTOR at VOLUME 50, BIAS 50.
 *   cc -O2 -Iengine tools/research/ff_levels.c engine/px_dsp.c firmware/px_math.c -lm -o build/ff_levels && build/ff_levels */
#define main level_audit_main
#include "level_audit.c"
#undef main
int main(void)
{
    static const char *LOOPS[5] = {"arpeggio", "chords", "chug", "lead", "riff"};
    float *in[5], *out; int len[5];
    for (int l = 0; l < 5; l++) { char path[128]; snprintf(path, sizeof path, "assets/loops/%s.wav", LOOPS[l]); in[l] = load(path, &len[l]); if (!in[l]) return 1; for (int i = 0; i < len[l]; i++) in[l][i] *= 0.15f; }
    int mx = 0; for (int l = 0; l < 5; l++) if (len[l] > mx) mx = len[l];
    out = malloc(sizeof(float) * (size_t)mx);
    double lo[5], dummy = 0; px_preset_t p;
    memset(&p, 0, sizeof p); px_order_default(p.order); p.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    for (int l = 0; l < 5; l++) { render(&p, in[l], out, len[l], &dummy); lo[l] = lufs(out, len[l]); }
    printf("trans,pickup,fuzz,mean_db,worst_db,best_db,peak_dbfs\n");
    for (int tr = 0; tr < 2; tr++) for (int pk = 0; pk <= 100; pk += 50) for (int fz = 0; fz <= 100; fz += 25) {
        px_preset_base(&p); for (int k = 0; k < PX_NBLK; k++) p.on[k] = 0; p.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
        p.on[PX_B_BST] = 1; p.type[PX_B_BST] = PX_BST_FUZZFACE; p.p[PX_B_BST][0] = (uint8_t)fz; p.p[PX_B_BST][1] = 50; p.p[PX_B_BST][2] = (uint8_t)pk; p.p[PX_B_BST][3] = 50; p.p[PX_B_BST][4] = tr ? 100 : 0;
        double mean = 0, worst = 99, best = -99, pkv = 0;
        for (int l = 0; l < 5; l++) { render(&p, in[l], out, len[l], &dummy); const double d = lufs(out, len[l]) - lo[l]; mean += d / 5; if (d < worst) worst = d; if (d > best) best = d; for (int k = 0; k < len[l]; k++) if (fabs(out[k]) > pkv) pkv = fabs(out[k]); }
        printf("%s,%d,%d,%.2f,%.2f,%.2f,%.1f\n", tr ? "SI" : "GE", pk, fz, mean, worst, best, 20 * log10(pkv + 1e-9));
    }
    return 0;
}
