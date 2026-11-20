/* FastTrack event controller v0 (host-only research, NOT firmware).  One authority for note admission, repick, legato, damp and release.
 * It consumes only EVIDENCE from the unchanged tracker (px_track: t->mono, t->conf, t->onset, the decimated waveform via pxt_period/pxt_refine/pxt_pick_fundamental/pxt_confirm_pick)
 * and does not call pxt_layer_run or px_mono_run.  No amplitude-relative veto exists in this file: no slur-by-loudness, no mute lockout, no dead-pick veto, no fixed 35 ms
 * confirm.  Events carry a reason.  Rule: a rejected candidate leaves no state behind (only the candidate slots are cleared).
 *
 * Evidence flow:  pick evidence (flux onset or level rise) -> CANDIDATE -> per frame leaky votes on the tracker's strongest pitch (4 slots, 30-40 cent clusters)
 *   -> ONE admission decision per frame: best slot share >= ADM_SHARE and the waveform confirms it (confirm_pick: no longer period fits better; fundamental check above 300 Hz)
 *   -> NOTE_ON (new pitch) / REPICK (pick on the sounding pitch).  Bounded: at ADM_MAX frames the best slot is admitted if the fundamental check passes, else the candidate is
 *   REJECTED with a logged reason (the old note, if any, simply continues).
 * ACTIVE: continuous pitch follow within 120 cents a frame (bends, vibrato); pitch HOLD (pitch only, never blocks a pick) while the level collapses;
 *   LEGATO_CHANGE = another steady pitch with no pick for 4 frames (octave: 20), as the legacy takeover but reported as its own event; fundamental arrival fix within 80 ms of an admit.
 * DAMP: fast level < 0.1 x the 150 ms peak -> DAMP + NOTE_OFF at once; the controller stays receptive (no refractory, no lockout).
 * Evidence-based constants below are the ONLY decision parameters; each is ablated in docs/FASTTRACK-EVENT-BENCHMARKS.md. */
