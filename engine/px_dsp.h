/* OpenPocket — guitar sound engine for the Donner Pocket X / M-Vave (JieLi AC791N)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * px_dsp.h — v3: a preset = 11 blocks; each block has a TYPE, an ON/OFF state and 5 settings 0..100.
 * The UI and the engine share exactly this structure.
 *
 *   default order: IN -> GATE -> BST -> COMP -> OCTAVE (PITCH) -> SYNTH -> SFX -> EQ -> MOD -> DELAY -> REVERB -> OUT
 *   v29: each preset carries its own block order (order[]). v70: no amp, cabinet, drive or NAM. v75: 10 blocks (BST, the pre / boost block, after the gate by default).
 *
 * Models are voiced from published circuit analyses (ElectroSmash, Electric Druid, Rob Robinette,
 * Duncan Amps, Celestion/Jensen datasheets, Boss/Roland/Ampeg manuals...) — see docs/SOUNDS-AND-CONTROLS.md.
 * Names evoke the models they are inspired by, without using any brand.
 * Single-precision float (AC791N FPU); minimal libm: sinf/cosf/expf/logf/powf/sqrtf (firmware/px_math.c).
 */
#ifndef PX_DSP_H
#define PX_DSP_H

#include <stdint.h>
#include "px_enhance.h"
#include "px_pv.h"                                      /* v75: POLY SHIFTER (the phase-vocoder pitch shifter) */
#include "px_pedals.h"                                /* the analogue pedal circuit models (RANGEMASTER, ...) */                                /* SFX ENHANCE's state (the engine holds one) */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PX_SR
#define PX_SR 44100.0f
#endif
#ifndef PX_OVERSAMPLE
#define PX_OVERSAMPLE 4
#endif
#define PX_MAX_BLOCK 64

/* --------------------------------------------------------------- blocks --- */
/* v70 (OpenPocket Synth): the blocks, listed by id (the preset's order[] gives the signal order; v75 appended BST, so older presets keep their
 * indices). The pedal groups them: INPUT (GATE, BST, COMP), OCTAVE (PITCH), SYNTH (SYN), TONE (SFX, EQ), MOD, DELAY, REVERB. */
enum { PX_B_GATE, PX_B_COMP, PX_B_PITCH, PX_B_SYN, PX_B_SFX, PX_B_EQ, PX_B_MOD, PX_B_DELAY, PX_B_REVERB,
       PX_B_BST, PX_NBLK };   /* v75: BST (PRE / BOOST) is appended last, so older presets keep their indices; it is processed where the preset's order puts it (by default after the gate) */
enum { PX_BST_RANGEMASTER, PX_BST_EPBOOST, PX_BST_MAXIMIZER, PX_BST_VOXWAH, PX_BST_ENHANCE, PX_BST_FUZZFACE, PX_BST_ACOUSTIC, PX_BST_SIGNATURE, PX_BST_COUNT };   /* stored PRE / BOOST type IDs */
#define PX_NPAR 5

enum { PX_GATE_NORMAL, PX_GATE_SUPPRESSOR, PX_GATE_COUNT };
enum { PX_CMP_SQUASH, PX_CMP_STUDIO, PX_CMP_OPTO, PX_CMP_SWELL, PX_CMP_VALVE, PX_CMP_COUNT };
enum { PX_PIT_OCTAVER, PX_PIT_WHAMMY, PX_PIT_HARMONY, PX_PIT_DETUNE, PX_PIT_POLYOCT_HQ,
       PX_PIT_LOWEST_HQ,
       PX_PIT_POLYSHIFT,                                /* v75: POLY SHIFTER, a phase-vocoder shifter for chords (engine/px_pv.h) */
       PX_PIT_COUNT };
