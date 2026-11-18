/* OpenPocket v48: the tracked acoustic voices (engine/px_acoustic.h) on the PC, with the pedal's libm.
 *   cc -O2 -Iengine -Ifirmware tests/engine/acoustic_test.c firmware/px_math.c -lm -o build/acoustic_test
 *   build/acoustic_test [assets/loops/lead.wav]           checks (tests/run_tests.sh)
 *   build/acoustic_test --calibrate                       prints the brass drive / pitch table (PXA_BCAL)
 *   build/acoustic_test --levels assets/loops/lead.wav    prints the output gains (PXA_G_*) that match BRASS's level
 *   build/acoustic_test --truth build/acoustic/lead.truth.csv   the note layer against the lead loop's transcription
 * Checks: steady notes in tune (each voice, driven directly), plucked notes through the whole engine land on the
 * right pitch (the note layer and the tracker), every type stays finite and falls silent after the guitar stops, and
 * on the lead loop each type is within 4 dB of BRASS's level.
 */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100.0f
static px_engine E;
static const int TYPES[4] = {PX_SYN_TRUMPET, PX_SYN_TRUMPET_MODEL, PX_SYN_CELLO, PX_SYN_CELLO_SECTION};
static const char *TN[4] = {"TRUMPET", "TRUMPET MODEL", "CELLO", "CELLO SECTION"};

static void set_type(int t)
{
    px_engine_init(&E, SR);
    px_preset_t p; memset(&p, 0, sizeof p); strcpy(p.name, "ACOUSTIC");
    for (int b = 0; b < PX_NBLK; b++) { p.order[b] = (uint8_t)b; for (int k = 0; k < PX_NPAR; k++) p.p[b][k] = 50; }
    p.on[PX_B_SYN] = 1; p.type[PX_B_SYN] = (uint8_t)t; p.p[PX_B_SYN][4] = 0;   /* DRY 0 */
    px_engine_set(&E, &p);
}

static float midi_hz(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }
static float cents(float a, float b) { return 1731.234f * logf(a / b); }

static float pitch_of(const float *y, int len, float lo, float hi)
{
    int lmin = (int)(SR / hi), lmax = (int)(SR / lo), bl = lmin; double best = -2;
    double *r = calloc(lmax + 3, sizeof *r);
    for (int L = lmin - 1; L <= lmax + 1; L++) { double s = 0, ea = 0, eb = 0;
        for (int i = 0; i < len; i++) { double a = y[i], b = y[i + L]; s += a * b; ea += a * a; eb += b * b; }
        r[L - lmin + 1] = s / sqrt(ea * eb + 1e-30); }
    for (int L = lmin; L <= lmax; L++) if (r[L - lmin + 1] > best) { best = r[L - lmin + 1]; bl = L; }
    for (int L = lmin; L < bl; L++) if (r[L - lmin + 1] > 0.92 * best && r[L - lmin + 1] >= r[L - lmin] && r[L - lmin + 1] >= r[L - lmin + 2]) { bl = L; break; }
    double a = r[bl - lmin], b = r[bl - lmin + 1], c = r[bl - lmin + 2], den = a - 2 * b + c, d = den != 0 ? 0.5 * (a - c) / den : 0;
    free(r); return (float)(SR / (bl + d));
}

/* the voice alone, with known pitch (guitar Hz) and expression */
static void steady(int t, float fg, float ex, float *y, int n)
{
    set_type(t);
    px_acoustic *s = &E.syn_ac; pxa_rate_consts(s);
    for (int i = 0; i < n; i++) { s->gate = 1; s->onset = i == 0; s->vel = ex; s->expr = ex; s->f0s = fg; y[i] = pxa_voice(&E, s, t); }
}

/* a plucked string (Karplus-Strong): guitar note m for dur s, then muted */
static void pluck(float m, float vel, float dur, float *x, int n)
{
    static float line[4096]; memset(line, 0, sizeof line); int w = 0; float prev = 0, lp = 0, hx = 0, hy = 0; uint32_t r = 7;
    const float f = midi_hz(m), N = SR / f - 0.5f; int exc = (int)(SR / f);
    for (int i = 0; i < n; i++) {
        float rp = (float)w - N; if (rp < 0) rp += 4096.0f; int i0 = (int)rp; float fr = rp - i0;
        float out = line[i0 & 4095] + (line[(i0 + 1) & 4095] - line[i0 & 4095]) * fr, e = 0;
        if (exc > 0) { r = r * 1664525u + 1013904223u; lp += (0.15f + 0.8f * vel) * ((float)(r >> 8) / 8388608.0f - 1.0f - lp); e = lp * 0.9f * vel; exc--; }
        const float loss = i < (int)(dur * SR) ? 0.9975f : 0.7f;
        line[w] = loss * 0.5f * (out + prev) + e; prev = out; w = (w + 1) & 4095;
        float hp = out - hx + 0.995f * hy; hx = out; hy = hp; x[i] = 0.45f * hp;
    }
}

