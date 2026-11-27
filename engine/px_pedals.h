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
#define PXP_OUT_PER_VOLT 0.163f      /* with the VOLUME taper below: VOLUME 50 is unity on guitar loops and 100 is +12 dB (owner: unity was near 90). The stage's gain is at 1-3 kHz and the guitar's energy below it, so a plain volts-to-units scale left it quiet */
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
#if defined(PXP_FF_TABLE_EXP) || defined(PXP_RM_TABLE_EXP)
/* exp(x) via 2^(x / ln 2), with the fractional power of two precomputed. */
static inline float pxp_exp_table(float x)
{
    static const float fraction[129] = {
        1.000000000f, 1.005429901f, 1.010889286f, 1.016378315f, 1.021897149f, 1.027445949f,
        1.033024879f, 1.038634102f, 1.044273782f, 1.049944086f, 1.055645178f, 1.061377227f,
        1.067140401f, 1.072934868f, 1.078760798f, 1.084618362f, 1.090507733f, 1.096429082f,
        1.102382583f, 1.108368412f, 1.114386743f, 1.120437752f, 1.126521619f, 1.132638520f,
        1.138788635f, 1.144972144f, 1.151189230f, 1.157440074f, 1.163724859f, 1.170043770f,
        1.176396992f, 1.182784711f, 1.189207115f, 1.195664392f, 1.202156731f, 1.208684324f,
        1.215247360f, 1.221846033f, 1.228480536f, 1.235151064f, 1.241857812f, 1.248600977f,
        1.255380757f, 1.262197350f, 1.269050957f, 1.275941778f, 1.282870016f, 1.289835873f,
        1.296839555f, 1.303881265f, 1.310961212f, 1.318079601f, 1.325236643f, 1.332432547f,
        1.339667524f, 1.346941786f, 1.354255547f, 1.361609021f, 1.369002423f, 1.376435971f,
        1.383909882f, 1.391424376f, 1.398979673f, 1.406575994f, 1.414213562f, 1.421892602f,
        1.429613338f, 1.437375997f, 1.445180807f, 1.453027996f, 1.460917794f, 1.468850433f,
        1.476826146f, 1.484845166f, 1.492907728f, 1.501014070f, 1.509164428f, 1.517359041f,
        1.525598151f, 1.533881998f, 1.542210825f, 1.550584878f, 1.559004400f, 1.567469640f,
        1.575980845f, 1.584538265f, 1.593142151f, 1.601792756f, 1.610490332f, 1.619235135f,
        1.628027422f, 1.636867450f, 1.645755478f, 1.654691768f, 1.663676580f, 1.672710180f,
        1.681792831f, 1.690924799f, 1.700106354f, 1.709337763f, 1.718619298f, 1.727951231f,
        1.737333835f, 1.746767386f, 1.756252160f, 1.765788436f, 1.775376493f, 1.785016611f,
        1.794709075f, 1.804454168f, 1.814252176f, 1.824103385f, 1.834008086f, 1.843966569f,
        1.853979125f, 1.864046048f, 1.874167634f, 1.884344179f, 1.894575982f, 1.904863342f,
        1.915206561f, 1.925605944f, 1.936061793f, 1.946574418f, 1.957144124f, 1.967771223f,
        1.978456026f, 1.989198847f, 2.000000000f,
    };
    /* Offset the index by 48 octaves to keep the conversion nonnegative. */
    const float position = pxp_clamp(x, -30.0f, 30.0f) * (128.0f * 1.44269504f) + (48.0f * 128.0f);
    const unsigned index = (unsigned)position, sample = index & 127u;
    const float mix = position - (float)index;
    union { unsigned u; float f; } scale = { .u = ((index >> 7) + 127u - 48u) << 23 };
    return scale.f * (fraction[sample] + mix * (fraction[sample + 1u] - fraction[sample]));
}
#endif
static inline float pxp_lerp3(const float *xs, const float *ys, float x) { return x <= xs[1] ? ys[0] + (ys[1] - ys[0]) * (x - xs[0]) / (xs[1] - xs[0]) : ys[1] + (ys[2] - ys[1]) * (x - xs[1]) / (xs[2] - xs[1]); }
/* a junction's exponential with a straight-line continuation above x = 18 (0.465 V: 0.12 A in a germanium junction, far beyond this circuit): finite value and slope, so a Newton overshoot cannot run away */
static inline float pxp_expl(float x, float *d)
{
    if (x <= 18.0f) {
#if defined(PXP_RM_TABLE_EXP)
        const float e = pxp_exp_table(x);
#else
        const float e = pxp_exp(x);
#endif
        *d = e;
        return e;
    }
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
    float Rs, L, C1, sr2, pk, pk_t, body;                  /* C1: the input capacitor (5 nF .. 100 nF with BODY) */
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
        last = dj; if (dj < 1e-3f) break;   /* v75: was 2e-5 V; a step under 1 mV leaves about 2e-5 V (Newton is quadratic), 1.5-1.9 iterations per sample instead of 2.0-2.4 on the loops, the output within -93 dB of the tight solve */
    }
    if (last > 1e-3f) pxp_bjt(r, vbe, vbc, &ic, &ib, &gbe, &gbc);      /* the iteration cap was reached on a large step: the linearised currents are not good enough */
    *pvbe = vbe; *pvbc = vbc; *pib = ib; *pic = ic;
}
static inline void pxp_rm_coeffs(px_rm *r, float sr2, float Rs, float L, float C1)      /* the circuit-rate constants (outside the sample loop) */
{
    r->Rs = Rs; r->L = L; r->C1 = C1; r->sr2 = sr2; const float dt = 1.0f / sr2;
    r->gL = dt / (2.0f * L); r->gC1 = 2.0f * C1 / dt; r->gC3 = 2.0f * 47e-6f / dt;
    r->Rtot = Rs + 1.0f / r->gL + 1.0f / r->gC1; r->Rpar = r->Rtot * r->Rbb / (r->Rtot + r->Rbb);
    r->Ge = 1.0f / r->R3 + r->gC3; r->invGe = 1.0f / r->Ge;
}
static inline void pxp_rm_init(px_rm *r, float sr)
{
    *r = (px_rm){0};
    r->Is = 1.8e-9f; r->bf = 100.0f; r->br = 8.0f; r->R3 = 3900.0f; r->RV = 10000.0f; r->Vcc = 9.0f;
    const float R1 = 470000.0f, R2 = 68000.0f; r->Rbb = R1 * R2 / (R1 + R2); r->Vth = r->Vcc * R2 / (R1 + R2);
    pxp_rm_coeffs(r, (float)PXP_OS * sr, 6000.0f, 2.5f, 5e-9f);
    /* DC: capacitors open, no series current */
    float vbe = 0.25f, vbc = -5.0f, ib, ic;
    pxp_newton(r, r->Vth, r->Rbb + r->R3, r->R3, r->Vth - r->Vcc, r->Rbb, -r->RV, &vbe, &vbc, &ib, &ic);
    const float vb = r->Vth - r->Rbb * ib; r->ve = r->R3 * (ib + ic); r->vc_dc = r->Vcc - r->RV * ic; r->vbe = r->pvbe = vbe; r->vbc = r->pvbc = vbc; r->vC = -vb;
    r->kx = 1.0f - expf(-1.0f / (sr * 0.010f)); r->hp_k = 1.0f - expf(-6.2831853f * 16.0f / sr); r->x = r->x_t = 0.001f;
    r->pk = r->pk_t = 0.0f; pxp_os_reset(&r->os);
}
/* VOLUME 0..100: unity at 50, +12 dB at 100 (a pot is an attenuator on the stage's own level, so the taper is written in dB: +0.24 dB per step above 50, 0.8 dB per step below it, silent at 0);
 * PICKUP 0..100 (single coil .. humbucker); BODY 0..100: the input capacitor C1 from the original 5 nF (a treble booster, corner 2.6 kHz) to 100 nF (a full-range boost, corner about 130 Hz): the
 * popular "input cap mod" (ElectroSmash: 5/10/15 nF to keep the voicing, 47/100 nF to open it up). The loudness is kept: pxp_rm_body_db */
