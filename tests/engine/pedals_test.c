/* SFX RANGEMASTER, EP BOOST and MAXIMIZER (engine/px_pedals.h) through the whole engine. The expected numbers come from the FULL nodal simulator of the same circuit (tools/research/pedals/rm_circuit.py,
 * checked against ElectroSmash's analysis: 38 dB at full volume, input corner about 2.6 kHz, bias Vbe 0.30 V / 0.2 mA / collector 7.1 V), not from this implementation.
 * Level: one engine unit = 2 V of pickup EMF, engine out = 0.163 per collector volt (so engine gain = circuit gain - 9.74 dB at VOLUME 100).
 *  - catalogue, silence, no DC, polarity (not inverted)
 *  - small-signal response of a single coil into the stage (the loading by the 10-12 kOhm input is part of the circuit)
 *  - level behaviour: clean when small, second harmonic grows, the gain falls (the stage compresses), asymmetric
 *  - PICKUP: a humbucker into the same stage has less treble; VOLUME is an attenuator after the clipping (its distortion does not change with it)
 *  - bounded, finite, no click on switching or a knob jump */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 2 };
static float x[N], y[N], z[N], ya[N], yb[N], yc[N];
static int g_body;                                  /* RANGEMASTER's BODY for the helpers below (0 = the original 5 nF) */
static void preset(px_preset_t *p, int on, int volume, int pickup)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_BST] = (uint8_t)on; p->type[PX_B_BST] = PX_BST_RANGEMASTER; p->p[PX_B_BST][0] = (uint8_t)volume; p->p[PX_B_BST][1] = (uint8_t)pickup; p->p[PX_B_BST][2] = (uint8_t)g_body;
}
static void render(const px_preset_t *p, const float *in, float *out, int n)
{
    static px_engine e; px_engine_init(&e, FS); px_engine_set(&e, p);
    for (int at = 0; at + 64 <= n; at += 64) px_engine_process(&e, in + at, out + at, 64);
}
static float mag(const float *s, int n, float hz)
{
    double re = 0, im = 0; for (int i = 0; i < n; i++) { const double w = 6.283185307179586 * hz * i / FS; re += s[i] * cos(w); im += s[i] * sin(w); }
    return (float)(sqrt(re * re + im * im) * 2.0 / n);
}
static void sine(float *o, int n, float hz, float amp) { for (int i = 0; i < n; i++) o[i] = amp * sinf(6.2831853f * hz * (float)i / FS); }
/* gain in dB of a sine through the stage (engine units), measured over whole cycles in the last second */
static float gain_db(float hz, float emf_mv, int volume, int pickup, float *h2, float *h3)
{
    const float amp = emf_mv * 1e-3f / PXP_EMF_PER_UNIT; px_preset_t p; sine(x, N, hz, amp); preset(&p, 1, volume, pickup); render(&p, x, y, N);
    const int cyc = (int)(44100.0f / hz), n = (int)((float)(44100 / cyc) * (float)cyc * 0 + 44100);
    const float a1 = mag(y + 44100, n, hz);
    if (h2) *h2 = 20.0f * log10f(mag(y + 44100, n, 2 * hz) / a1 + 1e-9f);
    if (h3) *h3 = 20.0f * log10f(mag(y + 44100, n, 3 * hz) / a1 + 1e-9f);
    return 20.0f * log10f(a1 / amp);
}

/* ---- EP BOOST: an EP-3-style JFET stage (engine/px_pedals.h). Expectations are physical (a square-law JFET is even-order dominant; the source bypass sets the treble/bass gain) and the
 * documented level (GAIN 100, EARLY: slightly above unity), not a reference simulation: the Xotic's schematic is not published. ---- */
