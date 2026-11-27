/* Host prototype: a polyphonic drawbar organ on POLY OCTAVE HQ's filter bank (no pitch tracking).
 * Each band of the 26-band bank holds the guitar near one partial as a complex signal z. Its drawbars are
 *   16' = |z| cos(arg z / 2) (the existing octave down),   8' = Re z,   4' = Re z^2/|z|,
 *   2 2/3' = Re z^3/|z|^2 (the quint, new),   2' = Re z^4/|z|^3 (bands < 1250 Hz).
 * Raw float32 mono 44.1 kHz on stdin, out on stdout.  usage: organ VOICE   (0 jazz, 1 full, 2 church, 3 poly octave ref) */
#include <stdio.h>
#include <stdlib.h>
#include "px_dsp.c"

static float bar[5];            /* 16, 8, 4, 2 2/3, 2 */
static float perc_gain, perc_bar = 3; /* percussion on the 2 2/3' (Hammond 3rd) */

static float org_bank(px_erb_octave *s, float x)
{
    float acc[5] = {0};
    for (int i = 0; i < ERB_O_BANDS; ++i) {
        const px_erb_band *b = &erb_oband[i];
        const float real = b->b0 * x + s->z1[i].r, imag = s->z1[i].i;
        s->z1[i].r = b->b1.r * x - (b->a1.r * real - b->a1.i * imag) + s->z2[i].r;
        s->z1[i].i = b->b1.i * x - (b->a1.r * imag + b->a1.i * real) + s->z2[i].i;
        s->z2[i].r = b->b2.r * x - (b->a2.r * real - b->a2.i * imag);
        s->z2[i].i = b->b2.i * x - (b->a2.r * imag + b->a2.i * real);
        const float rr = real * real, ii = imag * imag, power = rr + ii + 1e-24f, inv = erb_inverse_root(power);
        const unsigned negative = imag < 0.0f;
        unsigned phase = s->half_phase[i];
        if (real < 0.0f && (phase & 1) != negative) phase ^= 2;
        s->half_phase[i] = (uint8_t)((phase & 2) | negative);
        const float hc = 0.5f + 0.5f * real * inv, sq = hc > 0.0f ? power * hc + 1e-30f : 1e-30f;
        const float sub = sq * erb_inverse_root(sq);
        acc[0] += (phase & 2) ? -sub : sub;
        acc[1] += real;
        const float d = rr - ii;
        acc[2] += d * inv;                                   /* Re z^2 / |z| */
        if (erb_ofc[i] < 1800.0f) acc[3] += real * (rr - 3.0f * ii) * inv * inv;   /* Re z^3 / |z|^2 */
        if (erb_ofc[i] < ERB_UP2_TOP) acc[4] += (d * d - 4.0f * rr * ii) * inv * inv * inv;
    }
    static const float cal[5] = { ERB_GAIN_DOWN, 1.0f, ERB_GAIN_UP, 0.55f, ERB_GAIN_UP2 };
    float y = 0.0f;
    for (int k = 0; k < 5; ++k) y += (bar[k] + (k == perc_bar ? perc_gain : 0.0f)) * cal[k] * acc[k];
    return y;
}

static float org_run(px_erb_octave *s, float input)   /* erb_octaves2's rate chain around org_bank */
{
    const unsigned pos = s->pos;
    s->input[pos] = input;
    if ((pos & 1) == 0) {
        const unsigned p2 = s->pos2;
        s->in2[p2] = erb_halfband(s->input, pos, erb_hb);
        if ((p2 & 1) == 0) s->up2[p2] = org_bank(s, erb_halfband(s->in2, p2, erb_hb));
        s->up[pos] = (p2 & 1) ? s->up2[(p2 - 15) & 31] : 2.0f * erb_halfband(s->up2, p2, erb_hb);
        s->pos2 = (p2 + 1) & 31;
    }
    s->pos = (pos + 1) & 31;
    if (pos & 1) return s->up[(pos - 15) & 31];
    return 2.0f * erb_halfband(s->up, pos, erb_hb);
}

