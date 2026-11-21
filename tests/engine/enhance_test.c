/* SFX ENHANCE (engine/px_enhance.h): the NE570-inspired harmonic enhancer, through the whole engine.
 *  - catalogue: a live type with BETTER / LOUDER; LOUDER shows dB; APPLAUSE's tombstone is still skipped
 *  - unity: BETTER 0, LOUDER 0 is the dry signal; silence stays silence (denormal/DC state)
 *  - harmonics: even over odd, at the level of hard playing at the pedal (peak 0.14) the added second harmonic is audible (1..3 %), and it falls with the playing level (an amplitude law)
 *  - the fundamental is not changed (< 0.2 dB) at BETTER 100; LOUDER 100 is +12 dB on the clean signal
 *  - bounded and finite for a full-scale square and for input above full scale
 *  - switching the effect on/off or moving the knobs mid-note makes no click (the difference to the steady render stays small)
 *  - voicing A is the research prototype's maths bit for bit (kept as the reference); sensitivity scales the added harmonics by the virtual gain (LOW 2x .. EXTRA 7x) and
 *    never the clean signal; B/C overload smoothly (no hard clip: continuous slope, far less high-order content on a hot signal); C's input shaping measured (high-pass,
 *    presence, wider bandwidth); the input diagnostics read the guitar's peak, RMS and overload share
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
static void preset(px_preset_t *p, int on, int better, int louder)
{
    px_preset_factory(p, 7); for (int z = 0; z < PX_NBLK; z++) p->on[z] = 0;
    p->on[PX_B_SFX] = (uint8_t)on; p->type[PX_B_SFX] = PX_SFX_ENHANCE; p->p[PX_B_SFX][0] = (uint8_t)better; p->p[PX_B_SFX][1] = (uint8_t)louder;
}
static void render(const px_preset_t *p, const float *x, float *y, int n)
{
    static px_engine e; px_engine_init(&e, FS); px_engine_set(&e, p);
    for (int at = 0; at + 64 <= n; at += 64) px_engine_process(&e, x + at, y + at, 64);
}
static float mag(const float *y, int n, float hz)      /* DFT magnitude at hz over n samples (integer cycles chosen by the caller) */
{
    double re = 0, im = 0; for (int i = 0; i < n; i++) { const double w = 6.283185307179586 * hz * i / FS; re += y[i] * cos(w); im += y[i] * sin(w); }
    return (float)(sqrt(re * re + im * im) * 2.0 / n);
}
enum { N = 44100 * 2 }; static float x[N], y[N], d[N], z[N];

