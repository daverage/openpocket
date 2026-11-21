/* SPDX-License-Identifier: GPL-3.0-or-later
 * ENHANCE v3 (SFX): a clean harmonic/tonal enhancer built the way the classic exciters are built, not a model of one chip. Header-only, float, no buffers.
 * Why (docs/ENHANCE-RESEARCH.md): the NE570 gain-cell model of v1/v2 adds a few per cent of distortion where the guitar already has plenty of energy: the guitar's own second
 * harmonic is -19 dB re the fundamental and the model moved it by +1.9 dB (inaudible, and it saturates). Real enhancers (Aphex exciter, US 4,150,253; BBE; SPL) add energy where the
 * source is weak, with a side path, and dynamically. Two parallel side paths, mixed into an untouched dry signal (the dry path is never filtered or clipped):
 *  WARMTH    band-pass 80-400 Hz -> FULL-WAVE RECTIFIER (a pure even-harmonic generator, no saturation) -> 4-pole high-pass 100 Hz (the rectifier's DC and, on chords, its difference tones: two notes a third apart leave a tone at their difference, 51 Hz for 196+247 Hz, that sounds as rumble) -> low-pass 1.2 kHz.
 *            Its size is a mix gain, so the added second harmonic is a fixed share of the note whatever the playing level (no sensitivity setting needed).
 *  PRESENCE  Aphex-style: 2.5 kHz two-pole high-pass -> a ONE-SIDED soft clip whose threshold is relative to the running level of that band (1.45 x its mean magnitude), so only the
 *            louder peaks and pick transients are rounded -> 10 kHz low-pass. Level-relative, so it too needs no calibration.
 * Controls: BETTER 0..100 (overall amount), TONE 0..100 (0 warmth only, 50 both, 100 presence only), LOUDER 0..100 = 0..+12 dB clean boost.
 * No sqrt, no powf in the sample loop (the target has no hardware sqrt). The presence clip makes harmonics up to the 10 kHz low-pass; at 44.1 kHz a little of it can alias (a 2x
 * oversampled clip is the fix if it is audible). */
#ifndef PX_ENHANCE_H
#define PX_ENHANCE_H
#include <math.h>
#ifndef PXE_KW
#define PXE_KW 0.5f             /* WARMTH mix gain at BETTER 100: the added second harmonic is about 0.42 x this of the note (about -14 dB total added on the owner's clips) */
#endif
#ifndef PXE_KP
#define PXE_KP 0.5f             /* PRESENCE mix gain at BETTER 100 */
#endif
#ifndef PXE_THR
#define PXE_THR 1.45f           /* the presence clip's threshold, in mean magnitudes of the high band (1.45 = 1.3 x RMS for a sine) */
#endif
#ifndef PXE_CLIP
#define PXE_CLIP 0.6f           /* the part of a peak above the threshold that is removed (one-sided) */
#endif
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } pxe_bq;
typedef struct {
    pxe_bq w_hp, w_lp, w_post, w_post2, w_out, p_hp, p_lp;     /* warmth: 80 Hz high-pass, 400 Hz low-pass, two 100 Hz high-passes after the rectifier, 1.2 kHz low-pass; presence: 2.5 kHz high-pass, 10 kHz low-pass */
    float env, kenv, kglide;
    float wmix, pmix, level, wmix_t, pmix_t, level_t;
} px_enhance;
static inline float pxe_clamp(float x,float a,float b) { return x<a?a:x>b?b:x; }
static inline void pxe_design(pxe_bq *q,int high,float hz,float sr) {           /* two-pole Butterworth (Q 0.7071), RBJ */
    const float w0=6.2831853f*hz/sr, cw=cosf(w0), sw=sinf(w0), al=sw/(2.0f*0.70710678f), a0=1.0f+al;
    if(high) { q->b0=(1.0f+cw)*0.5f/a0; q->b1=-(1.0f+cw)/a0; q->b2=q->b0; }
    else     { q->b0=(1.0f-cw)*0.5f/a0; q->b1=(1.0f-cw)/a0;  q->b2=q->b0; }
    q->a1=-2.0f*cw/a0; q->a2=(1.0f-al)/a0; q->z1=q->z2=0.0f;
}
static inline float pxe_run(pxe_bq *q,float x) {
    const float y=q->b0*x+q->z1; q->z1=q->b1*x-q->a1*y+q->z2; q->z2=q->b2*x-q->a2*y; return y;
}
static inline void pxe_init(px_enhance *s,float sr) {
    *s=(px_enhance){0};
    pxe_design(&s->w_hp,1,80.f,sr); pxe_design(&s->w_lp,0,400.f,sr); pxe_design(&s->w_post,1,100.f,sr); pxe_design(&s->w_post2,1,100.f,sr); pxe_design(&s->w_out,0,1200.f,sr);
    pxe_design(&s->p_hp,1,2500.f,sr); pxe_design(&s->p_lp,0,10000.f,sr);
    s->kenv=1.0f-expf(-1.0f/(sr*0.020f)); s->kglide=1.0f-expf(-1.0f/(sr*0.010f));
    s->level=s->level_t=1.f;
}
/* BETTER, TONE, LOUDER 0..100. Outside the sample loop. TONE 0 warmth only, 50 both (each at the full BETTER share), 100 presence only. */
static inline void pxe_set(px_enhance *s,float better,float louder,float tone) {
    const float b=pxe_clamp(better,0.f,100.f)*0.01f, t=pxe_clamp(tone,0.f,100.f)*0.01f;
    s->wmix_t=PXE_KW*b*(t<0.5f?1.0f:2.0f*(1.0f-t));
    s->pmix_t=PXE_KP*b*(t>0.5f?1.0f:2.0f*t);
    s->level_t=powf(10.f,0.006f*pxe_clamp(louder,0.f,100.f));       /* 0..12 dB */
}
static inline float pxe_tick(px_enhance *s,float x) {
    s->wmix+=s->kglide*(s->wmix_t-s->wmix); s->pmix+=s->kglide*(s->pmix_t-s->pmix); s->level+=s->kglide*(s->level_t-s->level);
    float y=x;
    if(s->wmix>1e-5f || s->wmix_t>1e-5f) {                                    /* WARMTH: even harmonics of the low-mid band */
        float b=pxe_run(&s->w_lp,pxe_run(&s->w_hp,x));
        y+=s->wmix*pxe_run(&s->w_out,pxe_run(&s->w_post2,pxe_run(&s->w_post,fabsf(b))));
    }
    if(s->pmix>1e-5f || s->pmix_t>1e-5f) {                                    /* PRESENCE: one-sided soft clip of the high band, only the louder peaks */
        const float h=pxe_run(&s->p_hp,x), a=fabsf(h);
        s->env+=s->kenv*(a-s->env);
        const float over=h-PXE_THR*s->env;
        y+=s->pmix*pxe_run(&s->p_lp,over>0.0f?h-PXE_CLIP*over:h);
    }
    return s->level*y;
}
#endif
