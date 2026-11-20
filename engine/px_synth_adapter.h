/* OpenPocket Synth2: the host adapter between the mono tracker (px_mono.h) and Felucca's fixed-point synthesis engines
 * (third_party/felucca-engines, MIT; docs/research/synth2/SYNTH2-PLAN.md). Included once, at the end of px_dsp.c: one translation unit with the engine,
 * so the pedal, the host tests, `make render` and the WebAssembly simulator run the same code.
 *
 * What it is: one monophonic voice, played by ANALOG or LOFI (the SYNTH types ANALOG and ARCADE). Each input sample goes through the tracker; every 32 samples (Felucca's block, CTL) the adapter turns
 * what the tracker said (pitch, onset, retrigger, release, pick strength, loudness) into Felucca's voice and its per-block modulation, renders
 * the block, and the output is returned one block later (a fixed 32 samples, 0.73 ms, so any call size works and the result does not depend
 * on it). Pitch is continuous: bends, slides and vibrato move the oscillator (1/16 semitone plus Felucca's fine residual), never a
 * semitone grid. A new note restarts the envelope; a repicked or slurred note retriggers a sounding voice and keeps its phases and filter
 * (Felucca's retrigger rule); a release starts the envelope's release.
 * Felucca renders integers (Q15 amplitude, int32 output): they are converted to float once per block at the end, nothing per sample on the input side.
 *
 * Host contract that Felucca documents (felucca_engine.h): NVOICE, NPART, track_t with p[], the P_* indexes, the tables (fe_tables.h), then dsp.c
 * and the engines. Control values (envelope, patch macros) are computed here as float once a block. Nothing allocates, nothing blocks. */
#ifndef PX_SYNTH_ADAPTER_H
#define PX_SYNTH_ADAPTER_H
#define NVOICE 1
#define NPART 1
#include "../third_party/felucca-engines/fe_tables.h"
#include "../third_party/felucca-engines/src/felucca_engine.h"
enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track { int16_t p[P_COUNT]; voice_t v[NVOICE]; } track_t;
static track_t trk[NPART];
#include "../third_party/felucca-engines/src/dsp.c"
#include "../third_party/felucca-engines/src/eng_analog.c"
#include "../third_party/felucca-engines/src/eng_lofi.c"
#include "px_synth_patches.h"

#define PX_ARCADE_SPLIT_AS PX_SYN_ARCADE
#define PX_S2_DAMP_MS 40.0f                           /* release time constant after a hard mute: the patch's own would ring on for a second or more */
#define PX_S2_OUT 0.30f                                /* float output per Felucca full scale at LEVEL noon, trim 1.0 (set on the host against the loops' level) */

typedef struct {
    px_mono mono;                                      /* the tracker (px_mono.h) */
    int type;                                          /* PX_SYN_ANALOG, PX_SYN_ARCADE or PX_SYN_ARCADE_SPLIT */
    int arc;                                           /* 1: Felucca LOFI (ARCADE, ARCADE SPLIT) */
    uint8_t sq[5]; int split_note, upper;              /* ARCADE SPLIT: its settings, the split note, which range the sounding note is in */
    float dry_c;                                       /* the guitar's share as it is now (ramped: a MIX change makes no click) */
    float dry;                                         /* ARCADE's MIX: the clean guitar's share of the output */
    const engine_t *eng;
    const px_patch *patch;
    float cut_off, res_off, time_mul, level;           /* macros: cutoff and resonance offsets, envelope time multiplier, output gain */
    float env, sus, att_k, dec_k, rel_k, fenv_amp;     /* envelope 0..1 and its per-block coefficients; the amount into the filter */
    int stage;                                         /* 0 off, 1 attack, 2 decay / sustain, 3 release */
    float semis, pitch_cur;                            /* the note in MIDI semitones (continuous), what the voice plays now */
    float vel, expr_r, last_amp;
    float dmp_k; int damped;                                        /* the gate fell on a hard damp: the release is fast (PX_S2_DAMP_MS) */
    int pend_on, pend_re, pend_off;                    /* the block's events: a note starts, a sounding voice retriggers, the gate fell */
    int cnt, rpos; float ring[CTL];                    /* samples in this block; where the output is read */
    unsigned blk; int32_t out[CTL]; float lfo;
    float f0, gate;
} px_s2;
static px_s2 px_s2_st PX_LO(px_s2_st);