/* ---- ENHANCE v2: reference voicing, sensitivity, smooth overload, input shaping, diagnostics (direct on px_enhance.h) ---- */
typedef struct { float pre1, pre2, rect, slow, dc, post, mix, level, kpre, krect, kslow, kdc, kpost, kglide, a2, ripple, mix_target, level_target; } v1_t;   /* the research prototype (PXE_REF 3.5) */
static void v1_init(v1_t *s, float sr)
{
    memset(s, 0, sizeof *s);
    s->kpre = pxe_k(sr, 1.0f / (6.2831853f * 3500.f)); s->krect = pxe_k(sr, 0.0007f); s->kslow = pxe_k(sr, 0.025f); s->kdc = pxe_k(sr, 1.0f / (6.2831853f * 20.f));
    s->kpost = pxe_k(sr, 1.0f / (6.2831853f * 7000.f)); s->kglide = pxe_k(sr, 0.010f); s->level = s->level_target = 1.f; s->a2 = 0.068f; s->ripple = 0.04f;
}
static float v1_tick(v1_t *s, float x)
{
    s->mix += s->kglide * (s->mix_target - s->mix); s->level += s->kglide * (s->level_target - s->level);
    s->pre1 += s->kpre * (x - s->pre1); s->pre2 += s->kpre * (s->pre1 - s->pre2);
    float u = pxe_clamp(s->pre2 * 3.5f, -1.f, 1.f), a = fabsf(u);
    s->rect += s->krect * (a - s->rect); s->slow += s->kslow * (s->rect - s->slow);
    float wet = s->a2 * u * u + s->ripple * u * (s->rect - s->slow) / 0.318309886f;
    s->dc += s->kdc * (wet - s->dc); wet -= s->dc; s->post += s->kpost * (wet - s->post);
    return s->level * (x + s->mix * s->post * (1.0f / 3.5f));
}
static float wet_h2(int sens, int voice, float amp, float hz, int better)       /* second harmonic of a sine through the effect, percent of the fundamental */
{
    px_enhance e; pxe_init(&e, FS); pxe_set_input(&e, sens, voice); pxe_set(&e, (float)better, 0.0f);
    for (int i = 0; i < N; i++) y[i] = pxe_tick(&e, amp * sinf(6.2831853f * hz * (float)i / FS));
    return 100.0f * mag(y + 44100, 44100, 2.0f * hz) / mag(y + 44100, 44100, hz);
}
static float resp_db(int voice, float hz)                                  /* the sidechain's conditioning at hz (voices A/B share it) */
{
    px_enhance e; pxe_init(&e, FS); pxe_set_input(&e, 1, voice); double acc = 0; int n = 0;
    for (int i = 0; i < 88200; i++) { const float o = pxe_cond(&e, 0.1f * sinf(6.2831853f * hz * (float)i / FS)); if (i > 44100) { acc += (double)o * o; n++; } }
    return 20.0f * log10f(sqrtf((float)(acc / n)) / (0.1f / 1.41421356f));
}
static void v2_tests(void)
{
    /* A is the research prototype bit for bit */
    for (int cfg = 0; cfg < 3; cfg++) {
        const float better = cfg == 0 ? 35.f : cfg == 1 ? 100.f : 0.f, louder = cfg == 2 ? 50.f : 0.f;
        px_enhance e; pxe_init(&e, FS); pxe_set_input(&e, 1, 0); pxe_set(&e, better, louder);
        v1_t r; v1_init(&r, FS); r.mix_target = better * 0.01f; r.level_target = powf(10.f, 0.006f * louder);
        float worst = 0; unsigned rng = 12345u;
        for (int i = 0; i < 88200; i++) {
            rng = rng * 1664525u + 1013904223u; const float x = (i % 4000 < 2000 ? 0.45f : 0.06f) * sinf(6.2831853f * 196.0f * (float)i / FS) + 0.02f * ((float)(rng >> 9) / 4194304.0f - 1.0f);
            worst = fmaxf(worst, fabsf(pxe_tick(&e, x) - v1_tick(&r, x)));
        }
        CHECK(worst == 0.0f, "voicing A is the research prototype bit for bit (BETTER %.0f LOUDER %.0f: max difference %.3g)", better, louder, worst);
    }
    /* sensitivity: the added harmonics scale with the virtual gain (small signal, limiter idle); the clean signal and the fundamental do not move */
    { const float h[4] = {wet_h2(0, 1, 0.02f, 110.f, 100), wet_h2(1, 1, 0.02f, 110.f, 100), wet_h2(2, 1, 0.02f, 110.f, 100), wet_h2(3, 1, 0.02f, 110.f, 100)}; const float want[4] = {2.0f, 3.5f, 5.0f, 7.0f};
      for (int i = 1; i < 4; i++) { const float db = 20.0f * log10f(h[i] / h[0]), w = 20.0f * log10f(want[i] / want[0]); CHECK(fabsf(db - w) < 0.4f, "sensitivity %d: H2 %.1f dB over LOW, the gain ratio says %.1f dB", i, db, w); }
      printf("sensitivity H2 at amp 0.02: LOW %.3f NORMAL %.3f HIGH %.3f EXTRA %.3f %%\n", h[0], h[1], h[2], h[3]);
      px_enhance e; pxe_init(&e, FS); pxe_set_input(&e, 3, 1); pxe_set(&e, 0.f, 0.f); float m = 0; for (int i = 0; i < 20000; i++) { const float x = 0.3f * sinf(0.05f * (float)i); m = fmaxf(m, fabsf(pxe_tick(&e, x) - x)); }
      CHECK(m == 0.0f, "BETTER 0 at EXTRA sensitivity is still the dry signal (%.2g)", m); }
    /* B/C: the overload is smooth, A's is a hard clip */
    { const float L = PXE_KNEE; float worst2 = 0, slope_lo = 1.0f, slope_hi = 0.0f;
      for (float v = -2.5f; v < 2.5f; v += 0.001f) { const float d2 = fabsf(pxe_soft(v + 0.001f) - 2.0f * pxe_soft(v) + pxe_soft(v - 0.001f)); worst2 = fmaxf(worst2, d2); }
      slope_lo = (pxe_soft(0.001f) - pxe_soft(-0.001f)) / 0.002f; slope_hi = (pxe_soft(L + 0.01f) - pxe_soft(L - 0.01f)) / 0.02f;
      CHECK(worst2 < 3e-5f && fabsf(slope_lo - 1.0f) < 1e-3f && fabsf(slope_hi) < 0.02f, "B/C limiter: no kink (largest second difference %.2g), unity slope at 0 (%.4f), zero slope where it saturates (%.4f)", worst2, slope_lo, slope_hi);
      CHECK(pxe_soft(10.0f) == L * (2.0f / 3.0f) && pxe_soft(-10.0f) == -L * (2.0f / 3.0f) && fabsf(pxe_soft(0.49f) - 0.49f) < 0.02f, "B/C limiter: bounded at %.2f, nearly the identity at the 0.5 reference (%.3f)", L * (2.0f / 3.0f), pxe_soft(0.49f)); }
    { /* a hot sine: A's hard clip makes more high-order content in the added signal than B's soft limiter */
      float hi[2];
      for (int vo = 0; vo < 2; vo++) {
          px_enhance e; pxe_init(&e, FS); pxe_set_input(&e, 1, vo); pxe_set(&e, 100.f, 0.f);
          for (int i = 0; i < N; i++) { const float x = 0.6f * sinf(6.2831853f * 110.0f * (float)i / FS); y[i] = pxe_tick(&e, x) - x; }
          double acc = 0; for (int k = 7; k <= 15; k++) { const float m = mag(y + 44100, 44100, 110.0f * (float)k); acc += (double)m * m; } hi[vo] = (float)sqrt(acc) / mag(y + 44100, 44100, 220.0f);
      }
      CHECK(hi[1] < 0.8f * hi[0], "a hot sine (virtual input 2.1): the soft limiter makes less high-order content than the hard clip (H7..H15 over H2: B %.4f against A %.4f)", hi[1], hi[0]); }
    /* C: the conditioning, measured */
    { const float hp40 = resp_db(2, 40.f) - resp_db(1, 40.f), hp82 = resp_db(2, 82.f) - resp_db(1, 82.f), pres = resp_db(2, 2000.f) - resp_db(2, 330.f), bw = resp_db(2, 6000.f) - resp_db(1, 6000.f);
      printf("voicing C against B: 40 Hz %.1f dB, 82 Hz %.1f dB, 6 kHz %+.1f dB; inside C, 2 kHz against 330 Hz %+.1f dB\n", hp40, hp82, bw, pres);
      CHECK(hp40 < -4.0f && hp82 > -2.6f && hp82 < -1.2f, "C's high-pass: sub-bass down (%.1f dB at 40 Hz), the low E kept (%.1f dB at 82 Hz)", hp40, hp82);
      CHECK(pres > 2.5f && pres < 5.5f, "C's presence emphasis: %+.1f dB at 2 kHz against 330 Hz (about the %.0f dB asked for)", pres, (double)PXE_PRES_DB);
      CHECK(bw > 6.0f, "C keeps more pick bandwidth than B/A (%+.1f dB at 6 kHz)", bw);
      CHECK(fabsf(resp_db(0, 1000.f) - resp_db(1, 1000.f)) < 1e-6f, "A and B share the prototype's input conditioning"); }
    /* diagnostics */
    { px_enhance e; pxe_init(&e, FS); pxe_set_input(&e, 1, 1); float pk, rms, over;
      for (int i = 0; i < 88200; i++) pxe_tick(&e, 0.2f * sinf(6.2831853f * 220.0f * (float)i / FS));
      pxe_diag(&e, &pk, &rms, &over);
      CHECK(fabsf(pk - (-13.98f)) < 0.3f && fabsf(rms - (-17.0f)) < 0.3f && over > 0.42f && over < 0.56f, "diagnostics for a 0.2 sine: peak %.1f dBFS (-14.0), RMS %.1f dBFS (-17.0), cell above 0.5 for %.2f of the time (0.50)", pk, rms, over);
      for (int i = 0; i < 88200; i++) pxe_tick(&e, 0.05f * sinf(6.2831853f * 220.0f * (float)i / FS));
      pxe_diag(&e, &pk, &rms, &over); CHECK(over < 0.02f, "a quiet guitar (0.05) never drives the cell above 0.5 at NORMAL sensitivity (%.3f)", over); }
}

