/* SPDX-License-Identifier: GPL-3.0-or-later
 * ENHANCE v4 (SFX): a "compander" harmonic enhancer: the signal's own gain follows a fast rectified copy of the signal. Header-only, float, no buffers, no sqrt/powf/expf in the loop.
 * Why this and not an exciter (docs/ENHANCE-RESEARCH.md, docs/ENHANCE.md): the Better Box is described by its maker as an NE570 compander circuit that makes even and odd harmonics
 * WITHOUT clipping, acts as a subtle compressor and tightens the pick attack, with BETTER setting the even/odd balance. A gain cell whose control is a FAST rectified copy of the
 * signal modulates the signal with its own ripple: a half-wave detector ripples at f (gives 2f: even harmonics), a full-wave one at 2f (gives 3f: odd). v1/v2 modelled only the chip's
 * tiny static distortion, v3 an exciter on a fixed curve; neither had a gain that follows the signal. Offline prototype (sines): full-wave -> H2 < -110 dB, H3 -30 dB; half-wave ->
 * H2 -18, H3 -29; a 50/50 mix -> H2 -21.7, H3 -31 dB ("emphasises even-order, a light dusting of odd"); clean below the knee (-196 dB), gain falling 2..8 dB over -25..-10 dBFS.
 *   d   = 0.5|x| + max(x,0)            detector: an equal mix of full- and half-wave (the guess: the maker says only that BETTER sets the balance)
 *   env : one-pole, attack 0.1 ms, release 2 ms (fast enough to ripple within a note of 80 Hz and up)
 *   g   = 1 / (1 + S * max(env - THR, 0) / THR)          rational compressor above the knee (smooth, no log/exp); y = x * g
 * BETTER 0..100 sets S (0 .. PXE_SMAX) and so both the harmonics and the compression; LOUDER 0..100 = 0..+12 dB clean boost. Every parameter is a GUESS fitted to the maker's words,
 * not a measurement of a real Better Box (docs/ENHANCE.md). The knee THR is in engine units (guitar peaks about 0.14 at the pedal, docs/ENHANCE.md level section). */
#ifndef PX_ENHANCE_H
#define PX_ENHANCE_H
#include <math.h>
#ifndef PXE_THR
#define PXE_THR 0.04f           /* the knee: -28 dBFS in engine units (clean below it; a guitar at the pedal peaks near 0.14) */
#endif
#ifndef PXE_SMAX
#define PXE_SMAX 0.6f           /* compression slope at BETTER 100: at twice the knee the gain is 1/(1+S) = -4.1 dB; sine at 0.08: H2 -16 dB, H3 -25 dB */
#endif
#ifndef PXE_MIX
#define PXE_MIX 0.5f            /* detector: 0 full-wave (odd harmonics only) .. 1 half-wave (even-dominant); 0.5 = the guess */
#endif
typedef struct { float env, ka, kr, kglide, s, s_t, level, level_t, dc_z, dc_k; } px_enhance;
static inline float pxe_clamp(float x,float a,float b) { return x<a?a:x>b?b:x; }
static inline void pxe_init(px_enhance *s,float sr) {
    *s=(px_enhance){0};
    s->ka=1.0f-expf(-1.0f/(sr*0.0001f)); s->kr=1.0f-expf(-1.0f/(sr*0.002f)); s->kglide=1.0f-expf(-1.0f/(sr*0.010f));
    s->dc_k=1.0f-expf(-6.2831853f*10.0f/sr);                 /* the gain asymmetry leaves a little DC: a 10 Hz block */
    s->level=s->level_t=1.0f;
}
/* BETTER, LOUDER 0..100. Outside the sample loop. */
static inline void pxe_set(px_enhance *s,float better,float louder) {
    s->s_t=PXE_SMAX*pxe_clamp(better,0.f,100.f)*0.01f;
    s->level_t=powf(10.f,0.006f*pxe_clamp(louder,0.f,100.f));       /* 0..12 dB */
}
static inline float pxe_tick(px_enhance *s,float x) {
    s->s+=s->kglide*(s->s_t-s->s); s->level+=s->kglide*(s->level_t-s->level);
    float y=x;
    if(s->s>1e-5f || s->s_t>1e-5f) {
        const float hw=x>0.0f?x:0.0f, d=(1.0f-PXE_MIX)*fabsf(x)+PXE_MIX*2.0f*hw;
        s->env+=(d>s->env?s->ka:s->kr)*(d-s->env);
        const float over=(s->env-PXE_THR)*(1.0f/PXE_THR);
        y=x*(over>0.0f?1.0f/(1.0f+s->s*over):1.0f);
        s->dc_z+=s->dc_k*(y-s->dc_z); y-=s->dc_z;
    }
    return s->level*y;
}
#endif
