/* FastTrack host harness (research, host only): runs the REAL production mono tracker (engine/px_mono.h) over a WAV and logs
 * every stage between the guitar and the synth's note-on, so latency can be attributed.  Nothing here touches firmware/.
 *
 *   cc -O2 -w -fno-builtin -ffp-contract=off -I engine [-DPX_MONO_STAB=2 ...] tests/archive/fasttrack/ft_trace.c firmware/px_math.c -lm -o build/fasttrack/ft_base
 *   ft_base IN.wav [bass]      stdout: event lines; env FT_FRAMES=path (float32 x4 per tracker frame), FT_TRACE=path (float32 x3 per 64 samples)
 *
 * Event lines (t in ms from the first sample; one line each):
 *   R t        a pick begins in the note layer (rise, or a spectral-flux pick in a ringing note)  [stage: onset detection]
 *   H t f      first pitch hypothesis of that pick (any frame with confidence)                     [first pitch hypothesis]
 *   S t f      pitch stable for stabreq frames (layer condition 1; other gates may still hold)    [pitch confidence/stability]
 *   U t f d    the layer's own note-on, d = disposition after the mono wrapper:
 *                N spoken now, S slur, C held by the harmonic-ratio confirm, D dead pick, H hold/weak-pick undo, M mute thump undo
 *   C t f      a confirm-held note speaks                                                           [note-layer confirmation]
 *   N t f v r  the synth's note-on at f Hz, velocity v, r=1 if retrigger                            [synth note-on]
 *   L t f      slur: the voice moves to f, no new attack
 *   X t d      release (d=1 hard damp)
 */
#include <stdio.h>
#include <stdlib.h>
#include "../../../engine/px_dsp.c"
#include "ft_wav.h"
static px_engine e; static px_mono m;
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    long n; float *x = ft_rd(argv[1], &n); if (!x) return 1;
    px_engine_init(&e, 44100.0f);
    px_mono_init(&e, &m, argc > 2 ? PX_MONO_BASS : PX_MONO_GUITAR);
    FILE *ff = getenv("FT_FRAMES") ? fopen(getenv("FT_FRAMES"), "wb") : 0;
    FILE *ft = getenv("FT_TRACE") ? fopen(getenv("FT_TRACE"), "wb") : 0;
    int pend_prev = 0, st_prev = 0, hyp_done = 0, stab_done = 0, frame_prev = -1;
    for (long i = 0; i < n; i++) {
        const pxt_layer b = m.lay;
        px_mono_out o; px_mono_run(&e, &m, x[i], &o);
        const double t = (i + 1) * 1000.0 / 44100.0;
        const pxt_layer *L = &m.lay; const px_track *T = &m.trk;
        /* a new tracker frame? */
        if (T->frame != frame_prev) {
            frame_prev = T->frame;
            if (ff) { float r[4] = { (float)t, T->conf > PXT_CONF ? T->mono : 0.0f, T->conf, T->onset }; fwrite(r, 4, 4, ff); }
        }
        if ((L->state == 1 || L->state == 3) && ((b.state != 1 && b.state != 3) || L->wait_n < b.wait_n)) { printf("R %.2f\n", t); hyp_done = 0; stab_done = 0; }
        if ((L->state == 1 || L->state == 3)) {
            if (!hyp_done && L->stab_n >= 1) { printf("H %.2f %.2f\n", t, L->stab_f); hyp_done = 1; }
            if (!stab_done && L->stab_n >= (L->stabreq ? L->stabreq : 2)) { printf("S %.2f %.2f\n", t, L->stab_f); stab_done = 1; }
        }
        st_prev = L->state;
        if (L->onset) {
            char d = 'N';
            if (o.slur) d = 'S'; else if (m.pend > 0 && !o.onset) d = 'C'; else if (!o.onset) d = 'D';
            printf("U %.2f %.2f %c\n", t, L->f0, d);
        }
        if (pend_prev > 0 && m.pend == 0 && o.onset) printf("C %.2f %.2f\n", t, o.f0);
        pend_prev = m.pend;
        if (o.onset) printf("N %.2f %.2f %.2f %d\n", t, o.f0, o.vel, o.retrigger);
        if (o.slur) printf("L %.2f %.2f\n", t, o.f0);
        if (o.release) printf("X %.2f %d\n", t, o.damp);
        if (ft && (i & 63) == 63) { float r[3] = { (float)t, o.f0, (float)o.gate }; fwrite(r, 4, 3, ft); }
    }
    if (ff) fclose(ff); if (ft) fclose(ft);
    return 0;
}
