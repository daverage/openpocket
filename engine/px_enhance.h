/* SPDX-License-Identifier: GPL-3.0-or-later
 * NE570-inspired clean harmonic exciter, not a traced BB-01 circuit (ENHANCE, SFX block).
 * Header-only. Normalised floating-point audio, intended |x| <= 1. Prototype from the NE570 research package (2026-11), reviewed and integrated here.
 *
 * The research calibrates the gain cell's virtual 0 dBm to 0.5 digital peak. Real guitar at the pedal peaks far lower (tools/calib_levels.py: hard playing about
 * -17 dBFS = 0.14 at the converter), where that calibration adds only -57 dB / -48 dB of harmonics re the dry signal. The sidechain therefore works at a virtual
 * input gain (SENSITIVITY) and its contribution is scaled back by 1/gain: the dry path and the unity gain are untouched.
 * All conditioning below applies to the enhancement path only; the clean guitar is never filtered or clipped.
 *
 * Input sensitivity (the virtual gain of the gain cell; harmonic generation is level dependent, so quiet pickups and gentle playing need more): 0 LOW 2x (hot pickups,
 * boosted signals), 1 NORMAL 3.5x (the reference: hard playing at the pedal reaches the virtual 0.5), 2 HIGH 5x (lower-output pickups), 3 EXTRA 7x (very quiet signals).
 * There is deliberately NO automatic level compensation: the harmonic response following the picking intensity is part of the effect.
 *
 * Voicings (what conditions the sidechain before the gain cell, and how it overloads):
 *   A  the research prototype exactly: two cascaded 3.5 kHz one-pole low-passes, a HARD clamp of the cell input at +-1. Kept as the reference.
 *   B  A with a SMOOTH overload: a cubic soft limiter (identity at small signals, zero slope where it saturates), so loud picks, humbuckers, active pickups
 *      or a compressor before ENHANCE do not clip the harmonic generator's input; the generated harmonics stay predictable.
 *   C  B plus input shaping: a one-pole high-pass (PXE_HPF_HZ, 60 Hz: the low E is 82 Hz) against sub-bass and low-frequency intermodulation, a gentle presence
 *      emphasis (PXE_PRES_DB at about 1.2-3 kHz) and a wider anti-alias low-pass (PXE_LPF_HZ, 5.5 kHz instead of 3.5) to keep pick articulation.
 * Diagnostics: pxe_diag() reports the input peak and RMS (dBFS) and how often the cell input exceeded the calibrated 0 dBm (0.5): a way to check which sensitivity
 * a given guitar needs.
 */
