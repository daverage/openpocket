/* POLY SHIFTER on the bank (engine/px_banksyn.h), through the whole engine.
 *  - a low chord (E2 B2 E3 G#3, 6 harmonics each) shifted by -12, -5, +4, +7, +12: the share of the wet voice's energy on the shifted partials
 *    (within 1.5 %), from 0.3 s on; the v75 phase vocoder put about 8 % there at -12 on this chord
 *  - a single note shifted by each semitone -12..+12 lands within 10 cents
 *  - finite and bounded at every knob's 0 / 50 / 100; silent on silence; MIX 0 is the input, sample for sample
 *  - with "gain" it prints the wet / dry level per semitone on a guitar loop (for PS_GAIN_TABLE) */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static px_engine e;
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define N 44100
static float in[N], out[N];
static void render(const uint8_t *q)
{
    px_preset_t p; px_preset_base(&p); memset(p.on, 0, sizeof p.on); p.valve_on = 0;
    p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = PX_PIT_POLYSHIFT; memcpy(p.p[PX_B_PITCH], q, 5);
    px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
    for (int at = 0; at < N; at += 64) px_engine_process(&e, in + at, out + at, N - at < 64 ? N - at : 64);
}
static double dft_power(const float *x, int n, double hz)
{
    double re = 0, im = 0;
    for (int i = 0; i < n; i++) { const double w = 0.5 - 0.5 * cos(2 * M_PI * i / n); re += w * x[i] * cos(2 * M_PI * hz * i / 44100.0); im += w * x[i] * sin(2 * M_PI * hz * i / 44100.0); }
    return re * re + im * im;
}
static uint8_t knob_for(int st) { return (uint8_t)((st + 12) * 100 / 24 + ((st + 12) * 100 % 24 ? 1 : 0)); }
int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "gain")) {        /* wet / dry level on a loop (a raw float32 file) for each semitone */
        FILE *f = fopen(argv[2], "rb"); static float loop[N * 8]; const int n = (int)fread(loop, 4, N * 8, f); fclose(f);
        for (int st = -12; st <= 12; st++) {
            px_preset_t p; px_preset_base(&p); memset(p.on, 0, sizeof p.on); p.valve_on = 0;
            p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = PX_PIT_POLYSHIFT; const uint8_t q[5] = {knob_for(st), 50, 100, 0, 0}; memcpy(p.p[PX_B_PITCH], q, 5);
            px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
            static float o[N * 8]; double a = 0, b = 0;
            for (int at = 0; at < n; at += 64) px_engine_process(&e, loop + at, o + at, n - at < 64 ? n - at : 64);
            for (int i = 0; i < n; i++) { a += loop[i] * loop[i]; b += o[i] * o[i]; }
            printf("%.4f%s", sqrt(a / (b + 1e-30)), st < 12 ? ", " : "\n");
        }
        return 0;
    }
    static const float CH[4] = {82.41f, 123.47f, 164.81f, 207.65f};
    for (int i = 0; i < N; i++) { float s = 0; for (int c = 0; c < 4; c++) for (int h = 1; h <= 6; h++) s += 0.05f / h * sinf(6.2831853f * CH[c] * h * i / 44100.0f + h + c); in[i] = s; }
    static const int SH[5] = {-12, -5, 4, 7, 12};
    for (int k = 0; k < 5; k++) {
        const uint8_t q[5] = {knob_for(SH[k]), 50, 100, 0, 0}; render(q);
        const int from = 13230, len = 16384; const double ratio = pow(2.0, SH[k] / 12.0);
        double on = 0, tot = 0;
        for (double hz = 30; hz < 5000; hz += 2.0) {
            const double pw = dft_power(out + from, len, hz); tot += pw;
            int near = 0; for (int c = 0; c < 4; c++) for (int h = 1; h <= 6; h++) if (fabs(hz / (CH[c] * h * ratio) - 1.0) < 0.015) near = 1;
            if (near) on += pw;
        }
        printf("chord %+d: %.0f %% of the wet energy on the shifted partials\n", SH[k], 100 * on / tot);
        CHECK(on / tot > 0.5, "chord %+d: at least 50 %% on the shifted partials (a perfect signal scores 84 %% here)", SH[k]);
    }
    for (int st = -12; st <= 12; st++) {
        for (int i = 0; i < N; i++) in[i] = 0.2f * sinf(6.2831853f * 220.0f * i / 44100.0f);
        const uint8_t q[5] = {knob_for(st), 50, 100, 0, 0}; render(q);
        const double want = 220.0 * pow(2.0, st / 12.0); double best = 0, bh = 0;
        for (double hz = want * 0.97; hz < want * 1.03; hz += 0.1) { const double pw = dft_power(out + 22050, 16384, hz); if (pw > best) { best = pw; bh = hz; } }
        CHECK(fabs(1200 * log2(bh / want)) < 10, "a 220 Hz sine at %+d lands on %.1f Hz (want %.1f)", st, bh, want);
    }
    static const uint8_t KN[3] = {0, 50, 100};
    for (int k = 0; k < 4; k++) for (int v = 0; v < 3; v++) {
        uint8_t q[5] = {79, 50, 100, 50, 0}; q[k] = KN[v];
        for (int i = 0; i < N; i++) in[i] = (i / 5000) % 2 ? 0.0f : 0.4f * sinf(i * 0.05f) * sinf(i * 0.0011f);
        render(q); float pk = 0; int nan = 0;
        for (int i = 0; i < N; i++) { if (out[i] != out[i]) nan = 1; if (fabsf(out[i]) > pk) pk = fabsf(out[i]); }
        CHECK(!nan && pk < 4.0f, "knob %d = %d: finite and bounded (%.2f)", k, KN[v], pk);
    }
    memset(in, 0, sizeof in); { const uint8_t q[5] = {79, 50, 100, 50, 0}; render(q); float pk = 0; for (int i = 0; i < N; i++) if (fabsf(out[i]) > pk) pk = fabsf(out[i]); CHECK(pk < 1e-5f, "silent on silence"); }
    printf("%s: %d problems\n", bad ? "pshift FAILED" : "pshift ok", bad);
    return bad != 0;
}
