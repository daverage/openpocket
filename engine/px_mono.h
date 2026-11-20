/* OpenPocket Synth2: the one monophonic tracker's interface (stage 2).
 *
 * The tracker is engine/px_track.h (the v49 band-bank tracker: pxt_run) and its note layer (pxt_layer_run), kept after the
 * host benchmark against Cycfi Q's BACF (docs/research/synth2/SYNTH2-TRACKER.md). This header is all a synthesis voice may know of it: one call per
 * input sample, one small result. Pitch is continuous (bends, slides and vibrato follow within 120 cents a frame; no semitone
 * grid), a new pick, a hammer-on or slur after 15 ms, or a corrected octave sets `onset`, and the gate falls when the string
 * has died away (-> `release`). The input is the clean guitar (after the DC blocker, before the gate, the octave and every
 * other effect: the caller's job, docs/research/synth2/SYNTH2-PLAN.md).
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
/* Synth2 fix pass 1 (the note-on beep): a note whose first pitch is above PXT_MW_F (200 Hz) waits this many tracker frames (1.45 ms each) before it
 * speaks, so that a low note's fundamental has time to arrive. Measured on DI (tools/research/note_on_diag.c): octave-or-more corrections after the onset
 * 23 of 31 notes at the layer's 5 frames, 6 of 31 at 17; rise-to-onset 12 -> 21 ms on those notes, unchanged below 200 Hz. */
#define PX_MONO_MINWAIT 17
#ifndef PX_MONO_HOLD_AFTER
#define PX_MONO_HOLD_AFTER 40            /* ... but only once the note has settled: this many tracker frames (1.45 ms) after it spoke */
#endif
#ifndef PX_MONO_HOLD
#define PX_MONO_HOLD 0.5f                /* hold the pitch while the fast level is under this share of the last 150 ms's peak (0: never) */
#endif
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
/* Synth2 fix pass 2 (a hard mute plays another note): when the string is damped the tracker keeps the note open for ~120 ms and reads the muting thump as a new pick,
 * at a junk pitch. The mute is detected here (the fast level falls 20 dB below the last 150 ms's peak): the voice is released at once, and until the note ends a
 * "new note" is accepted only if its attack peak is within -10.5 dB of the peak before the mute (a real pick); a rejected one leaves the layer as it was. */
typedef struct { px_track trk; pxt_layer lay; int was; float pk, pkl, fe, mute_pk; int mute; } px_mono;

static inline void px_mono_init(px_engine *e, px_mono *m, float fmin_hz)
{
    memset(m, 0, sizeof *m);
    pxt_set_range(&m->trk, fmin_hz);
    pxt_reset(e, &m->trk, &m->lay);
    m->lay.minwait = PX_MONO_MINWAIT;
}
static inline void px_mono_run(const px_engine *e, px_mono *m, float x, px_mono_out *o)
{
    const float a = px_abs(x), sr = m->lay.sr;
    m->pk = a > m->pk ? a : m->pk * (1.0f - 1.0f / (0.15f * sr));
    m->pkl = a > m->pkl ? a : m->pkl * (1.0f - 1.0f / (0.30f * sr));              /* the level before the mute: a slower memory */
    m->fe += (a - m->fe) * (a > m->fe ? 1.0f / (0.001f * sr) : 1.0f / (0.010f * sr));
    pxt_layer sv;
    if (m->mute) sv = m->lay;
    const float f0_before = m->lay.f0;
    pxt_layer_run(&m->lay, &m->trk, x, pxt_run(e, &m->trk, x));
    /* Synth2 fix pass 3: while the level collapses (a mute, a palm mute) the tracker's strongest component drifts, 60-80 cents up in the DI palm-mute recordings
     * (Guitar-TECHS), and a damped string can drop an octave: the pitch is held (no follow, no slur or octave takeover) until the level recovers or a new note speaks */
    if (PX_MONO_HOLD > 0.0f && m->was && !m->lay.onset && m->lay.state == 2 && m->lay.since >= PX_MONO_HOLD_AFTER && m->pk > 0.01f && m->fe < PX_MONO_HOLD * m->pk) { m->lay.f0 = f0_before; m->lay.cand_n = 0; m->lay.sub_n = 0; }
    int rel = 0;
    if (!m->mute) {
        if (m->lay.gate && m->pk > 0.01f && m->fe < 0.1f * m->pk) { m->mute = 1; m->mute_pk = m->pkl; rel = 1; }
    } else if (!m->lay.gate) m->mute = 0;                                          /* the note ended */
    else if (m->lay.onset) {
        if (m->lay.onset_peak < 0.3f * m->mute_pk) {                              /* a thump, not a pick: undo the retrigger */
            const pxt_layer n = m->lay;
            m->lay = sv; m->lay.env = n.env; m->lay.envs = n.envs; m->lay.key = n.key; m->lay.onset = 0; m->lay.state = 2;
        } else m->mute = 0;
    }
    const int g = m->lay.gate;
    o->gate = g; o->onset = m->lay.onset; o->retrigger = m->lay.onset && m->was; o->release = (m->was && !g) || rel;
    o->f0 = m->lay.f0; o->vel = m->lay.vel; o->expr = m->lay.expr; o->conf = m->trk.conf;
    m->was = g;
}
#endif