#ifdef PXE_DIAG
/* the diagnostic build's SENS and VOICE knobs reach the effect through the engine, and read in words */
static void diag_tests(void)
{
    char b[8]; px_preset_t p;
    CHECK(px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 2, 0, b) == 1 && !strcmp(b, "LOW") && px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 2, 50, b) == 1 && !strcmp(b, "NORMAL") && px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 2, 70, b) == 1 && !strcmp(b, "HIGH") && px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 2, 100, b) == 1 && !strcmp(b, "EXTRA"), "SENS reads LOW / NORMAL / HIGH / EXTRA");
    CHECK(px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 3, 0, b) == 1 && !strcmp(b, "A REF") && px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 3, 50, b) == 1 && !strcmp(b, "B SOFT") && px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 3, 100, b) == 1 && !strcmp(b, "C SHAPE"), "VOICE reads A REF / B SOFT / C SHAPE");
    float h2[4];
    for (int c = 0; c < 4; c++) {
        preset(&p, 1, 100, 0); p.p[PX_B_SFX][2] = (uint8_t)(c == 0 ? 0 : c == 1 ? 50 : c == 2 ? 70 : 100); p.p[PX_B_SFX][3] = 50;
        for (int i = 0; i < N; i++) x[i] = 0.05f * sinf(6.2831853f * 110.0f * (float)i / FS);
        render(&p, x, y, N); h2[c] = 100.0f * mag(y + 44100, 44100, 220.0f) / mag(y + 44100, 44100, 110.0f);
    }
    CHECK(h2[0] < h2[1] && h2[1] < h2[2] && h2[2] < h2[3], "SENS through the engine: H2 rises LOW %.3f < NORMAL %.3f < HIGH %.3f < EXTRA %.3f %%", h2[0], h2[1], h2[2], h2[3]);
    float lo[3];
    for (int v = 0; v < 3; v++) {
        preset(&p, 1, 100, 0); p.p[PX_B_SFX][2] = 50; p.p[PX_B_SFX][3] = (uint8_t)(v == 0 ? 0 : v == 1 ? 50 : 100);
        for (int i = 0; i < N; i++) x[i] = 0.14f * sinf(6.2831853f * 82.4f * (float)i / FS);
        render(&p, x, y, N); lo[v] = 100.0f * mag(y + 44100, 44100, 164.8f) / mag(y + 44100, 44100, 82.4f);
    }
    printf("low E (82.4 Hz, peak 0.14) H2 through the engine: A %.3f  B %.3f  C %.3f %%\n", lo[0], lo[1], lo[2]);
    CHECK(lo[2] < lo[1] && lo[2] > 0.5f * lo[1], "C's high-pass costs the low E some second harmonic but keeps most of it (B %.3f, C %.3f %%)", lo[1], lo[2]);
}
#endif
int main(void)
{
    px_preset_t p, off;
    /* catalogue */
    CHECK(px_type_retired(PX_B_SFX, PX_SFX_ENHANCE) == 0, "ENHANCE is a live type");
    CHECK(strcmp(px_type_name(PX_B_SFX, PX_SFX_ENHANCE), "ENHANCE") == 0, "its name is ENHANCE");
#ifdef PXE_DIAG
    CHECK(px_nparams(PX_B_SFX, PX_SFX_ENHANCE) == 4 && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 0), "BETTER") && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 2), "SENS") && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 3), "VOICE"), "diagnostic build: BETTER, LOUDER, SENS, VOICE");