enum { PX_SFX_AUTOWAH, PX_SFX_LFOWAH, PX_SFX_RING, PX_SFX_CRUSH, PX_SFX_SLICER, PX_SFX_LOFI, PX_SFX_SYNTH,
       PX_SFX_SYNTH_HQ, PX_SFX_APPLAUSE, PX_SFX_ENHANCE, PX_SFX_RANGEMASTER, PX_SFX_EPBOOST, PX_SFX_MAXIMIZER, PX_SFX_ACOUSTIC, PX_SFX_PULSE, PX_SFX_CHIP, PX_SFX_CROSS, PX_SFX_FORMANT, PX_SFX_CROSS_SAW, PX_SFX_CROSS_TRI, PX_SFX_CROSS_OSC, PX_SFX_COUNT };
/* SYNTH block type ids. Synth2 (docs/research/synth2/SYNTH2-PLAN.md): the ids below are the catalogue of every firmware up to v73 and are never
 * renumbered or reused. Only POLY OCT FUZZ keeps running; every other legacy id is a TOMBSTONE: the block is bypassed, the
 * stored type and its five settings are kept as they are (px_block_canonical changes nothing) until the user picks a new
 * type, and the UI skips it (px_type_name is ""). New Synth2 types are appended after PX_SYN_STRING_DUO. */
enum { PX_SYN_X_ORGAN, PX_SYN_X_CHURCH, PX_SYN_X_STRINGS, PX_SYN_X_CHOIR, PX_SYN_X_MONO, PX_SYN_X_PWM,
       PX_SYN_X_12STR, PX_SYN_X_ACOUSTIC, PX_SYN_X_SITAR, PX_SYN_X_BARI, PX_SYN_X_BRASS, PX_SYN_X_STRINGS_HQ,
       PX_SYN_X_CHOIR_HQ, PX_SYN_X_ORGAN_HQ, PX_SYN_X_CHURCH_HQ, PX_SYN_X_BASS_HQ,
       PX_SYN_OCT_FUZZ,
       PX_SYN_X_TRUMPET, PX_SYN_X_CELLO, PX_SYN_X_CELLO_SECTION, PX_SYN_X_STRING_MACHINE,
       PX_SYN_X_PULSE_POLY, PX_SYN_X_SAW_POLY, PX_SYN_X_ARCADE_POLY, PX_SYN_X_FM_POLY,
       PX_SYN_X_STRING_ENS, PX_SYN_X_BRASS_ENS, PX_SYN_X_STRING_DUO,
       PX_SYN_ANALOG,                                  /* Synth2: Felucca ANALOG, one note followed from the guitar (engine/px_synth_adapter.h) */
       PX_SYN_ARCADE,                                  /* Synth2: Felucca LOFI (chip sounds), the same way */
       PX_SYN_ARCADE_SPLIT,                            /* Synth2: ARCADE with one patch for the lower notes and one for the upper */
       PX_SYN_INSTRUMENT,                                /* Synth2: strings, pad, brass (Felucca ANALOG patches), one note from the guitar: docs/research/synth2/SYNTH2-INSTRUMENT.md */
       PX_SYN_ENSEMBLE,                                /* Synth2: the INSTRUMENT patches played by a stack of voices (intervals, detune) from the one tracked note: docs/research/synth2/SYNTH2-ENSEMBLE-VOICES.md */
       PX_SYN_CHOIR,                                   /* Synth2: the original FM-1 synth's VOICE (formant) engine, sung vowels, a stack of singers: docs/research/synth2/SYNTH2-VOICE-CHOIR.md */
       PX_SYN_COUNT };
