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
#define PXP_OUT_PER_VOLT 0.076f      /* +4 dB over the first version: on guitar loops VOLUME 100 is about +5 dB (it was +1.5 dB, and -3.5 dB on a low-mid loop: the stage's gain is at 1-3 kHz, the guitar's energy below it) */
#endif
#ifndef PXP_OS
#define PXP_OS 1                    /* circuit-rate factor: 1 (the circuit at the audio rate) or 2 (half-band in and out, twice the cost). Measured (tests/engine, hard-played 1.5 kHz at 300 mV): the energy off the harmonic bins is -48 dB at 1x, -57 dB at 2x; at normal levels -87 / -97 dB; the harmonics are the same. 1x is enough for a boost */
#endif
#define PXP_VT 0.02585f
/* exp by range reduction (2^n and a degree-6 polynomial on |r| <= 0.35: relative error about 1e-6); no libm call in the sample loop */
static inline float pxp_exp(float x)
{
    if (x > 30.0f) x = 30.0f;
    if (x < -30.0f) x = -30.0f;
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
    const float v = pxp_clamp(volume, 0.0f, 100.0f) * 0.01f; r->x_t = (powf(30.0f, v) - 1.0f) / 29.0f; if (r->x_t < 0.001f) r->x_t = 0.001f;      /* a gentler log taper than a true 10k log pot: about -10 dB at 50, unity near 70 on guitar loops */
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
/* ---- EP BOOSTER: an EP-3-style JFET preamp stage (a common-source stage, one unknown: the drain current) ----
 * The Xotic EP Booster's schematic is not published; it is described by others as "inspired by" the Echoplex EP-3 preamp (a TIS58 JFET stage). This is that family of circuit:
 * JFET with the measured 2N5484 numbers from the Aion FX Ares documentation (IDSS 2.89 mA, Vp -1.32 V; the EP-3's TIS58 is the same kind of part), 18 V supply, drain 22k, source 3.3k bypassed
 * by Cs (22 nF = the brighter EARLY voicing; larger = FAT), gate 1M to ground behind a 100 nF input cap, the pickup (Rs, L) as the source, then the 500k volume pot as an attenuator.
 * Square-law JFET (saturation and triode regions, lambda 0.02). The topology and values are NOT verified against the Xotic; the level is scaled so that GAIN 100 (EARLY) is slightly above unity,
 * as the Ares notes describe the EP-3; the Fat voicing boosts more. GAIN 0..100 (a 500k log pot); VOICE 0..100: Cs 22 nF (EARLY) .. 100 nF (LATE) .. 470 nF (FAT). */
#ifndef PXP_EP_OUT
#define PXP_EP_OUT 0.106f           /* drain volts -> engine units (the stage's own level; pxp_ep_set normalises it so that GAIN 0 is unity and GAIN 100 is +10 dB) */
#endif
typedef struct {
    float Idss, Vp, lam, Rd, Rs, Vcc, Rg, beta;
    float gL, gC1, gCs, Rtot, Rpar, Ge, invGe;
    float i, vL, vC, vs, iCs, id, vd_dc, x, x_t, kx, hp_k, hp_z, voice;
} px_ep;
static inline float pxp_jfet(const px_ep *r, float vgs, float vds, float *dgs, float *dds)
{
    const float vov = vgs - r->Vp;
    if (vov <= 0.0f) { *dgs = 0.0f; *dds = 0.0f; return 0.0f; }
    if (vds < 0.0f) vds = 0.0f;
    const float cl = 1.0f + r->lam * vds;
    if (vds >= vov) { *dgs = 2.0f * r->beta * vov * cl; *dds = r->lam * r->beta * vov * vov; return r->beta * vov * vov * cl; }
    *dgs = 2.0f * r->beta * vds * cl; *dds = r->beta * (2.0f * vov - 2.0f * vds) * cl + r->lam * r->beta * vds * (2.0f * vov - vds);
    return r->beta * vds * (2.0f * vov - vds) * cl;
}
/* solve Id = f(Vgs = vg - Vs0 - Id/Ge, Vds = Vcc - Rd Id - Vs0 - Id/Ge) by a safeguarded Newton step from the previous Id */
static inline float pxp_ep_solve(const px_ep *r, float vg, float vs0, float id0)
{
    const float idmax = (r->Vcc - vs0) / (r->Rd + r->invGe); float lo = 0.0f, hi = idmax > 0.0f ? idmax : 0.0f, id = id0 < lo ? lo : id0 > hi ? hi : id0;
    for (int it = 0; it < 20; it++) {
        const float vs = vs0 + id * r->invGe, vgs = vg - vs, vds = r->Vcc - r->Rd * id - vs; float dgs, dds;
        const float f = pxp_jfet(r, vgs, vds, &dgs, &dds), F = id - f;
        if (F > 0.0f) hi = id; else lo = id;
        const float dF = 1.0f + dgs * r->invGe + dds * (r->Rd + r->invGe);
        float nid = id - F / dF; if (nid < lo || nid > hi) nid = 0.5f * (lo + hi);
        if (fabsf(nid - id) < 1e-9f) { id = nid; break; } id = nid;
    }
    return id;
}
static inline void pxp_ep_coeffs(px_ep *r, float sr, float Rs_pk, float L, float Cs)
{
    const float dt = 1.0f / sr; r->gL = dt / (2.0f * L); r->gC1 = 2.0f * 100e-9f / dt; r->gCs = 2.0f * Cs / dt;
    r->Rtot = Rs_pk + 1.0f / r->gL + 1.0f / r->gC1; r->Rpar = r->Rtot * r->Rg / (r->Rtot + r->Rg); r->Ge = 1.0f / r->Rs + r->gCs; r->invGe = 1.0f / r->Ge;
}
static inline void pxp_ep_init(px_ep *r, float sr)
{
    *r = (px_ep){0};
    r->Idss = 2.89e-3f; r->Vp = -1.32f; r->lam = 0.02f; r->Rd = 22000.0f; r->Rs = 3300.0f; r->Vcc = 18.0f; r->Rg = 1e6f; r->beta = r->Idss / (r->Vp * r->Vp);
    pxp_ep_coeffs(r, sr, 6000.0f, 2.5f, 22e-9f); r->voice = 0.0f;
    float id = 0.0f; for (int k = 0; k < 40; k++) { const float vs = id * r->Rs; float dgs, dds; id = pxp_jfet(r, -vs, r->Vcc - r->Rd * id - vs, &dgs, &dds) * 0.5f + id * 0.5f; }
    r->id = id; r->vs = id * r->Rs; r->vd_dc = r->Vcc - r->Rd * id; r->vC = 0.0f;
    r->kx = 1.0f - expf(-1.0f / (sr * 0.010f)); r->hp_k = 1.0f - expf(-6.2831853f * 3.0f / sr); r->x = r->x_t = 0.84f;      /* (GAIN 0, EARLY: -1.5 dB) */
}
static inline void pxp_ep_set(px_ep *r, float gain, float voice, float sr)
{
    const float g = pxp_clamp(gain, 0.0f, 100.0f), w = pxp_clamp(voice, 0.0f, 100.0f) * 0.01f;
    if (fabsf(w - r->voice) > 1e-4f) { r->voice = w; pxp_ep_coeffs(r, sr, 6000.0f, 2.5f, 22e-9f * powf(21.36f, w)); }     /* 22 nF .. 470 nF */
    /* a boost never cuts: GAIN 0 is unity (loudness on guitar loops, per voicing: the stage's own level is +1.5 dB EARLY, +4.5 LATE, +10.7 FAT at x = 1), GAIN 100 is +10 dB */
    const float dbv = w < 0.5f ? -1.5f + (-4.5f + 1.5f) * (w * 2.0f) : -4.5f + (-10.7f + 4.5f) * ((w - 0.5f) * 2.0f);
    r->x_t = powf(10.0f, dbv * 0.05f + g * 0.005f);
}
static inline float pxp_ep_tick(px_ep *r, float in)
{
    r->x += r->kx * (r->x_t - r->x);
    const float e = in * PXP_EMF_PER_UNIT;
    const float hL = r->i + r->gL * r->vL, hC = r->gC1 * r->vC + r->i, hS = r->gCs * r->vs + r->iCs, Ah = hL / r->gL - hC / r->gC1;
    const float vg = r->Rpar * ((e + Ah) / r->Rtot), vs0 = hS / r->Ge;
    const float id = pxp_ep_solve(r, vg, vs0, r->id), vs = vs0 + id * r->invGe, i = (e - vg + Ah) / r->Rtot;
    r->vL = (i - hL) / r->gL; r->vC = (i + hC) / r->gC1; r->i = i; r->iCs = r->gCs * vs - hS; r->vs = vs; r->id = id;
    float y = r->x * ((r->Vcc - r->Rd * id) - r->vd_dc);
    r->hp_z += r->hp_k * (y - r->hp_z);
    return -(y - r->hp_z) * PXP_EP_OUT;
}
/* ---- MAXIMIZER: the BBE Sonic Maximizer's process as a structure ----
 * Published description of the BBE process (Sound On Sound et al.): a phase correction that delays the lower frequencies progressively more (a total delay under 2 ms) plus frequency-
 * dependent boosting. Built as that: a Linkwitz-Riley 4th-order three-way split, the low and mid bands DELAYED against the high band, band gains, sum. The crossovers (258 Hz, 1.82 kHz),
 * the delays (1.36 ms low, 0.59 ms mid) and the gains were fitted to the real BBE 882i's measured magnitude response at five settings (offline measurement of captures: the fit is 0.76 dB RMS;
 * tools/research/pedals/bbe_measure.py, bbe_fit.py), not copied from a schematic. LO CONTOUR sets the low-band gain; PROCESS the mid-band gain (down to 0.37) and the high-band gain (up to 1.27)
 * ; below setting 1 of 10 on both knobs the processed path fades into the dry signal (0/0 is exactly bypass). Only the diagonal settings (LO = PROCESS) were measured: that split between the knobs is an assumption.
 * Linear and level independent (the measured harmonics are below -43 dB at every level); no dynamic stage was found in the captures. */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } pxp_bq;
