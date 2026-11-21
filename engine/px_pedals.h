/* SPDX-License-Identifier: GPL-3.0-or-later
 * Analogue pedal CIRCUIT models (SFX types RANGEMASTER, ...). Header-only, float. These are circuits solved sample by sample, not fitted waveforms or EQ curves: a component
 * changes the sound the way it does on the bench, and the source (the guitar's pickup) is part of the circuit, so a pickup and a playing level change what the pedal does.
 * Research and the reference simulators (the full nodal solver, checks against the analyst's numbers): tools/research/pedals, docs/PEDALS.md.
 *
 * RANGEMASTER (Dallas, 1965): one germanium PNP stage; ElectroSmash analysis values R1 470k, R2 68k, R3 3.9k, C1 5n, C3 47u, RV 10k, 9 V; Ebers-Moll Is 1.8e-9, bf 100, br 8
 * (a typical 0.2 mA, Vbe 0.30 V part; real OC44s vary). The original is a PNP with the positive rail grounded: solved as the mirrored NPN stage on +9 V (identical behaviour).
 * Reduced to what the transistor sees: base: the pickup chain EMF - Rs - L - C1 (trapezoidal L and C1) and the bias network R1||R2 as a Thevenin source; emitter: R3 || C3
 * (trapezoidal); collector: RV to the rail. Unknowns (Vbe, Vbc) by a 2x2 Newton with SPICE-style junction limiting. Output: the volume is an attenuator on the collector (the
 * wiper), then the 10 nF / 1 Mohm DC block. Run at the audio rate (PXP_OS 2 adds a 39-tap half-band pair: see the note there). Polarity is restored (the real stage inverts).
 * Level: one engine unit is PXP_EMF_PER_UNIT volts of pickup EMF (engine 0.5 = converter full scale) and PXP_OUT_PER_VOLT engine units come out per collector volt: both are choices.
 * PICKUP 0..100 sets the source: series resistance 6k..9k and inductance 2.5..5.5 H (a single coil .. a humbucker). */
