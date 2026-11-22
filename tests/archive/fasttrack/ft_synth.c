/* FastTrack: the WHOLE legacy synth path on the host (tracker layer + px_mono wrapper + px_synth_adapter + Felucca), traced from the pick to the envelope attack.
 *   FT_OVERLAY=1 tests/archive/fasttrack/build.sh synth_base -DFT_HOOKS -DFT_SYNTH ; ft_synth_base IN.wav   (env FT_TYPE 0..3, FT_PATCH n, FT_WAV=out.wav)
 * stdout (t in ms): R/H/S/U as ft_trace.c (layer), W t code f (suppression/adapter hooks, see mk_overlay.py), A t f = a synth ENVELOPE ATTACK started (voice 0 note_on counter changed),
 * and with OUT.wav the rendered synth (1 channel float->PCM16, level as the engine gives it). type: 0 ANALOG 1 ARCADE 2 INSTRUMENT 3 ENSEMBLE ; patch = the preset's first setting. */
#include <stdio.h>
#include <stdlib.h>
#include "../../../engine/px_dsp.c"
#define FT_NOMAIN
#include "ft_wav.h"
static px_engine e;
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    long n; float *x = ft_rd(argv[1], &n); if (!x) return 1;
    static const int TY[4] = { PX_SYN_ANALOG, PX_SYN_ARCADE, PX_SYN_INSTRUMENT, PX_SYN_ENSEMBLE };
    const int ty = getenv("FT_TYPE") ? TY[atoi(getenv("FT_TYPE")) & 3] : PX_SYN_ANALOG; const int patch = getenv("FT_PATCH") ? atoi(getenv("FT_PATCH")) : 5;
    uint8_t q[5] = { (uint8_t)patch, 50, 50, 50, 50 };
    px_engine_init(&e, 44100.0f); px_s2_reset(&e, ty, q);
    FILE *ftr = getenv("FT_TRACE") ? fopen(getenv("FT_TRACE"), "wb") : 0;
    px_mono *m = &px_s2_st.mono; short *o16 = getenv("FT_WAV") ? malloc(2 * n) : 0;
    unsigned prev_age = trk[0].v[0].age; int st_prev = 0, hyp = 0, stb = 0;
    for (long i = 0; i < n; i++) {
        ft_now = i; const pxt_layer b = m->lay; float y;
        px_synth_run(&e, &x[i], &y, 1);
        const double t = (i + 1) * 1000.0 / 44100.0; const pxt_layer *L = &m->lay;
        if ((L->state == 1 || L->state == 3) && ((b.state != 1 && b.state != 3) || L->wait_n < b.wait_n)) { printf("R %.2f\n", t); hyp = stb = 0; }
        if (L->state == 1 || L->state == 3) {
            if (!hyp && L->stab_n >= 1) { printf("H %.2f %.2f\n", t, L->stab_f); hyp = 1; }
            if (!stb && L->stab_n >= (L->stabreq ? L->stabreq : 2)) { printf("S %.2f %.2f\n", t, L->stab_f); stb = 1; }
        }
        if (L->onset) printf("U %.2f %.2f\n", t, L->f0);
        if (trk[0].v[0].age != prev_age) { printf("A %.2f %.2f\n", t, px_s2_st.f0); prev_age = trk[0].v[0].age; }
        if (ftr && (i & 63) == 63) { float r[3] = { (float)t, px_s2_st.f0, px_s2_st.gate }; fwrite(r, 4, 3, ftr); }
        if (o16) { float v = y * 1.0f; o16[i] = (short)(v > 1 ? 32767 : v < -1 ? -32767 : v * 32767); }
        (void)st_prev;
    }
    if (ftr) fclose(ftr);
    if (o16) { FILE *f = fopen(getenv("FT_WAV"), "wb"); ft_wr16(f, o16, n); fclose(f); }
    return 0;
}
