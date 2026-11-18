/* OpenPocket — v48: guitar-driven acoustic voices, SYN types TRUMPET, TRUMPET MODEL, CELLO, CELLO SECTION
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ported from the host prototype tools/sim/acoustic_voice.c (results: docs/REALISTIC-INSTRUMENT-PROTOTYPE.md).
 * The guitar is a controller: a note layer turns it into pitch, gate, pick strength and expression, which play an
 * independent instrument voice (it does not reshape the guitar's own sound, as BRASS and STRINGS do).
 *
 *  - v49: notes from the shared tracker (engine/px_track.h: the HQ octave's band bank, harmonic salience, picks
 *    from spectral flux, the low notes' octave checked on the waveform) and its note layer (pxt_layer).
 *  - TRUMPET, CELLO SECTION: looped recordings (VSCO 2 CE, CC0; engine/px_acoustic_bank.h, 8-bit mu-law in flash):
 *    nearest root, soft / loud layer by pick strength, expression as level and brightness.
 *  - CELLO: bowed string waveguide; TRUMPET MODEL: lip-reed brass waveguide. Both follow the published algorithms
 *    of the Synthesis ToolKit (STK Bowed / Brass, Perry R. Cook and Gary P. Scavone, MIT-style licence: NOTICE.md),
 *    written anew, with the changes the prototype needed (the brass lip gain normalised over the range, a breath
 *    floor, a drive / pitch table; the bowed loop's delay compensation).
 *
 * Memory: no new engine field. The state (px_acoustic) shares the SYN ensemble buffer (syn_cb, a union in px_dsp.h)
 * and the delay lines use the SYN shifter buffer (syn_buf); both are cleared on a change of SYN type. The code is
 * left in the flash (not in firmware/build.sh's RAM list): one call per block.
 * The Bluetooth test image (PXN_LEAN, its flash is full) has no sample bank: TRUMPET and CELLO SECTION play the
 * waveguides there.
 */
#ifndef PX_ACOUSTIC_H
#define PX_ACOUSTIC_H

#ifndef PXA_SAMPLES
#define PXA_SAMPLES (!PXN_LEAN)
#endif
#if PXA_SAMPLES
#ifdef PXA_BANK_FILE                                   /* (host experiments: another bank, tools/sim/trumpet_compare.c) */
#include PXA_BANK_FILE
#else
#include "px_acoustic_bank.h"
#endif
#endif

#define PXA_TWO_PI 6.283185307f
#ifndef PXA_SKIP
#define PXA_TRP_SOFT 0.25f                             /* v49: the trumpet's soft recording only for picks below this */
#define PXA_SKIP 0.75f                                 /* v49: the share of a cello recording's attack skipped */
#define PXA_KICK 1.0f                                  /* v49: the bowed string's start, of the bow's speed */
#define PXA_BOW_ATT 0.03f                              /* v49: the bow's attack, its soft-pick part (s) */
#endif
static inline int pxa_is(int t) { return t >= PX_SYN_TRUMPET && t <= PX_SYN_CELLO_SECTION; }
static inline int pxa_instr(int t) { return t == PX_SYN_TRUMPET || t == PX_SYN_TRUMPET_MODEL ? 0 : 1; }   /* 0 trumpet, 1 cello */
static inline int pxa_sampled(int t) { return PXA_SAMPLES && (t == PX_SYN_TRUMPET || t == PX_SYN_CELLO_SECTION); }
/* within c cents: the ratio a / b in [lo, hi] (no logarithm per sample) */
static inline int pxa_near(float a, float b, float lo, float hi) { if (b <= 0.0f) return 0; float r = a / b; return r > lo && r < hi; }
#define PXA_C15 0.991373f, 1.008702f
#define PXA_C20 0.988514f, 1.011619f
#define PXA_C50 0.971532f, 1.029302f
#define PXA_OCTAVE_LO 1.942753f, 2.058604f        /* 2x within 50 cents */
#define PXA_OCTAVE_LO_DOWN 0.485765f, 0.514733f

/* the drive / sounding ratio of the brass model, every 2 semitones from D3 (MIDI 50), solved on the host by
 * tests/engine/acoustic_test.c --calibrate with this file's own model */
