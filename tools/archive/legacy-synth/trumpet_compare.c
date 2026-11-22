/* v49 host experiment: the trumpet voices compared on the same tracker. Not firmware.
 *   cc -O2 -Iengine -Ifirmware tools/archive/legacy-synth/trumpet_compare.c firmware/px_math.c -lm -o build/trumpet_vsco
 *   cc -O2 -Iengine -Ifirmware -DPXA_BANK_FILE='"../build/iowa/px_acoustic_bank_iowa.h"' -DPTH_FILE='"../build/iowa/px_trumpet_harm.h"' \
 *      tools/archive/legacy-synth/trumpet_compare.c firmware/px_math.c -lm -o build/trumpet_iowa
 *   build/trumpet_X OUTDIR [DI.wav]
 * Plucked DI notes (Karplus-Strong) at guitar E2, C3, C4 - TRUMPET sounds E3, C4, C5 - at a normal and a gentle pick:
 *  - pick to trigger: the shared note layer's onset; trigger to audible: the output's 5 ms RMS reaching 20 dB under
 *    its peak; attack / sustain brightness: the spectral centroid of the first 50 ms after audible, and of 300..500 ms;
 *  - the looped sample voice (the engine's own TRUMPET) at TONE 50 (default) and 100 (open);
 *  - with PTH_FILE: the prototype - the recording's attack (to its end + 20 ms), crossfaded into a sustain synthesised
 *    from the recording's harmonic table (32 harmonics, soft / loud by pick strength), same tracker, same level law;
 *    and each sustain's 1/3-octave difference against the recording's own loop.
 * Writes OUTDIR/<voice>_notes.wav (the six notes in a row) and, given a DI, OUTDIR/<voice>_di.wav. */
#include "../../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef PTH_FILE
#include PTH_FILE
#endif

#define SR 44100.0f
static px_engine E;

static void set_trumpet(int tone)
{
    px_engine_init(&E, SR);
    px_preset_t p; memset(&p, 0, sizeof p); strcpy(p.name, "TRUMPET");
    for (int b = 0; b < PX_NBLK; b++) { p.order[b] = (uint8_t)b; for (int k = 0; k < PX_NPAR; k++) p.p[b][k] = 50; }
    p.on[PX_B_SYN] = 1; p.type[PX_B_SYN] = PX_SYN_TRUMPET; p.p[PX_B_SYN][1] = (uint8_t)tone; p.p[PX_B_SYN][4] = 0;
    px_engine_set(&E, &p);
}
static float midi_hz(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }

static void pluck(float m, float vel, float *x, int n)
{
    static float line[4096]; memset(line, 0, sizeof line); int w = 0; float prev = 0, lp = 0, hx = 0, hy = 0; uint32_t r = 7;
    const float f = midi_hz(m), N = SR / f - 0.5f; int exc = (int)(SR / f);
    for (int i = 0; i < n; i++) {
        float rp = (float)w - N; if (rp < 0) rp += 4096.0f; int i0 = (int)rp; float fr = rp - i0;
        float out = line[i0 & 4095] + (line[(i0 + 1) & 4095] - line[i0 & 4095]) * fr, e = 0;
        if (exc > 0) { r = r * 1664525u + 1013904223u; lp += (0.15f + 0.8f * vel) * ((float)(r >> 8) / 8388608.0f - 1.0f - lp); e = lp * 0.9f * vel; exc--; }
        const float loss = i < (int)(0.75f * SR) ? 0.9975f : 0.7f;
        line[w] = loss * 0.5f * (out + prev) + e; prev = out; w = (w + 1) & 4095;
        float hp = out - hx + 0.995f * hy; hx = out; hy = hp; x[i] = 0.45f * hp;
    }
}
static float centroid(const float *y, int a, int len)
{
    int L = 1; while (L < len) L <<= 1; L >>= 1; if (L < 256) L = 256;
    double num = 0, den = 0;
    for (int b = 1; b < L / 2; b++) { double re = 0, im = 0, w = 2 * M_PI * b / L;
        for (int i = 0; i < L; i++) { double h = 0.5 - 0.5 * cos(2 * M_PI * i / L); re += y[a + i] * h * cos(w * i); im -= y[a + i] * h * sin(w * i); }
        double p = re * re + im * im; num += p * b * SR / L; den += p; }
    return (float)(num / (den + 1e-30));
}
static void bands(const float *y, int a, int len, float *out)      /* 1/3-octave levels 100 Hz .. 8 kHz, normalised, dB */
{
    int L = 4096; double P[20] = {0}, tot = 0;
    for (int b = 1; b < L / 2; b++) { double f = b * SR / L, re = 0, im = 0, w = 2 * M_PI * b / L;
        for (int i = 0; i < L && i < len; i++) { double h = 0.5 - 0.5 * cos(2 * M_PI * i / L); re += y[a + i] * h * cos(w * i); im -= y[a + i] * h * sin(w * i); }
        double p = re * re + im * im;
        for (int k = 0; k < 20; k++) { double c = 1000 * pow(2.0, (k - 10) / 3.0); if (f >= c / pow(2, 1 / 6.0) && f < c * pow(2, 1 / 6.0)) { P[k] += p; tot += p; } } }
    for (int k = 0; k < 20; k++) out[k] = (float)(10 * log10(P[k] / (tot + 1e-30) + 1e-20));
}