/* does this SYNTH type run? (1: a live type; 0: a tombstone, or an id beyond the catalogue: both are bypassed) */
static inline int px_syn_runs(int t) { return t == PX_SYN_OCT_FUZZ || t == PX_SYN_ANALOG || t == PX_SYN_ARCADE || t == PX_SYN_ARCADE_SPLIT || t == PX_SYN_INSTRUMENT || t == PX_SYN_ENSEMBLE || t == PX_SYN_CHOIR; }
static inline int px_syn_mono(int t) { return t == PX_SYN_ANALOG || t == PX_SYN_ARCADE || t == PX_SYN_ARCADE_SPLIT || t == PX_SYN_INSTRUMENT || t == PX_SYN_ENSEMBLE || t == PX_SYN_CHOIR; }   /* the types played by the mono tracker and Felucca */
int         px_type_retired(int blk, int type);        /* 1: a tombstone (the UI skips it, the engine bypasses it) */
int         px_nlive(int blk);                         /* the types that are not tombstones (MIDI's model slices count only these) */
int         px_live_type(int blk, int i);              /* the i-th live type's id (0..px_nlive-1) */
int         px_live_index(int blk, int type);          /* a live type's place among the live types; 0 for a tombstone */
enum { PX_EQ_5BAND, PX_EQ_GEQ, PX_EQ_PARAM, PX_EQ_COUNT };
enum { PX_MOD_CHORUS, PX_MOD_DIMENSION, PX_MOD_FLANGER, PX_MOD_PHASE90, PX_MOD_STONE, PX_MOD_UNIVIBE,
       PX_MOD_OPTOTREM, PX_MOD_BIASTREM, PX_MOD_HARMTREM, PX_MOD_ROTARY, PX_MOD_VIBRATO, PX_MOD_COUNT };
enum { PX_DLY_DIGITAL, PX_DLY_ANALOG, PX_DLY_MEMORY, PX_DLY_TAPE, PX_DLY_SPACE, PX_DLY_REVERSE, PX_DLY_LOFI,
       PX_DLY_DUAL, PX_DLY_COUNT };
enum { PX_REV_ROOM, PX_REV_HALL, PX_REV_CATHEDRAL, PX_REV_PLATE, PX_REV_SPRING, PX_REV_SHIMMER, PX_REV_GATED,
       PX_REV_AMBIENT, PX_REV_COUNT };

/* ---------------------------------------------------------------- preset --- */
typedef struct {
    char    name[14];
    uint8_t on[PX_NBLK];
    uint8_t type[PX_NBLK];
    uint8_t p[PX_NBLK][PX_NPAR];   /* 0..100 */
    uint8_t order[PX_NBLK];        /* processing (and display) order: a permutation of the block indices */
    uint8_t valve_on, valve_drive, valve_level, patch_volume;
} px_preset_t;
#define PX_PRESET_BASE_SIZE (14u + (3u + PX_NPAR) * PX_NBLK)
#define PX_VALVE_DEFAULT 71u       /* about +12 dB */
#define PX_VALVE_LEVEL_UNITY 24u   /* half-dB steps, -12 to +12 dB */
#define PX_VALVE_LEVEL_MAX 48u
#define PX_VALVE_ACTION 10u        /* footswitch action, outside the reorderable blocks */
int px_valve_drive_db(int control);

void px_preset_base(px_preset_t *p);        /* v75: the blank sound every factory preset starts from (the gate on, the rest off, LEVEL 0 dB), for tests and tools that need a plain starting point */
void px_preset_canonical(px_preset_t *p);   /* v75: the three pedals that were SFX types move to the BST block (an SFX type that is a tombstone now), then px_block_canonical for every block */
void px_order_default(uint8_t *order);
int  px_order_valid(const uint8_t *order);   /* 1 = a permutation of 0..PX_NBLK-1 */

