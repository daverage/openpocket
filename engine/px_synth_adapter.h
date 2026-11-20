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
#ifndef PX_S2_NV
#define PX_S2_NV 9                                  /* the most voices one note can drive (ENSEMBLE's stacks; every other type uses voice 0 alone) */
#endif
#define NVOICE PX_S2_NV
#define NPART 1
#include "../third_party/felucca-engines/fe_tables.h"
#include "../third_party/felucca-engines/src/felucca_engine.h"
enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track { int16_t p[P_COUNT]; voice_t v[NVOICE]; } track_t;
static track_t trk[NPART];
#include "../third_party/felucca-engines/src/dsp.c"
#include "../third_party/felucca-engines/src/eng_analog.c"
#include "../third_party/felucca-engines/src/eng_lofi.c"
#include "../third_party/felucca-engines/src/eng_wheel.c"
#include "../third_party/felucca-host/fe_env_tables.h"                /* ENV_LIN: the VOICE engine's TALK morph reads it */
#define FELUCCA_CPU2 0
#include "../third_party/felucca-host/eng_formant.c"      /* the original FM-1 synth's VOICE engine (GPL-3.0-only, third_party/felucca-host/PINNED.md) */
#include "px_synth_patches.h"

#define PX_ARCADE_SPLIT_AS PX_SYN_ARCADE
/* The envelope as Felucca's host plays it (felucca voice.c env_tick; tables from tools/gen_tables.py): a time T = 1 ms x 10000^(v/127) for each of ATK, DEC, REL (v 0..127: 1 ms .. 10 s); the attack is
 * LINEAR and reaches full level in T; decay (toward SUS) and release are exponential, 99 % of the way after T (a time constant of T / 4.6). Builds 907-914 used my own curves (a one-pole whose time
 * constant was 1 + (v/127)^2 x 1500..3000 ms), 4-15 times slower than the presets were written for. PX_S2_ENV_ORIG 0 restores those (for the A/B and the identity tests). */
#ifndef PX_S2_ENV_ORIG
#define PX_S2_ENV_ORIG 1
#endif
/* Dynamics and glide as the original synths had them (fix pass 6). The level follows the pick strength through the original tracked voice's law, lvl = exp((expr - 1) x dyn_db x 0.115): a feather-light pick is
 * dyn_db below a hard one (expr is the note layer's expression, 0..1: the pick strength, falling with the string's own decay). Here the reference is a typical hard pick (PX_S2_DYN_REF) and the gain is capped, so the
 * loudest picks do not run away; PX_S2_DYN_DB is the default range (the patch's `dyn` overrides it). The pitch of a ringing note glides to each new value over PX_S2_GLIDE_MS (the original MONO: 12 ms); only a note that
 * starts from silence takes its pitch at once. */
#ifndef PX_S2_DYN_REF
#define PX_S2_DYN_REF 0.90f
#endif
#ifndef PX_S2_DYN_MAX
#define PX_S2_DYN_MAX 1.6f
#endif
#ifndef PX_S2_DYN_DB
#define PX_S2_DYN_DB 18.0f
#endif
#ifndef PX_S2_GLIDE_MS
#define PX_S2_GLIDE_MS 12.0f
#endif
#define PX_S2_DAMP_MS 40.0f                           /* release time constant after a hard mute: the patch's own would ring on for a second or more */
#ifndef PX_S2_DRIFT_C
#define PX_S2_DRIFT_C 8.0f                             /* ENSEMBLE: the rms of each voice's pitch wander, cents, at SPREAD noon (0 = off) */
#endif
#define PX_S2_DRIFT_K 0.0068f                          /* its corner: about 1.5 Hz (a one-pole per 32-sample block) */
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
    float vel, expr_r, expr_a, last_amp, dyn, glide_k;   /* expr_a: the note layer's expression (0..1); dyn: the dynamics, dB; glide_k: the pitch glide a block */
    int nv; float v0semis, v0lvl;                      /* ENSEMBLE: the voices in play; voice 0's interval (semitones) and level (the single-voice types: 1, 0.0 and 1.0) */
    struct { float semis, lvl, att_k, dec_k, rel_k, env, last_amp; int stage; } vc[PX_S2_NV];   /* voices 1.. (voice 0's state is the fields above) */
    uint8_t grp[PX_S2_NV]; int16_t vcut[PX_S2_NV]; int ngrp;   /* the voice's group (a singer, or the voice itself) and its filter cutoff (-1: the patch's) */
    float vph[PX_S2_NV], vib[PX_S2_NV], vibd, vdel_ms; int vage;   /* vibrato: each group's phase and the semitones it adds now, the depth (semitones), the delay, blocks since the note started */
    uint32_t rng[PX_S2_NV]; float drift[PX_S2_NV], spread;   /* ENSEMBLE: each voice's own slow random pitch wander (cents), and SPREAD's scale of it */
    int blk_done;                                      /* this block's shared step (the LFO, the engine's per-block call) has run */
    float dmp_k; int damped;                                        /* the gate fell on a hard damp: the release is fast (PX_S2_DAMP_MS) */
    int pend_on, pend_re, pend_off;                    /* the block's events: a note starts, a sounding voice retriggers, the gate fell */
    int cnt, rpos; float ring[CTL];                    /* samples in this block; where the output is read */
    unsigned blk; int32_t out[CTL]; float lfo;
    float f0, gate;
} px_s2;
static px_s2 px_s2_st PX_LO(px_s2_st);

