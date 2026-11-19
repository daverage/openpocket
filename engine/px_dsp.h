/* OpenPocket — guitar sound engine for the Donner Pocket X / M-Vave (JieLi AC791N)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * px_dsp.h — v3: a preset = 11 blocks; each block has a TYPE, an ON/OFF state and 5 settings 0..100.
 * The UI and the engine share exactly this structure.
 *
 *   default order: IN -> GATE -> COMP -> OCTAVE (PITCH) -> SYNTH -> SFX -> EQ -> MOD -> DELAY -> REVERB -> OUT
 *   v29: each preset carries its own block order (order[]). v70: 9 blocks, no amp, cabinet, drive or NAM.
 *
 * Models are voiced from published circuit analyses (ElectroSmash, Electric Druid, Rob Robinette,
 * Duncan Amps, Celestion/Jensen datasheets, Boss/Roland/Ampeg manuals...) — see docs/SOUNDS-AND-CONTROLS.md.
 * Names evoke the models they are inspired by, without using any brand.
 * Single-precision float (AC791N FPU); minimal libm: sinf/cosf/expf/logf/powf/sqrtf (firmware/px_math.c).
 */
#ifndef PX_DSP_H
#define PX_DSP_H

#include <stdint.h>

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
/* v70 (OpenPocket Synth): 9 blocks in signal order. The pedal groups them: INPUT (GATE, COMP), OCTAVE (PITCH),
 * SYNTH (SYN), TONE (SFX, EQ), MOD, DELAY, REVERB. */
enum { PX_B_GATE, PX_B_COMP, PX_B_PITCH, PX_B_SYN, PX_B_SFX, PX_B_EQ, PX_B_MOD, PX_B_DELAY, PX_B_REVERB, PX_NBLK };
#define PX_NPAR 5

enum { PX_GATE_NORMAL, PX_GATE_SMART, PX_GATE_COUNT };
enum { PX_CMP_SQUASH, PX_CMP_STUDIO, PX_CMP_OPTO, PX_CMP_SWELL, PX_CMP_COUNT };
enum { PX_PIT_OCTAVER, PX_PIT_WHAMMY, PX_PIT_HARMONY, PX_PIT_DETUNE, PX_PIT_POLYOCT_HQ,
       PX_PIT_LOWEST_HQ,
       PX_PIT_COUNT };
enum { PX_SFX_AUTOWAH, PX_SFX_LFOWAH, PX_SFX_RING, PX_SFX_CRUSH, PX_SFX_SLICER, PX_SFX_LOFI, PX_SFX_SYNTH,
       PX_SFX_SYNTH_HQ, PX_SFX_COUNT };
enum { PX_SYN_ORGAN, PX_SYN_CHURCH, PX_SYN_STRINGS, PX_SYN_CHOIR, PX_SYN_MONO, PX_SYN_PWM,
       PX_SYN_12STR, PX_SYN_ACOUSTIC, PX_SYN_SITAR, PX_SYN_BARI, PX_SYN_BRASS, PX_SYN_STRINGS_HQ, PX_SYN_CHOIR_HQ,
       PX_SYN_ORGAN_HQ, PX_SYN_CHURCH_HQ, PX_SYN_BASS_HQ, PX_SYN_OCT_FUZZ,
       /* v48: tracked acoustic voices (engine/px_acoustic.h) */
       PX_SYN_TRUMPET, PX_SYN_CELLO, PX_SYN_CELLO_SECTION, PX_SYN_STRING_MACHINE,
       PX_SYN_PULSE_POLY,
       PX_SYN_SAW_POLY, PX_SYN_ARCADE_POLY, PX_SYN_FM_POLY,
       PX_SYN_STRING_ENS, PX_SYN_BRASS_ENS,            /* v61: one note, a sampled lead and a light support layer */
       PX_SYN_STRING_DUO,                              /* v65: two tracked notes, a cello on the lower, a violin on the upper */
       PX_SYN_COUNT };
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
} px_preset_t;

void px_order_default(uint8_t *order);
int  px_order_valid(const uint8_t *order);   /* 1 = a permutation of 0..PX_NBLK-1 */