static inline float px_s2_ms_k(float ms) { return 1.0f - expf(-(float)CTL * 1000.0f / (FS * (ms < 0.5f ? 0.5f : ms))); }   /* a one-pole per block */

/* the patch and the macros from the block's five settings (all of them are the preset's: nothing else shapes the sound).
 * ANALOG: PATCH, CUTOFF, RESONANCE, ENVELOPE, LEVEL. ARCADE: PATCH, CHARACTER (the DUTY: pulse width / wave table), SWEEP (the pitch sweep down),
 * DECAY (a stepped patch's staircase, else the envelope's decay and release times), MIX (guitar against synth). 50 = the patch as written. */
static inline int px_s2_clamp127(float v) { const int i = (int)(v + (v < 0.0f ? -0.5f : 0.5f)); return i < 0 ? 0 : i > 127 ? 127 : i; }
static void px_s2_apply(px_s2 *s, int type, const uint8_t *q)
{
    const int ens = type == PX_SYN_ENSEMBLE;
    const int arc = type == PX_SYN_ARCADE || type == PX_SYN_ARCADE_SPLIT;
    const px_patch *pa = ens ? &PX_ENSEMBLE_PATCHES[px_ens_slot(q[0])] : &(arc ? PX_ARCADE_PATCHES : PX_ANALOG_PATCHES)[px_patch_slot(q[0])];
    const float m1 = f01(q[1]), m2 = f01(q[2]), m3 = f01(q[3]), m4 = f01(q[4]);
    s->type = type; s->arc = arc; s->patch = pa; s->eng = arc ? &ENG_LOFI : &ENG_ANALOG;   /* (ENSEMBLE: ANALOG) */
    track_t *t0 = &trk[0];
    for (int k = 0; k < 8; k++) t0->p[P_E0 + k] = pa->e[k];
    t0->p[P_ATK] = pa->env[0]; t0->p[P_REL] = pa->env[3]; t0->p[P_FTYPE] = 0;
    float tm = 1.0f, tdec = 1.0f, tmr = -1.0f;                                                /* tmr: the release's own multiplier (ENSEMBLE), else tm */
    if (ens) {                                                                              /* ENSEMBLE: PATCH, TONE, ATTACK, RELEASE, LEVEL */
        t0->p[P_E4] = (int16_t)px_s2_clamp127((float)pa->e[4] + (m1 - 0.5f) * 127.0f);     /* TONE: the filter's cutoff, +-63 steps around the patch's */
        tm = powf(2.0f, (m2 - 0.5f) * 4.0f);                                                /* ATTACK: x 1/4 .. x4 */
        tmr = powf(2.0f, (m3 - 0.5f) * 4.0f);                                               /* RELEASE: x 1/4 .. x4 */
        s->level = (m4 < 0.5f ? 4.0f * m4 * m4 : db2lin((m4 - 0.5f) * 12.0f)) * (pa->trim / 64.0f);
        s->dry = 0.0f;
    } else if (!arc) {
        t0->p[P_E4] = (int16_t)px_s2_clamp127((float)pa->e[4] + (m1 - 0.5f) * 127.0f);     /* CUTOFF: +-63 Felucca steps around the patch's */
        t0->p[P_E5] = (int16_t)px_s2_clamp127((float)pa->e[5] + (m2 - 0.5f) * 127.0f);     /* RESONANCE */
        tm = powf(2.0f, (m3 - 0.5f) * 4.0f);                                                /* ENVELOPE: attack and release x 1/4 .. x4 */
        s->level = (m4 < 0.5f ? 4.0f * m4 * m4 : db2lin((m4 - 0.5f) * 12.0f)) * (pa->trim / 64.0f);
        s->dry = 0.0f;
    } else {
        t0->p[P_E2] = (int16_t)px_s2_clamp127((float)pa->e[2] + (m1 - 0.5f) * 127.0f);     /* CHARACTER */
        t0->p[P_E4] = (int16_t)px_s2_clamp127((float)pa->e[4] + (m2 - 0.5f) * 127.0f);     /* SWEEP */
        if (pa->e[0] == 4) {                                                                /* a stepped patch: DECAY moves its staircase (CRSH / 4 = 1..16 one shot, 17..31 looping) */
            int b = pa->e[3] >> 2;
            if (b > 0) { b += (int)((m3 - 0.5f) * 16.0f + (m3 < 0.5f ? -0.5f : 0.5f)); const int lo = pa->e[3] >> 2 >= 17 ? 17 : 1, hi = pa->e[3] >> 2 >= 17 ? 31 : 16; b = b < lo ? lo : b > hi ? hi : b; }
            t0->p[P_E3] = (int16_t)(b << 2);
        } else tdec = powf(2.0f, (m3 - 0.5f) * 4.0f);                                      /* else the envelope's decay and release */
        tm = tdec;
        s->level = pa->trim / 64.0f * (m4 * 2.0f < 1.0f ? m4 * 2.0f : 1.0f);              /* MIX: 0 guitar only .. 50 both .. 100 synth only */
        s->dry = (1.0f - m4) * 2.0f < 1.0f ? (1.0f - m4) * 2.0f : 1.0f;
    }
    const float atk_ms = 1.0f + (pa->env[0] / 127.0f) * (pa->env[0] / 127.0f) * 1500.0f;
    const float dec_ms = 5.0f + (pa->env[1] / 127.0f) * (pa->env[1] / 127.0f) * 3000.0f;
    const float rel_ms = 10.0f + (pa->env[3] / 127.0f) * (pa->env[3] / 127.0f) * 3000.0f;
    s->att_k = px_s2_ms_k(atk_ms * tm); s->dec_k = px_s2_ms_k(dec_ms * (arc ? tdec : 1.0f)); s->rel_k = px_s2_ms_k(rel_ms * (tmr > 0.0f ? tmr : tm));
    s->dmp_k = px_s2_ms_k(PX_S2_DAMP_MS);
    s->sus = pa->env[2] / 127.0f;
    s->fenv_amp = (float)pa->fenv;
}
static void px_s2_set(px_s2 *s, int type, const uint8_t *q)
{
    if (type != PX_SYN_ARCADE_SPLIT) { px_s2_apply(s, type, q); return; }
    memcpy(s->sq, q, 5); s->split_note = px_split_note(q[2]);
    const uint8_t qq[5] = {q[s->upper ? 1 : 0], 50, 50, q[3], q[4]};       /* the sounding range's patch, its macros at noon, DECAY and MIX shared */
    px_s2_apply(s, PX_ARCADE_SPLIT_AS, qq); s->type = type;
}
static void px_s2_reset(px_engine *e, int type, const uint8_t *q)
{
    px_s2 *s = &px_s2_st;
    memset(s, 0, sizeof *s);
    memset(trk, 0, sizeof trk);
    px_mono_init(e, &s->mono, PX_MONO_GUITAR);
    px_s2_set(s, type, q);
    s->dry_c = s->dry;
}
/* semitones (MIDI, 69 = 440 Hz) -> Felucca's pitch16 (1/16 semitone) and the residual in its fine units (1/4096 of the increment) */
static inline void px_s2_pitch(float semis, int32_t *pitch16, int32_t *fine, int round_to_step)
{
    float x = semis * 16.0f + (round_to_step ? 0.5f : 0.0f);   /* (LOFI plays pitch16 only: round to its 1/16 semitone, ANALOG takes the residual in `fine`) */ if (x < 0.0f) x = 0.0f; if (x > 2046.9f) x = 2046.9f;
    const int p = (int)x;
    *pitch16 = p;
    *fine = round_to_step ? 0 : (int32_t)((x - (float)p) * 14.8f + 0.5f);   /* 2^(1/192) - 1 = 0.36 % = 14.8 / 4096 */
}
/* The NOISE patch's voice (Synth2 fix pass 1): the Arcade Poly noise of the synth branch (engine/px_pulse_poly.h at 3bdca5d^): a 32-bit xorshift (13, 17, 5)
 * stepped on the oscillator's phase wraps, so the grain follows the note, instead of Felucca's 15-bit LFSR (a 1-bit, buzzier pattern). Same sample hold (CRSH),
 * 4-bit quantiser, TONE low-pass and amplitude path as lofi_render, so everything else of the patch works as before. The uniform sample is 4.8 dB below the
 * LFSR's full-scale square, which the patch's trim makes up. */
