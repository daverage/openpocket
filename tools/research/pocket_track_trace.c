/* Host experiment: isod89/pocket-synth's mono pitch tracker (GPL-3.0, firmware/apps/synth/sy_track.c) and its MONO note
 * layer (sy_engine.c notes(), as written there, default settings) run over a WAV and logged in the format of
 * tests/archive/fasttrack/ft_trace.c, so tests/archive/fasttrack/ft_score.py scores it like the engine's tracker.
 *   POCKET_SRC=/path/to/pocket-synth/firmware/apps/synth  (a checkout; this file includes its sy_track.c)
 *   cc -O2 -I$POCKET_SRC tools/research/pocket_track_trace.c -lm -o build/fasttrack/ft_pocket
 *   ft_pocket IN.wav        stdout: "N t f v 0" at each note start; FT_TRACE=path: float32 x3 (t ms, Hz, gate) per 64 samples */
#include "sy_track.c"
#include <stdio.h>
#include <stdlib.h>
#include "../../tests/archive/fasttrack/ft_wav.h"
static sy_track_t T;
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    long n; float *x = ft_rd(argv[1], &n); if (!x) return 1;
    FILE *ft = getenv("FT_TRACE") ? fopen(getenv("FT_TRACE"), "wb") : 0;
    tr_init(&T);
    int gate = 0, held = 0, cand_n = 0, jump_n = 0; float target = 0, trig = 0, cand = 0, jump = 0, vel = 0; unsigned quiet = 0;
    for (long b = 0; b + TR_FR <= n; b += TR_FR) {
        tr_run(&T, x + b);
        const double tb = (b + TR_FR) * 1000.0 / 44100.0;
        if (T.onset) { const float v = T.level * 3.0f; vel = v > 1 ? 1 : v < 0.15f ? 0.15f : v; held = 1; }
#define ATTACK(m) do { printf("N %.3f %.3f %.3f 0\n", tb, 440.0 * pow(2.0, ((m) - 69.0) / 12.0), vel); } while (0)
        int stop = 0;
        if (T.voiced && T.hz > 0 && T.conf > 0.45f) {
            const float m = 69.0f + 12.0f * 1.442695f * logf(T.hz / 440.0f);
            int skip = 0;
            if (gate && !held && (m - target > 7 || target - m > 7)) {
                if (!jump_n || m - jump > 0.5f || jump - m > 0.5f) { jump = m; jump_n = 1; skip = 1; }
            }
            if (!skip) {
                jump_n = 0; quiet = 0;
                if (!gate) { gate = 1; ATTACK(m); held = 0; trig = m; cand_n = 0; }
                else if (held) { ATTACK(m); held = 0; trig = m; cand_n = 0; }
                else {
                    held = 0;
                    if (m - trig > 0.7f || trig - m > 0.7f) {
                        if (cand_n && (m - cand > 0.3f || cand - m > 0.3f)) cand_n = 0;
                        if (!cand_n) cand = m;
                        if (++cand_n >= 2u) { ATTACK(m); trig = m; cand_n = 0; }
                    } else cand_n = 0;
                }
                target = m;
            }
        } else if (quiet < 100000u) quiet++;
        if (gate && (T.level < T.gate * 0.7f || quiet > 60u)) gate = 0;
        (void)stop;
        if (ft) for (int k = 0; k < 2; k++) { const float r[3] = {(float)(tb - (1 - k) * 64 * 1000.0 / 44100.0), target > 0 ? 440.0f * powf(2.0f, (target - 69.0f) / 12.0f) : 0.0f, (float)gate}; fwrite(r, 4, 3, ft); }
    }
    if (ft) fclose(ft);
    return 0;
}