#else
    CHECK(px_nparams(PX_B_SFX, PX_SFX_ENHANCE) == 2 && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 0), "BETTER") && !strcmp(px_param_name(PX_B_SFX, PX_SFX_ENHANCE, 1), "LOUDER"), "two controls: BETTER, LOUDER");
#endif
    { char b[8]; CHECK(px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 1, 100, b) == 1 && !strcmp(b, "+12"), "LOUDER 100 reads +12 (dB): '%s'", b); CHECK(px_param_text(PX_B_SFX, PX_SFX_ENHANCE, 1, 0, b) == 1 && !strcmp(b, "0"), "LOUDER 0 reads 0: '%s'", b); }
    CHECK(px_nlive(PX_B_SFX) == PX_SFX_COUNT - 1 && px_type_retired(PX_B_SFX, PX_SFX_APPLAUSE) == 1, "APPLAUSE is still the one tombstone");
    /* unity and silence */
    for (int i = 0; i < N; i++) x[i] = 0.14f * sinf(6.2831853f * 110.0f * (float)i / FS);
    preset(&p, 1, 0, 0); off = p; off.on[PX_B_SFX] = 0; render(&p, x, y, N); render(&off, x, z, N);
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "BETTER 0, LOUDER 0 is the dry signal (max difference %.2g)", m); }
    preset(&p, 1, 100, 0); memset(x, 0, sizeof x); render(&p, x, y, N);
    { float m = 0; for (int i = N / 2; i < N; i++) m = fmaxf(m, fabsf(y[i])); CHECK(m < 1e-7f, "silence in, silence out (%.2g)", m); }
    /* harmonics and the fundamental at the level of hard playing at the pedal (0.14) and 20 dB below it */
    const float amp[3] = {0.14f, 0.014f, 0.5f}; float h2[3], h3[3], fund[3];
    for (int a = 0; a < 3; a++) {
        for (int i = 0; i < N; i++) x[i] = amp[a] * sinf(6.2831853f * 110.0f * (float)i / FS);
        preset(&p, 1, 100, 0); render(&p, x, y, N); const float *t = y + 44100; const int n = 44100;     /* the last second, whole cycles */
        fund[a] = mag(t, n, 110.0f); h2[a] = 100.0f * mag(t, n, 220.0f) / fund[a]; h3[a] = 100.0f * mag(t, n, 330.0f) / fund[a];
        printf("amp %.3f: H2 %.3f %%  H3 %.3f %%  fundamental change %.3f dB\n", amp[a], h2[a], h3[a], 20.0f * log10f(fund[a] / amp[a]));
        CHECK(fabsf(20.0f * log10f(fund[a] / amp[a])) < 0.2f, "the fundamental is not changed at BETTER 100 (amp %.3f)", amp[a]);
        CHECK(h2[a] > h3[a], "even harmonics lead (amp %.3f: H2 %.3f H3 %.3f)", amp[a], h2[a], h3[a]);
    }
    CHECK(h2[0] > 1.0f && h2[0] < 3.0f, "hard playing at the pedal (peak 0.14) adds 1..3 %% second harmonic at BETTER 100 (%.3f %%)", h2[0]);
    CHECK(h2[1] < h2[0] / 7.0f && h2[1] > h2[0] / 14.0f, "20 dB less playing level gives about 10 times less H2 (%.3f %% against %.3f %%)", h2[1], h2[0]);
    /* LOUDER */
    for (int i = 0; i < N; i++) x[i] = 0.1f * sinf(6.2831853f * 110.0f * (float)i / FS);
    preset(&p, 1, 0, 100); render(&p, x, y, N);
    { const float db = 20.0f * log10f(mag(y + 44100, 44100, 110.0f) / 0.1f); CHECK(fabsf(db - 12.0f) < 0.05f, "LOUDER 100 is +12 dB on a clean signal (%.3f dB)", db); }
    /* bounded and finite: a full-scale square, and input above full scale */
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f;
    preset(&p, 1, 100, 100); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 4.2f, "a full-scale square at BETTER 100, LOUDER 100 stays finite and bounded (peak %.2f; dry x +12 dB = 3.98)", pk); }
    for (int i = 0; i < N; i++) x[i] = 3.0f * sinf(6.2831853f * 82.0f * (float)i / FS);
    preset(&p, 1, 100, 0); render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 3.6f, "input above full scale: finite, the added part stays small (peak %.2f)", pk); }
    /* no click on switching or on a knob move: the largest sample-to-sample step of the render with the change must stay within a few percent of the steady renders' (their own slope) */
    for (int i = 0; i < N; i++) x[i] = 0.3f * sinf(6.2831853f * 220.0f * (float)i / FS);
    {
        static float ya[N], yb[N], yc[N];
        px_preset_t pa, pb; preset(&pa, 1, 100, 50); preset(&pb, 0, 100, 50);
        render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        const float ref = fmaxf(sa, sb);
        CHECK(worst < 1.15f * ref, "switching ENHANCE off and on mid-note: no click (largest step %.4f, steady renders %.4f / %.4f)", worst, sa, sb);
        preset(&pa, 1, 10, 0); preset(&pb, 1, 100, 100); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.15f * fmaxf(sa, sb), "a knob jump mid-note (BETTER 10 -> 100, LOUDER 0 -> +12 dB) glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    /* a stored retired APPLAUSE and an unknown type id stay bypassed (the new type did not shift anything) */
    preset(&p, 1, 50, 50); p.type[PX_B_SFX] = PX_SFX_APPLAUSE; off = p; off.on[PX_B_SFX] = 0;
    for (int i = 0; i < N; i++) x[i] = 0.2f * sinf(0.05f * (float)i); render(&p, x, y, N); render(&off, x, z, N);
    { float m = 0; for (int i = 0; i < N; i++) m = fmaxf(m, fabsf(y[i] - z[i])); CHECK(m < 1e-6f, "a stored APPLAUSE is still bypassed (%.2g)", m); }
    v2_tests();
#ifdef PXE_DIAG
    diag_tests();
#endif
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("ENHANCE ok: 0 problems\n"); return 0;
}