/* ------------------------------------------------------------ UI catalog --- */
const char *px_block_label(int blk);
const char *px_block_name(int blk);
int         px_ntypes(int blk);
int         px_type_canonical(int blk, int type);
void px_type_defaults(int blk, int type, uint8_t *p);   /* v76: the settings a type starts with when it is picked: every one at 50 (a mix and a level at the middle), except the types listed in px_dsp.c; not the gate block */
void px_block_canonical(int blk, uint8_t *type, uint8_t *p);   /* v64: px_type_canonical, and the retired type's controls */
const char *px_type_name(int blk, int type);
int         px_nparams(int blk, int type);
const char *px_param_name(int blk, int type, int k);
/* text for a knob value when a bare 0..100 says little ("+5TH", "440" ms...); returns 0 = show the number */
#define PX_PATCH_ACTION 11u             /* editor setting, independent of blocks; 0..100, 50 = unity */
int         px_dly_value_for_ms(int type, float ms);   /* the TIME value nearest to a delay of ms (MIDI clock sync) */
int         px_param_text(int blk, int type, int k, int v, char *buf8);
int         px_preset_count(void);
void        px_preset_factory(px_preset_t *p, int idx);

/* ---------------------------------------------------------------- state --- */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } px_bq;
/* v76: ACOUSTIC PREAMP (BST): low cut, de-quack (a static dip and shelf, and a dip that follows the pick attack), body, a feedback notch, level */
typedef struct { px_bq hp, dq1, dq2, dyn, b1, b2, nt; float ef, es, tr, kef, kes, kta, ktr, amt, lvl, lvl_t, kl; int nt_on; } px_ap;
typedef struct {
    px_bq low, body, definition;
    float color_lp[2], color_dc, color_k, dc_k;
    float fast, slow, fast_k, slow_k;
    float richness, punch, level, richness_target, punch_target, level_target, glide_k;
} px_signature;
/* v76: ACOUSTIC DRIVE (SFX): a drive shaped for a piezo (high-pass, low-pass and a dip before a soft asymmetric clipper, 2x oversampled), blended with the dry guitar */
typedef struct { px_bq hp, lp, dip, tone; float g, bias, off, blend, wet_n, lvl, lvl_t, kl, dc, kdc, dl[20]; unsigned di; pxp_os os; } px_ad;
typedef struct { float x[6], y[6]; } px_hb;           /* v38: a half-band filter's all-pass sections (x2 up or down) */

typedef struct {
    px_bq hp, shelf, lp, mid, t1, t2, t3;
    float gain, level, blend, bias, off, gate, dcs, dck, shelf_lo, mix;
    int   clip, tm, np, pre, dcb;
} px_drive_state;

/* v49: the HQ octave's analysis: 29 complex band-pass filters from 60 Hz to 2.05 kHz, each 37.5 Hz + 4 % of its
 * frequency from the next and 0.8 of that wide (32 Hz at the bottom, 95 Hz at the top), at a quarter of the rate
 * (11.025 kHz). Narrower than a guitar note's harmonic spacing up to where its partials matter, so a partial gets a
 * band of its own; v42 .. v48's 43 ERB-wide bands (up to 360 Hz) mixed several in one, and their doubled phases then
 * made the octave's gargle, warble and drop-outs (docs/archive/NOTE-TRACKING.md). */
#define PX_ERB_BANDS 29
typedef struct { float r, i; } px_erb_complex;
typedef struct { float b0; px_erb_complex b1, b2, a1, a2; } px_erb_band;
typedef struct {
    px_erb_complex z1[PX_ERB_BANDS], z2[PX_ERB_BANDS];
    float input[32], up[32];                           /* 44.1 kHz in; 22.05 kHz out (its odd slots stay zero) */
    float in2[32], up2[32];                            /* v49: 22.05 kHz in; 11.025 kHz out (the voices, mixed) */
    uint8_t half_phase[PX_ERB_BANDS];
    uint8_t quarter[PX_ERB_BANDS];                     /* v64: each band's wraps modulo 4 (two octaves down) */
    float down2;                                       /* v64: the -24 voice's level (POLY OCTAVE HQ's OCT -2) */
    float low_env[PX_ERB_BANDS];                       /* v73: each band's level, for the lowest-note selection */
    float low_sel;                                     /* v73: 0 off; > 0 the lowest-note selection's threshold */
    unsigned pos, pos2;
    unsigned idle;                                     /* v49: samples with every voice at zero (the bank rests) */
} px_erb_octave;
typedef struct {
    float previous, peak, elapsed, period[2], step, target, phase;
    int armed, gate, crossed;
} px_synth_tracking;