static void px_s2_noise(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int32_t hold = FE_HOLD(1 + p[P_E3] / 8);
    int32_t lpk = 3000 + (p[P_E7] << 8);
    if (lpk > 32767) lpk = 32767;
    lpk = fe_pole(lpk);
    const uint32_t inc = pitch_inc(clamp(m->pitch16, 0, 2047));
    uint32_t ph0 = v->ph[0], nz = (uint32_t)v->s[2];
    int32_t held = v->s[0], cnt = v->s[1], lp = v->s[4];
    if (nz < 0x10000u) nz ^= 0x91e10da5u;                                  /* (note_on seeds the LFSR's 0x7FFF: xorshift needs bits up high) */
    for (uint32_t i = 0; i < n; i++) {
        if (--cnt <= 0) {
            cnt = hold;
            if ((ph0 + inc * (uint32_t)hold) < ph0) { nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5; }
            int32_t s = (int32_t)(nz >> 16) - 32768;
            s = shl((s + (1 << 11)) >> 12, 12);                            /* 4 bits, rounded */
            if (p[P_E3] > 64) s = shl(s >> 12, 12);
            held = s;
        }
        ph0 += inc;
        lp += mulq15(held - lp, lpk);
        out[i] += voice_amp(lp, m, i);
    }
    v->ph[0] = ph0; v->s[0] = held; v->s[1] = cnt; v->s[2] = (int32_t)nz; v->s[4] = lp;
}
/* one control block: the events, the voice, the envelope, the render. Writes ring[CTL]. */
static __attribute__((noinline)) void px_s2_block(px_s2 *s)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[0];
    if (s->pend_on && s->f0 > 0.0f && s->type == PX_SYN_ARCADE_SPLIT) {        /* the note's range picks its patch (the notes from the split up are UPPER) */
        const int up = (int)(12.0f * 1.442695f * logf(s->f0 * (1.0f / 440.0f)) + 69.5f) >= s->split_note;
        if (up != s->upper) { s->upper = up; px_s2_set(s, PX_SYN_ARCADE_SPLIT, s->sq); }
    }
    if (s->f0 > 0.0f) {
        const float target = 12.0f * 1.442695f * logf(s->f0 * (1.0f / 440.0f)) + 69.0f + (float)s->patch->semis;
        if (s->pend_on || s->pitch_cur == 0.0f) s->pitch_cur = target;               /* a new note: there, no glide from the last */
        else s->pitch_cur += (target - s->pitch_cur) * 0.5f;                            /* a bend or vibrato: a short smoothing (about 1.5 ms) */
    }
    if (s->pend_on && s->f0 > 0.0f) {
        const int sounding = v->active && v->stage != 0;
        uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
        int32_t sv[8]; memcpy(sv, v->s, sizeof sv);
        v->note = (uint8_t)((int)(s->pitch_cur + 0.5f) & 127); v->vel = 100; v->gate = 1; v->active = 1; v->stage = 1; v->age++;
        if (!sounding) s->env = 0.0f;                                                  /* a voice still sounding keeps its level: the attack starts from it, no click */
        s->stage = 1;
        px_s2_pitch(s->pitch_cur, &v->pitch16, &v->fine, s->arc); v->pitch_cur = v->pitch16;
        if (!sounding) v->env = v->env_out = 0;
        s->eng->note_on(t, v);
        if (sounding) {                                                                 /* Felucca's retrigger rule: keep the phases and the filter */
            v->ph[0] = ph0; v->ph[1] = ph1; v->ph[2] = ph2;
            for (int k = 0; k < 8; k++) if (k != 2 && k != 3 && (s->eng->keep >> k & 1)) v->s[k] = sv[k];
        }
    }
    if (s->pend_off && v->active) { v->gate = 0; v->stage = 3; s->stage = 3; }
    s->pend_on = s->pend_re = s->pend_off = 0;
    for (int i = 0; i < CTL; i++) s->ring[i] = 0.0f;
    if (!v->active) { s->env = 0.0f; s->last_amp = 0.0f; return; }
    /* the envelope, once a block (ADSR: linear-ish attack by a one-pole, decay to the sustain, release to zero) */
    switch (s->stage) {
    case 1: s->env += (1.2f - s->env) * s->att_k; if (s->env >= 1.0f) { s->env = 1.0f; s->stage = 2; v->stage = 2; } break;
    case 2: s->env += (s->sus - s->env) * s->dec_k; break;
    case 3: s->env -= s->env * (s->damped && s->rel_k < s->dmp_k ? s->dmp_k : s->rel_k); if (s->env < 0.001f) { s->env = 0.0f; s->stage = 0; v->stage = 0; v->active = 0; } break;
    default: break;
    }
    /* Felucca's amplitude hook (LOFI's stepped envelope, CHIP = STEP): it reads and writes the voice's ADSR (Q24) and returns the amplitude */
    float shape = s->env;
    if (s->eng->amp) {
        v->env = (int32_t)(s->env * 16777216.0f);
        const int32_t a = s->eng->amp(t, v, (int32_t)(s->env * 32767.0f));
        s->env = (float)v->env * (1.0f / 16777216.0f);
        shape = (float)a * (1.0f / 32767.0f);
        if (!v->active) { s->env = 0.0f; s->stage = 0; s->last_amp = 0.0f; return; }
    }
    const float vg = 0.35f + 0.65f * s->vel;                                           /* pick strength */
    const float amp = shape * vg * (0.6f + 0.4f * s->expr_r);                          /* and the string's own decay */
    vmod_t m;
    memset(&m, 0, sizeof m);
    px_s2_pitch(s->pitch_cur, &v->pitch16, &v->fine, s->arc); v->pitch_cur = v->pitch16;
    const uint32_t base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
    m.pitch16 = v->pitch16; m.fine = v->fine; m.inc = base + (uint32_t)((int32_t)(base >> 12) * v->fine);
    m.amp0 = (int32_t)(s->last_amp * 32767.0f); m.amp1 = (int32_t)(amp * 32767.0f); s->last_amp = amp;
    m.envq15 = m.amp1;
    m.cutoff = (int32_t)(s->fenv_amp * s->env) << 8;                                   /* the envelope into the filter */
    s->lfo += 0.0021f; if (s->lfo >= 1.0f) s->lfo -= 1.0f;                              /* the pulse-width sweep: 0.5 Hz triangle */
    const float tri = s->lfo < 0.5f ? 4.0f * s->lfo - 1.0f : 3.0f - 4.0f * s->lfo;
    m.shape = (64 << 8) + (int32_t)(tri * (float)s->patch->pwm * 128.0f * (1.0f / 2.0f));
    memset(s->out, 0, sizeof s->out);
    if (s->patch->noise) px_s2_noise(t, v, s->out, CTL, &m); else s->eng->render(t, v, s->out, CTL, &m);
    const float g = s->level * PX_S2_OUT * (1.0f / 32768.0f);
    for (int i = 0; i < CTL; i++) s->ring[i] = (float)s->out[i] * g;
    s->blk++;
}
/* ARCADE's guitar share, ramped over the n samples: the caller mixes it with the signal that reaches the SYNTH block (after the blocks before it in the order) */
static inline void px_s2_dry(float *g0, float *g1) { px_s2 *s = &px_s2_st; *g0 = s->dry_c; s->dry_c += (s->dry - s->dry_c) * 0.25f; *g1 = s->dry_c; }
/* n samples of the clean guitar in, n samples of synth out (the previous control block's render: a fixed 32 samples later) */
PX_HOT static void px_synth_run(px_engine *e, const float *in, float *out, int n)
{
    px_s2 *s = &px_s2_st;
    for (int k = 0; k < n; k++) {
        px_mono_out o;
        px_mono_run(e, &s->mono, in[k], &o);
        if (o.onset) { s->pend_on = 1; s->pend_re = o.retrigger; s->vel = o.vel; s->pend_off = 0; s->damped = 0; }
        if (o.release) { s->pend_off = 1; if (o.damp) s->damped = 1; }
        s->f0 = o.f0; s->gate = (float)o.gate; s->expr_r = o.vel > 0.0f ? o.expr / o.vel : 1.0f;
        out[k] = s->ring[s->rpos];                                                  /* (the dry guitar is added where the block stands in the chain: px_s2_dry) */
        s->rpos++;
        if (++s->cnt == CTL) {
            px_s2_block(s);                                                              /* writes ring[] for the next 32 samples */
            s->cnt = 0; s->rpos = 0;
        }
    }
}
#endif