#ifndef FT_CTRL_H
#define FT_CTRL_H
#ifndef FTC_ADM_MIN
#define FTC_ADM_MIN 8        /* frames (1.45 ms) before the first admission is possible */
#endif
#ifndef FTC_ADM_MAX
#define FTC_ADM_MAX 45       /* hard bound: 65 ms, then admit on the fundamental check or reject with a reason */
#endif
#ifndef FTC_SHARE
#define FTC_SHARE 0.50f      /* share of the leaky vote the best pitch slot needs */
#endif
#ifndef FTC_SHARE_MAX
#define FTC_SHARE_MAX 0.38f  /* ... at the bound */
#endif
#ifndef FTC_BOUND_ADMIT
#define FTC_BOUND_ADMIT 0    /* at the bound: 1 admits an unconfirmed pitch (the legacy rule), 0 rejects it with a logged reason */
#endif
#ifndef FTC_RISE
#define FTC_RISE 0.0f        /* E1: a flux pick also needs the fast level to be this many times its minimum over the previous 8 frames (0 = off, v0 behaviour) */
#endif
#ifndef FTC_DECAY
#define FTC_DECAY 0.90f
#endif
enum { FTC_NONE = 0, FTC_NOTE_ON = 1, FTC_REPICK = 2, FTC_PITCH_UPDATE = 4, FTC_LEGATO = 8, FTC_NOTE_OFF = 16, FTC_DAMP = 32, FTC_REJECT = 64, FTC_PICK = 128 };
typedef struct { float f, v; } ftc_slot;
typedef struct {
    int cand, gate, key; float f0, vel;
    float env, fe, pk, onset_peak; int peak_n;          /* level trackers */
    ftc_slot slot[4]; int cand_frames;
    int since, leg_n, sub_n; float leg_f, sub_f;
    long frames; float sr;
    int last_reason; float fhist[8]; int fh;
} ft_ctrl;
typedef struct { int ev; float f0, vel; int gate; int reason; float t_pick_ms; int cand_frames; } ft_ctrl_out;
static void ft_ctrl_init(ft_ctrl *c, float sr) { memset(c, 0, sizeof *c); c->sr = sr; }
static void ftc_votes_clear(ft_ctrl *c) { memset(c->slot, 0, sizeof c->slot); c->cand_frames = 0; }
static void ftc_vote(ft_ctrl *c, float m)
{
    for (int k = 0; k < 4; k++) c->slot[k].v *= FTC_DECAY;
    if (m <= 0.0f) return;
    int hit = -1, low = 0;
    for (int k = 0; k < 4; k++) { if (pxt_near(m, c->slot[k].f, 40.0f) && c->slot[k].v > 0.0f) hit = k; if (c->slot[k].v < c->slot[low].v) low = k; }
    if (hit < 0) { hit = low; c->slot[hit].f = m; c->slot[hit].v = 0.0f; }
    else c->slot[hit].f += 0.3f * (m - c->slot[hit].f);
    c->slot[hit].v += 1.0f;
}
/* x: sample; t: the tracker; framed: pxt_run returned 1. o->ev is the event mask for this sample (usually 0). Two independent facts: c->cand (an admission is pending) and c->gate (a note sounds). */
static void ft_ctrl_run(ft_ctrl *c, const px_track *t, float x, int framed, ft_ctrl_out *o)
{
    const float a = px_abs(x), sr = c->sr;
    c->env += (a - c->env) * (a > c->env ? onepole_k(sr, 0.001f) : onepole_k(sr, 0.03f));
    c->fe += (a - c->fe) * (a > c->fe ? 1.0f / (0.001f * sr) : 1.0f / (0.010f * sr));
    c->pk = a > c->pk ? a : c->pk * (1.0f - 1.0f / (0.15f * sr));
    const int on = c->key ? c->env > 0.0015f : c->env > 0.0045f;
    const int rise = on && !c->key; c->key = on;
    o->ev = 0; o->reason = 0; o->cand_frames = 0;
    if (c->peak_n < (int)(0.006f * sr)) { if (a > c->onset_peak) c->onset_peak = a; c->peak_n++; }
    if (!on) {                                           /* the level is gone: release; any pending candidate is dropped (no evidence left) */
        if (c->gate) o->ev |= FTC_NOTE_OFF;
        c->gate = 0; c->cand = 0; ftc_votes_clear(c); c->peak_n = (int)(0.006f * sr); c->onset_peak = 0.0f;
        o->f0 = c->f0; o->vel = c->vel; o->gate = 0; return;
    }
    /* DAMP: a deliberate rapid damp releases the voice at once; nothing else changes and nothing is locked out */
    if (c->gate && !c->cand && c->pk > 0.01f && c->fe < 0.1f * c->pk) { o->ev |= FTC_DAMP | FTC_NOTE_OFF; c->gate = 0; c->pk = c->fe; }
    int flux_ok = framed && t->onset > 0.0f;
    if (flux_ok && FTC_RISE > 0.0f) { float mn = 1e9f; for (int k = 0; k < 8; k++) if (c->fhist[k] < mn) mn = c->fhist[k]; flux_ok = c->fe > FTC_RISE * (mn + 1e-5f); }
    if (framed) { c->fhist[c->fh] = c->fe; c->fh = (c->fh + 1) & 7; }
    if (rise || flux_ok) {           /* a genuine pick may interrupt anything, a pending candidate included; a sounding note keeps sounding until a new one is admitted */
        ftc_votes_clear(c); c->cand = 1; c->onset_peak = 0.0f; c->peak_n = 0; c->leg_n = 0; o->ev |= FTC_PICK;
    }
    if (framed) {
        const float m = t->conf > PXT_CONF ? t->mono : 0.0f;
        if (c->cand) {                                   /* ---- the ONE admission decision ---- */
            c->cand_frames++; ftc_vote(c, m);
            float tot = 1e-6f, bv = 0.0f; int bi = 0;
            for (int k = 0; k < 4; k++) { tot += c->slot[k].v; if (c->slot[k].v > bv) { bv = c->slot[k].v; bi = k; } }
            const float share = bv / tot, f = c->slot[bi].f; const int fr = c->cand_frames;
            int admit = 0; float pitch = 0.0f;
            if (fr >= FTC_ADM_MIN && f > 0.0f && share >= (fr >= FTC_ADM_MAX ? FTC_SHARE_MAX : FTC_SHARE)) {
                pitch = pxt_pick_fundamental(t, f);              /* above 300 Hz: a clearly better lower period replaces it; 0 = cannot be verified */
                if (pitch > 0.0f) admit = pxt_confirm_pick(t, pitch, fr) || (FTC_BOUND_ADMIT && fr >= FTC_ADM_MAX);   /* the waveform must repeat at this period and no longer period fits better */
                else if (fr >= FTC_ADM_MAX) { o->ev |= FTC_REJECT; o->reason = 1; }
            } else if (fr >= FTC_ADM_MAX) { o->ev |= FTC_REJECT; o->reason = 2; }
            if (admit) {
                const int same = c->gate && pxt_near(pitch, c->f0, 50.0f);
                c->f0 = pitch; c->vel = pxt_pick_velocity(c->onset_peak); c->gate = 1; c->cand = 0; c->since = 0; c->leg_n = 0; c->sub_n = 0;
                o->ev |= same ? FTC_REPICK : FTC_NOTE_ON; o->cand_frames = fr;
                c->pk = c->onset_peak > c->fe ? c->onset_peak : c->fe;       /* a started note is judged against its own level */
                ftc_votes_clear(c);
            } else if (o->ev & FTC_REJECT) { c->cand = 0; ftc_votes_clear(c); }   /* a rejected candidate leaves nothing behind */
        } else if (c->gate) {                            /* ---- a sounding note: continuous pitch, pitch hold, legato, fundamental fix ---- */
            const int hold = c->since >= 40 && c->pk > 0.01f && c->fe < 0.5f * c->pk;       /* holds the PITCH only; it can never block a pick or a candidate */
            if (!hold && m > 0.0f) {
                if (pxt_near(m, c->f0, 120.0f)) { c->f0 += 0.6f * (m - c->f0); c->leg_n = 0; }
                else {
                    if (pxt_near(m, c->leg_f, 50.0f)) c->leg_n++; else if (c->leg_n > 0) c->leg_n--; else { c->leg_f = m; c->leg_n = 1; }
                    const int octave = pxt_near(m, 2.0f * c->f0, 50.0f) || pxt_near(m, 0.5f * c->f0, 50.0f);
                    if (c->leg_n >= (octave ? 20 : 4)) {
                        const float p = pxt_pick_fundamental(t, c->leg_f);
                        if (p > 0.0f) { c->f0 = p; o->ev |= FTC_LEGATO; }
                        c->leg_n = 0;
                    }
                }
                if (c->since < 55 && m < 0.55f * c->f0 && pxt_near(c->f0 / (float)(int)(c->f0 / m + 0.5f), m, 50.0f)) {
                    if (pxt_near(m, c->sub_f, 50.0f)) { if (++c->sub_n >= 2) { c->f0 = m; c->sub_n = 0; o->ev |= FTC_PITCH_UPDATE; } } else { c->sub_f = m; c->sub_n = 1; }
                }
            }
            if (c->since < 1000) c->since++;
        }
    }
    o->f0 = c->f0; o->vel = c->vel; o->gate = c->gate;
}
#endif