#ifndef PX_ENHANCE_H
#define PX_ENHANCE_H
#include <math.h>
#ifndef PXE_SENS
#define PXE_SENS 1              /* default sensitivity index: 0 LOW, 1 NORMAL, 2 HIGH, 3 EXTRA */
#endif
#ifndef PXE_VOICE
#define PXE_VOICE 1             /* default voicing: 0 A (reference), 1 B, 2 C */
#endif
#ifndef PXE_KNEE
#define PXE_KNEE 1.5f           /* B/C: the soft limiter saturates at the virtual input level 1.0 (reached at 1.5 x the 0.5 reference, see pxe_soft) */
#endif
#ifndef PXE_HPF_HZ
#define PXE_HPF_HZ 60.f         /* C: high-pass of the sidechain (55..65 Hz keeps the low E; 90 Hz costs about 7 dB of its second harmonic) */
#endif
#ifndef PXE_PRES_DB
#define PXE_PRES_DB 4.f         /* C: presence emphasis, about this many dB at 2 kHz before the gain cell (0 = none) */
#endif
#ifndef PXE_LPF_HZ
#define PXE_LPF_HZ 5500.f       /* C: anti-alias low-pass of the sidechain input */
#endif
typedef struct {
    float pre1, pre2, rect, slow, dc, post, mix, level;
    float kpre, krect, kslow, kdc, kpost, kglide;
    float a2, ripple, mix_target, level_target;
    /* ENHANCE v2: sensitivity, voicing, input conditioning, diagnostics */
    float sr, ref;                       /* sample rate; the virtual input gain of the gain cell */
    int   voice;                         /* 0 A, 1 B, 2 C */
    float hp, lpa, lpb;                  /* the high-pass's low-pass, and the two presence band-limit states */
    float khp, kpa, kpb, gpres;          /* coefficients: 0 / 0 gain = stage off */
    float peak, rms2, over, kdiag, kpk;  /* diagnostics: input peak, mean square, fraction of the time the cell input exceeded 0.5 */
} px_enhance;
static inline float pxe_clamp(float x,float a,float b) { return x<a?a:x>b?b:x; }
static inline float pxe_k(float sr,float seconds) { return 1.0f-expf(-1.0f/(sr*seconds)); }
static const float PXE_SENS_X[4] = {2.0f, 3.5f, 5.0f, 7.0f};
/* sensitivity 0..3 and voicing 0..2: outside the sample loop (they change coefficients, never the filter history) */
static inline void pxe_set_input(px_enhance *s,int sens,int voice) {
    s->ref=PXE_SENS_X[sens<0?0:sens>3?3:sens];
    s->voice=voice<0?0:voice>2?2:voice;
    const float sr=s->sr;
    if(s->voice==2) {
        s->kpre=pxe_k(sr,1.0f/(6.2831853f*PXE_LPF_HZ));
        s->khp=pxe_k(sr,1.0f/(6.2831853f*PXE_HPF_HZ));
        s->kpa=pxe_k(sr,1.0f/(6.2831853f*3000.f));
        s->kpb=pxe_k(sr,1.0f/(6.2831853f*1200.f));
        s->gpres=(powf(10.f,PXE_PRES_DB/20.f)-1.0f)*4.3f;   /* the 1.2-3 kHz difference of two one-poles is a weak hump (about 0.4 of its input near 1.5-2 kHz): scaled so the boost is about PXE_PRES_DB at 2 kHz against 330 Hz (measured in tests/engine/enhance_test.c) */
    } else {
        s->kpre=pxe_k(sr,1.0f/(6.2831853f*3500.f));
        s->khp=0.f; s->gpres=0.f;
    }
}
static inline void pxe_init(px_enhance *s,float sr) {
    *s=(px_enhance){0};
    s->sr=sr;
    s->krect=pxe_k(sr,0.0007f); /* virtual Crect = 70 nF */
    s->kslow=pxe_k(sr,0.025f);
    s->kdc=pxe_k(sr,1.0f/(6.2831853f*20.f));
    s->kpost=pxe_k(sr,1.0f/(6.2831853f*7000.f));
    s->kglide=pxe_k(sr,0.010f);
    s->level=s->level_target=1.f;
    s->a2=0.068f; /* 5 mV equivalent; H2=1.7% at A=0.5 before filtering */
    s->ripple=0.04f; /* intentionally weak VCA control modulation */
    s->kdiag=pxe_k(sr,0.3f); s->kpk=pxe_k(sr,0.5f);
    pxe_set_input(s,PXE_SENS,PXE_VOICE);
}
/* Two knobs, 0..100; initial values 35, 0. Set outside sample loop. */
static inline void pxe_set(px_enhance *s,float better,float louder) {
    s->mix_target=pxe_clamp(better,0.f,100.f)*0.01f;
    s->level_target=powf(10.f,0.006f*pxe_clamp(louder,0.f,100.f)); /* 0..12 dB */
}
/* the sidechain's input conditioning (voicing C adds the high-pass and the presence band; A and B are the prototype's two low-passes) */
static inline float pxe_cond(px_enhance *s,float x) {
    float v=x;
    if(s->khp>0.f) { s->hp+=s->khp*(v-s->hp); v-=s->hp; }
    if(s->gpres!=0.f) { s->lpa+=s->kpa*(v-s->lpa); s->lpb+=s->kpb*(v-s->lpb); v+=s->gpres*(s->lpa-s->lpb); }
    s->pre1+=s->kpre*(v-s->pre1);
    s->pre2+=s->kpre*(s->pre1-s->pre2);
    return s->pre2;
}
/* B/C overload: cubic soft limiter, identity at small signals, saturating smoothly (zero slope) at +-PXE_KNEE*2/3 = 1.0 */
static inline float pxe_soft(float v) {
    const float L=PXE_KNEE;
    if(v>L) return L*(2.0f/3.0f);
    if(v<-L) return -L*(2.0f/3.0f);
    return v-v*v*v*(1.0f/(3.0f*L*L));
}
static inline void pxe_diag(const px_enhance *s,float *peak_dbfs,float *rms_dbfs,float *over_fraction) {
    *peak_dbfs=8.685889638f*logf(s->peak+1e-9f); *rms_dbfs=4.342944819f*logf(s->rms2+1e-18f); *over_fraction=s->over;   /* logf only: the target's math library (firmware/px_math.c) has no log10f */
}
static inline float pxe_tick(px_enhance *s,float x) {
    s->mix+=s->kglide*(s->mix_target-s->mix);
    s->level+=s->kglide*(s->level_target-s->level);
    /* Limit bandwidth only in the parallel sidechain. Dry path stays intact. */
    const float c=pxe_cond(s,x);
    const float v=c*s->ref;
    float u=s->voice==0 ? pxe_clamp(v,-1.f,1.f) : pxe_soft(v);
    float a=fabsf(u);
    s->rect+=s->krect*(a-s->rect);
    s->slow+=s->kslow*(s->rect-s->slow);
    /* Gain cell offset: parabolic error. Rectifier ripple: multiplicative
       error, with slow control removed to avoid a full expander response. */
    float wet=s->a2*u*u+s->ripple*u*(s->rect-s->slow)/0.318309886f;
    s->dc+=s->kdc*(wet-s->dc);
    wet-=s->dc;
    s->post+=s->kpost*(wet-s->post);
    const float ax=fabsf(x);                                   /* diagnostics: the guitar as it arrives */
    s->peak=ax>s->peak?ax:s->peak-s->kpk*s->peak;
    s->rms2+=s->kdiag*(x*x-s->rms2);
    s->over+=s->kdiag*((fabsf(v)>0.5f?1.0f:0.0f)-s->over);
    return s->level*(x+s->mix*s->post*(1.0f/s->ref));
}
#endif
