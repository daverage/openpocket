/* OpenPocket Synth2: the host adapter between the mono tracker (px_mono.h) and Felucca's fixed-point synthesis engines
 * (third_party/felucca-engines, MIT; docs/SYNTH2-PLAN.md). Included once, at the end of px_dsp.c: one translation unit with the engine,
 * so the pedal, the host tests, `make render` and the WebAssembly simulator run the same code.
 *
 * What it is: one monophonic voice. Each input sample goes through the tracker; every 32 samples (Felucca's block, CTL) the adapter turns
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
#include "px_synth_patches.h"

#define PX_S2_OUT 0.30f                                /* float output per Felucca full scale at LEVEL noon, trim 1.0 (set on the host against the loops' level) */

typedef struct {
    px_mono mono;                                      /* the tracker (px_mono.h) */
    const engine_t *eng;
    const px_patch *patch;
    float cut_off, res_off, time_mul, level;           /* macros: cutoff and resonance offsets, envelope time multiplier, output gain */
    float env, sus, att_k, dec_k, rel_k, fenv_amp;     /* envelope 0..1 and its per-block coefficients; the amount into the filter */
    int stage;                                         /* 0 off, 1 attack, 2 decay / sustain, 3 release */
    float semis, pitch_cur;                            /* the note in MIDI semitones (continuous), what the voice plays now */
    float vel, expr_r, last_amp;
    int pend_on, pend_re, pend_off;                    /* the block's events: a note starts, a sounding voice retriggers, the gate fell */
    int cnt, rpos; float ring[CTL];                    /* samples in this block; where the output is read */
    unsigned blk; int32_t out[CTL]; float lfo;
    float f0, gate;
} px_s2;
static px_s2 px_s2_st PX_LO(px_s2_st);

static inline float px_s2_ms_k(float ms) { return 1.0f - expf(-(float)CTL * 1000.0f / (FS * (ms < 0.5f ? 0.5f : ms))); }   /* a one-pole per block */