#define LES_N 2048
int main(int argc, char **argv)
{
    const int voice = argc > 1 ? atoi(argv[1]) : 0;
    static const float bars[4][5] = { {0.8f, 1.0f, 0.6f, 0.0f, 0.0f},     /* jazz: 888000000 + perc 3rd */
                                      {0.8f, 1.0f, 0.8f, 0.6f, 0.6f},     /* full: 16 8 4 2 2/3 2 */
                                      {0.9f, 0.8f, 0.7f, 0.0f, 0.5f},     /* church: no perc, swell */
                                      {0.6f, 1.0f, 0.5f, 0.0f, 0.0f} };   /* POLY OCTAVE-like reference */
    memcpy(bar, bars[voice & 3], sizeof bar);
    if (argc > 2) { memset(bar, 0, sizeof bar); bar[atoi(argv[2])] = 1.0f; }   /* one drawbar alone (checks) */
    static px_engine e; e.sr = 44100.0f; erb_prepare(&e);
    static px_erb_octave st;
    static float les[LES_N]; unsigned lw = 0;
    float fast = 0, slow = 0, agc = 0, gate = 0, swell = 0, perc = 0, lph = 0, lp = 0;
    int since = 1 << 30;
    const float lrate = voice == 1 ? 6.7f : 0.8f;            /* Leslie: fast on FULL, slow on JAZZ */
    float x;
    while (fread(&x, sizeof x, 1, stdin) == 1) {
        const float a = fabsf(x);
        fast += (a - fast) * 0.0224f; slow += (a - slow) * 0.0011f;
        /* percussion: a pick (fast envelope well over slow) restarts a 2nd/3rd harmonic decay, as a Hammond's */
        if (fast > 1.8f * slow && fast > 2e-3f && since > 2646) { perc = 1.0f; since = 0; }
        since++; perc *= 0.99985f;                           /* ~150 ms */
        perc_gain = (voice == 0 || voice == 1) ? 0.9f * perc : 0.0f;
        float w = org_run(&st, x);
        /* organ sustain: level the wet with a slow follower (guitar decay -> held tone), gate at the floor */
        const float wa = fabsf(w);
        agc += (wa - agc) * (wa > agc ? 0.004f : 0.0003f);
        const float open = fast > 1.5e-3f ? 1.0f : 0.0f;
        gate += (open - gate) * (open > gate ? 0.02f : 0.0015f);
        float g = 0.12f / (agc + 0.02f); if (g > 6.0f) g = 6.0f;
        if (voice == 3) g = 1.0f, gate = 1.0f;               /* reference: plain octaves, no sustain */
        float target = voice == 2 ? 1.0f : 1.0f;
        swell += (target * gate - swell) * (voice == 2 ? 0.00025f : 0.05f);
        float y = w * g * (voice == 2 ? swell : gate);
        y = tanhf(1.3f * y) / 1.3f;                          /* a little Leslie-preamp grit */
        if (voice != 2 && voice != 3) {                      /* rotary: Doppler delay + AM, horn-ish */
            lph += lrate / 44100.0f; if (lph >= 1.0f) lph -= 1.0f;
            const float m = sinf(2.0f * PI_F * lph);
            les[lw & (LES_N - 1)] = y;
            float dly = 60.0f + 25.0f * m; int di = (int)dly; float fr = dly - di;
            float d0 = les[(lw - di) & (LES_N - 1)], d1 = les[(lw - di - 1) & (LES_N - 1)];
            lw++;
            y = 0.55f * y + 0.45f * (d0 + fr * (d1 - d0)) * (1.0f + 0.35f * m);
        }
        lp += (y - lp) * 0.35f;                              /* tame the top */
        fwrite(&lp, sizeof lp, 1, stdout);
    }
    return 0;
}
