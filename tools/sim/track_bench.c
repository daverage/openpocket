/* Host benchmark of the note trackers (v49, engine/px_track.h; docs/NOTE-TRACKING.md). Not firmware.
 *   cc -O2 -Iengine -Ifirmware tools/sim/track_bench.c firmware/px_math.c -lm -o build/track_bench
 *   build/track_bench IN.wav OUT.csv
 * One row per tracker frame (1.45 ms): time, the new tracker's mono f0 / confidence / onset / level / notes, and for
 * comparison synth_hq_track as BASS HQ feeds it (old_layer: 0 since v49 removed the v48 note layer). tools/sim/track_eval.py scores the CSV against annotations. */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include "pulse_poly.h"
#ifdef PXT_HOST_JOINT
#include "joint_track.h"
#endif
#include <stdlib.h>
#include <string.h>

static float *wav_read(const char *fn, int *n, int *rate)
{
    FILE *f = fopen(fn, "rb"); if (!f) return 0;
    unsigned char h[12]; if (fread(h, 1, 12, f) != 12) { fclose(f); return 0; }
    int ch = 1, bits = 16, fmt = 1; long dpos = 0; unsigned dsz = 0; *rate = 44100;
    for (;;) { unsigned char c[8]; if (fread(c, 1, 8, f) != 8) break; unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { unsigned char b[16]; fread(b, 1, 16, f); fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; *rate = b[4] | b[5] << 8 | b[6] << 16; bits = b[14] | b[15] << 8; fseek(f, sz - 16, SEEK_CUR); }
        else if (!memcmp(c, "data", 4)) { dpos = ftell(f); dsz = sz; break; }
        else fseek(f, (sz + 1) & ~1u, SEEK_CUR); }
    if (!dpos) { fclose(f); return 0; }
    int bps = bits / 8, m = dsz / (bps * ch); float *x = malloc(sizeof(float) * m);
    for (int i = 0; i < m; i++) { double a = 0; for (int c = 0; c < ch; c++) {
        if (fmt == 3) { float v; fread(&v, 4, 1, f); a += v; }
        else if (bits == 16) { int16_t v; fread(&v, 2, 1, f); a += v / 32768.0; }
        else if (bits == 24) { unsigned char b[3]; fread(b, 1, 3, f); int v = b[0] | b[1] << 8 | b[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF; a += v / 8388608.0; }
        else { int32_t v; fread(&v, 4, 1, f); a += v / 2147483648.0; } }
        x[i] = (float)(a / ch); }
    fclose(f); *n = m; return x;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s IN.wav OUT.csv [gain] [pulse-output.f32]\n", argv[0]); return 1; }
    int n, rate; float *x = wav_read(argv[1], &n, &rate);
    if (!x) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    if (rate != 44100) fprintf(stderr, "warning: %d Hz\n", rate);
    const float g = argc > 3 ? (float)atof(argv[3]) : 1.0f;
    static px_engine e; px_engine_init(&e, 44100.0f);
    static px_track t; memset(&t, 0, sizeof t); pxt_prepare(&e);
    pxt_layer L; pxt_layer_init(&L, 44100.0f);
    /* for comparison: BASS HQ's tracker alone (the v48 acoustic voices' note layer is gone: its scores are in
     * docs/NOTE-TRACKING.md) */
    px_synth_tracking hq; memset(&hq, 0, sizeof hq); px_bq f1, f2; synth_hq_filters(&f1, &f2, 44100.0f); bq_reset(&f1); bq_reset(&f2);
    float env = 0;
    FILE *o = fopen(argv[2], "w");
    if (!o) return 1;
    FILE *pulse = argc > 4 ? fopen(argv[4], "wb") : NULL;
    if (argc > 4 && !pulse) { fclose(o); return 1; }
    pulse_poly_voice voices[2] = {{0}};
    e.syn_res = 0.25f;
    fprintf(o, "t,mono,conf,onset,level,old_layer,old_hq,layer,nn,n1,n2,n3,n4,n5,n6,v1,v2,pick1,pick2\n");
    for (int i = 0; i < n; i++) {
        const float v = x[i] * g;
        const float b = bq_run(&f2, bq_run(&f1, v)), a = fabsf(v);
        env += (a - env) * (a > env ? 0.0224f : 0.00075f);
        synth_hq_track(&hq, b, env > 0.003f, 44100.0f);
        const int fr = pxt_run(&e, &t, v);
        pxt_layer_run(&L, &t, v, fr);
#ifdef PXT_HOST_JOINT
        if (fr) joint_candidates(&t);
#endif
        int pick[2] = {0};
        if (pulse) {
            int active[2] = {voices[0].active, voices[1].active};
            if (fr) pulse_poly_frame(voices, &t);
            for (int v = 0; v < 2; ++v)
                pick[v] = voices[v].retrigger || (!active[v] && voices[v].active);
            float output = pulse_poly_sample(&e, voices);
            if (fwrite(&output, sizeof output, 1, pulse) != 1) { fclose(pulse); fclose(o); return 1; }
        }
        if (fr) {
            fprintf(o, "%.5f,%.3f,%.3f,%.2f,%.3g,%.3f,%.3f,%.3f,%d", (i + 1) / 44100.0, t.mono, t.conf, t.onset, t.level,
                    0.0f, hq.step > 0 ? 4 * hq.step * 44100.0f : 0.0f, L.gate ? L.f0 : 0.0f, t.nnotes);
            for (int k = 0; k < 6; k++) fprintf(o, ",%.3f", k < t.nnotes ? t.notes[k] : 0.0f);
            fprintf(o, ",%.3f,%.3f,%d,%d\n", voices[0].active ? voices[0].pitch : 0.0f,
                    voices[1].active ? voices[1].pitch : 0.0f, pick[0], pick[1]);
        }
    }
    if (pulse && fclose(pulse) != 0) { fclose(o); return 1; }
    fclose(o);
    free(x);
    return 0;
}