/* ------------------------------------------------------------ UI catalog --- */
const char *px_block_label(int blk);
const char *px_block_name(int blk);
int         px_ntypes(int blk);
int         px_type_canonical(int blk, int type);
void px_block_canonical(int blk, uint8_t *type, uint8_t *p);   /* v64: px_type_canonical, and the retired type's controls */
const char *px_type_name(int blk, int type);
int         px_nparams(int blk, int type);
const char *px_param_name(int blk, int type, int k);
/* text for a knob value when a bare 0..100 says little ("+5TH", "440" ms...); returns 0 = show the number */
#define PX_PATCH_LEVEL 2                /* the preset LEVEL: p[PX_B_GATE][2] ("PRESET LVL" on the GT page), 50 = 0 dB (v36) */
int         px_dly_value_for_ms(int type, float ms);   /* the TIME value nearest to a delay of ms (MIDI clock sync) */
int         px_param_text(int blk, int type, int k, int v, char *buf8);
int         px_preset_count(void);
void        px_preset_factory(px_preset_t *p, int idx);

/* ---------------------------------------------------------------- state --- */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } px_bq;
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
 * made the octave's gargle, warble and drop-outs (docs/NOTE-TRACKING.md). */
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
    unsigned pos, pos2;
    unsigned idle;                                     /* v49: samples with every voice at zero (the bank rests) */
} px_erb_octave;
typedef struct {
    float previous, peak, elapsed, period[2], step, target, phase;
    int armed, gate, crossed;
} px_synth_tracking;

/* v48: the state of the tracked acoustic voices (engine/px_acoustic.h); it shares the SYN ensemble buffer (syn_cb) */
#define PXA_HIST 160
typedef struct { int smp, active, fade; float pos, gain, rise; } px_acoustic_player;   /* v49: rise, its fade-in */
typedef struct {
    px_synth_tracking hi;
    px_bq lo1, lo2, u0, u1, u2, h1, h2, body[6];
    float k_ea, k_er, k_ha, k_hr, k_hs, k_xa, k_xr, k_att, k_fade10, k_fade20, k_fade5, k_bow_a, k_bow_r, k_br_a, k_br_r;
    float env, envs, hf, hfs, f0s, onset_peak, held_f0, cand_f0, ref_b[2], vel, peak_ref, expr;
    int key, state, cand_n, peak_n, stable_b[2], refractory, hw, hist_n, sel, sel_n, gate, onset, nbody, held;
    float hist[PXA_HIST];
    float oct_mul, rel_k, dry, bow, vib, bright, tone, dyn_db, gain, sr, inv_sr;
    px_acoustic_player pl[3]; float root_f, lp, root_t;   /* v49: root_t, since its last change of recording */
    int nw, bw, ow; unsigned tick; uint32_t rnd;
    float str_lp, bowv, vib_ph, vib_amt, held_t, noise_lp, kick_p, kick_a; int kick_n;   /* v49: kick, the string set going */
    float lip_y1, lip_y2, lip_a1, lip_g, dc_x, dc_y, breath, out_lp, fb, last_f, amp, lvl_t;
    float mp;                                         /* v49: the brass's blowing pressure for the note's pitch */
    /* v61: the ensembles' support layer, rendered beside the lead from the same note (engine/px_acoustic.h) */
    float ens_ph[2], ens_amp, ens_lp[2], ens_vib, ens_rel, ens_level, ens_k_att, ens_det, ens_tone0, ens_tone1,
          ens_vib_amt, ens_vib_inc, ens_f, ens_cand; int ens_n;   /* v66: ens_f the settled pitch */
} px_acoustic;

#define PX_PS_N 2048                   /* pitch shifter buffer (46 ms) */
#define PX_SYN_CB 1024                 /* SYN: ensemble / doubler / body reflections (23 ms) */
#define PX_DLY_N 45056                 /* v40: 2.04 s of delay at half rate (22.05 kHz, 16-bit: 88 KB), as the stock */
#define PX_RV_M 16384                  /* reverb memory: one 16-bit ring (32 KB) at half rate, laid out per type */
#define PX_GL_MAX 64                   /* v38: the most settings that glide (px_dsp.c: GL_F) */