static inline float px_s2_tms(int v) { return powf(10000.0f, (float)v * (1.0f / 127.0f)); }                       /* Felucca's time for a 0..127 value, ms */
static inline float px_s2_att_step(float ms) { const float st = (float)CTL * 1000.0f / (FS * (ms < 0.5f ? 0.5f : ms)); return st > 1.0f ? 1.0f : st; }   /* the linear attack's step a block */
static inline float px_s2_ms_k(float ms) { return 1.0f - expf(-(float)CTL * 1000.0f / (FS * (ms < 0.5f ? 0.5f : ms))); }   /* a one-pole per block */

/* the patch and the macros from the block's five settings (all of them are the preset's: nothing else shapes the sound).
 * ANALOG: PATCH, CUTOFF, RESONANCE, ENVELOPE, LEVEL. ARCADE: PATCH, CHARACTER (the DUTY: pulse width / wave table), SWEEP (the pitch sweep down),
 * DECAY (a stepped patch's staircase, else the envelope's decay and release times), MIX (guitar against synth). 50 = the patch as written. */
static inline int px_s2_clamp127(float v) { const int i = (int)(v + (v < 0.0f ? -0.5f : 0.5f)); return i < 0 ? 0 : i > 127 ? 127 : i; }
static void px_s2_apply(px_s2 *s, int type, const uint8_t *q)
{
    const int cho = type == PX_SYN_CHOIR;                                              /* CHOIR: the VOICE engine's vowels, sung by a stack of singers */
    const int ens2 = type == PX_SYN_ENSEMBLE || cho;                                   /* ENSEMBLE: the INSTRUMENT patches, played by a stack of voices */
    const int ens = type == PX_SYN_INSTRUMENT || ens2;
    const int arc = type == PX_SYN_ARCADE || type == PX_SYN_ARCADE_SPLIT;
    const px_patch *pa = cho ? &PX_VOWELS[px_vowel_slot(q[0])] : ens ? &PX_INSTRUMENT_PATCHES[px_inst_slot(q[0])] : &(arc ? PX_ARCADE_PATCHES : PX_ANALOG_PATCHES)[px_patch_slot(q[0])];
    const float m1 = f01(q[1]), m2 = f01(q[2]), m3 = f01(q[3]), m4 = f01(q[4]);
    const float mt = ens2 ? m3 : m1;                                                   /* TONE: INSTRUMENT's second setting, ENSEMBLE's fourth */
    s->type = type; s->arc = arc; s->patch = pa; s->eng = arc ? &ENG_LOFI : (ens && pa->eng == 3) ? &ENG_FORMANT : (ens && pa->eng) ? &ENG_WHEEL : &ENG_ANALOG;   /* (INSTRUMENT and ENSEMBLE: ANALOG, or WHEEL for the organ patches) */
    track_t *t0 = &trk[0];
    for (int k = 0; k < 8; k++) t0->p[P_E0 + k] = pa->e[k];
    t0->p[P_ATK] = pa->env[0]; t0->p[P_REL] = pa->env[3]; t0->p[P_FTYPE] = pa->ftype;
    float tm = 1.0f, tdec = 1.0f, tmr = -1.0f;                                                /* tmr: the release's own multiplier (INSTRUMENT), else tm */
    if (ens) {                                                                              /* INSTRUMENT: PATCH, TONE, ATTACK, RELEASE, LEVEL */
        if (pa->eng == 3) {                                                                 /* VOICE: TONE shifts all the formants (the singer's size), +-5 semitones */
            t0->p[P_E3] = (int16_t)((int)pa->e[3] + (int)((mt - 0.5f) * 10.0f + (mt < 0.5f ? -0.5f : 0.5f)));
        } else if (pa->eng) {                                                               /* WHEEL: TONE tilts the drawbars: up = upper bars +5 and body +1.5, sub bars -5 (one level is about 3 dB); 50 = as written */
            const float tn = (mt - 0.5f) * 2.0f, rh = tn < 0.0f ? -0.5f : 0.5f;
            t0->p[P_E1] = (int16_t)((int)pa->e[1] - (int)(tn * 5.0f + rh));
            t0->p[P_E2] = (int16_t)((int)pa->e[2] + (int)(tn * 1.5f + rh));
            t0->p[P_E3] = (int16_t)((int)pa->e[3] + (int)(tn * 5.0f + rh));
        } else
        t0->p[P_E4] = (int16_t)px_s2_clamp127((float)pa->e[4] + (mt - 0.5f) * 127.0f);     /* TONE: the filter's cutoff, +-63 steps around the patch's */
        if (!ens2) {
            tm = powf(2.0f, (m2 - 0.5f) * 4.0f);                                            /* ATTACK: x 1/4 .. x4 */
            tmr = powf(2.0f, (m3 - 0.5f) * 4.0f);                                           /* RELEASE: x 1/4 .. x4 */
        }
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
#if PX_S2_ENV_ORIG
    const float atk_ms = px_s2_tms(pa->env[0]), dec_ms = px_s2_tms(pa->env[1]) * (1.0f / 4.6f), rel_ms = px_s2_tms(pa->env[3]) * (1.0f / 4.6f);   /* (decay and release as time constants) */
    s->att_k = px_s2_att_step(atk_ms * tm); s->dec_k = px_s2_ms_k(dec_ms * (arc ? tdec : 1.0f)); s->rel_k = px_s2_ms_k(rel_ms * (tmr > 0.0f ? tmr : tm));
#else
    const float atk_ms = 1.0f + (pa->env[0] / 127.0f) * (pa->env[0] / 127.0f) * 1500.0f;
    const float dec_ms = 5.0f + (pa->env[1] / 127.0f) * (pa->env[1] / 127.0f) * 3000.0f;
    const float rel_ms = 10.0f + (pa->env[3] / 127.0f) * (pa->env[3] / 127.0f) * 3000.0f;
    s->att_k = px_s2_ms_k(atk_ms * tm); s->dec_k = px_s2_ms_k(dec_ms * (arc ? tdec : 1.0f)); s->rel_k = px_s2_ms_k(rel_ms * (tmr > 0.0f ? tmr : tm));
#endif
    s->dmp_k = px_s2_ms_k(PX_S2_DAMP_MS);
    s->dyn = pa->dyn ? (float)pa->dyn : arc ? 8.0f : (ens && pa->eng == 1) ? 6.0f : PX_S2_DYN_DB;   /* ARCADE's chips and the organs are nearly on / off; the rest follow the pick */
    s->glide_k = px_s2_ms_k(PX_S2_GLIDE_MS);
    s->nv = 1; s->v0semis = 0.0f; s->v0lvl = 1.0f; s->ngrp = 1;
    for (int i = 0; i < PX_S2_NV; i++) { s->vcut[i] = -1; s->grp[i] = (uint8_t)i; }
    s->vibd = pa->vib * 0.01f; s->vdel_ms = pa->vdel * 10.0f;
    if (ens2) {                                                                              /* ENSEMBLE: PATCH, VOICING, SPREAD, TONE, LEVEL */
        const px_stack *st = &PX_STACKS[px_stack_slot(q[1])];
        const float spread = m2 < 0.5f ? m2 * 2.0f : 1.0f + (m2 - 0.5f) * 4.0f;            /* SPREAD: the detune x0 .. x1 (50) .. x3 */
        float sq = 0.0f;
        for (int i = 0; i < st->nv; i++) sq += (st->v[i].lvl / 64.0f) * (st->v[i].lvl / 64.0f);
        const float norm = 1.0f / sqrtf(sq);                                                 /* the voices' powers add up to one voice's */
        s->nv = st->nv; s->spread = spread; s->ngrp = st->nv;
        s->level *= st->trim / 64.0f;
        for (int i = 0; i < st->nv; i++) {
            const float so = (float)st->v[i].semis + (float)st->v[i].cents * spread * 0.01f, lv = st->v[i].lvl / 64.0f * norm, am = st->v[i].atk / 64.0f;
            if (i == 0) { s->v0semis = so; s->v0lvl = lv; }
            else { s->vc[i].semis = so; s->vc[i].lvl = lv; }
            if (i > 0) { s->vc[i].att_k = PX_S2_ENV_ORIG ? px_s2_att_step(atk_ms * tm * am) : px_s2_ms_k(atk_ms * tm * am); s->vc[i].dec_k = s->dec_k; s->vc[i].rel_k = s->rel_k; }
        }
        if (st->v[0].atk != 64) s->att_k = PX_S2_ENV_ORIG ? px_s2_att_step(atk_ms * tm * (st->v[0].atk / 64.0f)) : px_s2_ms_k(atk_ms * tm * (st->v[0].atk / 64.0f));
    }
    for (int i = s->nv; i < PX_S2_NV; i++) { trk[0].v[i].active = 0; trk[0].v[i].stage = 0; s->vc[i].env = 0.0f; s->vc[i].stage = 0; s->vc[i].last_amp = 0.0f; }   /* voices a smaller stack no longer plays */
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
    memset(drw_t, 0, sizeof drw_t); memset(drw_v, 0, sizeof drw_v);
    formant_nz = 0x2545F491;                                                              /* the VOICE engine's breath-noise seed (eng_formant.c) */                     /* WHEEL's own state (eng_wheel.c: rotors, bar gains, phases) */
    px_mono_init(e, &s->mono, PX_MONO_GUITAR);
    for (int i = 0; i < PX_S2_NV; i++) { s->vcut[i] = -1; s->grp[i] = (uint8_t)i; }
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
/* one voice's note start: its pitch (the tracked note plus the voice's interval), the envelope, Felucca's note_on; a voice still sounding keeps its level, phases and filter */
static void px_s2_noteon(px_s2 *s, int vi, float *env, int *stage, float semis_off)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[vi];
    const int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t sv[8]; memcpy(sv, v->s, sizeof sv);
    v->note = (uint8_t)((int)(s->pitch_cur + semis_off + 0.5f) & 127); v->vel = 100; v->gate = 1; v->active = 1; v->stage = 1; v->age++;
    if (!sounding) *env = 0.0f;                                                       /* a voice still sounding keeps its level: the attack starts from it, no click */
    *stage = 1;
    px_s2_pitch(s->pitch_cur + semis_off, &v->pitch16, &v->fine, s->arc); v->pitch_cur = v->pitch16;
    if (!sounding) v->env = v->env_out = 0;
    s->eng->note_on(t, v);
    if (sounding) {                                                                    /* Felucca's retrigger rule: keep the phases and the filter */
        v->ph[0] = ph0; v->ph[1] = ph1; v->ph[2] = ph2;
        for (int k = 0; k < 8; k++) if (k != 2 && k != 3 && (s->eng->keep >> k & 1)) v->s[k] = sv[k];
    }
}
/* one voice's block: its envelope, its pitch and amplitude, the render into s->out (voice 0's state is the px_s2 fields, the others' is vc[]; the single-voice types run voice 0 alone, as before) */
static void px_s2_voice(px_s2 *s, int vi, float *env, int *stage, float *last_amp, float att_k, float dec_k, float rel_k, float semis_off, float lvl)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[vi];
    if (!v->active) { *env = 0.0f; *last_amp = 0.0f; return; }
    /* the envelope, once a block (ADSR: linear-ish attack by a one-pole, decay to the sustain, release to zero) */
    switch (*stage) {
#if PX_S2_ENV_ORIG
    case 1: *env += att_k; if (*env >= 1.0f) { *env = 1.0f; *stage = 2; v->stage = 2; } break;                 /* linear, as env_tick */
#else
    case 1: *env += (1.2f - *env) * att_k; if (*env >= 1.0f) { *env = 1.0f; *stage = 2; v->stage = 2; } break;
#endif
    case 2: *env += (s->sus - *env) * dec_k; break;
    case 3: *env -= *env * (s->damped && rel_k < s->dmp_k ? s->dmp_k : rel_k); if (*env < 0.001f) { *env = 0.0f; *stage = 0; v->stage = 0; v->active = 0; } break;
    default: break;
    }
    /* Felucca's amplitude hook (LOFI's stepped envelope, CHIP = STEP): it reads and writes the voice's ADSR (Q24) and returns the amplitude */
    float shape = *env;
    if (s->eng->amp) {
        v->env = (int32_t)(*env * 16777216.0f);
        const int32_t a = s->eng->amp(t, v, (int32_t)(*env * 32767.0f));
        *env = (float)v->env * (1.0f / 16777216.0f);
        shape = (float)a * (1.0f / 32767.0f);
        if (!v->active) { *env = 0.0f; *stage = 0; *last_amp = 0.0f; return; }
    }
    float dynf = expf((s->expr_a - PX_S2_DYN_REF) * s->dyn * 0.115129f);                /* the pick strength and the string's own decay (the original tracked voice's law) */
    if (dynf > PX_S2_DYN_MAX) dynf = PX_S2_DYN_MAX;
    const float amp = shape * dynf * lvl;                                              /* the voice's level in a stack */
    vmod_t m;
    memset(&m, 0, sizeof m);
    px_s2_pitch(s->pitch_cur + semis_off, &v->pitch16, &v->fine, s->arc); v->pitch_cur = v->pitch16;
    const uint32_t base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
    m.pitch16 = v->pitch16; m.fine = v->fine; m.inc = base + (uint32_t)((int32_t)(base >> 12) * v->fine);
    m.amp0 = (int32_t)(*last_amp * 32767.0f); m.amp1 = (int32_t)(amp * 32767.0f); *last_amp = amp;
    m.envq15 = m.amp1;
    m.cutoff = (int32_t)(s->fenv_amp * *env) << 8;                                     /* the envelope into the filter */
    if (!s->blk_done) {                                                                /* once a block, whichever voice renders first */
        s->lfo += 0.0021f; if (s->lfo >= 1.0f) s->lfo -= 1.0f;                          /* the pulse-width sweep: 0.5 Hz triangle */
        if (s->eng->block) s->eng->block(t);                                           /* Felucca's per-block step (WHEEL: bar gains, percussion, rotors) */
        s->blk_done = 1;
    }
    const float tri = s->lfo < 0.5f ? 4.0f * s->lfo - 1.0f : 3.0f - 4.0f * s->lfo;
    m.shape = (64 << 8) + (int32_t)(tri * (float)s->patch->pwm * 128.0f * (1.0f / 2.0f));
    if (s->vcut[vi] >= 0) t->p[P_E4] = s->vcut[vi];                                    /* CHOIR: each voice is a formant, its own filter */
    if (s->patch->noise) px_s2_noise(t, v, s->out, CTL, &m); else s->eng->render(t, v, s->out, CTL, &m);
    s->blk++;
}
/* one control block: the events, the voices, the envelopes, the render. Writes ring[CTL]. */
static __attribute__((noinline)) void px_s2_block(px_s2 *s)
{
    track_t *t = &trk[0];
    const int nv = s->nv;
    if (s->pend_on && s->f0 > 0.0f && s->type == PX_SYN_ARCADE_SPLIT) {        /* the note's range picks its patch (the notes from the split up are UPPER) */
        const int up = (int)(12.0f * 1.442695f * logf(s->f0 * (1.0f / 440.0f)) + 69.5f) >= s->split_note;
        if (up != s->upper) { s->upper = up; px_s2_set(s, PX_SYN_ARCADE_SPLIT, s->sq); }
    }
    if (s->f0 > 0.0f) {
        const float target = 12.0f * 1.442695f * logf(s->f0 * (1.0f / 440.0f)) + 69.0f + (float)s->patch->semis;
        if (s->pitch_cur == 0.0f || (s->pend_on && !(t->v[0].active && t->v[0].stage != 0))) s->pitch_cur = target;   /* a note from silence: there, no glide from the last */
        else s->pitch_cur += (target - s->pitch_cur) * s->glide_k;                      /* a bend, a vibrato, a slur, a re-pick: a glide (12 ms, as the original MONO) */
    }
    if (nv > 1 && PX_S2_DRIFT_C > 0.0f) {                                              /* ENSEMBLE: every voice drifts by itself (a deterministic xorshift, filtered): the beating of the copies is irregular, a chorus and not a phaser */
        const float amp = PX_S2_DRIFT_C * s->spread / 0.57735f / sqrtf(PX_S2_DRIFT_K / (2.0f - PX_S2_DRIFT_K));
        for (int g = 0; g < s->ngrp; g++) {                                          /* (a group: a singer's formant voices share one pitch, or a voice is its own group) */
            uint32_t r = s->rng[g] ? s->rng[g] : 0x9E3779B9u * (uint32_t)(g + 1);
            r ^= r << 13; r ^= r >> 17; r ^= r << 5; s->rng[g] = r;
            const float u = (float)(int32_t)r * (1.0f / 2147483648.0f);
            s->drift[g] += (u * amp - s->drift[g]) * PX_S2_DRIFT_K;
        }
    }
    if (s->vibd > 0.0f) {                                                              /* vibrato: ~5.3 Hz, each group its own rate and phase, fading in after the delay (bowed strings, singers) */
        const float tms = (float)s->vage * (float)CTL * 1000.0f / FS, fade = tms < s->vdel_ms ? 0.0f : (tms - s->vdel_ms) < 300.0f ? (tms - s->vdel_ms) * (1.0f / 300.0f) : 1.0f;
        for (int g = 0; g < s->ngrp; g++) {
            s->vph[g] += (5.3f + 0.27f * (float)g) * (float)CTL / FS; if (s->vph[g] >= 1.0f) s->vph[g] -= 1.0f;
            s->vib[g] = s->vibd * fade * sinf(6.2831853f * s->vph[g]) * (1.0f - 0.12f * (float)(g & 1));
        }
        s->vage++;
    }
    if (s->pend_on && s->f0 > 0.0f) {
        if (!(t->v[0].active && t->v[0].stage != 0)) { s->vage = 0; for (int g = 0; g < PX_S2_NV; g++) { s->vph[g] = 0.37f * (float)g; s->vib[g] = 0.0f; } }   /* a new note starts its vibrato over */
        for (int vi = 0; vi < nv; vi++) {
            uint8_t gsave[PX_S2_NV];
            if (vi == 0 && nv > 1) for (int k = 1; k < nv; k++) { gsave[k] = t->v[k].gate; t->v[k].gate = 0; }   /* (WHEEL strikes its percussion once per chord: on the first voice, whatever still rings) */
            px_s2_noteon(s, vi, vi ? &s->vc[vi].env : &s->env, vi ? &s->vc[vi].stage : &s->stage, (vi ? s->vc[vi].semis : s->v0semis) + s->drift[s->grp[vi]] * 0.01f + s->vib[s->grp[vi]]);
            if (vi == 0 && nv > 1) for (int k = 1; k < nv; k++) t->v[k].gate = gsave[k];
        }
    }
    if (s->pend_off) for (int vi = 0; vi < nv; vi++) if (t->v[vi].active) { t->v[vi].gate = 0; t->v[vi].stage = 3; if (vi) s->vc[vi].stage = 3; else s->stage = 3; }
    s->pend_on = s->pend_re = s->pend_off = 0;
    for (int i = 0; i < CTL; i++) s->ring[i] = 0.0f;
    memset(s->out, 0, sizeof s->out);
    s->blk_done = 0;
    px_s2_voice(s, 0, &s->env, &s->stage, &s->last_amp, s->att_k, s->dec_k, s->rel_k, s->v0semis + s->drift[s->grp[0]] * 0.01f + s->vib[s->grp[0]], s->v0lvl);
    for (int vi = 1; vi < nv; vi++) px_s2_voice(s, vi, &s->vc[vi].env, &s->vc[vi].stage, &s->vc[vi].last_amp, s->vc[vi].att_k, s->vc[vi].dec_k, s->vc[vi].rel_k, s->vc[vi].semis + s->drift[s->grp[vi]] * 0.01f + s->vib[s->grp[vi]], s->vc[vi].lvl);
    const float g = s->level * PX_S2_OUT * (1.0f / 32768.0f);
    for (int i = 0; i < CTL; i++) s->ring[i] = (float)s->out[i] * g;
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
        if (o.release) { s->pend_off = 1; if (o.damp) s->damped = 1; }
        if (o.onset) { s->pend_on = 1; s->pend_re = o.retrigger; s->vel = o.vel; s->pend_off = 0; s->damped = 0; }   /* (a note that starts on the sample of a release wins: fix pass 5) */
        s->f0 = o.f0; s->gate = (float)o.gate; s->expr_r = o.vel > 0.0f ? o.expr / o.vel : 1.0f; s->expr_a = o.expr;
        out[k] = s->ring[s->rpos];                                                  /* (the dry guitar is added where the block stands in the chain: px_s2_dry) */
        s->rpos++;
        if (++s->cnt == CTL) {
            px_s2_block(s);                                                              /* writes ring[] for the next 32 samples */
            s->cnt = 0; s->rpos = 0;
        }
    }
}
#endif
