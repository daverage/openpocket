/* Synth2 stage 1: the protected effects, hashed bit for bit.
 *   POLY OCTAVE HQ (PX_PIT_POLYOCT_HQ, the OCTAVE block) and POLY OCT FUZZ (PX_SYN_OCT_FUZZ, the SYNTH block)
 *   P  each effect alone: 33 settings (8 corner/mixed sets + each control swept 0..100 with the others at 50)
 *      x 11 signals (6 synthetic: low E, bass E1, bass B0, open E chord, power chord + dyad, picked transients;
 *      5 bundled guitar loops assets/loops/*.wav, mono PCM16) x block of 64 frames; the first 3 signals also at 48, 16, 1
 *   Q  each effect in a full chain: the factory presets with the other blocks on, wet lanes on and off, a random
 *      order, knobs turned and blocks switched while playing (the glides and fades)
 * Usage: protected [wavdir]. Two engines give the same output for these effects if and only if the lists are identical:
 * tests/protected.sh REF builds this file against REF's engine and this tree's and compares the lists. Built as the pedal
 * builds the engine (-fno-builtin -ffp-contract=off, firmware/px_math.c as the libm). Only type ids that exist in both
 * trees are used: types of the SYNTH block other than OCT FUZZ are never selected (they are being retired). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "px_dsp.c"
static px_engine e;
#define NSYN 66150                                     /* 1.5 s */
#define NSIG 11
static float sig[NSIG][NSYN];
static uint32_t rs = 4242u;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static uint64_t H(uint64_t h, const float *o, int n)
{
    for (int i = 0; i < n; i++) { uint32_t u; memcpy(&u, &o[i], 4); h = (h ^ u) * 1099511628211ull; }
    return h;
}
static float saw(float *ph, float f) { *ph += f / 44100.0f; if (*ph >= 1.0f) *ph -= 1.0f; return 2.0f * *ph - 1.0f; }
static void chord(float *o, const float *hz, int n, float amp, float decay, int strum)
{
    float ph[8] = {0}, env[8]; int started[8] = {0};
    for (int k = 0; k < n; k++) env[k] = 0;
    for (int i = 0; i < NSYN; i++) {
        float v = 0;
        for (int k = 0; k < n; k++) {
            if (i == k * strum) { started[k] = 1; env[k] = amp; }
            if (started[k]) { v += env[k] * (saw(&ph[k], hz[k]) + 0.4f * sinf(6.2831853f * ph[k] * 2.0f)); env[k] *= decay; }
        }
        o[i] = v * 0.35f;
    }
}
static float *wav16(const char *fn, int *n)
{
    FILE *f = fopen(fn, "rb"); if (!f) return 0;
    uint8_t h[12]; if (fread(h, 1, 12, f) != 12) { fclose(f); return 0; }
    for (;;) {
        uint8_t ck[8]; if (fread(ck, 1, 8, f) != 8) { fclose(f); return 0; }
        const uint32_t sz = ck[4] | ck[5] << 8 | ck[6] << 16 | (uint32_t)ck[7] << 24;
        if (memcmp(ck, "data", 4) == 0) {
            int16_t *s = malloc(sz); float *x = malloc(sizeof(float) * (sz / 2 + 1));
            if (fread(s, 1, sz, f) != sz) { fclose(f); return 0; }
            for (uint32_t i = 0; i < sz / 2; i++) x[i] = s[i] / 32768.0f;
            free(s); fclose(f); *n = (int)(sz / 2); return x;
        }
        fseek(f, (long)sz + (sz & 1), SEEK_CUR);
    }
}
static void mksigs(const char *dir)
{
    static const float LOWE[1] = {82.41f}, E1[1] = {41.2f}, B0[1] = {30.87f};
    static const float OPENE[6] = {82.41f, 123.47f, 164.81f, 207.65f, 246.94f, 329.63f};
    static const float POW[3] = {110.0f, 164.81f, 220.0f};
    chord(sig[0], LOWE, 1, 1.0f, 0.99996f, 0);
    chord(sig[1], E1, 1, 1.0f, 0.99997f, 0);
    chord(sig[2], B0, 1, 1.0f, 0.99997f, 0);
    chord(sig[3], OPENE, 6, 0.7f, 0.99995f, 900);
    chord(sig[4], POW, 3, 0.8f, 0.99996f, 0);
    {   /* two dyads and picked transients: noise bursts at the pick, a short pluck, silence between */
        float ph = 0, ph2 = 0, env = 0;
        for (int i = 0; i < NSYN; i++) {
            if (i % 11025 == 0) env = 1.0f;
            const float burst = (i % 11025) < 200 ? 0.5f * ((float)(rnd() & 65535) / 32768.0f - 1.0f) : 0.0f;
            sig[5][i] = (i > 40000 && i < 48000) ? 0.0f : 0.4f * env * (saw(&ph, 196.0f) + 0.7f * saw(&ph2, 293.66f)) + burst;
            env *= 0.9992f;
        }
    }
    static const char *const NM[5] = {"arpeggio", "chords", "chug", "lead", "riff"};
    for (int k = 0; k < 5; k++) {
        char fn[512]; snprintf(fn, sizeof fn, "%s/%s.wav", dir, NM[k]);
        int n; float *x = wav16(fn, &n);
        if (!x) { fprintf(stderr, "protected: cannot read %s\n", fn); exit(2); }
        for (int i = 0; i < NSYN; i++) sig[6 + k][i] = (i + 22050 < n) ? x[i + 22050] : 0.0f;
        free(x);
    }
}
static uint64_t run(const px_preset_t *p, const float *in, int bs)
{
    uint64_t h = 1469598103934665603ull;
    px_engine_init(&e, 44100.0f); px_engine_set(&e, p);
    float out[64];
    const int frames = bs == 1 ? 4000 : NSYN;
    for (int at = 0; at + bs <= frames; at += bs) { px_engine_process(&e, in + at, out, bs); h = H(h, out, bs); }
    return h;
}
static void isolate(px_preset_t *p, int blk, int type, const uint8_t *v)
{
    px_preset_factory(p, 7);
    for (int z = 0; z < PX_NBLK; z++) p->on[z] = (z == blk);
    p->type[blk] = (uint8_t)type; memcpy(p->p[blk], v, 5);
}
int main(int argc, char **argv)
{
    mksigs(argc > 1 ? argv[1] : "assets/loops");
    static const uint8_t V[8][5] = {{0, 0, 0, 0, 0}, {100, 100, 100, 100, 100}, {50, 50, 50, 50, 50}, {0, 50, 100, 0, 50},
                                    {100, 0, 50, 100, 0}, {30, 70, 10, 90, 60}, {80, 20, 95, 5, 40}, {65, 100, 0, 75, 100}};
    static const int SW[5] = {0, 25, 50, 75, 100};
    static const struct { int blk, type; const char *nm; } FX[2] = {{PX_B_PITCH, PX_PIT_POLYOCT_HQ, "POLYOCT_HQ"}, {PX_B_SYN, PX_SYN_OCT_FUZZ, "OCT_FUZZ"}};
    uint8_t S[33][5]; int ns = 0;
    for (int a = 0; a < 8; a++) memcpy(S[ns++], V[a], 5);
    for (int k = 0; k < 5; k++) for (int j = 0; j < 5; j++) { memset(S[ns], 50, 5); S[ns][k] = (uint8_t)SW[j]; ns++; }
    int n = 0;
    for (int f = 0; f < 2; f++) for (int s = 0; s < ns; s++) for (int g = 0; g < NSIG; g++) {
        px_preset_t p; isolate(&p, FX[f].blk, FX[f].type, S[s]);
        printf("P %s set %d sig %d bs 64 %016llx\n", FX[f].nm, s, g, (unsigned long long)run(&p, sig[g], 64)); n++;
        if (g < 3 && (s % 4) == 0) {
            static const int BS[3] = {48, 16, 1};
            for (int b = 0; b < 3; b++) { printf("P %s set %d sig %d bs %d %016llx\n", FX[f].nm, s, g, BS[b], (unsigned long long)run(&p, sig[g], BS[b])); n++; }
        }
    }
    static const int BSQ[4] = {64, 48, 16, 1};
    for (int f = 0; f < 2; f++) for (int c = 0; c < 24; c++) {
        px_preset_t p; px_preset_factory(&p, c % 24);
        for (int z = 0; z < PX_NBLK; z++) {
            p.on[z] = (rnd() & 3) != 0;
            if (z != PX_B_SYN) p.type[z] = (uint8_t)(rnd() % (unsigned)px_ntypes(z)); else (void)rnd();
            if (z == PX_B_PITCH && p.type[z] == PX_PIT_LOWEST_HQ) p.type[z] = PX_PIT_DETUNE;   /* (LOWEST OCTAVE HQ's gain changed on purpose: fix pass 1) */
            for (int k = 0; k < 5; k++) p.p[z][k] = (uint8_t)(rnd() % 101);
        }
        p.type[FX[f].blk] = (uint8_t)FX[f].type; p.on[FX[f].blk] = 1;
        if (FX[f].blk != PX_B_SYN) p.on[PX_B_SYN] = 0; else p.type[PX_B_SYN] = PX_SYN_OCT_FUZZ;
        if (c & 1) px_order_default(p.order);
        const int wet = (c >> 1) & 1, bs = BSQ[(c >> 2) & 3], frames = bs == 1 ? 6000 : NSYN;
        uint64_t h = 1469598103934665603ull;
        px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
        if (wet) px_wet_mode(&e, 1, 128);
        static float out[64]; const float *in = sig[(c * 5) % NSIG];
        for (int at = 0, b = 0; at + bs <= frames; at += bs, b++) {
            if (b % 97 == 40) {
                px_preset_t q = e.cur;
                for (int j = 0; j < 4; j++) { const int z = (int)(rnd() % PX_NBLK); q.p[z][rnd() % 5] = (uint8_t)(rnd() % 101); }
                const int z = (int)(rnd() % PX_NBLK); if (z != FX[f].blk && z != PX_B_SYN) q.on[z] ^= 1; else (void)0;
                if (px_engine_change(&e, &q) == PX_CH_KNOBS) { px_engine_prepare(&e, &q); px_engine_knobs(&e, &q); }
            }
            if (wet && bs == 64 && !(b & 1)) px_wet_swap(&e);
            px_engine_process(&e, in + at, out, bs);
            if (wet && bs == 64 && (b & 1)) px_wet_run(&e, 0, 0);
            h = H(h, out, bs);
        }
        printf("Q %s chain %d wet %d bs %d %016llx\n", FX[f].nm, c, wet, bs, (unsigned long long)h); n++;
    }
    printf("%d runs\n", n);
    return 0;
}