typedef struct {
    float sr;
    px_preset_t cur;

    float dc_x1, dc_y1, odc_x1, odc_y1, dn;          /* dn: alternating 1e-15 anti-denormal offset */

    /* gate */
    float gate_env, gate_g, gate_thr, gate_rel;
    struct {
        float dc, split, band[2], floor[2], slow[2], gain;
        float dc_k, split_k, env_k, slow_k, learn_k, fast_k, attack, range, sensitivity;
        unsigned hold, remaining;
        int open, active;
    } smart_gate;
    /* comp */
    float cmp_env, cmp_slow, cmp_att, cmp_rel, cmp_k, cmp_make, cmp_mix, cmp_sw, cmp_thr, cmp_rise, cmp_slowk;
    /* pitch: delay-line shifters (2 voices) + analog octaver */
    float ps_buf[PX_PS_N]; int ps_w, ps_nv;
    float ps_ph[2], ps_inc[2], ps_W[2], ps_lvl[2], ps_off[2][2], ps_dry, ps_r, ps_rt, ps_glide, ps_invW;
    px_erb_octave erb_pitch, erb_syn;                 /* (v49: the bank's coefficients are shared statics, px_dsp.c) */
    float erb_pitch_range;
    int erb_pitch_active;
    px_bq oct_lp, oct_lp2, oct_sublp, oct_sublp2, oct_bp, oct_uphp;
    float oct_env, oct_ff, oct_ff2, oct_prev, oct_dry, oct_s1, oct_s2, oct_up;
    /* special fx */
    float sfx_lo, sfx_bp, sfx_env, sfx_ph, sfx_ph2, sfx_inc, sfx_ff, sfx_prev, sfx_g;
    float sfx_a, sfx_b, sfx_c, sfx_d, sfx_mix, sfx_qd;
    int   sfx_pat, crush_n, crush_k; float crush_v, crush_q;
    px_bq sfx_f1, sfx_f2;
    px_synth_tracking sfx_track, syn_track_hq;
    int sfx_track_active;
    uint8_t ord[PX_NBLK];
    /* synth / instrument block (v29) */
    float syn_buf[PX_PS_N]; int syn_w;                 /* shifter source (organ, strings...); sitar: string combs */
    px_hb syn_fuzz_os[4];
    px_drive_state syn_fuzz;
    float syn_fuzz_tone;
    union { float syn_cb[PX_SYN_CB]; px_acoustic syn_ac; }; int syn_cw;   /* v48: syn_ac (px_acoustic.h) */
    float syn_ph[3], syn_off[3][2], syn_inc[3], syn_W[3], syn_lvl[3]; int syn_nv;
    float syn_dry, syn_out, syn_d8;                    /* dry guitar, effect level, organ 8' (the guitar's own pitch) */
    float syn_env, syn_slow, syn_ea, syn_er, syn_sr2;  /* envelope (fast, slow), coefficients */
    float syn_kg, syn_ka, syn_kr, syn_thr; int syn_key;   /* key gate: gain, attack, release, threshold, held */
    float syn_perc, syn_pk, syn_plvl, syn_click, syn_clk; uint32_t syn_rnd; int syn_snap;
    float syn_lfo[3], syn_linc[2], syn_cdep[2], syn_cbase;
    px_bq syn_f[6]; int syn_nf;
    float syn_lp, syn_lpk, syn_hp, syn_hpk;
    /* tracker + oscillator + ladder filter (mono / pwm / bass synths) */
    float syn_t1, syn_t2, syn_prev, syn_cnt, syn_per, syn_p1, syn_p2, syn_f0, syn_f0t, syn_glk, syn_oph, syn_mul;
    float syn_wave, syn_lad[4], syn_cut, syn_res, syn_ea2, syn_fenv, syn_fdk, syn_amp, syn_ff, syn_ff2, syn_sq;
    float syn_pwm, syn_pw0, syn_pwd;
    /* sitar */
    int   syn_cl[6], syn_cp[6], syn_c0[6], syn_type; float syn_cs[6], syn_cfb, syn_cdamp, syn_buzz, syn_sym;
    /* eq */
    px_bq eq[5]; int eq_n;
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
    /* v39: the wet lanes (px_wet_mode below): lane 0 the delay, 1 the reverb, two buffers each (px_dsp.c: px_wl), used
     * in turn. wl_b: the buffer the chain uses this block (it reads its wet out and writes its sends in); wl_n: how many
     * samples a buffer holds, wl_w: 1 once they are wet; wl_rd: wet samples the chain may read this block, wl_p: where
     * it is in the buffer; wl_dlag: the lag, in samples (the delay's taps read that much less far back) */
    uint8_t wl_on, wl_b, wl_w[2][2];
    uint16_t wl_n[2][2], wl_rd[2], wl_p[2];
    float wl_dlag;
    uint32_t wl_late, wl_pf[2];                        /* wet blocks cut short (out of time); wet time per lane (ticks) */
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
