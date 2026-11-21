/* SPDX-License-Identifier: GPL-3.0-or-later
 * ENHANCE v5 (SFX), the Better Box: a "compander" harmonic enhancer. Header-only, float, no buffers, no libm in the loop.
 * What the maker says (TWA/Godlyke): an NE570 compander circuit, even and odd harmonics WITHOUT distortion or diode clipping, a subtle compressor that "evens out" the response and
 * tightens the pick attack; BETTER sets the even/odd balance. A compander gain cell works at EVERY level (its control is the rectified signal in the log domain, so the gain follows the
 * signal's level as level^-1/2 for the chip's classic 2:1 compression): there is no threshold above which it starts, so a quiet note is processed like a loud one.
 * (v4 had a knee at -28 dBFS and so only worked on the loudest notes; v5 has none.) A gain controlled by a FAST rectified copy of the signal modulates the signal with its own ripple:
 * half-wave detection ripples at f (2f: even harmonics), full-wave at 2f (3f: odd).
 *   d   = 0.5|x| + max(x,0)                          detector: an equal mix of full- and half-wave (a guess: the maker only says BETTER sets the balance)
 *   env : one-pole, attack 0.1 ms, release 2 ms
 *   g   = 1 + B * (sqrt(ENV0 / (env + FLOOR)) - 1)   the 2:1 law, blended in by BETTER (B 0..PXE_TOP, linear to PXE_KNEE); FLOOR keeps silence and noise from being raised without limit
 *   y   = x * g, a 10 Hz DC block, LOUDER (0..+12 dB clean boost)
 * Every parameter is a GUESS fitted to the maker's words, not a measurement of a real Better Box (docs/ENHANCE.md). Levels are engine units (guitar peaks about 0.14 at the pedal). */
#ifndef PX_ENHANCE_H
#define PX_ENHANCE_H
#include <math.h>
#ifndef PXE_ENV0
#define PXE_ENV0 0.03f          /* the level at which the 2:1 law has unity gain (about -30 dBFS: an average guitar note) */
#endif
#ifndef PXE_FLOOR
#define PXE_FLOOR 0.003f        /* the detector's floor (-50 dBFS): below it the gain stops rising (at most sqrt(ENV0/FLOOR) = 3.2x, +10 dB) */
#endif
#ifndef PXE_TA
#define PXE_TA 0.0003f          /* detector attack (s): 0.1 ms slammed the gain down within a fraction of a cycle on a pick (a hard-limiter edge) */
#endif
#ifndef PXE_TR
#define PXE_TR 0.002f           /* detector release (s) */
#endif
#ifndef PXE_GS
#define PXE_GS 0.0f             /* gain smoothing corner (Hz; 0 = none): bandlimits the gain's own modulation */
#endif
#ifndef PXE_KNEE
#define PXE_KNEE 0.7f           /* BETTER up to this (of 1) blends the 2:1 law in linearly, as before; above it the depth rises slowly (the top of the knob). The harmonics and the intermodulation of a chord come from the same gain ripple: at the full depth the junk between the notes of a chord was only 3 dB below the second harmonic (about -23 dB re the notes, 'digital interference'); the top of the knob now stops at PXE_TOP */
#endif
#ifndef PXE_TOP
#define PXE_TOP 0.745f          /* the depth at BETTER 100 (about -27 dB of junk) */
#endif
#ifndef PXE_MIX
#define PXE_MIX 0.5f            /* detector: 0 full-wave (odd harmonics only) .. 1 half-wave (even-dominant); 0.5 = the guess */
#endif
typedef struct { float env, ka, kr, kglide, b, b_t, level, level_t, dc_z, dc_k, g, kg; } px_enhance;
static inline float pxe_clamp(float x,float a,float b) { return x<a?a:x>b?b:x; }
static inline float pxe_sqrt(float x)                    /* x > 0: bit-trick estimate and two Newton steps (relative error about 1e-6) */
{
    union { float f; unsigned u; } v; v.f = x; v.u = 0x1fbd1df5u + (v.u >> 1); float y = v.f;
    y = 0.5f * (y + x / y); y = 0.5f * (y + x / y); return y;
}
static inline void pxe_init(px_enhance *s,float sr) {
    *s=(px_enhance){0};
    s->ka=1.0f-expf(-1.0f/(sr*PXE_TA)); s->kr=1.0f-expf(-1.0f/(sr*PXE_TR)); s->kg=PXE_GS>0.0f?1.0f-expf(-6.2831853f*PXE_GS/sr):1.0f; s->g=1.0f; s->kglide=1.0f-expf(-1.0f/(sr*0.010f));
    s->dc_k=1.0f-expf(-6.2831853f*10.0f/sr);
    s->level=s->level_t=1.0f;
}
/* BETTER, LOUDER 0..100. Outside the sample loop. */
static inline void pxe_set(px_enhance *s,float better,float louder) {
    { const float b=pxe_clamp(better,0.f,100.f)*0.01f; s->b_t=b<=PXE_KNEE?b:PXE_KNEE+(b-PXE_KNEE)*((PXE_TOP-PXE_KNEE)/(1.0f-PXE_KNEE)); }
    s->level_t=powf(10.f,0.006f*pxe_clamp(louder,0.f,100.f));       /* 0..12 dB */
}
static inline float pxe_tick(px_enhance *s,float x) {
    s->b+=s->kglide*(s->b_t-s->b); s->level+=s->kglide*(s->level_t-s->level);
    float y=x;
    if(s->b>1e-5f || s->b_t>1e-5f) {
        const float hw=x>0.0f?x:0.0f, d=(1.0f-PXE_MIX)*fabsf(x)+PXE_MIX*2.0f*hw;
        s->env+=(d>s->env?s->ka:s->kr)*(d-s->env);
        const float gt=1.0f+s->b*(pxe_sqrt(PXE_ENV0/(s->env+PXE_FLOOR))-1.0f);
        s->g+=s->kg*(gt-s->g);
        y=x*s->g;
        s->dc_z+=s->dc_k*(y-s->dc_z); y-=s->dc_z;
    }
    return s->level*y;
}
#endif