#ifdef PTH_FILE
/* ---- the prototype voice: recorded attack + harmonic-table sustain ---- */
typedef struct { int k, active; float pos, ph, amp, xf, fade_k, root; } proto_t;
static float proto_tick(proto_t *v, const pxt_layer *L, float inv_sr)
{
    if (L->onset && L->f0 > 0) {
        const float f = 2.0f * L->f0;                  /* (an octave above the guitar) */
        int best = -1; float bd = 1e9f; const int dyn = L->vel > PXA_TRP_SOFT;
        for (int k = 0; k < PTH_N; k++) { if (PTH[k].dyn != dyn) continue; float r = f / PTH[k].f0; if (r < 1) r = 1 / r; if (r < bd) { bd = r; best = k; } }
        if (best >= 0) { v->k = best; v->active = 1; v->pos = 0; v->root = PTH[best].f0; v->xf = 0; }
    }
    if (!v->active) return 0.0f;
    const float f = L->f0 > 0 ? 2.0f * L->f0 : v->root;
    if (L->gate && (f > 1.414f * v->root || f < 0.707f * v->root)) {   /* the pitch moved 6 semitones: the nearer table */
        int best = -1; float bd = 1e9f; const int dyn = PTH[v->k].dyn;
        for (int k = 0; k < PTH_N; k++) { if (PTH[k].dyn != dyn) continue; float r = f / PTH[k].f0; if (r < 1) r = 1 / r; if (r < bd) { bd = r; best = k; } }
        if (best >= 0 && best != v->k) { v->k = best; v->root = PTH[best].f0; if (v->pos < (float)PTH[best].splice) v->pos = (float)PTH[best].splice; }
    }
    const pxa_smp *m = &PXA_BANK[v->k];
    /* the recorded attack, transposed as the sample voice does, up to its splice; then the synthesised sustain */
    const float splice = (float)PTH[v->k].splice, xfl = 0.02f * m->rate;
    float att = 0.0f, g_att = 1.0f;
    if (v->pos < splice) { att = pxa_hermite(m, v->pos < (float)m->le - 3 ? v->pos : (float)m->le - 3); v->pos += f / m->f0 * (float)m->rate * inv_sr; }
    if (v->pos > splice - xfl) { const float u = (v->pos - (splice - xfl)) / xfl; g_att = u >= 1 ? 0 : cosf(1.5708f * u); v->xf = u >= 1 ? 1 : sinf(1.5708f * u); }
    float syn = 0.0f;
    if (v->xf > 0.0f) {
        v->ph = wrap1(v->ph + f * inv_sr);
        for (int h = 1; h <= PTH_NH; h++) {
            if (h * f > PTH_TOP) break;
            const float a = powf(10.0f, ((float)PTH[v->k].db[h - 1] - 80.0f) / 20.0f);
            const float ph = v->ph * (float)h; syn += a * sinf(6.2831853f * (ph - (float)(int)ph));   /* (the fraction: wrap1 takes 1 once) */
        }
        syn *= 1.414f * 0.1259f;                       /* (unit-power table -> the loop's -18 dBFS RMS) */
    }
    /* level: the sample voice's law (expression, 21 dB range; 30 ms release) */
    const float target = L->gate ? powf(10.0f, (L->expr - 1.0f) * 21.0f / 20.0f) : 0.0f;
    v->amp += (target - v->amp) * (L->gate ? 0.0019f : 0.0008f);
    return (att * g_att + syn * v->xf) * v->amp * 0.814f * 1.4f;
}
#endif

