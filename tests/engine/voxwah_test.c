/* BST (PRE / BOOST, v75): the block itself and its VOX FIXED WAH type (engine/px_pedals.h: pxp_vw_*).
 *  - the catalogue: ten blocks, BST last in the enum and right after the gate in the default order, four types, the Vox wah's three controls
 *  - px_preset_canonical: RANGEMASTER / EP BOOST / MAXIMIZER stored in SFX move to BST (once, with their settings), never over a BST that is on
 *  - the tone curve (the ElectroSmash V847 analysis): the peak sits at about 450 Hz at POSITION 0, about 750 Hz at mid travel and about 1.6 kHz at 100,
 *    stands well above the skirts, a larger PEAK is narrower and higher; the low end below the peak is cut
 *  - LEVEL: 0 is the same loudness as the guitar, 100 is 12 dB more
 *  - bounded and finite, no click on switching or on a knob jump (POSITION 0 -> 100 mid-note) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 2 };
static float x[N], y[N], ya[N], yb[N], yc[N];
static void wah(px_preset_t *p, int on, int pos, int peak, int level)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_BST] = (uint8_t)on; p->type[PX_B_BST] = PX_BST_VOXWAH; p->p[PX_B_BST][0] = (uint8_t)pos; p->p[PX_B_BST][1] = (uint8_t)peak; p->p[PX_B_BST][2] = (uint8_t)level;
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
static float gain_db(const px_preset_t *p, float hz)                 /* a sine through the engine: the gain over the last second, in dB */
{
    sine(x, 44100, hz, 0.05f); render(p, x, y, 44100);
    return 20.0f * log10f(mag(y + 22050, 22050, hz) / 0.05f + 1e-9f);
}
static float rms(const float *s, int a, int b) { double e = 0; for (int i = a; i < b; i++) e += (double)s[i] * s[i]; return (float)sqrt(e / (b - a)); }
static float peak_hz(const px_preset_t *p, float *pk_db, float *at100, float *at5k)
{
    float best = -99.0f, bh = 0;
    for (float hz = 200.0f; hz <= 3000.0f; hz *= 1.03f) { const float g = gain_db(p, hz); if (g > best) { best = g; bh = hz; } }
    if (pk_db) *pk_db = best; if (at100) *at100 = gain_db(p, 100.0f); if (at5k) *at5k = gain_db(p, 5000.0f);
    return bh;
}