static void epreset(px_preset_t *p, int on, int gain, int voice)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_BST] = (uint8_t)on; p->type[PX_B_BST] = PX_BST_EPBOOST; p->p[PX_B_BST][0] = (uint8_t)gain; p->p[PX_B_BST][1] = (uint8_t)voice;
}
static float epgain(float hz, float amp, int gain, int voice, float *h2, float *h3)
{
    px_preset_t p; sine(x, N, hz, amp); epreset(&p, 1, gain, voice); render(&p, x, y, N);
    const float a1 = mag(y + 44100, 44100, hz);
    if (h2) *h2 = 20.0f * log10f(mag(y + 44100, 44100, 2 * hz) / a1 + 1e-9f);
    if (h3) *h3 = 20.0f * log10f(mag(y + 44100, 44100, 3 * hz) / a1 + 1e-9f);
    return 20.0f * log10f(a1 / amp);
}
static int ep_tests(void)
{
    px_preset_t p; char b[8]; int before = bad;
    CHECK(px_type_retired(PX_B_BST, PX_BST_EPBOOST) == 0 && !strcmp(px_type_name(PX_B_BST, PX_BST_EPBOOST), "EP BOOST") && px_nparams(PX_B_BST, PX_BST_EPBOOST) == 2 && !strcmp(px_param_name(PX_B_BST, PX_BST_EPBOOST, 0), "GAIN") && !strcmp(px_param_name(PX_B_BST, PX_BST_EPBOOST, 1), "VOICE"), "EP BOOST: GAIN and VOICE");
    CHECK(px_param_text(PX_B_BST, PX_BST_EPBOOST, 1, 0, b) == 1 && !strcmp(b, "EARLY") && px_param_text(PX_B_BST, PX_BST_EPBOOST, 1, 100, b) == 1 && !strcmp(b, "FAT"), "VOICE reads EARLY .. FAT: '%s'", b);
    epreset(&p, 1, 100, 0); memset(x, 0, sizeof x); render(&p, x, y, N);
    { float m = 0; for (int i = N / 2; i < N; i++) m = fmaxf(m, fabsf(y[i])); CHECK(m < 1e-6f, "EP: silence in, silence out (%.2g)", m); }
    { const float g0 = epgain(1000, 0.0005f, 0, 0, 0, 0), g100 = epgain(1000, 0.0005f, 100, 0, 0, 0), g50 = epgain(1000, 0.0005f, 50, 0, 0, 0);
      CHECK(fabsf(g0) < 3.0f, "EP EARLY, GAIN 0 is unity at 1 kHz: a boost never cuts (%.2f dB)", g0);
      CHECK(g100 > 8.0f && g100 < 13.0f, "EP EARLY, GAIN 100 is about +10 dB at 1 kHz (%.2f dB)", g100);
      CHECK(g100 - g50 > 3.0f && g50 - g0 > 3.0f, "GAIN rises steadily: %.1f, %.1f, %.1f dB at 0, 50, 100", g0, g50, g100); }
    { const float lo = epgain(300, 0.0005f, 100, 0, 0, 0), hi = epgain(8000, 0.0005f, 100, 0, 0, 0); CHECK(hi - lo > 6.0f, "EARLY is the bright voicing: 8 kHz %.1f dB above 300 Hz (%.1f / %.1f)", hi - lo, hi, lo); }
    { const float e = epgain(300, 0.0005f, 100, 0, 0, 0) - epgain(8000, 0.0005f, 100, 0, 0, 0), f = epgain(300, 0.0005f, 100, 100, 0, 0) - epgain(8000, 0.0005f, 100, 100, 0, 0);
      CHECK(f - e > 3.0f, "FAT thickens: 300 Hz against 8 kHz is %.1f dB more than in EARLY (%.1f / %.1f)", f - e, f, e); }
    { float h2s, h2l, h3l; epgain(440, 0.005f, 0, 0, &h2s, 0); const float gl = epgain(440, 0.5f, 0, 0, &h2l, &h3l), gs = epgain(440, 0.005f, 0, 0, 0, 0);
      CHECK(h2l > h2s + 12.0f, "the distortion grows with the playing level: H2 %.1f dB at 0.005, %.1f dB at 0.5", h2s, h2l);
      CHECK(h2l > h3l + 5.0f, "even order dominates (square law): H2 %.1f, H3 %.1f dB at 0.5", h2l, h3l);
      CHECK(gs - gl > -0.3f, "no expansion when hard driven: gain %.2f dB small, %.2f dB at 0.5 (the 18 V stage stays nearly linear: degeneration leaves about a fifth of the swing at the gate)", gs, gl); }
    sine(x, N, 440.0f, 0.5f); epreset(&p, 1, 0, 0); render(&p, x, y, N);
    { double s2 = 0; float pk = 0, nk = 0; for (int i = 44100; i < N; i++) { s2 += y[i]; pk = fmaxf(pk, y[i]); nk = fminf(nk, y[i]); } s2 /= 44100.0;
      CHECK(fabs(s2) < 0.003, "EP: no DC (%.5f)", s2); CHECK(fabsf(pk + nk) > 0.01f * (pk - nk), "EP: asymmetric clipping (peaks %+.3f / %+.3f)", pk, nk); CHECK(pk < 1.0f, "EP: usable level when hard driven (peak %.3f)", pk); }
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f; epreset(&p, 1, 100, 100); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 2.5f, "EP: a full-scale square is finite and bounded (peak %.3f)", pk); }
    { unsigned r = 3u; for (int i = 0; i < N; i++) { r = r * 1664525u + 1013904223u; x[i] = ((float)(r >> 9) / 4194304.0f - 1.0f) * 0.5f; } render(&p, x, y, N);
      int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 2.5f, "EP: loud noise is finite and bounded (peak %.3f)", pk); }
    sine(x, N, 220.0f, 0.1f);
    {
        px_preset_t pa, pb; epreset(&pa, 1, 80, 20); epreset(&pb, 0, 80, 20); render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "EP: switching off and on mid-note: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
        epreset(&pa, 1, 30, 0); epreset(&pb, 1, 100, 100); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "EP: a knob jump mid-note (GAIN 30 -> 100, VOICE 0 -> 100) glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    return bad - before;
}