/* the macros and the patch from the block's five settings (all of them are the preset's: nothing else shapes the sound) */
static void px_s2_set(px_s2 *s, const uint8_t *q)
{
    const px_patch *pa = &PX_ANALOG_PATCHES[px_patch_slot(q[0])];
    const float cut = f01(q[1]), res = f01(q[2]), env = f01(q[3]), lvl = f01(q[4]);
    s->patch = pa;
    s->cut_off = (cut - 0.5f) * 127.0f;                /* CUTOFF: +-63 Felucca steps around the patch's */
    s->res_off = (res - 0.5f) * 127.0f;                /* RESONANCE */
    s->time_mul = powf(2.0f, (env - 0.5f) * 4.0f);     /* ENVELOPE: attack and release x 1/4 .. x4 around the patch's (50 = as written) */
    const float t = s->time_mul;
    const float atk_ms = 1.0f + (pa->env[0] / 127.0f) * (pa->env[0] / 127.0f) * 1500.0f;
    const float dec_ms = 5.0f + (pa->env[1] / 127.0f) * (pa->env[1] / 127.0f) * 3000.0f;
    const float rel_ms = 10.0f + (pa->env[3] / 127.0f) * (pa->env[3] / 127.0f) * 3000.0f;
    s->att_k = px_s2_ms_k(atk_ms * t); s->dec_k = px_s2_ms_k(dec_ms); s->rel_k = px_s2_ms_k(rel_ms * t);
    s->sus = pa->env[2] / 127.0f;
    s->fenv_amp = (float)pa->fenv;
    s->level = (lvl < 0.5f ? 4.0f * lvl * lvl : db2lin((lvl - 0.5f) * 12.0f)) * (pa->trim / 64.0f);
    track_t *t0 = &trk[0];
    for (int k = 0; k < 8; k++) t0->p[P_E0 + k] = pa->e[k];
    t0->p[P_ATK] = pa->env[0]; t0->p[P_REL] = pa->env[3]; t0->p[P_FTYPE] = 0;
    int c = pa->e[4] + (int)(s->cut_off + (s->cut_off < 0 ? -0.5f : 0.5f)), r = pa->e[5] + (int)(s->res_off + (s->res_off < 0 ? -0.5f : 0.5f));
    t0->p[P_E4] = (int16_t)(c < 0 ? 0 : c > 127 ? 127 : c);
    t0->p[P_E5] = (int16_t)(r < 0 ? 0 : r > 127 ? 127 : r);
}
static void px_s2_reset(px_engine *e, const uint8_t *q)
{
    px_s2 *s = &px_s2_st;
    memset(s, 0, sizeof *s);
    memset(trk, 0, sizeof trk);
    s->eng = &ENG_ANALOG;
    px_mono_init(e, &s->mono, PX_MONO_GUITAR);
    px_s2_set(s, q);
}
/* semitones (MIDI, 69 = 440 Hz) -> Felucca's pitch16 (1/16 semitone) and the residual in its fine units (1/4096 of the increment) */
static inline void px_s2_pitch(float semis, int32_t *pitch16, int32_t *fine)
{
    float x = semis * 16.0f; if (x < 0.0f) x = 0.0f; if (x > 2046.9f) x = 2046.9f;
    const int p = (int)x;
    *pitch16 = p;
    *fine = (int32_t)((x - (float)p) * 14.8f + 0.5f);   /* 2^(1/192) - 1 = 0.36 % = 14.8 / 4096 */
}
/* one control block: the events, the voice, the envelope, the render. Writes ring[CTL]. */
static __attribute__((noinline)) void px_s2_block(px_s2 *s)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[0];
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
        px_s2_pitch(s->pitch_cur, &v->pitch16, &v->fine); v->pitch_cur = v->pitch16;
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
    case 3: s->env -= s->env * s->rel_k; if (s->env < 0.001f) { s->env = 0.0f; s->stage = 0; v->stage = 0; v->active = 0; } break;
    default: break;
    }
    const float vg = 0.35f + 0.65f * s->vel;                                           /* pick strength */
    const float amp = s->env * vg * (0.6f + 0.4f * s->expr_r);                         /* and the string's own decay */
    vmod_t m;
    memset(&m, 0, sizeof m);
    px_s2_pitch(s->pitch_cur, &v->pitch16, &v->fine); v->pitch_cur = v->pitch16;
    const uint32_t base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
    m.pitch16 = v->pitch16; m.fine = v->fine; m.inc = base + (uint32_t)((int32_t)(base >> 12) * v->fine);
    m.amp0 = (int32_t)(s->last_amp * 32767.0f); m.amp1 = (int32_t)(amp * 32767.0f); s->last_amp = amp;
    m.envq15 = m.amp1;
    m.cutoff = (int32_t)(s->fenv_amp * s->env) << 8;                                   /* the envelope into the filter */
    s->lfo += 0.0021f; if (s->lfo >= 1.0f) s->lfo -= 1.0f;                              /* the pulse-width sweep: 0.5 Hz triangle */
    const float tri = s->lfo < 0.5f ? 4.0f * s->lfo - 1.0f : 3.0f - 4.0f * s->lfo;
    m.shape = (64 << 8) + (int32_t)(tri * (float)s->patch->pwm * 128.0f * (1.0f / 2.0f));
    memset(s->out, 0, sizeof s->out);
    s->eng->render(t, v, s->out, CTL, &m);
    const float g = s->level * PX_S2_OUT * (1.0f / 32768.0f);
    for (int i = 0; i < CTL; i++) s->ring[i] = (float)s->out[i] * g;
    s->blk++;
}
/* n samples of the clean guitar in, n samples of synth out (the previous control block's render: a fixed 32 samples later) */
PX_HOT static void px_synth_run(px_engine *e, const float *in, float *out, int n)
{
    px_s2 *s = &px_s2_st;
    for (int k = 0; k < n; k++) {
        px_mono_out o;
        px_mono_run(e, &s->mono, in[k], &o);
        if (o.onset) { s->pend_on = 1; s->pend_re = o.retrigger; s->vel = o.vel; s->pend_off = 0; }
        if (o.release) s->pend_off = 1;
        s->f0 = o.f0; s->gate = (float)o.gate; s->expr_r = o.vel > 0.0f ? o.expr / o.vel : 1.0f;
        out[k] = s->ring[s->rpos];
        s->rpos++;
        if (++s->cnt == CTL) {
            px_s2_block(s);                                                              /* writes ring[] for the next 32 samples */
            s->cnt = 0; s->rpos = 0;
        }
    }
}
#endif