#ifndef PX_PEDALS_H
#define PX_PEDALS_H
#include <math.h>
#ifndef PXP_EMF_PER_UNIT
#define PXP_EMF_PER_UNIT 2.0f
#endif
#ifndef PXP_OUT_PER_VOLT
#define PXP_OUT_PER_VOLT 0.048f
#endif
#ifndef PXP_OS
#define PXP_OS 1                    /* circuit-rate factor: 1 (the circuit at the audio rate) or 2 (half-band in and out, twice the cost). Measured (tests/engine, hard-played 1.5 kHz at 300 mV): the energy off the harmonic bins is -48 dB at 1x, -57 dB at 2x; at normal levels -87 / -97 dB; the harmonics are the same. 1x is enough for a boost */
#endif
#define PXP_VT 0.02585f
/* exp by range reduction (2^n and a degree-6 polynomial on |r| <= 0.35: relative error about 1e-6); no libm call in the sample loop */
static inline float pxp_exp(float x)
{
    if (x > 30.0f) x = 30.0f; if (x < -30.0f) x = -30.0f;
    const float t = x * 1.44269504f; const int n = (int)(t + (t >= 0.0f ? 0.5f : -0.5f)); const float r = x - (float)n * 0.69314718f;
    const float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.166666667f + r * (0.0416666667f + r * (0.00833333333f + r * 0.00138888889f)))));
    union { float f; unsigned u; } v; v.u = (unsigned)(n + 127) << 23; return p * v.f;
}
static inline float pxp_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
/* a junction's exponential with a straight-line continuation above x = 18 (0.465 V: 0.12 A in a germanium junction, far beyond this circuit): finite value and slope, so a Newton overshoot cannot run away */
static inline float pxp_expl(float x, float *d)
{
    if (x <= 18.0f) { const float e = pxp_exp(x); *d = e; return e; }
    *d = 65659969.0f; return 65659969.0f * (1.0f + (x - 18.0f));
}
/* ---- 2x oversampling: 39-tap half-band h[0..38] (Kaiser 9, centre 19); h[19] = 0.499994921, h[19 +- (2k+1)] = PXP_G[k] ---- */
static const float PXP_H0 = 0.499994921f;
static const float PXP_G[10] = {0.314585725f, -0.095394314f, 0.047222598f, -0.025070877f, 0.012906453f, -0.006104977f, 0.002523165f, -0.000846682f, 0.000196767f, -0.000015319f};
typedef struct { float x[32]; float v[64]; unsigned ix, iv; } pxp_os;
static inline void pxp_os_reset(pxp_os *o) { for (int i = 0; i < 32; i++) o->x[i] = 0.0f; for (int i = 0; i < 64; i++) o->v[i] = 0.0f; o->ix = o->iv = 0; }
static inline void pxp_up(pxp_os *o, float in, float *e0, float *e1)     /* one sample in -> two out (even phase, odd phase) */
{
    o->ix++; o->x[o->ix & 31] = in; float s = 0.0f;
    for (int k = 0; k < 10; k++) s += PXP_G[k] * (o->x[(o->ix - (unsigned)(9 - k)) & 31] + o->x[(o->ix - (unsigned)(10 + k)) & 31]);
    *e0 = 2.0f * s; *e1 = 2.0f * PXP_H0 * o->x[(o->ix - 9u) & 31];
}
static inline void pxp_push(pxp_os *o, float v) { o->iv++; o->v[o->iv & 63] = v; }
static inline float pxp_down(const pxp_os *o)                            /* after the second push of a pair: the low-passed sample at the odd phase */
{
    float s = PXP_H0 * o->v[(o->iv - 19u) & 63];                         /* v[m-19], m = the newest */
    for (int k = 0; k < 10; k++) s += PXP_G[k] * (o->v[(o->iv - (unsigned)(18 - 2 * k)) & 63] + o->v[(o->iv - (unsigned)(20 + 2 * k)) & 63]);
    return s;
}
/* ---- Dallas Rangemaster ---- */
typedef struct {
    float Is, bf, br, R3, RV, Vcc, Rbb, Vth;                  /* parts */
    float gL, gC1, gC3, Rtot, Rpar, Ge, invGe;                /* trapezoid conductances and the base / emitter equivalents at the circuit rate */
    float i, vL, vC, ve, iC3, vbe, vbc, vc_dc, pvbe, pvbc;      /* states (pvbe, pvbc: the previous sample's junction voltages, for the predictor) */
    float x, x_t, kx, hp_k, hp_z, hp_y;                       /* volume (the wiper fraction, glided) and the output DC block */
    float Rs, L, sr2, pk, pk_t;
    pxp_os os;
} px_rm;
static inline void pxp_bjt(const px_rm *r, float vbe, float vbc, float *ic, float *ib, float *gbe, float *gbc)
{
    float db, dc; const float eb = pxp_expl(vbe * (1.0f / PXP_VT), &db), ec = pxp_expl(vbc * (1.0f / PXP_VT), &dc);
    *ic = r->Is * (eb - ec) - r->Is * (ec - 1.0f) / r->br; *ib = r->Is * (eb - 1.0f) / r->bf + r->Is * (ec - 1.0f) / r->br;
    *gbe = r->Is * db * (1.0f / PXP_VT); *gbc = r->Is * dc * (1.0f / PXP_VT);
}
/* vbe = ab - rbb*ib - rbc*ic ; vbc = ac - rcb*ib - rcc*ic ; Newton from the given (vbe, vbc); the currents are carried by the same linearisation (no exp after the last step) */
static inline void pxp_newton(const px_rm *r, float ab, float rbb, float rbc, float ac, float rcb, float rcc, float *pvbe, float *pvbc, float *pib, float *pic)
{
    float vbe = *pvbe, vbc = *pvbc, ic = 0, ib = 0, gbe, gbc, last = 1.0f;
    for (int it = 0; it < 30; it++) {
        pxp_bjt(r, vbe, vbc, &ic, &ib, &gbe, &gbc);
        const float F1 = vbe - (ab - rbb * ib - rbc * ic), F2 = vbc - (ac - rcb * ib - rcc * ic);
        const float dib_be = gbe / r->bf, dib_bc = gbc / r->br, dic_be = gbe, dic_bc = -gbc * (1.0f + 1.0f / r->br);
        const float J11 = 1.0f + rbb * dib_be + rbc * dic_be, J12 = rbb * dib_bc + rbc * dic_bc, J21 = rcb * dib_be + rcc * dic_be, J22 = 1.0f + rcb * dib_bc + rcc * dic_bc;
        const float det = 1.0f / (J11 * J22 - J12 * J21);
        float d1 = (-F1 * J22 + F2 * J12) * det, d2 = (-F2 * J11 + F1 * J21) * det;
        const float a1 = fabsf(d1), a2 = fabsf(d2), dj = a1 > a2 ? a1 : a2; if (dj > 0.1f) { const float k = 0.1f / dj; d1 *= k; d2 *= k; }
        vbe += d1; vbc += d2; ib += dib_be * d1 + dib_bc * d2; ic += dic_be * d1 + dic_bc * d2;
        last = dj; if (dj < 2e-5f) break;
    }
    if (last > 1e-3f) pxp_bjt(r, vbe, vbc, &ic, &ib, &gbe, &gbc);      /* a large last step: the linearised currents are not good enough */
    *pvbe = vbe; *pvbc = vbc; *pib = ib; *pic = ic;
}
static inline void pxp_rm_coeffs(px_rm *r, float sr2, float Rs, float L)      /* the circuit-rate constants (outside the sample loop) */
{
    r->Rs = Rs; r->L = L; r->sr2 = sr2; const float dt = 1.0f / sr2;
    r->gL = dt / (2.0f * L); r->gC1 = 2.0f * 5e-9f / dt; r->gC3 = 2.0f * 47e-6f / dt;
    r->Rtot = Rs + 1.0f / r->gL + 1.0f / r->gC1; r->Rpar = r->Rtot * r->Rbb / (r->Rtot + r->Rbb);
    r->Ge = 1.0f / r->R3 + r->gC3; r->invGe = 1.0f / r->Ge;
}
static inline void pxp_rm_init(px_rm *r, float sr)
{
    *r = (px_rm){0};
    r->Is = 1.8e-9f; r->bf = 100.0f; r->br = 8.0f; r->R3 = 3900.0f; r->RV = 10000.0f; r->Vcc = 9.0f;
    const float R1 = 470000.0f, R2 = 68000.0f; r->Rbb = R1 * R2 / (R1 + R2); r->Vth = r->Vcc * R2 / (R1 + R2);
    pxp_rm_coeffs(r, (float)PXP_OS * sr, 6000.0f, 2.5f);
    /* DC: capacitors open, no series current */
    float vbe = 0.25f, vbc = -5.0f, ib, ic;
    pxp_newton(r, r->Vth, r->Rbb + r->R3, r->R3, r->Vth - r->Vcc, r->Rbb, -r->RV, &vbe, &vbc, &ib, &ic);
    const float vb = r->Vth - r->Rbb * ib; r->ve = r->R3 * (ib + ic); r->vc_dc = r->Vcc - r->RV * ic; r->vbe = r->pvbe = vbe; r->vbc = r->pvbc = vbc; r->vC = -vb;
    r->kx = 1.0f - expf(-1.0f / (sr * 0.010f)); r->hp_k = 1.0f - expf(-6.2831853f * 16.0f / sr); r->x = r->x_t = 0.001f;
    r->pk = r->pk_t = 0.0f; pxp_os_reset(&r->os);
}
/* VOLUME 0..100 (a 10k log taper: the wiper's share of the resistance), PICKUP 0..100 (single coil .. humbucker) */
static inline void pxp_rm_set(px_rm *r, float volume, float pickup)
{
    const float v = pxp_clamp(volume, 0.0f, 100.0f) * 0.01f; r->x_t = (powf(100.0f, v) - 1.0f) / 99.0f; if (r->x_t < 0.001f) r->x_t = 0.001f;
    const float p = pxp_clamp(pickup, 0.0f, 100.0f) * 0.01f; r->pk_t = p;
    if (fabsf(r->pk_t - r->pk) > 1e-4f) { r->pk = r->pk_t; pxp_rm_coeffs(r, r->sr2, 6000.0f + 3000.0f * p, 2.5f + 3.0f * p); }
}
static inline float pxp_rm_core(px_rm *r, float e)                         /* one circuit-rate step: EMF volts in, the wiper's AC volts out (inverted, as the stage is) */
{
    const float hL = r->i + r->gL * r->vL, hC = r->gC1 * r->vC + r->i, hE = r->gC3 * r->ve + r->iC3, Ah = hL / r->gL - hC / r->gC1;
    const float Vb0 = r->Rpar * ((e + Ah) / r->Rtot + r->Vth / r->Rbb), Ve0 = hE / r->Ge;
    float ib, ic;
    const float g0 = r->vbe, g1 = r->vbc; float vbe = 2.0f * r->vbe - r->pvbe, vbc = 2.0f * r->vbc - r->pvbc;      /* predictor: linear extrapolation of the last two samples */
    vbe = pxp_clamp(vbe, g0 - 0.05f, g0 + 0.05f); vbc = pxp_clamp(vbc, g1 - 1.0f, g1 + 1.0f);
    pxp_newton(r, Vb0 - Ve0, r->Rpar + r->invGe, r->invGe, Vb0 - r->Vcc, r->Rpar, -r->RV, &vbe, &vbc, &ib, &ic);
    r->pvbe = g0; r->pvbc = g1; r->vbe = vbe; r->vbc = vbc;
    const float vb = Vb0 - r->Rpar * ib, ve = Ve0 + (ib + ic) * r->invGe, vc = r->Vcc - r->RV * ic, i = (e - vb + Ah) / r->Rtot;
    r->vL = (i - hL) / r->gL; r->vC = (i + hC) / r->gC1; r->i = i; r->iC3 = r->gC3 * ve - hE; r->ve = ve;
    return r->x * (pxp_clamp(vc, -0.2f, r->Vcc + 0.6f) - r->vc_dc);                 /* the collector cannot leave the supply range */
}
static inline float pxp_rm_tick(px_rm *r, float in)
{
    r->x += r->kx * (r->x_t - r->x);
#if PXP_OS == 2
    float e0, e1; pxp_up(&r->os, in * PXP_EMF_PER_UNIT, &e0, &e1);
    pxp_push(&r->os, pxp_rm_core(r, e0)); pxp_push(&r->os, pxp_rm_core(r, e1));
    const float y = pxp_down(&r->os);
#else
    const float y = pxp_rm_core(r, in * PXP_EMF_PER_UNIT);
#endif
    r->hp_z += r->hp_k * (y - r->hp_z);                                   /* the output DC block */
    return -(y - r->hp_z) * PXP_OUT_PER_VOLT;
}
#endif