static void engine_run(const float *x, float *y, int n)
{ for (int i = 0; i < n; i += 64) { int b = n - i < 64 ? n - i : 64; px_engine_process(&E, x + i, y + i, b); } }

static float *wav16(const char *fn, int *n)                      /* the bundled loops: mono PCM16 */
{
    FILE *f = fopen(fn, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 44, SEEK_SET);
    int m = (int)((sz - 44) / 2); float *x = malloc(sizeof(float) * m); int16_t v;
    for (int i = 0; i < m; i++) { if (fread(&v, 2, 1, f) != 1) { m = i; break; } x[i] = v / 32768.0f; }
    fclose(f); *n = m; return x;
}
static double rms_db(const float *y, int n) { double s = 0; for (int i = 0; i < n; i++) s += (double)y[i] * y[i]; return 10 * log10(s / n + 1e-30); }

static void calibrate(void)
{
    const int n = (int)(0.9f * SR); float *y = malloc(sizeof(float) * n);
    for (int k = 0; k < PXA_BCAL_N; k++) pxa_bcal_tab[k] = 1.0f;
    for (int k = 0; k < PXA_BCAL_N; k++) {
        const float target = midi_hz(PXA_BCAL_LO + 2.0f * k);
        float r = 1.0f;
        for (int it = 0; it < 6; it++) {
            pxa_bcal_tab[k] = r;
            steady(PX_SYN_TRUMPET_MODEL, target / 2.0f, 0.7f, y, n);
            float m = pitch_of(y + (int)(0.5f * SR), (int)(0.3f * SR), target * 0.6f, target * 1.6f);
            r *= target / m;
        }
        pxa_bcal_tab[k] = r;
    }
    printf("static const float PXA_BCAL[PXA_BCAL_N] = {\n   ");
    for (int k = 0; k < PXA_BCAL_N; k++) printf(" %.5ff,%s", pxa_bcal_tab[k], k == 8 ? "\n   " : "");
    printf("};\n");
    free(y);
}

/* the note layer on the lead loop against its transcription (tools/sim/acoustic_truth.py makes the file): a played
 * note is right when the layer's pitch is within 50 cents for most of the middle half of the time it is the newest */