/* ---- MAXIMIZER: the BBE process. Expected numbers are the REAL unit's measured magnitude response (the BBE 882i captures, offline: tools/research/pedals/bbe_measure.py) at settings
 * LO CONTOUR = PROCESS = 3, 5, 7, 10. The model is fitted to these (0.76 dB RMS), so this checks that the engine realises the fit; the knob split is an assumption. ---- */
static void bpreset(px_preset_t *p, int on, int lo, int proc)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_BST] = (uint8_t)on; p->type[PX_B_BST] = PX_BST_MAXIMIZER; p->p[PX_B_BST][0] = (uint8_t)lo; p->p[PX_B_BST][1] = (uint8_t)proc;
}
static float bgain(float hz, float amp, int lo, int proc, float *h2)
{
    px_preset_t p; sine(x, N, hz, amp); bpreset(&p, 1, lo, proc); render(&p, x, y, N);
    const float a1 = mag(y + 44100, 44100, hz); if (h2) *h2 = 20.0f * log10f(mag(y + 44100, 44100, 2 * hz) / a1 + 1e-9f);
    return 20.0f * log10f(a1 / amp);
}
static int bbe_tests(void)
{
    px_preset_t p; int before = bad;
    CHECK(px_type_retired(PX_B_BST, PX_BST_MAXIMIZER) == 0 && !strcmp(px_type_name(PX_B_BST, PX_BST_MAXIMIZER), "MAXIMIZER") && px_nparams(PX_B_BST, PX_BST_MAXIMIZER) == 2 && !strcmp(px_param_name(PX_B_BST, PX_BST_MAXIMIZER, 0), "LO CONTOUR") && !strcmp(px_param_name(PX_B_BST, PX_BST_MAXIMIZER, 1), "PROCESS"), "MAXIMIZER: LO CONTOUR and PROCESS");
    { static const float hz[7] = {102.0f, 399.0f, 709.0f, 1015.0f, 2585.0f, 7071.0f, 11694.0f};
      static const float ref[4][7] = {{-0.5f, -1.4f, -1.6f, -1.5f, -0.2f, 1.1f, 1.2f}, {-0.4f, -2.6f, -3.0f, -2.7f, -0.6f, 1.6f, 1.8f}, {-0.2f, -4.5f, -5.6f, -4.9f, -1.0f, 1.8f, 2.2f}, {1.8f, -6.8f, -12.2f, -9.6f, -1.2f, 2.9f, 3.4f}};
      static const int set[4] = {30, 50, 70, 100};
      for (int s = 0; s < 4; s++) for (int k = 0; k < 7; k++) { const float g = bgain(hz[k], 0.05f, set[s], set[s], 0) - pxp_bbe_makeup_db((float)set[s], (float)set[s]), tol = ((k == 2 || k == 3) && s == 3) ? 3.0f : (k == 4 ? 2.6f : 2.0f);      /* the fit's worst points: the dip at 10/10 and a small notch at the 1.8 kHz crossover */
        CHECK(fabsf(g - ref[s][k]) < tol, "MAXIMIZER %d/%d at %.0f Hz: %.1f dB (makeup removed) against the real unit's %.1f dB", set[s] / 10, set[s] / 10, hz[k], g, ref[s][k]); } }
    { float h2; bgain(709.0f, 0.3f, 100, 100, &h2); CHECK(h2 < -80.0f, "linear: no harmonics at any level (H2 %.0f dB)", h2); }
    bpreset(&p, 1, 0, 0); sine(x, N, 440.0f, 0.2f); render(&p, x, y, N); { px_preset_t q = p; q.on[PX_B_BST] = 0; render(&q, x, z, N); }
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "0 / 0 is exactly bypass (%.2g)", m); }
    { const float lo = bgain(60.0f, 0.05f, 100, 0, 0) - pxp_bbe_makeup_db(100, 0), flat = bgain(60.0f, 0.05f, 0, 0, 0); CHECK(lo - flat > 1.0f, "LO CONTOUR alone lifts the lows (%+.1f dB at 60 Hz)", lo - flat); }
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f; bpreset(&p, 1, 100, 100); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 3.0f, "MAXIMIZER: a full-scale square is finite and bounded (peak %.2f)", pk); }
    sine(x, N, 220.0f, 0.1f);
    {
        px_preset_t pa, pb; bpreset(&pa, 1, 80, 80); bpreset(&pb, 0, 80, 80); render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "MAXIMIZER: switching off and on mid-note: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
        bpreset(&pa, 1, 10, 10); bpreset(&pb, 1, 100, 100); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "MAXIMIZER: a knob jump mid-note glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    return bad - before;
}