static void wav_write(const char *fn, const float *x, int n)
{
    FILE *f = fopen(fn, "wb"); if (!f) return;
    unsigned h[11]; float pk = 1e-9f; for (int i = 0; i < n; i++) if (fabsf(x[i]) > pk) pk = fabsf(x[i]);
    const float g = pk > 0.9f ? 0.9f / pk : 1.0f;
    fwrite("RIFF", 1, 4, f); h[0] = 36 + n * 2; fwrite(h, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    unsigned v[4] = {16, 1 | (1 << 16), 44100, 88200}; fwrite(v, 4, 4, f); unsigned short ba[2] = {2, 16}; fwrite(ba, 2, 2, f);
    fwrite("data", 1, 4, f); h[0] = n * 2; fwrite(h, 4, 1, f);
    for (int i = 0; i < n; i++) { short s = (short)lrintf(x[i] * g * 32767); fwrite(&s, 2, 1, f); }
    fclose(f);
}
static float *wav_read(const char *fn, int *n)
{
    FILE *f = fopen(fn, "rb"); if (!f) return 0;
    unsigned char hd[12]; if (fread(hd, 1, 12, f) != 12) return 0;
    int ch = 1, bits = 16, fmt = 1; unsigned dsz = 0; int ok = 0;
    for (;;) { unsigned char c[8]; if (fread(c, 1, 8, f) != 8) break; unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { unsigned char b[16]; fread(b, 1, 16, f); fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; bits = b[14] | b[15] << 8; fseek(f, sz - 16, SEEK_CUR); }
        else if (!memcmp(c, "data", 4)) { dsz = sz; ok = 1; break; } else fseek(f, (sz + 1) & ~1u, SEEK_CUR); }
    if (!ok) return 0;
    int bps = bits / 8, m = dsz / (bps * ch); if (m > 44100 * 40) m = 44100 * 40;
    float *x = malloc(sizeof(float) * m);
    for (int i = 0; i < m; i++) { double a = 0; for (int c = 0; c < ch; c++) {
        if (fmt == 3) { float v; fread(&v, 4, 1, f); a += v; } else if (bits == 16) { short v; fread(&v, 2, 1, f); a += v / 32768.0; }
        else if (bits == 24) { unsigned char b[3]; fread(b, 1, 3, f); int v = b[0] | b[1] << 8 | b[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF; a += v / 8388608.0; }
        else { int v; fread(&v, 4, 1, f); a += v / 2147483648.0; } }
        x[i] = (float)(a / ch); }
    fclose(f); *n = m; return x;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/trumpet";
    const int n = (int)(1.2f * SR), nn = 6 * n;
    static float x[60000], y[60000]; float *all = calloc(nn, sizeof(float)), *allp = calloc(nn, sizeof(float));
    const float GM[3] = {40, 48, 60}, VEL[2] = {0.8f, 0.2f};
    printf("%-6s %-6s %-14s %8s %9s %9s %9s %9s %9s\n", "note", "pick", "voice", "trig ms", "pitch ms", "aud ms", "att Hz", "sus Hz", "sus diff");
    int slot = 0;
    for (int q = 0; q < 3; q++) for (int v = 0; v < 2; v++, slot++) {
        pluck(GM[q], VEL[v], x, n);
        for (int tone = 50; tone <= 100; tone += 50) {
            set_trumpet(tone);
            int trig = -1, okp = -1; const float want = midi_hz(GM[q] + 12);
            for (int i = 0; i < n; i++) { px_engine_process(&E, x + i, y + i, 1); if (trig < 0 && pxa_lay.onset) trig = i;
                if (okp < 0 && pxa_lay.gate && fabsf(1200 * log2f(2 * pxa_lay.f0 / want)) < 50) okp = i; }
            float pk = 0; double acc; int aud = -1;
            for (int i = 220; i < n; i += 44) { acc = 0; for (int k = i - 220; k < i; k++) acc += y[k] * y[k]; float r = sqrtf(acc / 220); if (r > pk) pk = r; }
            for (int i = 220; i < n; i += 22) { acc = 0; for (int k = i - 220; k < i; k++) acc += y[k] * y[k]; if (sqrtf(acc / 220) > 0.1f * pk) { aud = i - 110; break; } }
            if (tone == 50) memcpy(all + slot * n, y, sizeof(float) * n);
            const float ca = aud >= 0 ? centroid(y, aud, 2205) : 0, cs = centroid(y, (int)(0.3f * SR), 8192);
            printf("%-6.0f %-6.1f %-14s %8.1f %9.1f %9.1f %9.0f %9.0f\n", GM[q] + 12, VEL[v], tone == 50 ? "loop TONE 50" : "loop TONE 100", trig * 1000 / SR, okp * 1000 / SR, (aud - trig) * 1000 / SR, ca, cs);
        }
#ifdef PTH_FILE
        {   /* the prototype, on the same tracker */
            set_trumpet(50);
            proto_t pv; memset(&pv, 0, sizeof pv);
            static px_track tt; static pxt_layer LL; pxt_reset(&E, &tt, &LL);
            int trig = -1, okp = -1; const float want = midi_hz(GM[q] + 12);
            for (int i = 0; i < n; i++) { pxt_layer_run(&LL, &tt, x[i], pxt_run(&E, &tt, x[i])); if (trig < 0 && LL.onset) trig = i; y[i] = proto_tick(&pv, &LL, 1.0f / SR);
                if (okp < 0 && LL.gate && fabsf(1200 * log2f(2 * LL.f0 / want)) < 50) okp = i; }
            float pk = 0; double acc; int aud = -1;
            for (int i = 220; i < n; i += 44) { acc = 0; for (int k = i - 220; k < i; k++) acc += y[k] * y[k]; float r = sqrtf(acc / 220); if (r > pk) pk = r; }
            for (int i = 220; i < n; i += 22) { acc = 0; for (int k = i - 220; k < i; k++) acc += y[k] * y[k]; if (sqrtf(acc / 220) > 0.1f * pk) { aud = i - 110; break; } }
            memcpy(allp + slot * n, y, sizeof(float) * n);
            const float ca = aud >= 0 ? centroid(y, aud, 2205) : 0, cs = centroid(y, (int)(0.3f * SR), 8192);
            /* sustain against the recording's own loop at its pitch (the note it chose) */
            const pxa_smp *m = &PXA_BANK[pv.k]; static float rec[16384];
            for (int i = 0; i < 16384; i++) { const int j = m->ls + (i / 2) % (int)(m->le - m->ls); rec[i] = PXA_MU[m->d[j]]; }
            float br[20], bo[20], bl[20]; bands(rec, 0, 16384, br); bands(y, (int)(0.3f * SR), 8192, bo); bands(all + slot * n, (int)(0.3f * SR), 8192, bl);
            double dp = 0, dl = 0; int nb = 0; for (int k = 0; k < 20; k++) if (br[k] > -60) { dp += fabs(bo[k] - br[k]); dl += fabs(bl[k] - br[k]); nb++; }
            printf("%-6.0f %-6.1f %-14s %8.1f %9.1f %9.1f %9.0f %9.0f %6.1f dB (loop %.1f dB)\n", GM[q] + 12, VEL[v], "attack+synth", trig * 1000 / SR, okp * 1000 / SR, (aud - trig) * 1000 / SR, ca, cs, dp / nb, dl / nb);
        }
#endif
    }
    char fn[512];
#ifdef PTH_FILE
    const char *tag = "iowa";
#else
    const char *tag = "vsco";
#endif
    snprintf(fn, sizeof fn, "%s/%s_loop_notes.wav", out, tag); wav_write(fn, all, nn);
#ifdef PTH_FILE
    snprintf(fn, sizeof fn, "%s/iowa_attack_synth_notes.wav", out); wav_write(fn, allp, nn);
#endif
    if (argc > 2) {                                     /* the owner's DI through the voice(s) */
        int m; float *d = wav_read(argv[2], &m);
        if (d) {
            float *o = calloc(m, sizeof(float));
            set_trumpet(50); for (int i = 0; i < m; i += 64) px_engine_process(&E, d + i, o + i, m - i < 64 ? m - i : 64);
            snprintf(fn, sizeof fn, "%s/%s_loop_di.wav", out, tag); wav_write(fn, o, m);
#ifdef PTH_FILE
            set_trumpet(50); proto_t pv; memset(&pv, 0, sizeof pv); static px_track tt; static pxt_layer LL; pxt_reset(&E, &tt, &LL);
            for (int i = 0; i < m; i++) { pxt_layer_run(&LL, &tt, d[i], pxt_run(&E, &tt, d[i])); o[i] = proto_tick(&pv, &LL, 1.0f / SR); }
            snprintf(fn, sizeof fn, "%s/iowa_attack_synth_di.wav", out); wav_write(fn, o, m);
#endif
        }
    }
    return 0;
}