int main(void)
{
    px_preset_t p, q;
    char b[16];
    /* ---- the catalogue ---- */
    CHECK(PX_NBLK == 10 && PX_B_BST == 9, "ten blocks, BST the last (%d, %d)", PX_NBLK, PX_B_BST);
    CHECK(!strcmp(px_block_label(PX_B_BST), "BST") && !strcmp(px_block_name(PX_B_BST), "PRE / BOOST"), "BST is named PRE / BOOST");
    CHECK(px_ntypes(PX_B_BST) == 7 && px_nlive(PX_B_BST) == 7, "seven live types");
    CHECK(!strcmp(px_type_name(PX_B_BST, PX_BST_VOXWAH), "FIXED WAH") && px_nparams(PX_B_BST, PX_BST_VOXWAH) == 3 &&
          !strcmp(px_param_name(PX_B_BST, PX_BST_VOXWAH, 0), "POSITION") && !strcmp(px_param_name(PX_B_BST, PX_BST_VOXWAH, 1), "PEAK") && !strcmp(px_param_name(PX_B_BST, PX_BST_VOXWAH, 2), "LEVEL"), "VOX FIXED WAH: POSITION, PEAK, LEVEL");
    CHECK(px_param_text(PX_B_BST, PX_BST_VOXWAH, 0, 0, b) == 1 && !strcmp(b, "450") && px_param_text(PX_B_BST, PX_BST_VOXWAH, 0, 100, b) == 1 && !strcmp(b, "1600"), "POSITION reads the peak's frequency: 450 .. 1600 Hz ('%s')", b);
    CHECK(px_param_text(PX_B_BST, PX_BST_VOXWAH, 0, 50, b) == 1 && atoi(b) >= 740 && atoi(b) <= 770, "mid travel is about 750 Hz ('%s')", b);
    CHECK(px_param_text(PX_B_BST, PX_BST_VOXWAH, 2, 100, b) == 1 && !strcmp(b, "+12"), "LEVEL 100 reads +12");
    { uint8_t o[PX_NBLK]; px_order_default(o); CHECK(px_order_valid(o) && o[0] == PX_B_GATE && o[1] == PX_B_BST, "the default order: the gate, then BST"); }
    px_preset_factory(&p, 0); CHECK(p.on[PX_B_BST] == 0 && px_order_valid(p.order), "a factory preset: BST off");
    for (int t = 0; t < PX_BST_COUNT; t++) CHECK(!px_type_retired(PX_B_BST, t), "BST type %d is live", t);
    for (int t = PX_SFX_ENHANCE; t <= PX_SFX_MAXIMIZER; t++) CHECK(px_type_retired(PX_B_SFX, t) && !strcmp(px_type_name(PX_B_SFX, t), ""), "SFX type %d is a tombstone now", t);

    /* ---- px_preset_canonical ---- */
    for (int t = 0; t < 4; t++) {
        const int sfx = t == 3 ? PX_SFX_ENHANCE : PX_SFX_RANGEMASTER + t;   /* RANGEMASTER, EP BOOST, MAXIMIZER, then ENHANCE */
        px_preset_base(&p); p.on[PX_B_SFX] = 1; p.type[PX_B_SFX] = (uint8_t)sfx; for (int k = 0; k < 5; k++) p.p[PX_B_SFX][k] = (uint8_t)(11 + k);
        px_preset_canonical(&p);
        CHECK(p.on[PX_B_BST] == 1 && p.type[PX_B_BST] == (t == 3 ? PX_BST_ENHANCE : PX_BST_RANGEMASTER + t) && p.p[PX_B_BST][0] == 11 && p.p[PX_B_BST][2] == 13, "SFX type %d moves to BST with its settings", sfx);
        CHECK(p.on[PX_B_SFX] == 0 && p.type[PX_B_SFX] == PX_SFX_AUTOWAH, "... and SFX is an envelope wah, off");
        q = p; px_preset_canonical(&q); CHECK(!memcmp(&p, &q, sizeof p), "canonical twice: nothing changes");
    }
    px_preset_base(&p); p.on[PX_B_SFX] = 1; p.type[PX_B_SFX] = PX_SFX_EPBOOST; p.on[PX_B_BST] = 1; p.type[PX_B_BST] = PX_BST_VOXWAH; q = p; px_preset_canonical(&q);
    { px_preset_t w = p; w.on[PX_B_SFX] = 0; w.type[PX_B_SFX] = PX_SFX_AUTOWAH;      /* v75: a retired type is never kept: the block is the first live type, off, its settings as they were */
      CHECK(!memcmp(&w, &q, sizeof w), "with BST already on, an old SFX pedal becomes the live first type, off (settings kept) and BST is not overwritten"); }

    /* ---- the tone curve ---- */
    {
        static const struct { int pos; float hz; } W[3] = {{0, 450.0f}, {50, 757.0f}, {100, 1600.0f}};
        float height[3];
        for (int i = 0; i < 3; i++) {
            float pk, lo, hi; wah(&p, 1, W[i].pos, 50, 0);
            const float hz = peak_hz(&p, &pk, &lo, &hi); height[i] = pk;
            CHECK(fabsf(hz / W[i].hz - 1.0f) < 0.08f, "POSITION %d: the peak is at %.0f Hz (expected about %.0f)", W[i].pos, hz, W[i].hz);
            CHECK(pk - hi > 6.0f && pk - lo > 6.0f, "POSITION %d: the peak stands above the skirts (%.1f dB; 100 Hz %.1f, 5 kHz %.1f)", W[i].pos, pk, lo, hi);
            CHECK(pk < 20.0f, "POSITION %d: a peak of %.1f dB is not above the V847's 18 dB", W[i].pos, pk);
        }
        float pk0, pk1; wah(&p, 1, 50, 0, 0); peak_hz(&p, &pk0, 0, 0); wah(&p, 1, 50, 100, 0); peak_hz(&p, &pk1, 0, 0);
        CHECK(pk1 > pk0 + 3.0f, "a larger PEAK is a higher peak (%.1f -> %.1f dB)", pk0, pk1);
        { wah(&p, 1, 50, 50, 0); float lo = gain_db(&p, 60.0f), mid = gain_db(&p, 757.0f); CHECK(mid - lo > 12.0f, "the low end is cut: 60 Hz is %.1f dB under the peak", mid - lo); }
    }

    /* ---- LEVEL ---- */
    {
        unsigned r = 5u; float lp = 0;                                   /* guitar-like: noise through a one-pole low-pass at about 1.5 kHz */
        for (int i = 0; i < N; i++) { r = r * 1664525u + 1013904223u; const float n = ((float)(r >> 9) / 4194304.0f - 1.0f); lp += 0.19f * (n - lp); x[i] = lp * 0.6f; }
        px_preset_t off; wah(&off, 0, 50, 50, 0); render(&off, x, ya, N);
        wah(&p, 1, 50, 50, 0); render(&p, x, yb, N);
        wah(&q, 1, 50, 50, 100); render(&q, x, yc, N);
        const float d0 = 20.0f * log10f(rms(yb, 44100, N) / rms(ya, 44100, N)), d1 = 20.0f * log10f(rms(yc, 44100, N) / rms(yb, 44100, N));
        CHECK(fabsf(d0) < 4.0f, "LEVEL 0 keeps the guitar's loudness (%.1f dB on low-passed noise; the makeup is calibrated on guitar loops, tests/pedal_levels.mjs)", d0);
        CHECK(fabsf(d1 - 12.0f) < 0.6f, "LEVEL 100 is 12 dB over LEVEL 0 (%.2f dB)", d1);
    }

    /* ---- bounded, finite ---- */
    wah(&p, 1, 100, 100, 100);
    for (int i = 0; i < N; i++) x[i] = ((i / 200) & 1) ? 1.0f : -1.0f;
    render(&p, x, y, N);
    { int fin = 1; float pk = 0; for (int i = 0; i < N; i++) { if (!isfinite(y[i])) fin = 0; pk = fmaxf(pk, fabsf(y[i])); } CHECK(fin && pk < 6.0f, "a full-scale square at the loudest setting: finite and bounded (peak %.3f)", pk); }

    /* ---- no click: switching on and off, and a POSITION jump mid-note ---- */
    sine(x, N, 440.0f, 0.1f);
    {
        px_preset_t pa, pb; wah(&pa, 1, 50, 50, 30); wah(&pb, 0, 50, 50, 30); render(&pa, x, ya, N); render(&pb, x, yb, N);
        float sa = 0, sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine e; px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100 + 64 * 7) px_engine_set(&e, &pb); if (at == 44100 + 64 * 400) px_engine_set(&e, &pa); px_engine_process(&e, x + at, yc + at, 64); }
        float worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "switching VOX FIXED WAH off and on mid-note: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
        wah(&pa, 1, 0, 50, 30); wah(&pb, 1, 100, 50, 30); render(&pa, x, ya, N); render(&pb, x, yb, N);
        sa = sb = 0; for (int i = 44100; i < N; i++) { sa = fmaxf(sa, fabsf(ya[i] - ya[i - 1])); sb = fmaxf(sb, fabsf(yb[i] - yb[i - 1])); }
        px_engine_init(&e, FS); px_engine_set(&e, &pa);
        for (int at = 0; at + 64 <= N; at += 64) { if (at == 44100) px_engine_set(&e, &pb); px_engine_process(&e, x + at, yc + at, 64); }
        worst = 0; for (int i = 44100; i < N; i++) worst = fmaxf(worst, fabsf(yc[i] - yc[i - 1]));
        CHECK(worst < 1.5f * fmaxf(sa, sb), "a POSITION jump 0 -> 100 mid-note glides: no click (largest step %.4f, steady %.4f / %.4f)", worst, sa, sb);
    }
    /* Q: the peak's -3 dB Q follows the position as in a real wah (Q ~ 1/fc; Vox-style wahs measure 2.3..3.3 at the toe, 8..11 at the heel), read off the
     * designed filter's own response (the high-pass is far below the peak) */
    {
        const struct { float pos, peak, q; } W[] = {{1.0f, 0.0f, 2.4f}, {1.0f, 1.0f, 3.3f}, {0.5f, 0.5f, 6.05f}, {0.0f, 0.0f, 8.53f}, {0.0f, 1.0f, 11.73f}};
        for (unsigned k = 0; k < sizeof W / sizeof W[0]; k++) {
            px_vw v; pxp_vw_init(&v, FS); v.pos = W[k].pos; v.peak = W[k].peak; pxp_vw_design(&v);
            const float fc = pxp_vw_hz(W[k].pos); float pk = -1e9f, f0 = 0, lo = 0, hi = 0;
            float mag[4000];
            for (int i = 0; i < 4000; i++) {                               /* 50 Hz .. 4 kHz, linear */
                const float f = 50.0f + (float)i, w = 6.2831853f * f / FS, c1 = cosf(w), s1 = sinf(w), c2 = cosf(2 * w), s2 = sinf(2 * w);
                const float nr = v.b0 + v.b1 * c1 + v.b2 * c2, ni = -(v.b1 * s1 + v.b2 * s2), dr = 1 + v.a1 * c1 + v.a2 * c2, di = -(v.a1 * s1 + v.a2 * s2);
                mag[i] = 10.0f * log10f((nr * nr + ni * ni) / (dr * dr + di * di));
                if (mag[i] > pk) { pk = mag[i]; f0 = f; }
            }
            for (int i = 0; i < 4000; i++) if (mag[i] >= pk - 3.0f) { if (!lo) lo = 50.0f + (float)i; hi = 50.0f + (float)i; }
            const float q = f0 / (hi - lo + 1.0f);
            CHECK(fabsf(f0 - fc) < 0.04f * fc && fabsf(q - W[k].q) < 0.12f * W[k].q, "wah Q: POSITION %.0f PEAK %.0f: peak %.0f Hz (want %.0f), -3 dB Q %.2f (want %.2f)", W[k].pos * 100, W[k].peak * 100, f0, fc, q, W[k].q);
        }
    }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("VOX FIXED WAH / BST ok: 0 problems\n"); return 0;
}