/* a guitar-like signal (196 Hz and 19 harmonics falling as 1/h, peak 0.14): the pedals must not be quieter than the dry signal (RMS over the last second) */
/* BS.1770 K-weighting at 44.1 kHz (the same coefficients as tools/nam_level.py), then the RMS of the last second: loudness, not plain RMS (the BBE scoops the mids the K-weighting favours) */
static float rms1(const float *s)
{
    const double f1 = 1681.974450955533, G = 3.999843853973347, Q1 = 0.7071752369554196, f2 = 38.13547087602444, Q2 = 0.5003270373238773;
    double K = tan(3.141592653589793 * f1 / 44100.0), Vh = pow(10.0, G / 20.0), Vb = pow(Vh, 0.4996667741545416), a0 = 1 + K / Q1 + K * K;
    const double b0 = (Vh + Vb * K / Q1 + K * K) / a0, b1 = 2 * (K * K - Vh) / a0, b2 = (Vh - Vb * K / Q1 + K * K) / a0, a1 = 2 * (K * K - 1) / a0, a2 = (1 - K / Q1 + K * K) / a0;
    const double K2 = tan(3.141592653589793 * f2 / 44100.0), c0 = 1 + K2 / Q2 + K2 * K2, c1 = 2 * (K2 * K2 - 1) / c0, c2 = (1 - K2 / Q2 + K2 * K2) / c0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0, e = 0;
    for (int i = 0; i < N; i++) { const double xi = s[i], y = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = xi; y2 = y1; y1 = y;
        const double v = y, w = v - 2 * u1 + u2 - c1 * v1 - c2 * v2; u2 = u1; u1 = v; v2 = v1; v1 = w; if (i >= 44100) e += w * w; }
    return (float)sqrt(e / 44100.0);
}
static void guitarish(float *o, int n) { for (int i = 0; i < n; i++) { float v = 0; for (int h = 1; h <= 20; h++) v += sinf(6.2831853f * 196.0f * (float)h * (float)i / FS) / (float)h; o[i] = v * 0.05f; } }
static void nodrop_tests(void)
{
    px_preset_t p; guitarish(x, N); { px_preset_t q; epreset(&q, 0, 0, 0); render(&q, x, z, N); } const float dry = rms1(z);
    preset(&p, 1, 100, 0); render(&p, x, y, N); CHECK(rms1(y) > dry * 0.95f, "RANGEMASTER VOLUME 100 is not quieter than the guitar (%.1f dB)", 20 * log10f(rms1(y) / dry));
    preset(&p, 1, 50, 0); render(&p, x, y, N); CHECK(fabsf(20 * log10f(rms1(y) / dry)) < 3.5f, "RANGEMASTER VOLUME 50 is about unity (%.1f dB; the owner found unity near 90 before)", 20 * log10f(rms1(y) / dry));
    for (int bd = 0; bd <= 100; bd += 50) { g_body = bd; preset(&p, 1, 50, 0); render(&p, x, y, N); CHECK(fabsf(20 * log10f(rms1(y) / dry)) < 3.5f, "RANGEMASTER at BODY %d keeps unity at VOLUME 50 (%.1f dB)", bd, 20 * log10f(rms1(y) / dry)); } g_body = 0;
    epreset(&p, 1, 0, 0); render(&p, x, y, N); CHECK(rms1(y) > dry * 0.9f, "EP BOOST at GAIN 0 is about unity (%.1f dB)", 20 * log10f(rms1(y) / dry));
    epreset(&p, 1, 0, 100); render(&p, x, y, N); CHECK(rms1(y) > dry * 0.9f, "EP BOOST at GAIN 0, FAT, is about unity (%.1f dB)", 20 * log10f(rms1(y) / dry));
    for (int k = 0; k < 5; k++) { const int lo[5] = {50, 100, 0, 100, 70}, pr[5] = {50, 100, 100, 50, 70}; bpreset(&p, 1, lo[k], pr[k]); render(&p, x, y, N);
        CHECK(rms1(y) > dry * 0.97f, "MAXIMIZER %d/%d is not quieter than the guitar (%.1f dB; there is no output control)", lo[k] / 10, pr[k] / 10, 20 * log10f(rms1(y) / dry)); }
}
int main(void)
{
    px_preset_t p, off; char b[8];
    CHECK(px_type_retired(PX_B_BST, PX_BST_RANGEMASTER) == 0 && !strcmp(px_type_name(PX_B_BST, PX_BST_RANGEMASTER), "RANGEMASTER"), "RANGEMASTER is a live type");
    CHECK(px_nparams(PX_B_BST, PX_BST_RANGEMASTER) == 3 && !strcmp(px_param_name(PX_B_BST, PX_BST_RANGEMASTER, 0), "VOLUME") && !strcmp(px_param_name(PX_B_BST, PX_BST_RANGEMASTER, 1), "PICKUP") && !strcmp(px_param_name(PX_B_BST, PX_BST_RANGEMASTER, 2), "BODY"), "VOLUME, PICKUP and BODY");
    CHECK(px_param_text(PX_B_BST, PX_BST_RANGEMASTER, 2, 0, b) == 1 && !strcmp(b, "TREBLE") && px_param_text(PX_B_BST, PX_BST_RANGEMASTER, 2, 100, b) == 1 && !strcmp(b, "FULL"), "BODY reads TREBLE .. FULL: '%s'", b);
    CHECK(px_param_text(PX_B_BST, PX_BST_RANGEMASTER, 1, 0, b) == 1 && !strcmp(b, "SINGLE") && px_param_text(PX_B_BST, PX_BST_RANGEMASTER, 1, 100, b) == 1 && !strcmp(b, "HUMBKR"), "PICKUP reads SINGLE .. HUMBKR: '%s'", b);
    /* silence, DC */
    preset(&p, 1, 100, 0); memset(x, 0, sizeof x); render(&p, x, y, N);
    { float m = 0; for (int i = N / 2; i < N; i++) m = fmaxf(m, fabsf(y[i])); CHECK(m < 1e-6f, "silence in, silence out (%.2g)", m); }
    sine(x, N, 1000.0f, 0.05f); render(&p, x, y, N);
    { double s = 0; for (int i = 44100; i < N; i++) s += y[i]; s /= 44100.0; CHECK(fabs(s) < 0.003, "no DC at 100 mV EMF (%.5f)", s); }
    /* polarity restored: at 1 kHz the full simulator's (inverting) output is -135 deg from the EMF, so the engine's is +45 deg, (the default circuit rate has no half-band delay) */
    { double xr = 0, xi = 0, yr = 0, yi = 0; for (int i = 44100; i < N; i++) { const double w = 6.283185307179586 * 1000.0 * i / FS; xr += x[i] * cos(w); xi -= x[i] * sin(w); yr += y[i] * cos(w); yi -= y[i] * sin(w); }
      double ph = (atan2(yi, yr) - atan2(xi, xr)) * 180.0 / 3.141592653589793 + (PXP_OS == 2 ? 19.0 * 1000.0 / 44100.0 * 360.0 : 0.0); while (ph > 180) ph -= 360; while (ph < -180) ph += 360;
      CHECK(fabs(ph - 45.0) < 25.0, "polarity restored: phase %.0f deg at 1 kHz after removing the half-band delay (reference +45)", ph); }
    /* small signal, single coil (6k, 2.5 H), volume max: MNA reference dB (circuit gain) - 20.35 */
    { const float f[] = {100, 300, 1000, 2600, 5000, 10000}, ref[] = {7.9f, 17.8f, 30.5f, 27.6f, 20.6f, 13.9f};
      for (int k = 0; k < 6; k++) { const float g = gain_db(f[k], 1.0f, 100, 0, 0, 0) + 9.74f;
        CHECK(fabsf(g - ref[k]) < 1.6f, "single coil, %.0f Hz: circuit gain %.1f dB against the reference %.1f dB", f[k], g, ref[k]); } }
    /* level behaviour at 1 kHz */
    { float h2a, h2b, h2c, h3c; const float ga = gain_db(1000, 3.0f, 100, 0, &h2a, 0), gb = gain_db(1000, 30.0f, 100, 0, &h2b, 0), gc = gain_db(1000, 300.0f, 100, 0, &h2c, &h3c);
      CHECK(h2a < -40.0f, "3 mV: clean (H2 %.1f dB)", h2a);
      CHECK(h2b > h2a + 10.0f && h2c > h2b + 10.0f, "the second harmonic grows with level: %.1f, %.1f, %.1f dB", h2a, h2b, h2c);
      CHECK(h2c > h3c + 3.0f, "even order dominates at 300 mV (H2 %.1f, H3 %.1f dB)", h2c, h3c);
      CHECK(ga - gc > 4.0f, "the stage compresses: gain %.1f dB at 3 mV, %.1f dB at 300 mV", ga, gc); }
    /* PICKUP: a humbucker's inductance and resistance against the 12 kOhm input */
    { const float s5 = gain_db(5000, 1.0f, 100, 0, 0, 0), h5 = gain_db(5000, 1.0f, 100, 100, 0, 0); CHECK(s5 - h5 > 4.0f, "a humbucker has less 5 kHz into the stage (single coil %.1f, humbucker %.1f dB)", s5, h5); }
    /* VOLUME: an attenuator after the clipping: unity at 50, +12 dB at 100 */
    { float h2a, h2b; const float g100 = gain_db(1000, 100.0f, 100, 0, &h2a, 0), g50 = gain_db(1000, 100.0f, 50, 0, &h2b, 0), g25 = gain_db(1000, 100.0f, 25, 0, 0, 0);
      CHECK(fabsf((g100 - g50) - 12.0f) < 1.5f, "VOLUME 50 is 12 dB below 100 (unity at 50, +12 dB at 100): %.1f dB", g100 - g50);
      CHECK(g50 - g25 > 14.0f, "below 50 the volume falls faster (50 -> 25: %.1f dB)", g50 - g25);
      CHECK(fabsf(h2a - h2b) < 1.0f, "the volume does not change the distortion (H2 %.1f / %.1f dB)", h2a, h2b); }
    /* BODY: the input capacitor C1 from 5 nF (the original treble booster) to 100 nF (full range) */
    { g_body = 0; const float lo0 = gain_db(100, 1.0f, 100, 0, 0, 0), hi0 = gain_db(3000, 1.0f, 100, 0, 0, 0); g_body = 100; const float lo1 = gain_db(100, 1.0f, 100, 0, 0, 0), hi1 = gain_db(3000, 1.0f, 100, 0, 0, 0); g_body = 50; const float lo5 = gain_db(100, 1.0f, 100, 0, 0, 0); g_body = 0;
      CHECK(lo1 - lo0 + pxp_rm_body_db(1.0f) > 12.0f, "BODY 100 opens the low end: 100 Hz is %.1f dB more than at BODY 0 (before the loudness makeup)", lo1 - lo0 + pxp_rm_body_db(1.0f));
      CHECK(lo5 + pxp_rm_body_db(0.5f) > lo0 + 3.0f && lo5 + pxp_rm_body_db(0.5f) < lo1 + pxp_rm_body_db(1.0f) - 3.0f, "BODY 50 is in between (100 Hz before makeup: %.1f, %.1f, %.1f dB)", lo0, lo5 + pxp_rm_body_db(0.5f), lo1 + pxp_rm_body_db(1.0f));
      CHECK(fabsf(hi1 - hi0 + pxp_rm_body_db(1.0f)) < 4.5f, "BODY leaves the treble: the circuit moves 3 kHz by %.1f dB (the output loudness makeup of %.1f dB removed)", hi1 - hi0 + pxp_rm_body_db(1.0f), pxp_rm_body_db(1.0f)); }
    /* the level at the pedal: guitar peaks 0.14 engine units (280 mV EMF) */
    sine(x, N, 440.0f, 0.14f); preset(&p, 1, 100, 0); render(&p, x, y, N);
    { float pk = 0; for (int i = 44100; i < N; i++) pk = fmaxf(pk, fabsf(y[i])); CHECK(pk > 0.05f && pk < 0.9f, "a hard-played note comes out at a usable level (peak %.3f; VOLUME 100 is +12 dB)", pk); }
    /* bounded, finite */
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f; preset(&p, 1, 100, 50); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 1.6f, "a full-scale square: finite and bounded (peak %.3f)", pk); }
    { unsigned r = 3u; for (int i = 0; i < N; i++) { r = r * 1664525u + 1013904223u; x[i] = ((float)(r >> 9) / 4194304.0f - 1.0f) * 0.5f; } render(&p, x, y, N);
      int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 1.6f, "loud noise: finite and bounded (peak %.3f)", pk); }
    /* no click on switching or a knob jump */
    sine(x, N, 220.0f, 0.1f);
    {
        px_preset_t pa, pb; preset(&pa, 1, 80, 20); preset(&pb, 0, 80, 20); render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "switching RANGEMASTER off and on mid-note: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
        preset(&pa, 1, 30, 0); preset(&pb, 1, 100, 100); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "a knob jump mid-note (VOLUME 30 -> 100, PICKUP 0 -> 100) glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    ep_tests();
    nodrop_tests();
    bbe_tests();
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("PEDALS ok: 0 problems\n"); return 0;
}