static int truth_check(const char *wav, const char *csv, int verbose)
{
    FILE *f = fopen(csv, "r"); if (!f) return -1;
    float a[256], b[256], m[256], v[256]; int k = 0; char line[256];
    while (fgets(line, sizeof line, f) && k < 256) { if (line[0] == '#') continue; if (sscanf(line, "%f,%f,%f,%f", &a[k], &b[k], &m[k], &v[k]) == 4) k++; }
    fclose(f);
    int n; float *x = wav16(wav, &n); if (!x) return -1;
    float *f0 = malloc(sizeof(float) * n); float y;
    set_type(PX_SYN_CELLO);
    for (int i = 0; i < n; i++) { px_engine_process(&E, x + i, &y, 1); f0[i] = E.syn_ac.gate ? E.syn_ac.f0s : 0.0f; }
    int played = 0, ok = 0;
    for (int i = 0; i < k; i++) {
        float end = b[i]; for (int j = i + 1; j < k; j++) if (a[j] > a[i] + 0.001f) { if (a[j] < end) end = a[j]; break; }
        if (b[i] - a[i] < 0.06f || v[i] < 30 || end - a[i] < 0.03f) continue;
        played++;
        int i0 = (int)(a[i] * SR), i1 = (int)(end * SR); if (i0 < 0) i0 = 0; if (i1 > n) i1 = n;
        int good = 0, tot = 0; const float tf = midi_hz(m[i]);
        for (int q = i0 + (i1 - i0) / 4; q < i1 - (i1 - i0) / 4; q++) { tot++; if (f0[q] > 0 && fabsf(cents(f0[q], tf)) < 50) good++; }
        if (good * 2 > tot) ok++; else if (verbose) printf("  missed %.3f s MIDI %.0f\n", a[i], m[i]);
    }
    free(x); free(f0);
    printf("lead loop against its transcription: %d of %d notes\n", ok, played);
    return played - ok;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--truth")) { truth_check("assets/loops/lead.wav", argv[2], 1); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--calibrate")) { calibrate(); return 0; }
    const int levels = argc > 2 && !strcmp(argv[1], "--levels");
    const char *loop = levels ? argv[2] : (argc > 1 ? argv[1] : "assets/loops/lead.wav");
    int bad = 0;
    const int n = (int)(1.2f * SR); float *y = malloc(sizeof(float) * n * 4), *x = malloc(sizeof(float) * n * 4);   /* (room for 2 s) */

    if (!levels) {
        /* 1. steady notes, the voice alone: within 10 cents */
        const float gm[3] = {40, 52, 64};                     /* guitar E2, E3, E4 */
        for (int v = 0; v < 4; v++) for (int q = 0; q < 3; q++) {
            const float fg = midi_hz(gm[q]);
            steady(TYPES[v], fg, 0.7f, y, n);
            const float want = fg * E.syn_ac.oct_mul, got = pitch_of(y + (int)(0.6f * SR), (int)(0.3f * SR), want * 0.7f, want * 1.4f);
            float pk = 0; for (int i = (int)(0.6f * SR); i < (int)(0.9f * SR); i++) pk = fabsf(y[i]) > pk ? fabsf(y[i]) : pk;
            const float c = cents(got, want);
            if (fabsf(c) > 10.0f || pk < 1e-3f) { bad++; printf("PROBLEM %s steady %.1f Hz: %.1f Hz (%+.0f cents, peak %.4f)\n", TN[v], want, got, c, pk); }
        }
        /* 2. a plucked note through the whole engine (note layer + tracker): the instrument's pitch */
        const float pm[6] = {40, 45, 50, 55, 59, 64};
        for (int v = 0; v < 4; v++) for (int q = 0; q < 6; q++) {
            set_type(TYPES[v]);
            const int m = (int)(2.0f * SR);
            pluck(pm[q], 0.8f, 1.0f, x, m); engine_run(x, y, m);
            const float want = midi_hz(pm[q]) * E.syn_ac.oct_mul;
            const float got = pitch_of(y + (int)(0.45f * SR), (int)(0.3f * SR), want * 0.7f, want * 1.4f);
            const float c = cents(got, want);
            int finite = 1; for (int i = 0; i < m; i++) if (!isfinite(y[i])) finite = 0;
            float tail = 0; for (int i = (int)(1.8f * SR); i < m; i++) tail = fabsf(y[i]) > tail ? fabsf(y[i]) : tail;
            if (!finite || fabsf(c) > 25.0f || tail > 1e-3f) { bad++;
                printf("PROBLEM %s plucked MIDI %.0f: %.1f Hz for %.1f (%+.0f cents)%s, tail %.5f\n", TN[v], pm[q], got, want, c, finite ? "" : " NOT FINITE", tail); }
        }
    }
    /* 3. the note layer on the lead loop, when its transcription has been made (tools/sim/acoustic_truth.py) */
    if (!levels) { int miss = truth_check(loop, "build/acoustic/lead.truth.csv", 1);
        if (miss < 0) printf("(no build/acoustic/lead.truth.csv: the lead loop's note check skipped)\n");
        else if (miss > 2) { bad++; printf("PROBLEM %d notes of the lead loop missed\n", miss); } }
    /* 4. level on the lead loop against BRASS */
    int ln; float *lx = wav16(loop, &ln);
    if (!lx) { printf("PROBLEM no %s\n", loop); return 1; }
    float *ly = malloc(sizeof(float) * ln);
    set_type(PX_SYN_BRASS); engine_run(lx, ly, ln); const double ref = rms_db(ly, ln);
    for (int v = 0; v < 4; v++) {
        set_type(TYPES[v]); engine_run(lx, ly, ln); const double r = rms_db(ly, ln);
        if (levels) printf("%-14s %6.1f dB, BRASS %6.1f dB: gain x %.3f\n", TN[v], r, ref, pow(10, (ref - r) / 20));
        else if (fabs(r - ref) > 4.0) { bad++; printf("PROBLEM %s on %s: %.1f dB, BRASS %.1f dB\n", TN[v], loop, r, ref); }
    }
    if (!levels) printf("acoustic voices: %d problems\n", bad);
    return bad != 0;
}
