/* FastTrack: tracker evidence (unchanged px_track) -> event controller (ft_ctrl.h) -> NEUTRAL oscillator.  No px_mono, no pxt_layer_run, no Felucca.
 *   cc ... -I engine tests/archive/fasttrack/ft_ctrl_trace.c firmware/px_math.c -o build/fasttrack/ft_ctrl ; ft_ctrl IN.wav   (env FT_TRACE, FT_WAV)
 * Output grammar = ft_synth.c so ft_score.py / ft_attrib.py read it: R pick evidence, W N note-on/repick (A = the neutral oscillator's envelope restarts), W K repick marker,
 * W L legato change, W X release (damp flag), W Z rejected candidate (reason), W V pitch update.  The oscillator is a plain voice: sine+2 harmonics, 3 ms attack, 15 ms release
 * (8 ms after a damp), pitch glides 5 ms, level = velocity; every NOTE_ON / REPICK restarts the attack (articulation = always attack: the event is reported either way). */
#include <stdio.h>
#include <stdlib.h>
#include "../../../engine/px_dsp.c"
#include "ft_wav.h"
#include "ft_ctrl.h"
/* ORACLES (diagnostics, never implementable): FT_ORACLE_ONSET=lag_ms  pick evidence comes from the annotated group onsets (+lag), not from the audio;
 * FT_ORACLE_PITCH=1  the admitted pitch is the annotated pitch of the current group nearest the estimator's best candidate (a single-note group: that note), admitted at the earliest-admission frame.
 * Truth = the CSV beside the WAV (on, off, midi; build/stage5/sets).  Annotation timing is only good to about +-10 ms (Guitar-TECHS lag fit). */
static double g_on[20000]; static float g_hz[20000][6]; static int g_np[20000], g_n;
static void load_truth(const char *wav)
{
    char path[1024]; snprintf(path, sizeof path, "%s", wav); char *d = strrchr(path, '.'); if (d) strcpy(d, ".csv");
    FILE *f = fopen(path, "r"); if (!f) return; double a, b, m; double last = -1e9;
    while (fscanf(f, "%lf,%lf,%lf", &a, &b, &m) == 3) {
        a *= 1000.0;
        if (g_n && a - last < 40.0 && g_np[g_n - 1] < 6) { g_hz[g_n - 1][g_np[g_n - 1]++] = 440.0f * powf(2.0f, ((float)m - 69.0f) / 12.0f); }
        else if (g_n < 20000) { g_on[g_n] = a; g_np[g_n] = 1; g_hz[g_n][0] = 440.0f * powf(2.0f, ((float)m - 69.0f) / 12.0f); g_n++; last = a; }
    }
    fclose(f);
}
static px_engine e; static px_track ptk; static pxt_layer lay_unused; static ft_ctrl C;
int main(int argc, char **argv)
{
    long n; float *x = ft_rd(argv[1], &n); if (!x) return 1;
    const char *oo = getenv("FT_ORACLE_ONSET"); const int orp = getenv("FT_ORACLE_PITCH") != 0; if (oo || orp) load_truth(argv[1]); C.oracle_on = oo != 0;
    int gi_next = 0; px_engine_init(&e, 44100.0f); pxt_reset(&e, &ptk, &lay_unused); ptk.sensitive_picks = 1; ft_ctrl_init(&C, 44100.0f); C.oracle_on = oo != 0;
    FILE *ftr = getenv("FT_TRACE") ? fopen(getenv("FT_TRACE"), "wb") : 0; short *w16 = getenv("FT_WAV") ? malloc(2 * n) : 0;
    float ph = 0, env = 0, tgt = 0, fcur = 0, vel = 0.5f; int stage = 0, damp = 0;
    for (long i = 0; i < n; i++) {
        const int framed = pxt_run(&e, &ptk, x[i]);
        C.oracle_pick = 0; C.oracle_hz = 0.0f;
        if (oo) { const double tn = (i + 1) * 1000.0 / 44100.0 - atof(oo); if (gi_next < g_n && tn >= g_on[gi_next]) { C.oracle_pick = 1; gi_next++; } ptk.onset = 0.0f; }
        if (orp) { const double tn = (i + 1) * 1000.0 / 44100.0; int gi = -1; for (int k = 0; k < g_n && g_on[k] <= tn; k++) gi = k;
                   if (gi >= 0) { float best = 1e9f, bh = g_hz[gi][0]; for (int k = 0; k < g_np[gi]; k++) { const float q = ptk.mono > 0 ? fabsf(logf(ptk.mono / g_hz[gi][k])) : 1.0f; if (q < best) { best = q; bh = g_hz[gi][k]; } } C.oracle_hz = bh; } }
        ft_ctrl_out o; ft_ctrl_run(&C, &ptk, x[i], framed, &o);
        const double t = (i + 1) * 1000.0 / 44100.0;
        if (o.ev & FTC_PICK) printf("R %.2f %d\n", t, ptk.weak_pick);
        if (o.ev & (FTC_NOTE_ON | FTC_REPICK)) { printf("W %.2f N %.2f\n", t, o.f0); if (o.ev & FTC_REPICK) printf("W %.2f K %.2f\n", t, o.f0); printf("A %.2f %.2f %d %d\n", t, o.f0, (o.ev & FTC_REPICK) != 0, o.cand_frames); stage = 1; vel = 0.15f + 0.85f * o.vel; if (fcur == 0.0f) fcur = o.f0; }
        if (o.ev & FTC_LEGATO) printf("W %.2f L %.2f\n", t, o.f0);
        if (o.ev & FTC_PITCH_UPDATE) printf("W %.2f V %.2f\n", t, o.f0);
        if (o.ev & FTC_NOTE_OFF) { printf("W %.2f X %d\n", t, (o.ev & FTC_DAMP) ? 1 : 0); stage = 3; damp = (o.ev & FTC_DAMP) != 0; }
        if (o.ev & FTC_REJECT) printf("W %.2f Z %d\n", t, o.reason);
        if (w16 || (ftr && (i & 63) == 63)) {
            tgt = o.gate ? o.f0 : tgt; fcur += (tgt - fcur) * (1.0f - expf(-1.0f / (0.005f * 44100.0f)));
            if (stage == 1) { env += (1.0f - env) * (1.0f / (0.003f * 44100.0f)); }
            else if (stage == 3) { env -= env * (1.0f / ((damp ? 0.008f : 0.015f) * 44100.0f)); }
            if (w16 && fcur > 0.0f) { ph += fcur / 44100.0f; ph -= floorf(ph); const float p = 6.2831853f * ph; w16[i] = (short)(0.4f * vel * env * (sinf(p) + 0.5f * sinf(2 * p) + 0.3f * sinf(3 * p)) * 32000.0f / 1.8f); } else if (w16) w16[i] = 0;
        }
        if (ftr && (i & 63) == 63) { float r[3] = { (float)t, o.f0, (float)o.gate }; fwrite(r, 4, 3, ftr); }
    }
    if (ftr) fclose(ftr);
    if (w16) { FILE *f = fopen(getenv("FT_WAV"), "wb"); ft_wr16(f, w16, n); fclose(f); }
    return 0;
}
