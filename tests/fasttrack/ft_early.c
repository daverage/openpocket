/* FastTrack probe (host only): the tracker's raw evidence around every annotated attack and around every flux pick.  Does not decide anything.
 *   ft_early IN.wav   (truth = the CSV beside it)    stdout:
 *   G idx on_ms n_pitches f_true0 ..           an annotated group (strum = onsets within 40 ms)
 *   F idx t_rel mono conf in_notes sal_ratio dtrue dvalid dmono      per tracker frame, t_rel = -10 .. +60 ms from the annotated onset
 *       in_notes: rank of the true pitch among the tracker's note candidates (0 = strongest, -1 absent); sal_ratio: its salience over the strongest's;
 *       dtrue: pxt_period at the TRUE period (0 = repeats exactly, 1 = not) over the last 20 dec samples, valid only if the window lies after the onset; dmono: the same at mono's period
 *   P t flux weak flux_avg level_ratio conf mono dmono pk_rel       every flux pick (t->onset > 0): level_ratio = fast level / its 8-frame minimum, pk_rel = fast level / 150 ms peak */
#include <stdio.h>
#include <stdlib.h>
#include "../../engine/px_dsp.c"
#include "ft_wav.h"
static double g_on[20000]; static float g_hz[20000][6]; static int g_np[20000], g_n;
static px_engine e; static px_track ptk; static pxt_layer lay;
int main(int argc, char **argv)
{
    long n; float *x = ft_rd(argv[1], &n); if (!x) return 1;
    char path[1024]; snprintf(path, sizeof path, "%s", argv[1]); char *d = strrchr(path, '.'); strcpy(d, ".csv");
    FILE *f = fopen(path, "r"); double a, b, m, last = -1e9;
    while (f && fscanf(f, "%lf,%lf,%lf", &a, &b, &m) == 3) { a *= 1000.0; const float hz = 440.0f * powf(2.0f, ((float)m - 69.0f) / 12.0f);
        if (g_n && a - last < 40.0 && g_np[g_n - 1] < 6) g_hz[g_n - 1][g_np[g_n - 1]++] = hz; else if (g_n < 20000) { g_on[g_n] = a; g_np[g_n] = 1; g_hz[g_n][0] = hz; g_n++; last = a; } }
    for (int k = 0; k < g_n; k++) { printf("G %d %.1f %d", k, g_on[k], g_np[k]); for (int j = 0; j < g_np[k]; j++) printf(" %.2f", g_hz[k][j]); printf("\n"); }
    px_engine_init(&e, 44100.0f); pxt_reset(&e, &ptk, &lay); ptk.sensitive_picks = 1;
    float fe = 0, pk = 0, fh[8] = {0}; int fhi = 0;
    for (long i = 0; i < n; i++) {
        const float av = px_abs(x[i]); fe += (av - fe) * (av > fe ? 1.0f / 44.1f : 1.0f / 441.0f); pk = av > pk ? av : pk * (1.0f - 1.0f / (0.15f * 44100.0f));
        if (!pxt_run(&e, &ptk, x[i])) continue;
        const double t = (i + 1) * 1000.0 / 44100.0; const float mono = ptk.conf > PXT_CONF ? ptk.mono : 0.0f;
        float mn = 1e9f; for (int k = 0; k < 8; k++) if (fh[k] < mn) mn = fh[k]; fh[fhi] = fe; fhi = (fhi + 1) & 7;
        const float rate = pxt_rate * 0.25f;
        if (ptk.onset > 0.0f) {
            float dm = 1.0f; if (mono > 0.0f) { const float lag = rate / mono; if (lag > 4 && lag + 25 < PXT_DEC) dm = pxt_period(&ptk, lag, 20); }
            printf("P %.2f %.2f %d %.2f %.2f %.3f %.1f %.3f %.3f\n", t, ptk.onset, ptk.weak_pick, ptk.flux_avg, fe / (mn + 1e-5f), ptk.conf, mono, dm, fe / (pk + 1e-9f));
        }
        int gi = -1; for (int k = 0; k < g_n && g_on[k] <= t + 10.0; k++) gi = k;
        if (gi < 0 || t - g_on[gi] > (getenv("FT_WIN") ? atof(getenv("FT_WIN")) : 60.0)) continue;
        const double tr = t - g_on[gi];
        /* the group's lowest pitch is "the" true fundamental for the probe (a chord's bass); single-note groups are the clean case */
        const float ft = g_hz[gi][0];
        int rank = -1; float sal_t = 0, sal_m = 0;
        for (int k = 0; k < ptk.nnotes; k++) if (ptk.sal[k] > sal_m) sal_m = ptk.sal[k];
        for (int k = 0; k < ptk.nnotes; k++) if (pxt_near(ptk.notes[k], ft, 50.0f)) { rank = 0; for (int j = 0; j < ptk.nnotes; j++) if (ptk.sal[j] > ptk.sal[k]) rank++; sal_t = ptk.sal[k]; }
        const float lag = rate / ft; const int recent = (int)(tr * rate / 1000.0), w = 20; const int valid = tr > 0 && recent >= w + (int)lag + 2 && w + (int)lag + 2 < PXT_DEC;
        const float dt = lag > 4 && lag + 25 < PXT_DEC ? pxt_period(&ptk, lag, w) : 1.0f;
        float dm = 1.0f; if (mono > 0.0f) { const float lg = rate / mono; if (lg > 4 && lg + 25 < PXT_DEC) dm = pxt_period(&ptk, lg, w); }
        float dd[4] = {1, 1, 1, 1};      /* d at the candidate's period x 1/2, x 1/3, x 2, x 3 (frequency x2, x3, /2, /3) */
        if (mono > 0.0f) { const float lg = rate / mono; const float mult[4] = {0.5f, 1.0f / 3.0f, 2.0f, 3.0f};
            for (int k = 0; k < 4; k++) { const float l2 = lg * mult[k]; if (l2 > 4 && l2 + 25 < PXT_DEC) dd[k] = pxt_period(&ptk, l2, w); } }
        printf("F %d %.2f %.1f %.3f %d %.3f %.3f %d %.3f %.3f %.3f %.3f %.3f\n", gi, tr, mono, ptk.conf, rank, sal_m > 0 ? sal_t / sal_m : 0.0f, dt, valid, dm, dd[0], dd[1], dd[2], dd[3]);
    }
    return 0;
}