static inline void pxp_bq_design(pxp_bq *q, int high, float hz, float sr)       /* RBJ two-pole Butterworth (Q 0.7071) */
{
    const float w0 = 6.2831853f * hz / sr, cw = cosf(w0), sw = sinf(w0), al = sw / (2.0f * 0.70710678f), a0 = 1.0f + al;
    if (high) { q->b0 = (1.0f + cw) * 0.5f / a0; q->b1 = -(1.0f + cw) / a0; q->b2 = q->b0; } else { q->b0 = (1.0f - cw) * 0.5f / a0; q->b1 = (1.0f - cw) / a0; q->b2 = q->b0; }
    q->a1 = -2.0f * cw / a0; q->a2 = (1.0f - al) / a0; q->z1 = q->z2 = 0.0f;
}
static inline float pxp_bq_run(pxp_bq *q, float x) { const float y = q->b0 * x + q->z1; q->z1 = q->b1 * x - q->a1 * y + q->z2; q->z2 = q->b2 * x - q->a2 * y; return y; }
typedef struct {
    pxp_bq lp1[2], hp1[2], lp2[2], hp2[2];             /* LR4 = two cascaded Butterworth sections: split at f1 (low | rest), then the rest at f2 (mid | high) */
    float dl[128], dm[64]; unsigned il, im; int nl, nm;       /* the delay lines of the low and mid bands, and their lengths in samples */
    float gl, gm, gh, mix, gl_t, gm_t, gh_t, mix_t, kglide, mk, mk_t;      /* mk: the loudness makeup (see pxp_bbe_set) */
} px_bbe;
static inline float pxp_lerp_tab(const float *xs, const float *ys, int n, float x)      /* piecewise linear, clamped */
{
    if (x <= xs[0]) return ys[0];
    if (x >= xs[n - 1]) return ys[n - 1];
    int i = 1; while (x > xs[i]) i++; return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
}
static inline void pxp_bbe_init(px_bbe *b, float sr)
{
    { unsigned char *z = (unsigned char *)b; for (unsigned k = 0; k < sizeof *b; k++) z[k] = 0; }
    for (int k = 0; k < 2; k++) { pxp_bq_design(&b->lp1[k], 0, 258.0f, sr); pxp_bq_design(&b->hp1[k], 1, 258.0f, sr); pxp_bq_design(&b->lp2[k], 0, 1821.0f, sr); pxp_bq_design(&b->hp2[k], 1, 1821.0f, sr); }
    b->nl = (int)(0.001362f * sr + 0.5f); b->nm = (int)(0.000589f * sr + 0.5f); if (b->nl > 127) b->nl = 127; if (b->nm > 63) b->nm = 63;
    b->gl = b->gl_t = b->gm = b->gm_t = b->gh = b->gh_t = 1.0f; b->mix = b->mix_t = 0.0f; b->mk = b->mk_t = 1.0f; b->kglide = 1.0f - expf(-1.0f / (sr * 0.010f));
}
/* no output control, so never quieter than the guitar: the loudness change (BS.1770, five guitar loops, tests/pedal_levels.mjs) on a 3 x 3 grid of the knobs, made up with a 1.2 dB margin, bilinear */
static inline float pxp_bbe_makeup_db(float lo, float proc)
{
    static const float MK[3][3] = {{0.0f, 2.9f, 5.2f}, {1.8f, 3.1f, 5.6f}, {0.6f, 1.9f, 4.0f}};            /* [LO CONTOUR 0, 50, 100][PROCESS 0, 50, 100] dB */
    const float lf = pxp_clamp(lo, 0.0f, 100.0f) * 0.02f, pf = pxp_clamp(proc, 0.0f, 100.0f) * 0.02f; const int li = lf >= 1.0f ? 1 : 0, pi = pf >= 1.0f ? 1 : 0;
    const float lt = lf - (float)li, pt = pf - (float)pi;
    const float a = MK[li][pi] + (MK[li][pi + 1] - MK[li][pi]) * pt, c = MK[li + 1][pi] + (MK[li + 1][pi + 1] - MK[li + 1][pi]) * pt;
    return a + (c - a) * lt;
}
/* LO CONTOUR, PROCESS 0..100 (the knob's 0..10 is one tenth of that). Gain nodes: the measured fits at settings 1(2), 3, 5, 7, 10. */
static inline void pxp_bbe_set(px_bbe *b, float lo, float proc)
{
    static const float LX[5] = {0.0f, 3.0f, 5.0f, 7.0f, 10.0f}, LG[5] = {1.0f, 0.925f, 0.946f, 0.969f, 1.241f};
    static const float PX[5] = {1.0f, 3.0f, 5.0f, 7.0f, 10.0f}, PM[5] = {1.082f, 0.937f, 0.804f, 0.620f, 0.371f}, PH[5] = {1.155f, 1.169f, 1.193f, 1.208f, 1.268f};
    const float l = pxp_clamp(lo, 0.0f, 100.0f) * 0.1f, p = pxp_clamp(proc, 0.0f, 100.0f) * 0.1f;
    b->gl_t = pxp_lerp_tab(LX, LG, 5, l); b->gm_t = pxp_lerp_tab(PX, PM, 5, p); b->gh_t = pxp_lerp_tab(PX, PH, 5, p); { const float m = p > l ? p : l; b->mix_t = m < 1.0f ? m : 1.0f; }   /* processing engages as either knob leaves 0 */

    b->mk_t = powf(10.0f, pxp_bbe_makeup_db(lo, proc) * 0.05f);
}
static inline float pxp_bbe_tick(px_bbe *b, float x)
{
    b->gl += b->kglide * (b->gl_t - b->gl); b->gm += b->kglide * (b->gm_t - b->gm); b->gh += b->kglide * (b->gh_t - b->gh); b->mix += b->kglide * (b->mix_t - b->mix); b->mk += b->kglide * (b->mk_t - b->mk);
    if (b->mix < 1e-6f && b->mix_t < 1e-6f && b->gl_t == 1.0f) return x * b->mk;
    const float low = pxp_bq_run(&b->lp1[1], pxp_bq_run(&b->lp1[0], x)), rest = pxp_bq_run(&b->hp1[1], pxp_bq_run(&b->hp1[0], x));
    const float mid = pxp_bq_run(&b->lp2[1], pxp_bq_run(&b->lp2[0], rest)), high = pxp_bq_run(&b->hp2[1], pxp_bq_run(&b->hp2[0], rest));
    b->il++; b->dl[b->il & 127] = low; b->im++; b->dm[b->im & 63] = mid;
    const float dlow = b->dl[(b->il - (unsigned)b->nl) & 127], dmid = b->dm[(b->im - (unsigned)b->nm) & 63];
    const float proc = b->gl * dlow + b->gm * dmid + b->gh * high;
    return (x + b->mix * (proc - x)) * b->mk;
}
#endif
