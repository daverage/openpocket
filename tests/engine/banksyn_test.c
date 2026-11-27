/* The waveform-following synths (engine/px_banksyn.h, BANK ORGAN): through the whole engine, on synthetic plucks.
 *  - every type at 0 / 50 / 100 of each knob: finite, bounded, silent on silence, and it sounds on a pluck
 *  - the leads play the guitar's note (SUB BASS an octave below) within 20 cents, 60 ms into the note, from E2 up
 *  - the sound does not depend on the call's block size (64, 48, 16, 1)
 * Whether they play well is for a pedal and an ear. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static px_engine e;
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define N 44100
static float in[N], out[N];
static void pluck(float f)
{
    for (int i = 0; i < N; i++) {
        const float t = (float)i / 44100.0f, env = (1.0f - expf(-t / 0.003f)) * expf(-t / 0.5f);
        float s = 0.0f; for (int h = 1; h <= 6; h++) s += sinf(6.2831853f * f * (float)h * t + (float)h) / (float)h;
        in[i] = 0.15f * env * s;
    }
}
static void render(int t, const uint8_t *q, int bs)
{
    px_preset_t p; px_preset_base(&p); memset(p.on, 0, sizeof p.on); p.valve_on = 0;
    p.on[PX_B_SYN] = 1; p.type[PX_B_SYN] = (uint8_t)t; memcpy(p.p[PX_B_SYN], q, 5);
    px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
    for (int at = 0; at < N; at += bs) { const int n = N - at < bs ? N - at : bs; px_engine_process(&e, in + at, out + at, n); }
}
static float pitch(int from, int len)               /* autocorrelation, 35 Hz .. 1 kHz */
{
    static double ac[1260]; double bv = -1e30;
    for (int lag = 44; lag < 1260; lag++) { double s = 0; for (int i = 0; i < len; i++) s += out[from + i] * out[from + i + lag]; ac[lag] = s; if (s > bv) bv = s; }
    for (int lag = 45; lag < 1259; lag++) if (ac[lag] > 0.9 * bv && ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1]) return 44100.0f / (float)lag;   /* the first period, not a multiple */
    return 0.0f;
}
int main(void)
{
    static const uint8_t KN[3] = {0, 50, 100};
    for (int t = PX_SYN_ORGAN; t < PX_SYN_COUNT; t++) {
        if (!px_syn_runs(t)) continue;                 /* (retired: tombstones, tests/engine/syn_ids_test.c) */
        for (int k = 0; k < 5; k++) for (int v = 0; v < 3; v++) {
            uint8_t q[5]; px_type_defaults(PX_B_SYN, t, q); q[k] = KN[v];
            pluck(110.0f); render(t, q, 64);
            float pk = 0.0f; int nan = 0;
            for (int i = 0; i < N; i++) { if (out[i] != out[i]) nan = 1; if (fabsf(out[i]) > pk) pk = fabsf(out[i]); }
            CHECK(!nan && pk < 4.0f, "%s knob %d = %d: finite and bounded (peak %.2f)", px_type_name(PX_B_SYN, t), k, KN[v], pk);
            if (k != 4 || v) CHECK(pk > 1e-3f, "%s knob %d = %d: it sounds", px_type_name(PX_B_SYN, t), k, KN[v]);
        }
        uint8_t q[5]; px_type_defaults(PX_B_SYN, t, q);
        memset(in, 0, sizeof in); render(t, q, 64);
        float pk = 0.0f; for (int i = 0; i < N; i++) if (fabsf(out[i]) > pk) pk = fabsf(out[i]);
        CHECK(pk < 1e-4f, "%s: silent on silence (%.2g)", px_type_name(PX_B_SYN, t), pk);
        pluck(146.83f); render(t, q, 64);
        static float ref[N]; memcpy(ref, out, sizeof ref);
        static const int BS[3] = {48, 16, 1};
        for (int b = 0; b < 3; b++) { render(t, q, BS[b]); CHECK(!memcmp(ref, out, sizeof ref), "%s: block size %d gives the same sound", px_type_name(PX_B_SYN, t), BS[b]); }
    }
    static const int LEADS[] = {PX_SYN_SQ_BRASS, PX_SYN_SQ_FLUTE};
    static const float NOTES[] = {82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f};
    for (unsigned l = 0; l < sizeof LEADS / sizeof *LEADS; l++) for (unsigned j = 0; j < 6; j++) {
        const int t = LEADS[l]; uint8_t q[5]; px_type_defaults(PX_B_SYN, t, q); q[3] = 0;   /* SUB off: the note itself */
        pluck(NOTES[j]); render(t, q, 64);
        const float want = NOTES[j] * (t == PX_SYN_BS_SUBBASS ? 0.5f : 1.0f), got = pitch(2646, 4096);
        const float c = 1200.0f * log2f(got / want);
        if (!(want < 50.0f)) CHECK(fabsf(c) < 20.0f, "%s plays %.1f Hz as %.1f Hz (%+.0f cents)", px_type_name(PX_B_SYN, t), want, got, c);
    }
    printf("%s: %d problems\n", bad ? "banksyn FAILED" : "banksyn ok", bad);
    return bad != 0;
}