#ifndef PXA_BR_PUP
#define PXA_BR_P0   64.0f                              /* v49: (see pxa_voice) */
#define PXA_BR_PUP  1.5f
#define PXA_BR_PMAX 4.0f
#endif
#define PXA_BCAL_LO 50
#define PXA_BCAL_N 18
static const float PXA_BCAL[PXA_BCAL_N] = {
    0.93902f, 0.94811f, 0.95680f, 0.96529f, 0.97368f, 0.98221f, 0.99103f, 0.99971f, 1.00666f,
    1.01382f, 1.02132f, 1.02875f, 1.03657f, 1.04462f, 1.05238f, 1.05928f, 1.06552f, 1.07133f};
static float pxa_bcal_tab[PXA_BCAL_N];                /* (the calibration harness writes here; 0 = use PXA_BCAL) */

static inline float pxa_midi(float f) { return 69.0f + 17.312340f * logf(f * (1.0f / 440.0f)); }
static float pxa_brass_cal(float f)
{
    const float *t = pxa_bcal_tab[0] > 0.0f ? pxa_bcal_tab : PXA_BCAL;
    float u = (pxa_midi(f) - PXA_BCAL_LO) * 0.5f;
    if (u < 0.0f) u = 0.0f;
    if (u > PXA_BCAL_N - 1.001f) u = PXA_BCAL_N - 1.001f;
    int i = (int)u; return t[i] + (t[i + 1] - t[i]) * (u - (float)i);
}

static void pxa_setup(px_engine *e, int t, float a, float b, float c, float d, float f)
{
    px_acoustic *s = &e->syn_ac;
    const float sr = e->sr;
    const int instr = pxa_instr(t);
    /* note layer filters (coefficients only: the states were cleared with the type) */
    synth_hq_filters(&s->lo1, &s->lo2, sr);
    bq_highpass(&s->u0, sr, 200.0f, 0.7f); bq_lowpass(&s->u1, sr, 1000.0f, 0.54f); bq_lowpass(&s->u2, sr, 1000.0f, 1.31f);
    bq_highpass(&s->h1, sr, 1500.0f, 0.7f); bq_highpass(&s->h2, sr, 1500.0f, 0.7f);
    s->k_ea = onepole_k(sr, 0.001f); s->k_er = onepole_k(sr, 0.03f);
    s->k_ha = onepole_k(sr, 0.0005f); s->k_hr = onepole_k(sr, 0.010f); s->k_hs = onepole_k(sr, 0.050f);
    s->k_xa = onepole_k(sr, 0.015f); s->k_xr = onepole_k(sr, 0.40f);
    s->hist_n = (int)(0.003f * sr); if (s->hist_n > PXA_HIST) s->hist_n = PXA_HIST;
    /* OCTAVE (a): -1 / 0 / +1 around the instrument's own register (the trumpet an octave above the guitar) */
    const int oct = a < 0.34f ? -1 : (a < 0.67f ? 0 : 1);
    s->oct_mul = (instr == 0 ? 2.0f : 1.0f) * (oct < 0 ? 0.5f : (oct > 0 ? 2.0f : 1.0f));
    s->rel_k = onepole_k(sr, 0.010f + 0.25f * d * d);          /* RELEASE (d): time constant 10 .. 260 ms */
    s->dry = f;
    s->k_att = onepole_k(sr, 0.012f);
    if (t == PX_SYN_CELLO) {
        s->bow = 0.15f + 0.6f * b;                             /* BOW (b): pressure */
        s->vib = 0.007f * c;                                   /* VIBRATO (c): up to 0.7 % (12 cents) */
        s->dyn_db = 18.0f;
    } else if (t == PX_SYN_TRUMPET_MODEL) {
        s->bright = b;                                         /* BRIGHT (b) */
        s->dyn_db = 6.0f + 30.0f * c;                          /* DYNAMICS (c): 6 .. 36 dB */
    } else {
        /* TONE (b): the low-pass at full expression, 3 .. 43 harmonics; v49: wide open from the middle up (100 harmonics:
         * the recording as it is), so the brass keeps its bright upper partials */
        s->tone = b < 0.5f ? 3.0f + 80.0f * b * b : 23.0f + 154.0f * (b - 0.5f);
        s->dyn_db = 6.0f + 30.0f * c;                          /* DYNAMICS (c) */
    }
    if (instr == 1) {          /* cello body: air mode, main wood modes, bridge hill; no rumble, no fizz */
        bq_highpass(&s->body[0], sr, 55.0f, 0.7f);
        bq_peak(&s->body[1], sr, 105.0f, 2.0f, 5.0f);
        bq_peak(&s->body[2], sr, 205.0f, 2.5f, 6.0f);
        bq_peak(&s->body[3], sr, 420.0f, 1.8f, 3.0f);
        bq_peak(&s->body[4], sr, 2300.0f, 0.9f, 4.0f);
        bq_lowpass(&s->body[5], sr, 5500.0f, 0.7f);
        s->nbody = 6;
    } else {                   /* trumpet bell */
        bq_highpass(&s->body[0], sr, 250.0f, 0.7f);
        bq_peak(&s->body[1], sr, 1200.0f, 0.8f, 3.0f);
        bq_lowpass(&s->body[2], sr, 7000.0f, 0.7f);
        s->nbody = 3;
    }
    /* output: the level each voice gives at full expression, brought to the engine's other SYN voices (calibrated on
     * assets/loops/lead.wav against BRASS, tests/engine/acoustic_test.c) */
    s->gain = t == PX_SYN_CELLO ? 0.25f * PXA_G_CELLO : t == PX_SYN_TRUMPET_MODEL ? 7.0f * PXA_G_TMODEL
            : pxa_sampled(t) ? 1.4f * PXA_G_SAMPLE * (t == PX_SYN_TRUMPET ? PXA_G_TRUMPET : 1.0f) : 1.0f;
    if (!pxa_sampled(t) && t == PX_SYN_TRUMPET) s->gain = 7.0f * PXA_G_TMODEL;     /* (no bank: the model) */
    if (!pxa_sampled(t) && t == PX_SYN_CELLO_SECTION) s->gain = 0.25f * PXA_G_CELLO;
    s->sr = sr; s->inv_sr = 1.0f / sr;
    pxt_prepare(e);
}

