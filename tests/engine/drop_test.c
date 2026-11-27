/* DROP TUNE (engine/px_banksyn.h), through the whole engine, DROP 2 (drop D), RANGE fret 4, MIX 100, on guitar-like notes (6 harmonics):
 *  - a low-string note moves down two semitones (E2 -> D2, G2 -> F2): the new fundamental at least 10 dB over the old one
 *  - a note on another string stays (A2, E3): its fundamental at least 10 dB over the note two semitones under it
 *  - the open E A D strings together: D2, A2 and D3 sound, E2 at least 10 dB under D2
 *  - finite and bounded at every knob's 0 / 50 / 100, silent on silence */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static px_engine e;
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define N 44100
static float in[N], out[N];
static void notes(const float *hz, int n)
{
    for (int i = 0; i < N; i++) {
        const float t = (float)i / 44100.0f, env = (1.0f - expf(-t / 0.003f)) * expf(-t / 1.2f);
        float s = 0; for (int c = 0; c < n; c++) for (int h = 1; h <= 6; h++) s += 0.06f / h * sinf(6.2831853f * hz[c] * h * t + h + c);
        in[i] = env * s;
    }
}
static void render(const uint8_t *q)
{
    px_preset_t p; px_preset_base(&p); memset(p.on, 0, sizeof p.on); p.valve_on = 0;
    p.on[PX_B_PITCH] = 1; p.type[PX_B_PITCH] = PX_PIT_DROP; memcpy(p.p[PX_B_PITCH], q, 5);
    px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
    for (int at = 0; at < N; at += 64) px_engine_process(&e, in + at, out + at, N - at < 64 ? N - at : 64);
}
static double db(double hz)                            /* level at hz over 0.3 .. 0.67 s (Hann) */
{
    const int from = 13230, n = 16384; double re = 0, im = 0;
    for (int i = 0; i < n; i++) { const double w = 0.5 - 0.5 * cos(2 * M_PI * i / n); re += w * out[from + i] * cos(2 * M_PI * hz * i / 44100.0); im += w * out[from + i] * sin(2 * M_PI * hz * i / 44100.0); }
    return 10 * log10(re * re + im * im + 1e-30);
}
int main(void)
{
    const uint8_t q[5] = {30, 50, 100, 0, 0};          /* DROP 2, RANGE fret 4, MIX 100 */
    static const float LOW[2][2] = {{82.41f, 73.42f}, {98.0f, 87.31f}};
    for (int k = 0; k < 2; k++) {
        notes(&LOW[k][0], 1); render(q);
        const double a = db(LOW[k][1]), b = db(LOW[k][0]);
        printf("low string %.1f Hz: at %.1f %+.1f dB, at the old %.1f %+.1f dB\n", LOW[k][0], LOW[k][1], a, LOW[k][0], b);
        CHECK(a > b + 10, "the low string's %.1f Hz moves to %.1f Hz", LOW[k][0], LOW[k][1]);
    }
    static const float OTHER[2] = {110.0f, 164.81f};
    for (int k = 0; k < 2; k++) {
        notes(&OTHER[k], 1); render(q);
        const double a = db(OTHER[k]), b = db(OTHER[k] * 0.8909f);
        printf("other string %.1f Hz: kept %+.1f dB, two semitones under %+.1f dB\n", OTHER[k], a, b);
        CHECK(a > b + 10, "a %.1f Hz note on another string stays", OTHER[k]);
    }
    const float EAD[3] = {82.41f, 110.0f, 146.83f};
    notes(EAD, 3); render(q);
    const double d2 = db(73.42f), e2 = db(82.41f), a2 = db(110.0f), d3 = db(146.83f);
    printf("open E A D: D2 %+.1f, E2 %+.1f, A2 %+.1f, D3 %+.1f dB\n", d2, e2, a2, d3);
    CHECK(d2 > e2 + 10, "open E A D: the E becomes a D");
    CHECK(a2 > d2 - 15 && d3 > d2 - 15, "open E A D: A2 and D3 still sound");
    static const uint8_t KN[3] = {0, 50, 100};
    for (int k = 0; k < 3; k++) for (int v = 0; v < 3; v++) {
        uint8_t r[5] = {30, 50, 100, 0, 0}; r[k] = KN[v]; notes(EAD, 3); render(r);
        float pk = 0; int nan = 0; for (int i = 0; i < N; i++) { if (out[i] != out[i]) nan = 1; if (fabsf(out[i]) > pk) pk = fabsf(out[i]); }
        CHECK(!nan && pk < 4.0f, "knob %d = %d: finite and bounded (%.2f)", k, KN[v], pk);
    }
    memset(in, 0, sizeof in); render(q); { float pk = 0; for (int i = 0; i < N; i++) if (fabsf(out[i]) > pk) pk = fabsf(out[i]); CHECK(pk < 1e-5f, "silent on silence"); }
    printf("%s: %d problems\n", bad ? "drop FAILED" : "drop ok", bad);
    return bad != 0;
}
