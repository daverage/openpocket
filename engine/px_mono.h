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
#ifndef PX_MONO_MINWAIT
#define PX_MONO_MINWAIT 20
#endif
/* Synth2 fix pass 3 (the bleeps): a note speaks only after its pitch has been stable for PX_MONO_STAB tracker frames (the layer's default is 2: 3 ms), and no note speaks sooner than
 * PX_MONO_WAIT_LO frames after its pick (PX_MONO_MINWAIT for notes above 200 Hz). Chosen on a real recording of palm mutes and tremolo, and on the Guitar-TECHS DI sets (docs/research/synth2/SYNTH2-FIXPASS2.md):
 * a pitch more than 50 cents from where the note settles was spoken by 78 % of the onsets before, 19 % after; median latency +2..4 ms, 90th percentile +9..16 ms. */
#ifndef PX_MONO_STAB
#define PX_MONO_STAB 10
#endif
#ifndef PX_MONO_WAIT_LO
#define PX_MONO_WAIT_LO 14
#endif
/* Synth2 fix pass 5 (the organ's bleeps): a note start whose pitch is a harmonic multiple (x2, x3, x4 or /2, /3, /4) of the note played in the last PX_MONO_REF_MS is usually the tracker
 * locking on an upper partial of the pick transient, which corrects itself within 3-20 ms (bleep.wav). It is held back PX_MONO_CONFIRM_MS (the voice keeps the previous pitch; the layer
 * may correct meanwhile) and then speaks at the pitch the layer has by then. 0 = off. */
#ifndef PX_MONO_CONFIRM_MS
#define PX_MONO_CONFIRM_MS 35
#endif
/* Synth2 fix pass 5: a pick whose sound is already gone when its pitch is known (a dead or muted string: the level is PX_MONO_DEAD x its own attack peak or less) is not a note: it would play
 * a short note at a junk pitch (the bleep). 0 = off. */
#ifndef PX_MONO_DEAD
#define PX_MONO_DEAD 0.1f
#endif
#ifndef PX_MONO_REF_MS
#define PX_MONO_REF_MS 800
#endif
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
    int damp;                            /* 1 on the sample a hard damp is detected (with `release`): the voice should stop fast, not ring its patch's release */
} px_mono_out;
/* Synth2 fix pass 2 (a hard mute plays another note): when the string is damped the tracker keeps the note open for ~120 ms and reads the muting thump as a new pick,
 * at a junk pitch. The mute is detected here (the fast level falls 20 dB below the last 150 ms's peak): the voice is released at once, and until the note ends a
 * "new note" is accepted only if its attack peak is within -10.5 dB of the peak before the mute (a real pick); a rejected one leaves the layer as it was. */
typedef struct { px_track trk; pxt_layer lay; int was; float pk, pkl, fe, mute_pk; int mute; float f_ref, f_hold; int ref_age, on_age, pend; } px_mono;

static inline void px_mono_init(px_engine *e, px_mono *m, float fmin_hz)
{
    memset(m, 0, sizeof *m);
    pxt_set_range(&m->trk, fmin_hz);
    pxt_reset(e, &m->trk, &m->lay);
    m->lay.minwait = PX_MONO_MINWAIT;
    m->lay.stabreq = PX_MONO_STAB; m->lay.minwait_lo = PX_MONO_WAIT_LO;
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
        if (m->lay.gate && !m->lay.onset && m->pk > 0.01f && m->fe < 0.1f * m->pk) { m->mute = 1; m->mute_pk = m->pkl; rel = 1; }   /* (not on the sample a note starts: fix pass 5) */
    } else if (!m->lay.gate) m->mute = 0;                                          /* the note ended */
    else if (m->lay.onset) {
        if (m->lay.onset_peak < 0.3f * m->mute_pk) {                              /* a thump, not a pick: undo the retrigger */
            const pxt_layer n = m->lay;
            m->lay = sv; m->lay.env = n.env; m->lay.envs = n.envs; m->lay.key = n.key; m->lay.onset = 0; m->lay.state = 2;
        } else m->mute = 0;
    }
    /* fix pass 5: the harmonic-ratio start is held back (see PX_MONO_CONFIRM_MS) */
    int onset = m->lay.onset;
    if (PX_MONO_CONFIRM_MS > 0) {
        if (onset) {
            const float ref = m->ref_age < (int)(PX_MONO_REF_MS * 0.001f * sr) ? m->f_ref : 0.0f;
            int sus = 0;
            if (ref > 0.0f && m->lay.f0 > 0.0f) {
                const float c = 1200.0f * 1.442695f * logf(m->lay.f0 / ref);        /* cents (ln 2 / 1200 = 1 / 1731) */
                static const float K[6] = {1200.0f, 1901.955f, 2400.0f, -1200.0f, -1901.955f, -2400.0f};
                for (int k = 0; k < 6; k++) { const float d = c - K[k]; if (d < 60.0f && d > -60.0f) sus = 1; }
            }
            if (sus) { m->pend = (int)(PX_MONO_CONFIRM_MS * 0.001f * sr); m->f_hold = f0_before; onset = 0; }
            else m->pend = 0;
        } else if (m->pend > 0) {
            if (!m->lay.gate) m->pend = 0;
            else if (--m->pend == 0) onset = 1;                                      /* it speaks now, at the pitch the layer has by now */
        }
        if (onset) { m->on_age = 0; if (m->lay.f0 > 0.0f) m->f_ref = m->lay.f0; m->ref_age = 0; }
        else if (m->lay.gate) { if (m->on_age < 0x7fffff) m->on_age++; if (m->on_age > (int)(0.06f * sr) && m->pend == 0 && m->lay.f0 > 0.0f) { m->f_ref = m->lay.f0; m->ref_age = 0; } }
        else if (m->ref_age < 0x7fffff) m->ref_age++;
    }
    if (onset && PX_MONO_DEAD > 0.0f && m->fe < PX_MONO_DEAD * m->lay.onset_peak) {   /* a dead pick: no note, and the pitch stays where it was */
        onset = 0; m->pend = 0; m->lay.f0 = m->f_ref > 0.0f ? m->f_ref : f0_before;
    }
    if (onset) {                                                                  /* fix pass 5: a started note is judged against its own level, not the loud note before it (a soft pick after a loud one is not a mute) */
        const float own = m->lay.onset_peak > m->fe ? m->lay.onset_peak : m->fe;
        if (own < m->pk) m->pk = own;
        if (own < m->pkl) m->pkl = own;
    }
    const int g = m->lay.gate;
    o->gate = g; o->onset = onset; o->retrigger = onset && m->was; o->release = (m->was && !g) || rel; o->damp = rel;
    o->f0 = (m->pend > 0 && m->f_hold > 0.0f) ? m->f_hold : m->lay.f0; o->vel = m->lay.vel; o->expr = m->lay.expr; o->conf = m->trk.conf;
    m->was = g;
}
#endif
