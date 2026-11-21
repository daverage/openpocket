/* OpenPocket host experiment (v62, not firmware): a genuine two-octave-down voice from the HQ octave bank.
 * Each band's analytic signal z = |z| e^{i a}; the octave down plays |z| cos(a/2) with the half-angle's sign kept
 * across wraps (erb_bank). Two octaves down needs a/4: count the band's wraps modulo 4, k, and play
 *   |z| cos((a + 2 pi k) / 4) = |z| * sign * sqrt((1 + c2) / 2),  c2 = (-1)^k sqrt((1 + cos a) / 2)
 * with sign from k and the sign of Im z. Scored like tools/research/octave_banks.c: energy at the target partials
 * (here a quarter of the input's), arrival of a low note, and added level wobble on recordings.
 *   cc -O3 -DPX_ERB_LAYOUT2=1 -Iengine -Ifirmware tools/research/sub2_proto.c firmware/px_math.c -lm -o build/sub2_proto
 *   build/sub2_proto assets/loops/{chug,riff,chords,arpeggio,lead}.wav
 */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>

typedef struct { px_erb_complex z1[ERB_O_BANDS], z2[ERB_O_BANDS]; uint8_t k[ERB_O_BANDS], neg[ERB_O_BANDS];
                 float in[32], in2[32], o2[32], o[32]; unsigned pos, pos2; } sub2_state;
static float sub2_bank(sub2_state *s, float x, int quarter)
{
    float out = 0.0f;
    for (int i = 0; i < ERB_O_BANDS; ++i) {
        const px_erb_band *b = &erb_oband[i];
        const float re = b->b0 * x + s->z1[i].r, im = s->z1[i].i;
        s->z1[i].r = b->b1.r * x - (b->a1.r * re - b->a1.i * im) + s->z2[i].r;
        s->z1[i].i = b->b1.i * x - (b->a1.r * im + b->a1.i * re) + s->z2[i].i;
        s->z2[i].r = b->b2.r * x - (b->a2.r * re - b->a2.i * im);
        s->z2[i].i = b->b2.i * x - (b->a2.r * im + b->a2.i * re);
        const float power = re * re + im * im + 1e-24f, inv = erb_inverse_root(power), mag = power * inv;
        const unsigned negative = im < 0.0f;
        /* a crossing of the negative real axis (counter-clockwise: Im from + to -) advances k */
        if (re < 0.0f && negative != s->neg[i]) s->k[i] = (uint8_t)((s->k[i] + (negative ? 1 : 3)) & 3);
        s->neg[i] = (uint8_t)negative;
        const float c = re * inv;                                   /* cos a */
        float h = 0.5f + 0.5f * c; h = h > 0.0f ? h * erb_inverse_root(h + 1e-30f) : 0.0f;   /* |cos a/2| */
        if (!quarter) { out += mag * ((s->k[i] & 1) ? -h : h); continue; }
        const float c2 = (s->k[i] & 1) ? -h : h;                    /* cos((a + 2 pi k) / 2) */
        float q = 0.5f + 0.5f * c2; q = q > 0.0f ? q * erb_inverse_root(q + 1e-30f) : 0.0f;   /* |cos((a+2pi k)/4)| */
        float sign;
        switch (s->k[i]) { case 0: sign = 1.0f; break; case 1: sign = negative ? 1.0f : -1.0f; break;
                           case 2: sign = -1.0f; break; default: sign = negative ? -1.0f : 1.0f; }
        out += mag * sign * q;
    }
    return out * ERB_GAIN_DOWN;
}
static float sub2_run(sub2_state *s, float input, int quarter)
{
    const unsigned pos = s->pos; s->in[pos] = input;
    if ((pos & 1) == 0) {
        const unsigned p2 = s->pos2; s->in2[p2] = erb_halfband(s->in, pos, erb_hb);
        if ((p2 & 1) == 0) s->o2[p2] = sub2_bank(s, erb_halfband(s->in2, p2, erb_hb), quarter);
        s->o[pos] = (p2 & 1) ? s->o2[(p2 - 15) & 31] : 2.0f * erb_halfband(s->o2, p2, erb_hb);
        s->pos2 = (p2 + 1) & 31;
    }
    s->pos = (pos + 1) & 31;
    if (pos & 1) return s->o[(pos - 15) & 31];
    return 2.0f * erb_halfband(s->o, pos, erb_hb);
}
static void render(const float *x, float *y, int n, int quarter)
{ static sub2_state s; memset(&s, 0, sizeof s); for (int i = 0; i < n; ++i) y[i] = sub2_run(&s, x[i], quarter); }

static void tones(float *x, int n, const float *f0, int nf)
{
    memset(x, 0, sizeof(float) * n);
    for (int s = 0; s < nf; s++) for (int h = 1; h <= 10; h++) { double ph = s + h;
        for (int i = 0; i < n; i++) { const double f = f0[s] * h * (1.0 + 0.0002 * h * h); ph += 2 * M_PI * f / 44100.0;
            x[i] += (float)(0.08 / h * exp(-i / (3.0 * 44100.0)) * sin(ph)); } }
}
static float target(const float *y, int n, const float *f0, int nf, float ratio)
{
    const int a = n / 4, L = 16384; double tot = 0, hit = 0;
    for (int b = 1; b < L / 2 && b * 44100.0 / L < 6000; b++) {
        const double fb = b * 44100.0 / L; int want = 0;
        for (int s = 0; s < nf; s++) for (int h = 1; h <= 10; h++) { const double f = ratio * f0[s] * h * (1.0 + 0.0002 * h * h); if (fabs(fb - f) <= 2.0 * 44100.0 / L) want = 1; }
        double re = 0, im = 0; const double w = 2 * M_PI * b / L;
        for (int i = 0; i < L; i++) { const double hw = 0.5 - 0.5 * cos(2 * M_PI * i / L); re += y[a + i] * hw * cos(w * i); im -= y[a + i] * hw * sin(w * i); }
        const double p = re * re + im * im; tot += p; if (want) hit += p;
    }
    return (float)(100.0 * hit / (tot + 1e-30));
}
int main(void)
{
    static px_engine E; px_engine_init(&E, 44100.0f); erb_prepare(&E);
    const int n = 3 * 44100; float *x = malloc(4 * n), *y = malloc(4 * n);
    printf("%-28s %10s %10s\n", "", "-12 (ref)", "-24");
    for (int q = 0; q < 2; ++q) {
        double single = 0, low = 0, chord = 0; int ns = 0, nl = 0;
        for (int m = 40; m <= 76; m += 3) {
            const float f = 440.0f * powf(2.0f, (m - 69) / 12.0f); tones(x, n, &f, 1); render(x, y, n, q);
            const float t = target(y, n, &f, 1, q ? 0.25f : 0.5f); single += t; ns++;
            if (m < 55) { low += t; nl++; }
        }
        static const float C[3][3] = {{82.41f, 123.47f, 164.81f}, {110.0f, 164.81f, 220.0f}, {196.0f, 246.94f, 293.66f}};
        for (int c = 0; c < 3; c++) { tones(x, n, C[c], 3); render(x, y, n, q); chord += target(y, n, C[c], 3, q ? 0.25f : 0.5f) / 3; }
        printf("%s single E2..E5 %.1f%%, E2..G3 %.1f%%, chords %.1f%%\n", q ? "-24:" : "-12:", single / ns, low / nl, chord);
    }
    return 0;
}