#define PX_PS_N 2048                   /* pitch shifter buffer (46 ms) */
#define PX_DLY_N 45056                 /* v40: 2.04 s of delay at half rate (22.05 kHz, 16-bit: 88 KB), as the stock */
#define PX_RV_M 16384                  /* reverb memory: one 16-bit ring (32 KB) at half rate, laid out per type */
#define PX_GL_MAX 64                   /* v38: the most settings that glide (px_dsp.c: GL_F) */

/* v75: the global settings (outside the presets; docs/SOUNDS-AND-CONTROLS.md, "Global settings"): what the player sets once for the pedal and the room.
 *   hpf     input high-pass: 0 off, else 20 .. 140 Hz (a steady low rumble out of everything after it; 25 Hz and below for bass)
 *   eq[5]   output EQ, the gain of each band in half-dB steps from -12 to +12 dB, 24 = flat (80 Hz shelf, 250 Hz, 1 kHz, 3.5 kHz, 9 kHz shelf)
 *   lim     output limiter: 0 off, else its ceiling is -lim / 2 dB (so 1 .. 24: -0.5 .. -12 dB)
 *   tails   1: a delay or reverb that is switched off, or left by a preset change, plays out instead of stopping (px_engine_set / px_engine_knobs)
 * All zero (eq 24) is the engine as it was: nothing runs. */
struct px_engine_s;
typedef struct { uint8_t hpf, eq[5], lim, tails; } px_global_t;
void px_global_default(px_global_t *g);
void px_engine_globals(struct px_engine_s *e, const px_global_t *g);

