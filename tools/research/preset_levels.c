/* SPDX-License-Identifier: GPL-3.0-only
 * Loudness of every factory preset against the guitar alone, on the five bundled loops (assets/loops, at the pedal's level x0.15), BS.1770 (the helpers of level_audit.c).
 *   cc -O2 -Iengine tools/research/preset_levels.c engine/px_dsp.c firmware/px_math.c -lm -o build/preset_levels && build/preset_levels > levels.csv
 * Columns: slot (from 1), name, the preset's LEVEL now, mean dB over the loops against the dry guitar, worst loop, best loop, peak dBFS (worst loop). tools/research/preset_levels.py
 * sets each preset's LEVEL (engine/px_dsp.c, FACT) from them. */
#define main level_audit_main
#include "level_audit.c"
#undef main
int main(void)
{
    static const char *LOOPS[5] = {"arpeggio", "chords", "chug", "lead", "riff"};
    float *in[5], *out; int len[5];
    for (int l = 0; l < 5; l++) {
        char path[128]; snprintf(path, sizeof path, "assets/loops/%s.wav", LOOPS[l]);
        in[l] = load(path, &len[l]); if (!in[l]) { fprintf(stderr, "no %s\n", path); return 1; }
        for (int i = 0; i < len[l]; i++) in[l][i] *= 0.15f;
    }
    int mx = 0; for (int l = 0; l < 5; l++) if (len[l] > mx) mx = len[l];
    out = malloc(sizeof(float) * (size_t)mx);
    double lo[5], dummy = 0; px_preset_t p;
    memset(&p, 0, sizeof p); px_order_default(p.order); p.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    for (int l = 0; l < 5; l++) { render(&p, in[l], out, len[l], &dummy); lo[l] = lufs(out, len[l]); }
    printf("slot,name,level,mean_db,worst_db,best_db,peak_dbfs\n");
    for (int i = 0; i < px_preset_count(); i++) {
        px_preset_factory(&p, i);
        double mean = 0, worst = 99, best = -99, pk = 0;
        for (int l = 0; l < 5; l++) {
            render(&p, in[l], out, len[l], &dummy);
            const double d = lufs(out, len[l]) - lo[l]; mean += d / 5; if (d < worst) worst = d; if (d > best) best = d;
            for (int k = 0; k < len[l]; k++) if (fabs(out[k]) > pk) pk = fabs(out[k]);
        }
        printf("%d,%s,%d,%.2f,%.2f,%.2f,%.1f\n", i + 1, p.name, p.p[PX_B_GATE][PX_PATCH_LEVEL], mean, worst, best, 20 * log10(pk + 1e-9));
    }
    return 0;
}