static inline float pxp_rm_body_db(float w)      /* dB taken off the output as BODY opens the low end (measured on guitar loops, tests/pedal_levels.mjs) */
{
    static const float X[3] = {0.0f, 0.5f, 1.0f}, Y[3] = {0.0f, 7.2f, 9.6f};
    return pxp_lerp3(X, Y, w);
}
static inline void pxp_rm_set(px_rm *r, float volume, float pickup, float body)
{
    const float v = pxp_clamp(volume, 0.0f, 100.0f);
    const float db = v >= 50.0f ? (v - 50.0f) * 0.24f : (v - 50.0f) * 0.8f;
    r->x_t = v < 1.0f ? 0.0f : powf(10.0f, (db - 12.0f - pxp_rm_body_db(pxp_clamp(body, 0.0f, 100.0f) * 0.01f)) * 0.05f);
    const float p = pxp_clamp(pickup, 0.0f, 100.0f) * 0.01f, w = pxp_clamp(body, 0.0f, 100.0f) * 0.01f;
    r->pk_t = p;
    if (fabsf(p - r->pk) > 1e-4f || fabsf(w - r->body) > 1e-4f) { r->pk = p; r->body = w; pxp_rm_coeffs(r, r->sr2, 6000.0f + 3000.0f * p, 2.5f + 3.0f * p, 5e-9f * powf(20.0f, w)); }
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
    float e0, e1; pxp_up(&r->os, in * PXP_FF_IN, &e0, &e1);
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
        if (fabsf(nid - id) < 1e-6f) { id = nid; break; } id = nid;   /* v75: was 1e-9 A; 1 uA is 0.1 % of the bias current: 1.0-1.5 iterations per sample instead of 2.0-2.3, the output within -99 dB */
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

/* ---- BST VOX FIXED WAH: the Vox V847 wah's tone curve with the pedal parked (a "cocked wah" boost) ---------------------------------
 * Figures from the ElectroSmash analysis of the V847 (https://electrosmash.mas-effects.com/vox-v847-analysis.html): an LC resonance (L1 500 mH
 * with C2, the pot's wiper changing the apparent capacitance) swept from about 450 Hz (heel) to 1.6 kHz (toe), about 750 Hz at mid travel,
 * boosting by up to 18 dB; R7 (33k) sets the sharpness of the peak. Modelled as one peaking biquad plus the pot network's low cut.
 * Q (v75, researched): ElectroSmash gives none, but measurements of Vox-style wahs on the music-electronics-forum thread "a Vox wah project:
 * some circuit analysis and measuring results" (Helmholtz, post 83) give the -3 dB Q as about 2.3..3.3 at the treble (toe) end and 8..11 at
 * the bass (heel) end: the resonance is an LC with Q = Rp*sqrt(C/L), and the pot's effective C is about 25x larger at the heel, so Q rises as
 * the peak goes down: Q ~ 1/fc. Here Q(-3 dB) = (2.4 + 0.9*PEAK) * 1600/fc: 2.4..3.3 at the toe, 8.5..11.7 at the heel (those figures are for
 * one circuit, not a V847 on the bench). A peaking biquad's own Q is the width at half the gain in dB, which at 18 dB makes the -3 dB Q 2.8x
 * larger; the design divides that out: Qbiquad = Q3 * A / sqrt(A^4 - 2), A = 10^(dB/40) (exact for the analogue prototype).
 * The position law (fc = 450 * (1600/450)^(pos^1.3), which puts mid travel at 757 Hz) and the peak height range (8 .. 18 dB) are fits to the
 * ElectroSmash figures, not measurements of a unit: check them against a recorded sweep. POSITION never moves while playing,
 * so the coefficients are only recomputed (every 16 samples, glided) after a knob move. */
typedef struct { float b0, b1, b2, a1, a2, z1, z2, hx, hy, hk, pos, tgt, peak, g, gt, sr; unsigned n, dirty; } px_vw;
static inline float pxp_vw_hz(float pos) { return 450.0f * powf(1600.0f / 450.0f, powf(pos, 1.3f)); }
static inline float pxp_vw_peak_db(float peak) { return 8.0f + 10.0f * peak; }
static inline float pxp_vw_q3(float fc, float peak) { return (2.4f + 0.9f * peak) * (1600.0f / fc); }   /* the -3 dB Q of the peak: 1/fc */
static inline float pxp_vw_makeup_db(float pos, float peak)      /* the loudness the wah adds to guitar loops, taken off, with 1 dB over unity: measured on the five bundled loops (tests/pedal_levels.mjs + pedal_levels_measure.py), a grid of POSITION 0 25 50 75 100 x PEAK 0 50 100, bilinear between */
{
    static const float G[3][5] = {{  0.2f,  -0.1f,   1.0f,   0.6f,   1.0f},
                                  { -2.0f,  -2.3f,  -0.3f,  -0.8f,  -0.2f},
                                  { -5.1f,  -5.2f,  -2.5f,  -2.9f,  -2.2f}};
    float x = pxp_clamp(pos, 0.0f, 1.0f) * 4.0f, y = pxp_clamp(peak, 0.0f, 1.0f) * 2.0f;
    int i = (int)x, j = (int)y; if (i > 3) i = 3; if (j > 1) j = 1;
    const float fx = x - (float)i, fy = y - (float)j;
    const float a = G[j][i] + (G[j][i + 1] - G[j][i]) * fx, b = G[j + 1][i] + (G[j + 1][i + 1] - G[j + 1][i]) * fx;
    return a + (b - a) * fy;
}
static inline void pxp_vw_design(px_vw *v)
{
    const float fc = pxp_vw_hz(v->pos), w0 = 6.2831853f * fc / v->sr;
    const float A = powf(10.0f, pxp_vw_peak_db(v->peak) * 0.025f), A2 = A * A;
    const float Q = pxp_vw_q3(fc, v->peak) * A / sqrtf(A2 * A2 - 2.0f);   /* the biquad's Q for that -3 dB Q (A^4 >= 6.3 for 8 dB) */
    const float al = sinf(w0) / (2.0f * Q), c = cosf(w0);
    const float a0 = 1.0f + al / A, r = 1.0f / a0;
    v->b0 = (1.0f + al * A) * r; v->b1 = -2.0f * c * r; v->b2 = (1.0f - al * A) * r;
    v->a1 = -2.0f * c * r; v->a2 = (1.0f - al / A) * r;
}
static inline void pxp_vw_init(px_vw *v, float sr)
{
    *v = (px_vw){0};
    v->sr = sr; v->pos = v->tgt = 0.5f; v->peak = 0.5f; v->g = v->gt = 1.0f;
    v->hk = expf(-6.2831853f * 80.0f / sr);                       /* the low cut: a one-pole high-pass at 80 Hz */
    pxp_vw_design(v);
}
static inline void pxp_vw_set(px_vw *v, float pos, float peak, float level)
{
    v->tgt = pxp_clamp(pos, 0.0f, 100.0f) * 0.01f;
    v->peak = pxp_clamp(peak, 0.0f, 100.0f) * 0.01f;
    v->gt = powf(10.0f, (pxp_vw_makeup_db(v->tgt, v->peak) + pxp_clamp(level, 0.0f, 100.0f) * 0.12f) * 0.05f);   /* LEVEL 0 = the same loudness, 100 = +12 dB */
    v->dirty = 1;
}
static inline float pxp_vw_tick(px_vw *v, float x)
{
    if (v->dirty && (++v->n & 15u) == 0) {
        v->pos += (v->tgt - v->pos) * 0.1f;
        if (fabsf(v->tgt - v->pos) < 1e-4f) { v->pos = v->tgt; v->dirty = 0; }
        pxp_vw_design(v);
    }
    const float h = v->hk * (v->hy + x - v->hx); v->hx = x; v->hy = h;
    const float y = v->b0 * h + v->z1;
    v->z1 = v->b1 * h - v->a1 * y + v->z2; v->z2 = v->b2 * h - v->a2 * y;
    v->g += (v->gt - v->g) * 0.002f;
    return y * v->g;
}

/* ---- FUZZ FACE (v75): the Dallas Arbiter circuit, solved per sample ----
 * The topology and values are the ElectroSmash analysis of the first (germanium PNP) Fuzz Face (https://electrosmash.com/fuzz-face; the text is read, its schematic image is not): the guitar
 * through C1 2.2 uF into Q1's base; Q1's collector feeds Q2's base directly and sits on R1 33k; Q2's emitter goes to ground through the 1k FUZZ pot (its wiper, with C2 20 uF to ground, takes the
 * emitter resistance away for the signal: more fuzz = less AC degeneration) and, through R4 100k, back to Q1's base (the global shunt-series feedback: it sets Q1's bias and the low input
 * impedance, about 5k, that makes the pedal load the pickup and clean up with the guitar's volume); Q2's collector load is R3 8.2k then R2 470 to the supply, and the output is taken at that
 * junction (a -25 dB divider) through C3 10 nF into the 500k VOLUME. 9 V. The real pedal is a PNP on a positive ground: here everything is its mirror, an NPN with the emitters at ground, which is
 * the same equations. Each transistor is Ebers-Moll (forward and reverse junction) with a collector-base leakage (germanium) or none (silicon); the unknowns are the four node voltages (Q1 base,
 * Q1 collector = Q2 base, Q2 emitter, Q2 collector) and one Newton step solves them from the previous sample's, the capacitors and the pickup (Rs, L, C1) as trapezoid companions.
 * The germanium numbers (Is, beta 85 and 120 as the analysis simulates, a leakage of 10 uA over 9 V) are chosen to land on the analysis's bias (Q1 collector about 0.6 V and Q2 collector 4.5 V
 * from the emitter side at FUZZ 50), not measured on a unit; the silicon is a BC108-like NPN (the usual silicon Fuzz Face). Nothing here is measured on a pedal. */
#ifndef PXP_FF_OS
#define PXP_FF_OS 2                 /* circuit-rate factor: 2 (half-band in and out): a fuzz makes harmonics far above what the boosts do */
#endif
#ifndef PXP_FF_MAXIT
#define PXP_FF_MAXIT 8              /* Newton steps at most per circuit step */
#endif
#ifndef PXP_FF_TOL
#define PXP_FF_TOL 1e-3f            /* stop when a step is under this many volts */
#endif
#ifndef PXP_FF_OUT
#define PXP_FF_OUT 0.125f             /* the tap's AC volts -> engine units, before the VOLUME taper (set so that FUZZ 50 / VOLUME 50 is about the guitar's loudness: tests/pedal_levels.mjs) */
#endif
typedef struct {
    float Is, bf1, bf2, br, lim;                             /* the transistors (lim: where the junction exponential goes straight) */
    float Isbf1, Isbf2, Isbr, ibf1, ibf2, ibr, gR1, gR4, gRc;   /* Is / beta, 1 / beta, 1 / R: no division in the sample loop */
    float igL, igC1, iRt, kD;
    float R1, R4, R2, Rc, Rt, Rb, Vcc;                       /* the circuit */
    float gL, gC1, gW, Rtot, gsrc, ge2, khw, ge2dc;          /* trapezoid companions (the circuit rate) and the emitter network's (D / Rt, 1 / Rt ...) */
    float x[4], xp[4];                                       /* node voltages now and the sample before */
    float i, vL, vC, vW, iW;                                 /* the pickup branch (loop current, L, C1) and C2's state */
    float hp_k, hp_z, vol, vol_t, kv, fz, fz_t, bias, sr2, Rs, L;
    int trans, tick;
    pxp_os os;
} px_ff;
#ifdef PXP_FF_COUNT
static long pxp_ff_iters;
#endif
static inline float pxp_ff_exp(float x)
{
#if defined(PXP_FF_TABLE_EXP)
    return pxp_exp_table(x);
#else
    return pxp_exp(x);
#endif
}

/* a junction's exponential, straight-lined above x = lim (the germanium's 18 = 0.465 V, the silicon's 30 = 0.78 V): finite, so Newton cannot run away, and never a false root */
static inline float pxp_explim(float x, float lim, float *d)
{
    if (x <= lim) { const float e = pxp_ff_exp(x); *d = e; return e; }
    const float e = pxp_ff_exp(lim); *d = e; return e * (1.0f + (x - lim));
}
/* the junction currents and slopes of one transistor: be, bc = the junction voltages; Isbf = Is / beta_f, Isbr = Is / beta_r, ibr = 1 / beta_r */
static inline void pxp_ff_bjt(float Is, float Isbf, float Isbr, float ibr, float lim, float be, float bc, float *ic, float *ib, float *gbe, float *gbc, float *dicbc)
{
    float d1, d2 = 0.0f; const float eb = pxp_explim(be * (1.0f / PXP_VT), lim, &d1);
    float ec = 0.0f; if (bc > -0.30f) ec = pxp_explim(bc * (1.0f / PXP_VT), lim, &d2);   /* (a junction far in reverse is nothing: no second exponential) */
    *ic = Is * (eb - ec) - Isbr * (ec - 1.0f);
    *ib = Isbf * (eb - 1.0f) + Isbr * (ec - 1.0f);
    *gbe = Is * d1 * (1.0f / PXP_VT); *gbc = Is * d2 * (1.0f / PXP_VT);
    *dicbc = -*gbc * (1.0f + ibr);
}
/* Newton on the four node voltages (maxit at most; stops when a step is under tol volts). src = e + Ah, the pickup branch's Thevenin voltage, gsrc its conductance; ge2 / he2 the emitter
 * network's current i = ge2 * v - he2. While Q2's collector is more than 0.3 V above its base (it is, bar the loudest swings) Q2's collector current does not depend on its collector, which is then just
 * the supply less Rc times that current: three unknowns, a 3x3 system, one exponential less. Otherwise (Q2 saturating) the whole 4x4. (No leakage: the germanium numbers are fitted without it.) */
static inline float pxp_ff_solve(const px_ff *r, float src, float gsrc, float ge2, float he2, float *x, int maxit, float tol)
{
    const float Is = r->Is, gR1 = r->gR1, gR4 = r->gR4, gRc = r->gRc, Vcc = r->Vcc, ibf1 = r->ibf1, ibf2 = r->ibf2, ibr = r->ibr;
    float worst = 0.0f;
    for (int it = 0; it < maxit; it++) {
#ifdef PXP_FF_COUNT
        pxp_ff_iters++;
#endif
        float ic1, ib1, g1e, g1c, d1c, ic2, ib2, g2e, g2c, d2c;
        pxp_ff_bjt(Is, r->Isbf1, r->Isbr, ibr, r->lim, x[0], x[0] - x[1], &ic1, &ib1, &g1e, &g1c, &d1c);
        pxp_ff_bjt(Is, r->Isbf2, r->Isbr, ibr, r->lim, x[1] - x[2], x[1] - x[3], &ic2, &ib2, &g2e, &g2c, &d2c);
        const float b1e = g1e * ibf1, b1c = g1c * ibr, b2e = g2e * ibf2, b2c = g2c * ibr;                /* dIb / dvbe, dIb / dvbc */
        const float F0 = (src - x[0]) * gsrc + (x[2] - x[0]) * gR4 - ib1;
        const float F1 = (Vcc - x[1]) * gR1 - ic1 - ib2;
        const float F2 = ic2 + ib2 - (x[2] - x[0]) * gR4 - ge2 * x[2] + he2;
        float dx[4];
        if (g2c == 0.0f) {                                                                              /* Q2 forward: x3 follows from its current */
            const float a00 = -gsrc - gR4 - b1e - b1c, a01 = b1c, a02 = gR4;
            const float a10 = -(g1e + d1c), a11 = -gR1 + d1c - b2e, a12 = b2e;
            const float a20 = gR4, a21 = g2e + b2e, a22 = -g2e - b2e - gR4 - ge2;
            const float p0 = 1.0f / a00, f1 = a10 * p0, f2 = a20 * p0;                                 /* eliminate dx0 */
            const float a11p = a11 - f1 * a01, a12p = a12 - f1 * a02, r1 = -F1 + f1 * F0;
            const float a21p = a21 - f2 * a01, a22p = a22 - f2 * a02, r2 = -F2 + f2 * F0;
            const float p1 = 1.0f / a11p, f3 = a21p * p1;                                              /* eliminate dx1 */
            const float a22q = a22p - f3 * a12p, r2q = r2 - f3 * r1;
            dx[2] = r2q / a22q;
            dx[1] = (r1 - a12p * dx[2]) * p1;
            dx[0] = (-F0 - a01 * dx[1] - a02 * dx[2]) * p0;
            dx[3] = (Vcc - ic2 * r->Rc) - x[3] - 0.0f;                                                 /* the collector goes to where its current puts it (this step's, not the next) */
            dx[3] = (Vcc - (ic2 + g2e * (dx[1] - dx[2])) * r->Rc) - x[3];
        } else {                                                                                       /* Q2 saturating: the four together */
            float a[4][5];
            const float F3 = (Vcc - x[3]) * gRc - ic2;
            a[0][0] = -gsrc - gR4 - b1e - b1c; a[0][1] = b1c; a[0][2] = gR4; a[0][3] = 0.0f; a[0][4] = -F0;
            a[1][0] = -(g1e + d1c); a[1][1] = -gR1 + d1c - (b2e + b2c); a[1][2] = b2e; a[1][3] = b2c; a[1][4] = -F1;
            a[2][0] = gR4; a[2][1] = (g2e + d2c) + (b2e + b2c); a[2][2] = -g2e - b2e - gR4 - ge2; a[2][3] = -d2c - b2c; a[2][4] = -F2;
            a[3][0] = 0.0f; a[3][1] = -(g2e + d2c); a[3][2] = g2e; a[3][3] = -gRc + d2c; a[3][4] = -F3;
            for (int k = 0; k < 3; k++) {
                const float piv = 1.0f / a[k][k];
                for (int i = k + 1; i < 4; i++) { const float f = a[i][k] * piv; if (f == 0.0f) continue; for (int j = k + 1; j < 5; j++) a[i][j] -= f * a[k][j]; }
            }
            dx[3] = a[3][4] / a[3][3];
            dx[2] = (a[2][4] - a[2][3] * dx[3]) / a[2][2];
            dx[1] = (a[1][4] - a[1][2] * dx[2] - a[1][3] * dx[3]) / a[1][1];
            dx[0] = (a[0][4] - a[0][1] * dx[1] - a[0][2] * dx[2]) / a[0][0];
        }
        float m = 0.0f; for (int k = 0; k < 4; k++) { const float v = dx[k] < 0.0f ? -dx[k] : dx[k]; if (v > m) m = v; }
        if (m > 0.12f) { const float s = 0.12f / m; for (int k = 0; k < 4; k++) dx[k] *= s; }          /* a step over 120 mV is cut: the exponentials do not run away */
        for (int k = 0; k < 4; k++) x[k] += dx[k];
        worst = m;
        if (m < tol) break;
    }
    return worst;
}
#ifndef PXP_FF_IN
#define PXP_FF_IN 0.25f             /* guitar EMF volts per engine unit into the circuit (RANGEMASTER and the others use PXP_EMF_PER_UNIT) */
#endif
#ifndef PXP_FF_TAPER
#define PXP_FF_TAPER 3.0f           /* the FUZZ pot's law: the emitter-side resistance falls as (1 - fuzz)^TAPER (a 1k audio pot; linear made the top fifth do all the work) */
#endif
static inline void pxp_ff_pot(px_ff *r, float fz)               /* the 1k pot: Rt from the emitter to the wiper, Rb from the wiper to ground, always 1k in all */
{
    const float u = 1.0f - fz, rt = powf(u, PXP_FF_TAPER);
    r->Rt = 1000.0f * rt + 1.0f; r->Rb = 1001.0f - 1000.0f * rt;
}
static inline void pxp_ff_coeffs(px_ff *r)                      /* the circuit-rate constants for the present FUZZ (Rt, Rb) and the pickup (Rs, L) */
{
    const float dt = 1.0f / r->sr2;
    r->gL = dt / (2.0f * r->L); r->gC1 = 2.0f * 2.2e-6f / dt; r->gW = 2.0f * 20e-6f / dt;
    r->igL = 1.0f / r->gL; r->igC1 = 1.0f / r->gC1;
    r->Rtot = r->Rs + r->igL + r->igC1; r->gsrc = 1.0f / r->Rtot;
    r->gR1 = 1.0f / r->R1; r->gR4 = 1.0f / r->R4; r->gRc = 1.0f / r->Rc;
    r->Isbf1 = r->Is / r->bf1; r->Isbf2 = r->Is / r->bf2; r->Isbr = r->Is / r->br; r->ibf1 = 1.0f / r->bf1; r->ibf2 = 1.0f / r->bf2; r->ibr = 1.0f / r->br;
    const float Rt = r->Rt, Rb = r->Rb; r->iRt = 1.0f / Rt;
    const float D = 1.0f / (r->iRt + 1.0f / Rb + r->gW); r->kD = D;                             /* the wiper node: Rt from the emitter, Rb to ground, C2 to ground */
    r->ge2 = r->iRt * (1.0f - D * r->iRt); r->khw = D * r->iRt; r->ge2dc = 1.0f / (Rt + Rb);
}
static inline void pxp_ff_dc(px_ff *r)                          /* the quiescent point: the capacitors open, no signal */
{
    float *x = r->x;
    if (r->trans) { x[0] = 0.62f; x[1] = 1.3f; x[2] = 0.6f; x[3] = 4.5f; } else { x[0] = 0.2f; x[1] = 0.6f; x[2] = 0.35f; x[3] = 4.5f; }
    pxp_ff_solve(r, 0.0f, 0.0f, r->ge2dc, 0.0f, x, 200, 1e-7f);
    r->i = 0.0f; r->vL = 0.0f; r->vC = -x[0]; r->vW = x[2] * r->Rb / (r->Rt + r->Rb); r->iW = 0.0f;
    for (int k = 0; k < 4; k++) r->xp[k] = x[k];
    r->hp_z = r->R2 * (r->Vcc - x[3]) / r->Rc;
}
static inline void pxp_ff_params(px_ff *r, int trans, float fuzz01, float bias01, float pickup01)
{
    const int tchg = trans != r->trans;
    r->trans = trans;
    if (trans) { r->Is = 2.0e-14f; r->bf1 = 200.0f; r->bf2 = 260.0f; r->br = 3.0f; r->lim = 30.0f; }
    else { r->Is = 3.08e-7f; r->bf1 = 70.0f; r->bf2 = 120.0f; r->br = 5.0f; r->lim = 18.0f; }   /* (Q1 collector 0.71 V, Q2 collector 4.52 V at BIAS 50) */
    r->fz_t = fuzz01;
    r->Rs = 6000.0f + 3000.0f * pickup01; r->L = 2.5f + 3.0f * pickup01;
    r->Rc = 8200.0f * expf((0.5f - bias01) * 1.0f - (trans ? 0.33f : 0.0f)) + r->R2;   /* the Q2 collector load (R3, 8.2k at BIAS 50; a silicon pair draws more, so its stock is a little lower) */
    if (tchg || r->tick == 0) { r->fz = fuzz01; pxp_ff_pot(r, r->fz); pxp_ff_coeffs(r); pxp_ff_dc(r); r->tick = 1; }
    else pxp_ff_coeffs(r);
}
static inline void pxp_ff_init(px_ff *r, float sr, int trans)
{
    *r = (px_ff){0};
    r->sr2 = (float)PXP_FF_OS * sr; r->R1 = 33000.0f; r->R4 = 100000.0f; r->R2 = 470.0f; r->Vcc = 9.0f; r->trans = -1;
    r->hp_k = 1.0f - expf(-6.2831853f * 31.8f / sr); r->vol = r->vol_t = 1.0f; r->kv = 1.0f - expf(-1.0f / (sr * 0.010f));
    pxp_ff_params(r, trans, 0.5f, 0.5f, 0.0f);
    pxp_os_reset(&r->os);
}
/* one circuit-rate step: the guitar's EMF volts in, the tap's AC volts out */
static inline float pxp_ff_core(px_ff *r, float e)
{
    const float hL = r->i + r->gL * r->vL, hC = r->gC1 * r->vC + r->i, Ah = hL * r->igL - hC * r->igC1, hW = r->gW * r->vW + r->iW;
    float x[4];
    for (int k = 0; k < 4; k++) { float v = 2.0f * r->x[k] - r->xp[k]; const float d = v - r->x[k]; v = r->x[k] + (d > 0.4f ? 0.4f : d < -0.4f ? -0.4f : d); x[k] = v; }   /* the predictor: a line through the last two */
    pxp_ff_solve(r, e + Ah, r->gsrc, r->ge2, hW * r->khw, x, PXP_FF_MAXIT, PXP_FF_TOL);
    for (int k = 0; k < 4; k++) { r->xp[k] = r->x[k]; r->x[k] = x[k]; }
    const float i = (e + Ah - x[0]) * r->gsrc;
    r->vL = (i - hL) * r->igL; r->vC = (i + hC) * r->igC1; r->i = i;
    r->vW = (x[2] * r->iRt + hW) * r->kD; r->iW = r->gW * r->vW - hW;
    return r->R2 * r->gRc * (r->Vcc - x[3]);
}
/* the make-up that keeps the loudness about level across FUZZ (measured on the five loops with FUZZ 0, 25, 50, 75, 100: -16, -11, -3, +2, +2 dB at VOLUME 50; mostly made up) */
static inline float pxp_ff_makeup_db(float f)
{
    static const float M[5] = {12.0f, 8.0f, 2.5f, -1.2f, -2.0f};
    const float x = pxp_clamp(f, 0.0f, 1.0f) * 4.0f; const int i = x >= 4.0f ? 3 : (int)x;
    return M[i] + (M[i + 1] - M[i]) * (x - (float)i);
}
static inline void pxp_ff_set(px_ff *r, float fuzz, float vol, float pickup, float bias, float trans)
{
    const float v = pxp_clamp(vol, 0.0f, 100.0f), db = v >= 50.0f ? (v - 50.0f) * 0.24f : (v - 50.0f) * 0.8f;
    const float f = pxp_clamp(fuzz, 0.0f, 100.0f) * 0.01f;
    r->vol_t = v < 1.0f ? 0.0f : powf(10.0f, (db + pxp_ff_makeup_db(f)) * 0.05f) * PXP_FF_OUT;
    const float p = pxp_clamp(pickup, 0.0f, 100.0f) * 0.01f, b = pxp_clamp(bias, 0.0f, 100.0f) * 0.01f;
    const int t = trans > 50.0f;                                     /* TRANSISTOR: germanium up to 50, silicon above */
    if (t != r->trans || fabsf(b - r->bias) > 1e-4f || fabsf(p - (r->Rs - 6000.0f) / 3000.0f) > 1e-4f) {
        const float keep = r->fz; pxp_ff_params(r, t, f, b, p); r->bias = b; r->fz = keep;        /* (a new transistor set, bias or pickup: the operating point is found again) */
        if (t != r->trans) {}
    }
    r->fz_t = f;
}
static inline float pxp_ff_tick(px_ff *r, float in)
{
    r->vol += r->kv * (r->vol_t - r->vol);
    if (r->fz != r->fz_t && (++r->tick & 7) == 0) {            /* FUZZ glides: the emitter network again every 8 samples while it moves */
        r->fz += (r->fz_t - r->fz) * 0.15f; if (fabsf(r->fz_t - r->fz) < 1e-4f) r->fz = r->fz_t;
        pxp_ff_pot(r, r->fz); pxp_ff_coeffs(r);
    }
#if PXP_FF_OS == 2
    float e0, e1; pxp_up(&r->os, in * PXP_FF_IN, &e0, &e1);
    pxp_push(&r->os, pxp_ff_core(r, e0)); pxp_push(&r->os, pxp_ff_core(r, e1));
    const float y = pxp_down(&r->os);
#else
    const float y = pxp_ff_core(r, in * PXP_FF_IN);
#endif
    r->hp_z += r->hp_k * (y - r->hp_z);                        /* C3 and the volume pot: the output DC block (and the pedal's bass cut) */
    return (y - r->hp_z) * r->vol;
}
#endif
