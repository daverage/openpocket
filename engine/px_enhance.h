/* SPDX-License-Identifier: GPL-3.0-or-later
 * NE570-inspired clean harmonic exciter, not a traced BB-01 circuit (ENHANCE, SFX block).
 * Header-only. Normalised floating-point audio, intended |x| <= 1. Prototype from the NE570 research package (2026-11), reviewed and integrated here.
 * The research calibrates the gain cell's virtual 0 dBm to 0.5 peak digital. Real guitar at the pedal peaks far lower (tools/calib_levels.py: hard playing
 * about -17 dBFS = 0.14 at the converter), where that calibration adds only -53 dB (BETTER 35) / -44 dB (BETTER 100) of harmonics re the dry signal:
 * inaudible. PXE_REF is the one change to the research maths: the sidechain works at PXE_REF times the signal (hard playing at the pedal reaches the
 * virtual 0.5) and its contribution is scaled back by 1/PXE_REF, so the dry path and the unity gain are untouched. PXE_REF 1.0 is the research prototype.
 */
#ifndef PX_ENHANCE_H
#define PX_ENHANCE_H
#include <math.h>
#ifndef PXE_REF
#define PXE_REF 3.5f            /* virtual input gain of the gain cell: 0.14 (hard playing at the pedal) -> 0.5 (the research's 0 dBm) */
#endif
typedef struct {
    float pre1, pre2, rect, slow, dc, post, mix, level;
    float kpre, krect, kslow, kdc, kpost, kglide;
    float a2, ripple, mix_target, level_target;
} px_enhance;
static inline float pxe_clamp(float x,float a,float b) { return x<a?a:x>b?b:x; }
static inline float pxe_k(float sr,float seconds) { return 1.0f-expf(-1.0f/(sr*seconds)); }
static inline void pxe_init(px_enhance *s,float sr) {
    *s=(px_enhance){0};
    s->kpre=pxe_k(sr,1.0f/(6.2831853f*3500.f));
    s->krect=pxe_k(sr,0.0007f); /* virtual Crect = 70 nF */
    s->kslow=pxe_k(sr,0.025f);
    s->kdc=pxe_k(sr,1.0f/(6.2831853f*20.f));
    s->kpost=pxe_k(sr,1.0f/(6.2831853f*7000.f));
    s->kglide=pxe_k(sr,0.010f);
    s->level=s->level_target=1.f;
    s->a2=0.068f; /* 5 mV equivalent; H2=1.7% at A=0.5 before filtering */
    s->ripple=0.04f; /* intentionally weak VCA control modulation */
}
/* Two knobs, 0..100; initial values 35, 0. Set outside sample loop. */
static inline void pxe_set(px_enhance *s,float better,float louder) {
    s->mix_target=pxe_clamp(better,0.f,100.f)*0.01f;
    s->level_target=powf(10.f,0.006f*pxe_clamp(louder,0.f,100.f)); /* 0..12 dB */
}
static inline float pxe_tick(px_enhance *s,float x) {
    s->mix+=s->kglide*(s->mix_target-s->mix);
    s->level+=s->kglide*(s->level_target-s->level);
    /* Limit bandwidth only in the parallel sidechain. Dry path stays intact. */
    s->pre1+=s->kpre*(x-s->pre1);
    s->pre2+=s->kpre*(s->pre1-s->pre2);
    float u=pxe_clamp(s->pre2*PXE_REF,-1.f,1.f);   /* the gain cell's operating level; the guard keeps the excitation bounded */
    float a=fabsf(u);
    s->rect+=s->krect*(a-s->rect);
    s->slow+=s->kslow*(s->rect-s->slow);
    /* Gain cell offset: parabolic error. Rectifier ripple: multiplicative
       error, with slow control removed to avoid a full expander response. */
    float wet=s->a2*u*u+s->ripple*u*(s->rect-s->slow)/0.318309886f;
    s->dc+=s->kdc*(wet-s->dc);
    wet-=s->dc;
    s->post+=s->kpost*(wet-s->post);
    return s->level*(x+s->mix*s->post*(1.0f/PXE_REF));
}
#endif