typedef struct px_engine_s {
    float sr;
    px_preset_t cur;

    float dc_x1, dc_y1, odc_x1, odc_y1, dn;          /* dn: alternating 1e-15 anti-denormal offset */

    /* gate */
    float gate_env, gate_g, gate_thr, gate_rel;
    struct {
        float peak, mean, sustain, floor, gain, hf_split, hf_env, hf_floor, hf_gain, output_lp;
        float peak_rel, sustain_rel, learn, fall, attack, release, sensitivity, split_k, hush;
        unsigned hold, remaining;
        int open, active, disabled;
    } suppressor;
    px_bq hum[3], hum_old[3];
    float hum_fade;
    int hum_mode, hum_pending, hum_current;
    /* comp */
    float cmp_env, cmp_slow, cmp_att, cmp_rel, cmp_k, cmp_make, cmp_mix, cmp_sw, cmp_thr, cmp_rise, cmp_slowk;
    /* pitch: delay-line shifters (2 voices) + analog octaver */
    float ps_buf[PX_PS_N]; int ps_w, ps_nv;
    float ps_ph[2], ps_inc[2], ps_W[2], ps_lvl[2], ps_off[2][2], ps_dry, ps_r, ps_rt, ps_glide, ps_invW;
    px_erb_octave erb_pitch, erb_syn;
    px_pv pv;                                          /* v75: POLY SHIFTER's state (px_pv.h) */                 /* (v49: the bank's coefficients are shared statics, px_dsp.c) */
    float erb_pitch_range;
    int erb_pitch_active;
    px_bq oct_lp, oct_lp2, oct_sublp, oct_sublp2, oct_bp, oct_uphp;
    float oct_env, oct_ff, oct_ff2, oct_prev, oct_dry, oct_s1, oct_s2, oct_up;
    /* special fx */
    float sfx_lo, sfx_bp, sfx_env, sfx_ph, sfx_ph2, sfx_inc, sfx_ff, sfx_prev, sfx_g;
    float sfx_a, sfx_b, sfx_c, sfx_d, sfx_mix, sfx_qd, sfx_sync_phase, sfx_sync_inc, sfx_sync_freq, sfx_sync_age;
    int   sfx_pat, crush_n, crush_k; float crush_v, crush_q;
    px_bq sfx_f1, sfx_f2;
    px_synth_tracking sfx_track;
    int sfx_track_active;
    uint8_t ord[PX_NBLK];
    /* SYNTH block: POLY OCT FUZZ (the only type that runs; Synth2 types are added here) */
    px_bbe bbe;                                        /* SFX MAXIMIZER: three bands, two delay lines (about 1 KB) */
    px_ep ep;                                          /* SFX EP BOOST: the JFET stage (about 100 bytes) */
    px_vw vw;                                          /* BST VOX FIXED WAH: one biquad (about 70 bytes) */
    px_ap ap;                                          /* BST ACOUSTIC PREAMP (v76): seven biquads, about 250 bytes */
    px_signature signature;                            /* BST SIGNATURE: tone, JFET color and pick enhancement */
    px_ad ad;                                          /* SFX ACOUSTIC DRIVE (v76): four biquads and the oversampling filter, about 450 bytes */
    px_ff ff;                                          /* BST FUZZ FACE: the two-transistor circuit, solved per sample (engine/px_pedals.h) */
    px_rm rm;                                          /* SFX RANGEMASTER: the circuit (about 700 bytes), set up by pxp_rm_init like ENHANCE */
    px_enhance enh;                                    /* SFX ENHANCE v4 (about 40 bytes); set up by pxe_init in px_engine_init and when the SFX type or on/off changes */
    px_hb syn_fuzz_os[4];
    px_drive_state syn_fuzz;
    float syn_fuzz_tone;
    float syn_lvl[3], syn_out;                         /* octave up / down levels, effect level */
    float syn_lp, syn_lpk; int syn_type;               /* tone low-pass; the type last set up */
    /* eq */
    px_bq eq[5]; int eq_n;
    float hum_s1[6], hum_s2[6], hum_c[6]; int hum_n, hum_hz, hum_vote;   /* v75 HUM FILTER: the mains detector (six resonators: the 1st..3rd harmonics of 50 and of 60 Hz, half-second windows), the mains it chose, its pending vote */
    /* modulation */
    float mod_buf[2048]; int mod_w;
    float mod_ph, mod_inc, mod_depth, mod_mix, mod_fb, mod_base, mod_sweep, mod_shape;
    px_bq mod_lp;
    float ph_z[4], ph_fb, ph_lo, ph_hi, uv_k[4], uv_lamp, uv_up, uv_dn, trem_g, trem_att, trem_rel;
    float rot_h, rot_d, rot_hs, rot_ds, rot_ht, rot_dt, rot_hk, rot_dk, rot_bal;
    /* delay */
    /* v40: the line runs at half rate (22.05 kHz) like the stock firmware's delays: one tick every two samples (the two
     * inputs averaged in), the output interpolated between the last two ticks; times and depths below are in ticks */
    int16_t dly_buf[PX_DLY_N]; int dly_w, dly_type, dly_heads, rev_start, rev_cnt, dly_tick;
    float dly_xa, dly_y0, dly_y1, dly_dp;              /* the input kept for the tick, the last two outputs, the last tap */
    float dly_len, dly_tgt, dly_fb, dly_mix, dly_ph, dly_ph2, dly_inc, dly_inc2, dly_wow, dly_flut, dly_mod;
    float dly_sat, dly_satn, dly_q, dly_hn, dly_hf, rev_xf, dly_env, dly_rq;
    px_bq dly_lp, dly_hp;
    /* reverb (v35): pre-delay + early reflections, 4 input diffusers, then either an 8-line feedback delay network
     * with modulated lines and per-line high-frequency decay, or Dattorro's plate tank (8 units, 2 modulated);
     * all lines in one 16-bit ring that turns once per sample at 22.05 kHz (+ spring dispersion, shimmer, gate) */
    int16_t rv_m[PX_RV_M];
    int   rv_lay, rv_alg;                              /* layout in use (type + 1; 0: none), 0 FDN / 1 plate */
    int   rv_b[13], rv_n[13], rv_ptr, rv_tick;         /* units: 0 pre-delay, 1..4 diffusers, 5..12 lines / tank */
    float rv_sr, rv_xa, rv_y0, rv_y1;                  /* half rate: its rate, the input kept, the last two outputs */
    float rv_d[13], rv_dg[4];                          /* delay of each unit (samples), diffuser gains */
    int   rv_pd, rv_ner, rv_ert[4]; float rv_erg[4];   /* pre-delay, early reflections (taps, gains) */
    float rv_g[8], rv_p[8], rv_s[8];                   /* FDN: loop gain (low), damping pole, damping state */
    float rv_ph1, rv_ph2, rv_inc1, rv_inc2, rv_mdep;   /* modulation: two slow LFOs, depth (samples) */
    float rv_dec, rv_dd2, rv_damp, rv_bw, rv_fl, rv_fr; /* plate: decay, decay diffusion 2, damping, input band, feedback */
    int   rv_tap[14];                                  /* plate: output taps (samples into tank units) */
    float rv_in, rv_out, rv_mix, rv_env, rv_rq, rv_fade;
    float cmp_la[128]; int cmp_lw;                     /* v36: swell look-ahead (128 samples) */
    struct {
        px_hb up, down;
        float cathode_v, output_dc, cathode_step, dc_step, drive, level, mix;
    } triode;
    float btr[PX_NBLK], patch_g;                        /* v36: per-block level trims, preset level */          /* rv_rq: rounding (0.5), or 0 once the tail is very quiet */
    int   rv_gate, rv_hold_n, sp_on; float rv_hold, rv_genv, rv_gg, rv_gate_rel;
    float sp_z[6], sp_a;
    float sh_buf[PX_PS_N]; int sh_w; float sh_ph, sh_out, sh_lp, sh_k, rv_shim, sh_hp, sh_hk;
    px_bq rv_tone, rv_hp;
    /* v38: changes without clicks. A knob's new values glide there over PX_GLIDE_TICKS blocks (gl_t: where to, gl_left:
     * blocks to go); a block switched on or off fades in or out (bf: its gain now, bfa: at the start of this block, bfd:
     * +1 / -1 while it fades). run: the preset the processing plays, cur with the blocks still fading out kept on. */
    float gl_t[PX_GL_MAX]; int gl_left;
    float bf[PX_NBLK], bfa[PX_NBLK]; int8_t bfd[PX_NBLK];
    px_preset_t run;
    uint32_t pf[PX_NBLK];                              /* v38: time spent per block (px_clock ticks) */
    float pfs[PX_NBLK];                                /* v75: each block's time per call, smoothed (ticks): what px_engine_heaviest ranks */
    uint16_t shed;                                     /* v75: blocks switched off by the load safeguard (px_engine_shed): bit b = block b */
    /* v39: the wet lanes (px_wet_mode below): lane 0 the delay, 1 the reverb, two buffers each (px_dsp.c: px_wl), used
     * in turn. wl_b: the buffer the chain uses this block (it reads its wet out and writes its sends in); wl_n: how many
     * samples a buffer holds, wl_w: 1 once they are wet; wl_rd: wet samples the chain may read this block, wl_p: where
     * it is in the buffer; wl_dlag: the lag, in samples (the delay's taps read that much less far back) */
    uint8_t wl_on, wl_b, wl_w[2][2];
    uint16_t wl_n[2][2], wl_rd[2], wl_p[2];
    float wl_dlag;
    uint32_t wl_late, wl_pf[2];                        /* wet blocks cut short (out of time); wet time per lane (ticks) */
    /* v75: the global settings (px_global_t) and what they run: the input high-pass, the output EQ and limiter, the tails of a delay or reverb */
    px_global_t glob;
    px_bq g_hp, g_eq[5];
    int g_hp_on, g_eq_n;
    float g_lim_c, g_lim_g;                            /* the ceiling (linear) and the limiter's gain now */
    int16_t spill[2];                                  /* the delay (0) and reverb (1) tails still playing out: blocks of 64 frames left; 0: not spilling */
    uint8_t sp_type[2];                                /* the type each had when it was switched off: its derived settings stay as they were while it plays out */
} px_engine;