/* ---- v49: the note layer is the shared tracker's (px_track.h): its state out of the engine (it does not fit the SYN
 * union beside the voice), in the low RAM on the pedal; cleared with a change of SYN type (pxa_rate_consts) ---- */

#if PXA_SAMPLES
static inline float pxa_mu_at(const pxa_smp *m, int i) { if (i >= (int)m->le) i -= (int)(m->le - m->ls); return PXA_MU[m->d[i]]; }
static float pxa_hermite(const pxa_smp *m, float pos)
{
    const int i = (int)pos; const float t = pos - (float)i;
    const float xm1 = i > 0 ? PXA_MU[m->d[i - 1]] : PXA_MU[m->d[i]], x0 = PXA_MU[m->d[i]],
                x1 = pxa_mu_at(m, i + 1), x2 = pxa_mu_at(m, i + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
static int pxa_pick(int instr, float f, int dyn)
{
    int best = -1; float bd = 1e9f;
    for (int k = 0; k < PXA_NBANK; k++) {
        const pxa_smp *m = &PXA_BANK[k]; if (m->instr != instr || m->dyn != dyn) continue;
        float r = f / m->f0; if (r < 1.0f) r = 1.0f / r;
        if (r < bd) { bd = r; best = k; }
    }
    return best;
}
static void pxa_start(px_acoustic *s, int smp, float pos, float rise)
{
    for (int k = 0; k < 3; k++) if (s->pl[k].active) s->pl[k].fade = 1;
    int q = -1;
    for (int k = 0; k < 3; k++) if (!s->pl[k].active || s->pl[k].gain < 1e-3f) { q = k; break; }
    if (q < 0) { q = 0; for (int k = 1; k < 3; k++) if (s->pl[k].gain < s->pl[q].gain) q = k; }
    s->pl[q].smp = smp; s->pl[q].pos = pos; s->pl[q].gain = rise > 0.0f ? 0.0f : 1.0f; s->pl[q].fade = 0; s->pl[q].active = 1;
    s->pl[q].rise = rise;
}
static float pxa_sample_tick(px_acoustic *s, int instr, float f)
{
    if (s->onset && f > 0.0f) {
        const int k = pxa_pick(instr, f, s->vel > (instr == 0 ? PXA_TRP_SOFT : 0.55f));
        /* v49: the cello section's recordings bow in slowly (up to 0.36 s): most of that attack is skipped, a short
         * fade-in instead, so the note answers the pick */
        if (k >= 0) {
            const pxa_smp *m = &PXA_BANK[k];
            const float skip = instr == 1 ? PXA_SKIP * (float)m->att : 0.0f;
            pxa_start(s, k, skip, skip > 0.0f ? s->k_fade5 : 0.0f); s->root_f = m->f0; s->root_t = 0.0f;
        }
        s->held = 1;
    } else if (s->gate && f > 0.0f && s->held && s->root_f > 0.0f && s->root_t > 0.06f &&
               !pxa_near(f, s->root_f, 0.707107f, 1.414214f)) {            /* a slur 6 semitones away: next root (v49: at
                                                                             most every 60 ms, so a flicker cannot churn) */
        const int k = pxa_pick(instr, f, s->vel > (instr == 0 ? PXA_TRP_SOFT : 0.55f));
        if (k >= 0 && PXA_BANK[k].f0 != s->root_f) { pxa_start(s, k, (float)PXA_BANK[k].ls, s->k_fade20); s->root_f = PXA_BANK[k].f0; s->root_t = 0.0f; }
    }
    s->root_t += s->inv_sr;
    if (!s->gate) s->held = 0;
    const float fade_k = 1.0f - s->k_fade10;
    float y = 0.0f;
    for (int k = 0; k < 3; k++) {
        if (!s->pl[k].active) continue;
        if (s->pl[k].fade) { s->pl[k].gain *= fade_k; if (s->pl[k].gain < 1e-4f) { s->pl[k].active = 0; continue; } }
        else if (s->pl[k].gain < 1.0f) s->pl[k].gain += (1.0f - s->pl[k].gain) * s->pl[k].rise;
        const pxa_smp *m = &PXA_BANK[s->pl[k].smp];
        y += pxa_hermite(m, s->pl[k].pos) * s->pl[k].gain;
        const float ff = f > 0.0f ? f : m->f0;
        s->pl[k].pos += ff / m->f0 * (float)m->rate * s->inv_sr;
        if (s->pl[k].pos >= (float)m->le) s->pl[k].pos -= (float)(m->le - m->ls);
    }
    /* brightness follows expression: one-pole at (3 .. TONE) harmonics */
    float fc = (f > 0.0f ? f : 200.0f) * (3.0f + s->tone * s->expr), k = PXA_TWO_PI * fc * s->inv_sr;
    if (k > 0.9f) k = 0.9f;
    s->lp += k * (y - s->lp);
    return s->lp;
}
#endif

/* delay lines in syn_buf: the bowed string's neck [0, 1024) and bridge [1024, 1280); the brass bore [0, 2048) */
static inline float pxa_dl(const float *b, int mask, int w, float delay)
{
    float rp = (float)w - delay; while (rp < 0.0f) rp += (float)(mask + 1);
    const int i = (int)rp; const float fr = rp - (float)i;
    const float x0 = b[i & mask], x1 = b[(i + 1) & mask];
    return x0 + (x1 - x0) * fr;
}

static float pxa_bowed_tick(px_engine *e, px_acoustic *s, float f, float nz)
{
    float *neck = e->syn_buf, *bridge = e->syn_buf + 1024;
    const float target = s->gate ? 0.03f + 0.22f * s->expr : 0.0f;
    const float k = s->gate ? s->k_bow_a : s->k_bow_r;
    s->bowv += (target - s->bowv) * k;
    const float slope = 5.0f - 4.0f * (s->bow + 0.3f * s->expr);
    s->vib_ph = wrap1(s->vib_ph + 5.2f * s->inv_sr);
    const float fv = f * (1.0f + s->vib_amt * lfo_sin(s->vib_ph));
    const float base = s->sr / fv - 1.7f;                 /* STK subtracts 4; measured on this loop: 1.7 */
    float bd = base * 0.127236f, nd = base * (1.0f - 0.127236f);
    bd = bd < 1.0f ? 1.0f : (bd > 250.0f ? 250.0f : bd);
    nd = nd < 1.0f ? 1.0f : (nd > 1020.0f ? 1020.0f : nd);
    const float bo = pxa_dl(bridge, 255, s->bw, bd), no = pxa_dl(neck, 1023, s->nw, nd);
    const float pole = 0.75f - 0.2f * 22050.0f * s->inv_sr;
    s->str_lp = 0.95f * (1.0f - pole) * bo + pole * s->str_lp;
    const float br = -s->str_lp, nr = -no, sv = br + nr;
    s->noise_lp += 0.3f * (nz - s->noise_lp);
    const float dv = s->bowv * (1.0f + 0.04f * s->noise_lp) - sv;
    float q = (dv + 0.001f) * slope; q = px_abs(q) + 0.75f; q = q * q; q = 1.0f / (q * q);   /* the bow table */
    q = q > 0.98f ? 0.98f : (q < 0.01f ? 0.01f : q);
    float nv = dv * q;
    if (s->kick_n > 0) {                                  /* v49: one period of sawtooth set the string going */
        nv += s->kick_a * (2.0f * (float)s->kick_n / s->kick_p - 1.0f); s->kick_n--;
    }
    neck[s->nw] = br + nv; s->nw = (s->nw + 1) & 1023;
    bridge[s->bw] = nr + nv; s->bw = (s->bw + 1) & 255;
    float y = s->str_lp;
    if (s->held_t < 0.08f && s->gate) y += 0.05f * s->bowv * nz * (1.0f - s->held_t * 12.5f);   /* bow scrape */
    return y;
}

static float pxa_brass_tick(px_engine *e, px_acoustic *s, float f, float nz)
{
    float *bore = e->syn_buf;
    const float target = s->gate ? 0.85f + 0.25f * s->expr : 0.0f;
    s->breath += (target - s->breath) * (s->gate ? s->k_br_a : s->k_br_r);
    const float r = 0.997f;
    const float mp = s->mp > 1.0f ? s->mp : 1.0f;
    const float mouth = 0.3f * mp * s->breath * (1.0f + 0.012f * nz);
    float bdl = s->sr / f * 2.0f + 10.0f; if (bdl > 2040.0f) bdl = 2040.0f;
    const float bp = 0.85f * pxa_dl(bore, 2047, s->ow, bdl);
    const float dp = mouth - bp;
    const float lip = s->lip_g * dp - s->lip_a1 * s->lip_y1 - r * r * s->lip_y2;
    s->lip_y2 = s->lip_y1; s->lip_y1 = lip;
    float d2 = lip * lip; if (d2 > 1.0f) d2 = 1.0f;
    const float o = d2 * mouth + (1.0f - d2) * bp;
    const float dc = o - s->dc_x + 0.99f * s->dc_y; s->dc_x = o; s->dc_y = dc;
    bore[s->ow] = dc; s->ow = (s->ow + 1) & 2047;
    /* radiation (half a differentiator) and, near full breath, a soft clip for the brassy steepening */
    const float e2 = s->expr * s->expr, g = 1.0f + (1.0f + 6.0f * s->bright) * e2 * e2, sh = px_tanh(g * dc * 15.0f) / (g * 15.0f);
    float y = sh + (2.0f + 4.0f * s->bright) * (sh - s->out_lp); s->out_lp = sh;
    if (s->held_t < 0.03f && s->gate) y += 0.02f * s->breath * nz * s->vel;     /* tongue / breath */
    return y * (1.0f / mp);                          /* v49: (the pressure's extra level back out) */
}

static void pxa_rate_consts(px_acoustic *s)
{
    if (s->k_fade10 != 0.0f) return;                 /* constants that need the rate (once per type) */
    memset(&pxa_trk, 0, sizeof pxa_trk); pxt_layer_init(&pxa_lay, s->sr);   /* v49: a fresh tracker for the new type */
    s->k_fade10 = onepole_k(s->sr, 0.010f); s->k_fade20 = onepole_k(s->sr, 0.020f);
    s->k_bow_a = onepole_k(s->sr, 0.004f); s->k_bow_r = onepole_k(s->sr, 0.060f);
    s->k_fade5 = onepole_k(s->sr, 0.005f);
    s->k_br_a = onepole_k(s->sr, 0.010f); s->k_br_r = onepole_k(s->sr, 0.015f);
}

/* the instrument for one sample, from the note layer's s->f0s, gate, onset, vel, expr (tests drive it directly) */
static inline float pxa_voice(px_engine *e, px_acoustic *s, int t)
{
    const int instr = pxa_instr(t), sampled = pxa_sampled(t);
    const int bowed = !sampled && instr == 1, brass = !sampled && instr == 0;
    s->rnd = s->rnd * 1664525u + 1013904223u;
    const float nz = (float)(s->rnd >> 8) * (2.0f / 16777216.0f) - 1.0f;
    float f = s->f0s * s->oct_mul;
    if (f <= 0.0f) f = s->last_f > 0.0f ? s->last_f : 110.0f;
    if (s->onset) {
        s->held_t = 0.0f; s->vib_amt = 0.0f;
        s->k_bow_a = onepole_k(s->sr, 0.004f + PXA_BOW_ATT * (1.0f - s->vel));   /* v49: 4 .. 34 ms (was 10 .. 110) */
        if (bowed && f > 0.0f) { s->kick_p = s->sr / f; s->kick_n = (int)s->kick_p; s->kick_a = PXA_KICK * (0.03f + 0.22f * s->expr); }
        s->k_br_a = onepole_k(s->sr, 0.004f + 0.030f * (1.0f - s->vel));
        s->tick = 0;                                 /* the slow parts at once for the new note */
    }
    s->held_t += s->inv_sr;
    if (bowed && s->held_t > 0.35f && s->gate) s->vib_amt += (s->vib - s->vib_amt) * 0.00006f;
    if ((s->tick++ & 15) == 0) {                     /* every 16 samples: level from expression, brass lip */
        s->lvl_t = s->gate ? expf((s->expr - 1.0f) * s->dyn_db * 0.115129f) : 0.0f;
        if (brass) {
            const float fb = f * pxa_brass_cal(f), w = PXA_TWO_PI * fb * 0.92f * s->inv_sr;
            s->lip_a1 = -2.0f * 0.997f * cosf(w); s->lip_g = 6.0f * 2.0f * (1.0f - 0.997f) * sinf(w);
            s->fb = fb;
            /* v49: a high note needs more pressure to speak, as for a player: above PXA_BR_P0 it rises PXA_BR_PUP
             * times an octave (the model fell silent above about sounding C5 otherwise) */
            const float o = (pxa_midi(f) - PXA_BR_P0) * (1.0f / 12.0f);
            s->mp = o > 0.0f ? powf(PXA_BR_PUP, o) : 1.0f;
            if (s->mp > PXA_BR_PMAX) s->mp = PXA_BR_PMAX;
        }
    }
    float y;
#if PXA_SAMPLES
    if (sampled) y = pxa_sample_tick(s, instr, s->f0s * s->oct_mul);
    else
#endif
    if (bowed) y = pxa_bowed_tick(e, s, f, nz);
    else y = pxa_brass_tick(e, s, s->fb > 0.0f ? s->fb : f, nz);
    s->last_f = f;
    if (!sampled) for (int i = 0; i < s->nbody; i++) y = bq_run(&s->body[i], y);
    s->amp += (s->lvl_t - s->amp) * (s->gate ? s->k_att : s->rel_k);
    return y * s->gain * (bowed ? 1.0f : s->amp);    /* the bowed string's level is its bow; it rings out on its own */
}

/* one block of the SYN voice (in place), output trimmed by strim */
static __attribute__((noinline)) void pxa_run(px_engine *e, int t, float *xb, int n, float strim)
{
    px_acoustic *s = &e->syn_ac;
    pxa_rate_consts(s);
    for (int k = 0; k < n; k++) {
        const float x = xb[k];
        pxt_layer_run(&pxa_lay, &pxa_trk, x, pxt_run(e, &pxa_trk, x));
        s->gate = pxa_lay.gate; s->onset = pxa_lay.onset; s->vel = pxa_lay.vel; s->expr = pxa_lay.expr;
        if (pxa_lay.gate) s->f0s = pxa_lay.f0;
        xb[k] = (pxa_voice(e, s, t) + x * s->dry + e->dn) * strim;
    }
}

#endif
