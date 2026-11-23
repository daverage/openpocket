/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host harness for the VALVE (triode) stage in engine/px_dsp.c. It #includes the engine source, so the firmware is NOT
 * modified: the very same triode_block / triode_load_line run here as on the pedal (44.1 kHz, x2 oversampled, 64-sample blocks).
 *
 *   valve_host run  in.f32 outprefix drive_db      in.f32 = mono float32, 44.1 kHz, engine units (x4 V at 0 dB drive)
 *       writes  <p>.final.f32  (n)    the engine's real output with mix = 1 (incl. the x1.05075 calibration)
 *               <p>.wet.f32    (2n)   the stage output at 88.2 kHz before the half-band down-sampler: (Ia-Ia0)*makeup - DC
 *               <p>.vin.f32 .vk.f32 .ig.f32 .ia.f32 .ik.f32 (2n)  internal valve state per oversampled sample
 *   valve_host post in.f32 wet88.f32 outfile drive_db
 *       runs the engine's OUTPUT stage (half-band down, dry add, x1.05075) on an externally supplied wet signal
 *       (e.g. the SPICE one, already in engine units), so the fixed scaling is applied separately from the circuit.
 * Also checks that the exporting replica loop is bit-identical to triode_block.                                    */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"

#define FS 44100.0f
#define BLK 64

static float *rd(const char *fn, int *n)
{
    FILE *f = fopen(fn, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", fn); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    float *b = malloc((size_t)sz + 4); *n = (int)(sz / 4);
    if (fread(b, 4, (size_t)*n, f) != (size_t)*n) exit(2);
    fclose(f); return b;
}
static void wr(const char *pre, const char *ext, const float *b, int n)
{
    char fn[1024]; snprintf(fn, sizeof fn, "%s.%s.f32", pre, ext);
    FILE *f = fopen(fn, "wb"); fwrite(b, 4, (size_t)n, f); fclose(f);
}
static void prime(px_engine *e, float drive_db)
{
    px_engine_init(e, FS);
    triode_reset(e);
    e->triode.drive = 4.0f * db2lin(drive_db);       /* exactly line 1715: 4 * db2lin(valve_drive_db(...)) */
    e->triode.mix = 1.0f;
    e->run.valve_on = 1;
}

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "run")) {
        int n; float *x = rd(argv[2], &n); n -= n % BLK;
        const char *pre = argv[3]; const float db = (float)atof(argv[4]);
        static px_engine ea, eb;
        prime(&ea, db); prime(&eb, db);
        float *fin = calloc((size_t)n, 4), *ref = calloc((size_t)n, 4);
        float *o[6]; for (int k = 0; k < 6; k++) o[k] = calloc((size_t)(2 * n), 4);   /* wet vin vk ig ia ik */
        for (int at = 0; at < n; at += BLK) {
            /* 1. the real thing */
            memcpy(ref + at, x + at, BLK * 4); triode_block(&ea, ref + at, BLK);
            /* 2. replica of triode_block with the state exported (same arithmetic, same order) */
            float *samples = fin + at; memcpy(samples, x + at, BLK * 4);
            const float drive = eb.triode.drive, makeup = PXT_PLATE_SCALE / drive;
            hb1_up(&eb.triode.up, samples, px_os_u, BLK, px_os_d);
            for (int i = 0; i < 2 * BLK; ++i) {
                const float xx = px_os_u[i], vin = drive * xx, vk = eb.triode.cathode_v;
                const triode_currents cur = triode_load_line(vin, vk);
                const float ig = triode_grid_current(vin - vk);
                eb.triode.cathode_v += eb.triode.cathode_step * (PXT_CATHODE_KOHM * cur.cathode - vk);
                const float wet = (cur.plate - PXT_PLATE_IDLE) * makeup;
                eb.triode.output_dc += eb.triode.dc_step * (wet - eb.triode.output_dc);
                px_os_u[i] = wet - eb.triode.output_dc - xx;
                const int j = 2 * at + i;
                o[0][j] = wet - eb.triode.output_dc; o[1][j] = vin; o[2][j] = vk; o[3][j] = ig; o[4][j] = cur.plate; o[5][j] = cur.cathode;
            }
            hb1_down(&eb.triode.down, px_os_u, px_os_t, BLK, px_os_d);
            for (int i = 0; i < BLK; ++i) samples[i] += eb.triode.mix * ((samples[i] + px_os_t[i]) * 1.05075f - samples[i]);
        }
        double md = 0, pk = 1e-9; for (int i = 0; i < n; i++) { double d = fabs((double)fin[i] - ref[i]); if (d > md) md = d; if (fabs(ref[i]) > pk) pk = fabs(ref[i]); }
        md /= pk;   /* relative to the output peak */
        fprintf(stderr, "replica vs triode_block: max |diff|/peak = %g (%s)\n", md, md == 0 ? "bit-identical" : md < 1e-5 ? "float rounding only" : "MISMATCH");
        wr(pre, "final", ref, n);
        const char *nm[6] = {"wet", "vin", "vk", "ig", "ia", "ik"};
        for (int k = 0; k < 6; k++) wr(pre, nm[k], o[k], 2 * n);
        return md < 1e-5 ? 0 : 3;
    }
    if (argc >= 6 && !strcmp(argv[1], "post")) {
        int n, m; float *x = rd(argv[2], &n); float *w = rd(argv[3], &m); n -= n % BLK;
        if (m < 2 * n) { fprintf(stderr, "wet signal too short\n"); return 2; }
        static px_engine e; prime(&e, (float)atof(argv[5]));
        float *y = calloc((size_t)n, 4);
        for (int at = 0; at < n; at += BLK) {
            float *s = y + at; memcpy(s, x + at, BLK * 4);
            hb1_up(&e.triode.up, s, px_os_u, BLK, px_os_d);
            for (int i = 0; i < 2 * BLK; ++i) px_os_u[i] = w[2 * at + i] - px_os_u[i];
            hb1_down(&e.triode.down, px_os_u, px_os_t, BLK, px_os_d);
            for (int i = 0; i < BLK; ++i) s[i] = (s[i] + px_os_t[i]) * 1.05075f;
        }
        FILE *f = fopen(argv[4], "wb"); fwrite(y, 4, (size_t)n, f); fclose(f);
        return 0;
    }
    fprintf(stderr, "usage: valve_host run in.f32 prefix drive_db | post in.f32 wet88.f32 out.f32 drive_db\n");
    return 1;
}