void px_engine_init(px_engine *e, float sr);
void px_engine_set(px_engine *e, const px_preset_t *p);
/* v38: changes without clicks.
 * px_engine_change: what going from the engine's preset to p takes: PX_CH_NONE; PX_CH_KNOBS (settings and blocks on or
 *   off only: px_engine_knobs, between two blocks, no stop); PX_CH_FULL (another type or order: px_engine_set,
 *   the caller fading the output out and back in round it).
 * px_engine_knobs: the new settings glide in over ~23 ms, blocks switched on or off fade. Light (only the blocks whose
 *   settings changed are recomputed), so the audio side can call it at the start of a block.
 * px_engine_prepare: kept for the firmware's call; nothing is made ahead any more. */
enum { PX_CH_NONE, PX_CH_KNOBS, PX_CH_FULL };
int  px_engine_change(const px_engine *e, const px_preset_t *p);
void px_engine_knobs(px_engine *e, const px_preset_t *p);
/* v75: load shedding (the firmware's safeguard, firmware/px_audio.h). px_engine_heaviest: the block that costs the most of those playing (the gate
 * and, with the wet lanes on, the delay and reverb excepted: they are not on the chain's deadline), or -1. px_engine_shed: switches it off with the
 * usual fade, the preset (what the screen and the editor show) unchanged. It comes back when the block is switched or its type changed (px_engine_knobs),
 * when another preset is set, or at px_engine_unshed. e->shed is the set of shed blocks. */
