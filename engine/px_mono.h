/* OpenPocket Synth2: the one monophonic tracker's interface (stage 2).
 *
 * The tracker is engine/px_track.h (the v49 band-bank tracker: pxt_run) and its note layer (pxt_layer_run), kept after the
 * host benchmark against Cycfi Q's BACF (docs/SYNTH2-TRACKER.md). This header is all a synthesis voice may know of it: one call per
 * input sample, one small result. Pitch is continuous (bends, slides and vibrato follow within 120 cents a frame; no semitone
 * grid), a new pick, a hammer-on or slur after 15 ms, or a corrected octave sets `onset`, and the gate falls when the string
 * has died away (-> `release`). The input is the clean guitar (after the DC blocker, before the gate, the octave and every
 * other effect: the caller's job, docs/SYNTH2-PLAN.md).
 *
 * Two ranges of the same tracker: guitar (default, 60 Hz: B1 and up) and bass (px_mono_init(.., PX_MONO_BASS): 38 Hz, E1 with
 * room for a down-tuned bass). On the host the bass range gives 84 % notes right on the bass lines against 63 %, with no loss
 * on guitar; on the pedal (v71) it let guitar notes drop an octave, so only a bass sound selects it.
 *
 * Latency (host, from the annotated onset to the first right frame): median 4 ms, 90th percentile 19 ms on electric guitar
 * (Guitar-TECHS); on bass the low fundamentals need two periods at least (E1: 48 ms), the bench shows 43 / 83 ms. The tracker
 * frames every 1.45 ms (PXT_HOP). Not measured on a pedal. */
#ifndef PX_MONO_H
#define PX_MONO_H
#define PX_MONO_GUITAR 0.0f              /* the default floor, PXT_FMIN (60 Hz) */
#define PX_MONO_BASS   38.0f
typedef struct {
    float f0;                            /* Hz while gate (continuous pitch); holds the last pitch after it falls */
    float vel;                           /* pick strength 0.05 .. 1, set at each onset (-40 .. -6 dBFS of the first 6 ms) */
    float expr;                          /* vel x the level against the attack's, >= 0.3 vel: the note's loudness as it decays */
    float conf;                          /* the tracker's confidence in its latest frame's pitch (0 .. 1); updates per frame */
    int gate;                            /* a note sounds */
    int onset;                           /* 1 on the sample a note starts (also a repick, slur or corrected octave) */
    int retrigger;                       /* onset while the gate was already on: restart the envelope, keep the voice */
    int release;                         /* 1 on the sample the gate falls */
} px_mono_out;
typedef struct { px_track trk; pxt_layer lay; int was; } px_mono;

static inline void px_mono_init(px_engine *e, px_mono *m, float fmin_hz)
{
    memset(m, 0, sizeof *m);
    pxt_set_range(&m->trk, fmin_hz);
    pxt_reset(e, &m->trk, &m->lay);
}
static inline void px_mono_run(const px_engine *e, px_mono *m, float x, px_mono_out *o)
{
    pxt_layer_run(&m->lay, &m->trk, x, pxt_run(e, &m->trk, x));
    const int g = m->lay.gate;
    o->gate = g; o->onset = m->lay.onset; o->retrigger = m->lay.onset && m->was; o->release = m->was && !g;
    o->f0 = m->lay.f0; o->vel = m->lay.vel; o->expr = m->lay.expr; o->conf = m->trk.conf;
    m->was = g;
}
#endif
