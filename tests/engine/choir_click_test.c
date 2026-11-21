/* CHOIR (the VOICE engine) at a note change on a sounding voice. It clicked at every such change: a step in the output 3-9 times the voice's own pulse edges, 26-34 ms after the pick. Two causes in Felucca's formant
 * note_on: it takes a sounding voice for a fresh one (the adapter keeps the envelope in its own floats, so v->env is 0) and zeroes the filter state under the sound; and it re-picks the random vowel, moving every
 * formant target in one block. The adapter now restores the filter state and slides the vowel to its new value. Measured here: for each patch and each interval, the largest sample-to-sample step within -40..+60 ms
 * of the second pick, against that note's own steady step (99.9th percentile of the same note, 0.4-0.9 s later): 1.3-1.7x now, 4.8-8.9x before. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define N (44100 * 3)
static float in[N], out[N], d[N];
static void notes(float f1, float f2)
{
    memset(in, 0, sizeof in); const float f[2] = {f1, f2}, t0[2] = {0.3f, 1.3f};
    for (int k = 0; k < 2; k++) for (int i = (int)(t0[k] * 44100); i < N; i++) { const float t = (float)i / 44100.0f - t0[k], env = (1 - expf(-t / 0.003f)) * expf(-t / 0.8f); float v = 0;
        for (int h = 1; h <= 6; h++) v += sinf(6.2831853f * f[k] * (float)h * t) / (float)h; in[i] += 0.18f * v * env; }
}
static float ratio(int patch, float base, int semis)
{
    const float f2 = base * powf(2.0f, (float)semis / 12.0f);
    px_preset_t p; px_preset_factory(&p, 7); for (int k = 0; k < PX_NBLK; k++) p.on[k] = 0; p.on[PX_B_SYN] = 1; p.type[PX_B_SYN] = PX_SYN_CHOIR; for (int k = 0; k < 5; k++) p.p[PX_B_SYN][k] = 50; p.p[PX_B_SYN][0] = (uint8_t)patch;
    static px_engine e; px_engine_init(&e, 44100.0f); px_engine_set(&e, &p); notes(base, f2); for (int at = 0; at + 64 <= N; at += 64) px_engine_process(&e, in + at, out + at, 64);
    int nd = 0; for (int i = (int)((1.3 + 0.40) * 44100); i < (int)((1.3 + 0.90) * 44100); i++) d[nd++] = fabsf(out[i + 1] - out[i]);
    for (int x = 0; x < nd; x++) for (int y = x + 1; y < nd; y++) if (d[y] < d[x]) { const float t = d[x]; d[x] = d[y]; d[y] = t; }
    const float own = d[(int)(nd * 0.999)]; float worst = 0;
    for (int i = (int)((1.3 - 0.04) * 44100); i < (int)((1.3 + 0.06) * 44100); i++) { const float s = fabsf(out[i + 1] - out[i]); if (s > worst) worst = s; }
    return worst / own;
}
int main(void)
{
    static const int patches[] = {0, 25, 50, 75, 100}, iv[] = {-7, -2, 1, 7, 12}; float worst = 0;
    for (unsigned a = 0; a < 5; a++) for (unsigned b = 0; b < 5; b++) for (int base = 0; base < 2; base++) {
        const float r = ratio(patches[a], base ? 247.0f : 196.0f, iv[b]); if (r > worst) worst = r;
        CHECK(r < 2.4f, "CHOIR patch %d, %.0f Hz %+d semitones: the note change steps %.2fx the voice's own edges (it was 3-9x)", patches[a], base ? 247.0f : 196.0f, iv[b], r); }
    printf("  CHOIR note changes: worst step %.2fx the voice's own steady step over 50 cases\n", worst);
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("CHOIR click ok: 0 problems\n"); return 0;
}