int  px_engine_heaviest(const px_engine *e);
void px_engine_shed(px_engine *e, int b);
void px_engine_unshed(px_engine *e);
void px_engine_prepare(const px_engine *e, const px_preset_t *p);
void px_engine_process(px_engine *e, const float *in, float *out, int n);
enum { PX_PART_ALL };                  /* px_engine_part: the whole chain (the NAM stages are gone) */
void px_engine_part(px_engine *e, const float *in, float *out, int n, int part);
extern uint32_t (*px_clock)(void);      /* v38: set by the firmware: px_engine.pf counts each block's time */
/* v39: wet lanes, as in the stock firmware (docs/STOCK-FIRMWARE.md 7.3). With them on, the DELAY and REVERB blocks no
 * longer process in the chain: at their place the chain sends its signal to the lane and mixes in the lane's wet output
 * of two blocks before (PX_WL_N frames a block); px_wet_run makes that wet output, on whichever core has time, outside
 * the chain's own deadline. The delay's taps read wl_dlag samples less far back, so its echoes keep their time to the
 * sample; the reverb starts lag samples later (5.8 ms at 2 x 128 frames).
 *   px_wet_mode(e, on, lag): on or off (the lanes emptied); lag: the frames between a send and its wet output.
 *   px_wet_swap(e): at each block boundary, nothing running: the buffers change roles.
 *   px_wet_run(e, deadline, stop): makes the wet output of the other buffer's sends, 16 frames at a time. It stops
 *     early (the rest of that block's wet is silence, wl_late counts it) once px_clock() passes deadline (0: none) or
 *     *stop is set (0: none). Returns 1 when it got through. Off the chain's deadline: it may run on the other core
 *     while the chain runs. */
#define PX_WL_N 128
void px_wet_mode(px_engine *e, int on, int lag);
void px_wet_swap(px_engine *e);
int  px_wet_run(px_engine *e, uint32_t deadline, volatile const uint8_t *stop);

#ifdef __cplusplus
}
#endif
#endif
