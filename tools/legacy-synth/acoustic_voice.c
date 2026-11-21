/* Host listening prototype: guitar-driven acoustic voices (docs/research/REALISTIC-INSTRUMENT-SYNTHESIS.md, its handoff
 * docs/archive/REALISTIC-SYNTH-HANDOFF.md, results in docs/research/REALISTIC-INSTRUMENT-PROTOTYPE.md). Not firmware.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   tools/legacy-synth/acoustic_voice.sh            (builds the bank with acoustic_prep.py, then this, then renders everything)
 *   build/acoustic_voice BANK OUTDIR [DI.wav ...]
 *
 * Two instruments (solo trumpet one octave above the guitar, cello-range bowed voice at the guitar's pitch), each
 * with two voices:
 *   SAMPLE     looped recordings (VSCO 2 CE, CC0; tools/legacy-synth/acoustic_prep.py): nearest root, soft / loud layer chosen
 *              by pick strength, continuous expression as level + brightness, crossfade on repick and on a root change
 *              during a slur, a shaped release.
 *   WAVEGUIDE  bowed string (bow table, two delay lines split at the bow, string loss filter, body resonances) and lip
 *              reed brass (lip resonator, squared pressure difference, bore delay, DC block, bell). The structure and
 *              the starting constants follow the published algorithms as implemented in the Synthesis ToolKit
 *              (STK Bowed.cpp / Brass.cpp, Perry Cook and Gary Scavone; STK licence: github.com/thestk/stk LICENSE);
 *              written anew here, not copied. Read STK's notice before any of this goes into the firmware.
 * Each voice is driven by one control stream (pitch, gate, onset, pick strength, expression per sample), coming
 * either from a FIXTURE (a score: known pitch / gate / expression, no tracking at all) or from a DI recording through
 * the engine's own single-note HQ tracker (synth_hq_track, the PWM HQ / BASS HQ one) plus a note layer here.
 *
 * Also renders the engine's current BRASS and STRINGS on the same DI, and a synthetic DI of the fixture score
 * (Karplus-Strong strings with known pitch and onsets), on which the tracked path is measured against the truth.
 * Writes OUTDIR/raw/ (as rendered), OUTDIR/matched/ (each at -20 dBFS RMS) and OUTDIR/report.md.
 */
#include "../../engine/px_dsp.c"        /* the engine as one unit: its tracker, filters and helpers are static */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR 44100.0f
#define TWO_PI 6.283185307f

/* ------------------------------------------------------------------------------------------------ WAV I/O --- */
static int rd32(FILE *f, uint32_t *v) { uint8_t b[4]; if (fread(b, 1, 4, f) != 4) return 0;
    *v = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; return 1; }
static int rd16(FILE *f, uint16_t *v) { uint8_t b[2]; if (fread(b, 1, 2, f) != 2) return 0; *v = b[0] | b[1] << 8; return 1; }
static void wr32(FILE *f, uint32_t v) { uint8_t b[4] = {v, v >> 8, v >> 16, v >> 24}; fwrite(b, 1, 4, f); }
static void wr16(FILE *f, uint16_t v) { uint8_t b[2] = {v, v >> 8}; fwrite(b, 1, 2, f); }

static float *wav_read(const char *fn, int *n)                  /* mono mix, 44.1 kHz expected */
{
    FILE *f = fopen(fn, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", fn); return 0; }
    char id[4]; uint32_t sz; if (fread(id, 1, 4, f) != 4 || !rd32(f, &sz) || fread(id, 1, 4, f) != 4) { fclose(f); return 0; }
    uint16_t fmt = 1, ch = 1, bits = 16; uint32_t rate = 0, dsz = 0; long dpos = 0;
    for (;;) { char ck[4]; uint32_t cs; if (fread(ck, 1, 4, f) != 4 || !rd32(f, &cs)) break;
        if (!memcmp(ck, "fmt ", 4)) { uint32_t br; uint16_t ba; rd16(f, &fmt); rd16(f, &ch); rd32(f, &rate); rd32(f, &br);
            rd16(f, &ba); rd16(f, &bits); if (cs > 16) fseek(f, cs - 16, SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) { dsz = cs; dpos = ftell(f); fseek(f, (cs + 1) & ~1u, SEEK_CUR);
        } else fseek(f, (cs + 1) & ~1u, SEEK_CUR); }
    if (!dpos) { fclose(f); return 0; }
    if (rate != 44100) fprintf(stderr, "warning: %s is %u Hz, processed as 44.1 kHz\n", fn, rate);
    int bytes = bits / 8, frames = dsz / (bytes * ch);
    float *x = malloc(sizeof(float) * frames); fseek(f, dpos, SEEK_SET);
    for (int i = 0; i < frames; i++) { double acc = 0;
        for (int c = 0; c < ch; c++) { double s = 0;
            if (fmt == 3 && bits == 32) { float v; fread(&v, 4, 1, f); s = v; }
            else if (bits == 16) { int16_t v; fread(&v, 2, 1, f); s = v / 32768.0; }
            else if (bits == 24) { uint8_t b[3]; fread(b, 1, 3, f); int32_t v = b[0] | b[1] << 8 | b[2] << 16;
                if (v & 0x800000) v |= ~0xFFFFFF; s = v / 8388608.0; }
            else if (bits == 32) { int32_t v; fread(&v, 4, 1, f); s = v / 2147483648.0; }
            acc += s; }
        x[i] = (float)(acc / ch); }
    fclose(f); *n = frames; return x;
}

static void wav_write(const char *fn, const float *x, int n, float gain)
{
    FILE *f = fopen(fn, "wb"); if (!f) { fprintf(stderr, "cannot write %s\n", fn); exit(1); }
    fwrite("RIFF", 1, 4, f); wr32(f, 36 + n * 2); fwrite("WAVEfmt ", 1, 8, f); wr32(f, 16); wr16(f, 1); wr16(f, 1);
    wr32(f, 44100); wr32(f, 88200); wr16(f, 2); wr16(f, 16); fwrite("data", 1, 4, f); wr32(f, n * 2);
    for (int i = 0; i < n; i++) { float v = x[i] * gain; v = v > 1 ? 1 : v < -1 ? -1 : v; wr16(f, (uint16_t)(int16_t)lrintf(v * 32767.0f)); }
    fclose(f);
}

static double rms_db(const float *x, int n) { double s = 0; for (int i = 0; i < n; i++) s += (double)x[i] * x[i];
    return 10.0 * log10(s / (n ? n : 1) + 1e-20); }
static float peak_abs(const float *x, int n) { float p = 0; for (int i = 0; i < n; i++) p = fabsf(x[i]) > p ? fabsf(x[i]) : p; return p; }

static float midi_hz(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }
static float hz_midi(float f) { return 69.0f + 12.0f * log2f(f / 440.0f); }

/* --------------------------------------------------------------------------------------- the control stream --- */
typedef struct { float f0, expr, vel; uint8_t gate, onset; } ctl_t;   /* per sample; f0 = guitar pitch (Hz) */

/* ------------------------------------------------------------------------------------------ fixture score --- */
typedef struct { float t, dur, midi, vel; int legato; float bend, bend_at, bend_time, vib_cents; int swell; } note_t;
/* guitar pitches; quiet and accented picks, a swell with vibrato, a slur then a bend, fast repicks on a ringing
 * string, a slide, short stabs, a long note with vibrato, a high note. */
static const note_t SCORE[] = {
    {0.20f, 0.80f, 40, 0.30f, 0, 0, 0, 0, 0, 0},              /* E2 quiet */
    {1.20f, 0.50f, 45, 1.00f, 0, 0, 0, 0, 0, 0},              /* A2 accent */
    {1.90f, 0.90f, 50, 0.35f, 0, 0, 0, 0, 0, 0},              /* D3 quiet */
    {3.00f, 2.50f, 55, 0.45f, 0, 0, 0, 0, 18, 1},             /* G3 held swell, vibrato */
    {5.80f, 0.60f, 57, 0.70f, 0, 0, 0, 0, 0, 0},              /* A3 ... */
    {6.40f, 1.20f, 60, 0.70f, 1, 2.0f, 0.50f, 0.25f, 0, 0},   /* ... slur to C4, then bend to D4 */
    {8.10f, 0.15f, 52, 0.90f, 0, 0, 0, 0, 0, 0},              /* E3 fast repicks */
    {8.25f, 0.15f, 52, 0.60f, 0, 0, 0, 0, 0, 0},
    {8.40f, 0.15f, 52, 0.90f, 0, 0, 0, 0, 0, 0},
    {8.55f, 0.15f, 52, 0.60f, 0, 0, 0, 0, 0, 0},
    {8.70f, 0.15f, 52, 0.90f, 0, 0, 0, 0, 0, 0},
    {8.85f, 0.20f, 52, 0.60f, 0, 0, 0, 0, 0, 0},
    {9.40f, 1.20f, 59, 0.80f, 0, 5.0f, 0.25f, 0.20f, 0, 0},   /* B3 slide up to E4 */
    {10.90f, 0.12f, 43, 1.00f, 0, 0, 0, 0, 0, 0},             /* G2 stabs, muted */
    {11.25f, 0.12f, 43, 1.00f, 0, 0, 0, 0, 0, 0},
    {11.60f, 0.12f, 46, 1.00f, 0, 0, 0, 0, 0, 0},
    {12.20f, 1.60f, 52, 0.60f, 0, 0, 0, 0, 12, 0},            /* E3 long, vibrato */
    {14.20f, 0.90f, 64, 0.50f, 0, 0, 0, 0, 0, 0},             /* E4 */
};
#define NSCORE ((int)(sizeof SCORE / sizeof SCORE[0]))
#define SCORE_LEN 15.6f

static float smooth01(float u) { u = u < 0 ? 0 : u > 1 ? 1 : u; return u * u * (3 - 2 * u); }
static float note_midi_at(const note_t *n, float tt)
{
    float m = n->midi;
    if (n->bend != 0.0f) m += n->bend * smooth01((tt - n->bend_at) / n->bend_time);
    if (n->vib_cents > 0.0f && tt > 0.4f) m += n->vib_cents * 0.01f * smooth01((tt - 0.4f) / 0.4f) * sinf(TWO_PI * 5.5f * tt);
    return m;
}
static int note_at(float t) { int k = -1; for (int i = 0; i < NSCORE; i++) if (t >= SCORE[i].t && t < SCORE[i].t + SCORE[i].dur) k = i; return k; }

static void fixture_controls(ctl_t *c, int n)
{
    int last = -1;
    for (int i = 0; i < n; i++) {
        float t = i / SR; int k = note_at(t);
        memset(&c[i], 0, sizeof c[i]);
        if (k < 0) { last = -1; continue; }
        const note_t *s = &SCORE[k]; float tt = t - s->t;
        c[i].gate = 1; c[i].f0 = midi_hz(note_midi_at(s, tt)); c[i].vel = s->vel;
        c[i].onset = k != last && !s->legato;
        c[i].expr = s->swell ? 0.15f + 0.85f * smooth01(tt / (s->dur * 0.7f))
                             : s->vel * (0.85f + 0.15f * expf(-tt / 0.2f));
        last = k;
    }
}

/* --------------------------------------------------- synthetic DI of the score (Karplus-Strong, known truth) --- */
static int note_at_in(const note_t *sc, int ns, float t) { int k = -1; for (int i = 0; i < ns; i++) if (t >= sc[i].t && t < sc[i].t + sc[i].dur) k = i; return k; }

static void synth_di_score(const note_t *sc, int ns, float *y, int n)
{
    static float line[4096]; int w = 0; float prev = 0, hpx = 0, hpy = 0, lp_exc = 0;
    uint32_t rnd = 12345; int last = -1, exc_left = 0; float exc_amp = 0, exc_k = 0, loss = 0.0f;
    memset(line, 0, sizeof line);
    for (int i = 0; i < n; i++) {
        float t = i / SR; int k = note_at_in(sc, ns, t);
        float f = 82.4f;
        if (k >= 0) {
            const note_t *s = &sc[k]; f = midi_hz(note_midi_at(s, t - s->t));
            if (k != last) {
                if (!s->legato) { exc_left = (int)(SR / f); exc_amp = 0.9f * s->vel; exc_k = 0.15f + 0.8f * s->vel; }
                else { exc_left = (int)(SR / f); exc_amp = 0.12f; exc_k = 0.3f; }   /* hammer-on: a small tap */
            }
            loss = 0.9975f;
        } else {
            /* between notes: muted (a palm or the fretting hand lifting), unless the next note starts at once */
            loss = 0.70f;                               /* about 40 dB in 50..100 ms over the guitar's range */
        }
        last = k;
        float N = SR / f - 0.5f, rp = (float)w - N; if (rp < 0) rp += 4096.0f;
        int i0 = (int)rp; float fr = rp - i0; float a = line[i0 & 4095], b = line[(i0 + 1) & 4095];
        float out = a + (b - a) * fr;
        float e = 0;
        if (exc_left > 0) { rnd = rnd * 1664525u + 1013904223u; float nz = (float)(rnd >> 8) / 8388608.0f - 1.0f;
            lp_exc += exc_k * (nz - lp_exc); e = lp_exc * exc_amp; exc_left--; }
        float s2 = loss * 0.5f * (out + prev); prev = out;
        line[w] = s2 + e; w = (w + 1) & 4095;
        float hp = out - hpx + 0.995f * hpy; hpx = out; hpy = hp;                 /* pickup: no DC */
        y[i] = 0.45f * hp;
    }
}
static void synth_di(float *y, int n) { synth_di_score(SCORE, NSCORE, y, n); }

/* ------------------------------------------------------------- tracked: engine HQ tracker + a note layer --- */
typedef struct { float t; float f0; float vel; int kind; } trig_t;            /* kind 0 attack, 1 slur root change */
typedef struct { int n; trig_t v[512]; } trig_log;

/* twoband = 0: the engine's HQ tracker exactly as BASS HQ / PWM HQ feed it (two 150 Hz low-passes: made for the low
 * strings; a lead above about 400 Hz loses its fundamental there). twoband = 1: a second copy of the same tracker on
 * 200 Hz .. 1 kHz, used while that band clearly carries more than the low one (hysteresis 0.3 / 0.6). */
static void tracked_controls(const float *x, int n, ctl_t *c, float *raw_f0, trig_log *log, int twoband)
{
    px_synth_tracking trk, trkh; memset(&trk, 0, sizeof trk); memset(&trkh, 0, sizeof trkh);
    px_bq f1, f2, h1, h2, u0, u1, u2; synth_hq_filters(&f1, &f2, SR); bq_reset(&f1); bq_reset(&f2);
    bq_highpass(&u0, SR, 200.0f, 0.7f); bq_lowpass(&u1, SR, 1000.0f, 0.54f); bq_lowpass(&u2, SR, 1000.0f, 1.31f);
    bq_reset(&u0); bq_reset(&u1); bq_reset(&u2);
    const float bk = onepole_k(SR, 0.020f); float el = 0, eh = 0; int high = 0, sel = 0, sel_n = 0;
    float ref_b[2] = {0, 0}; int stable_b[2] = {0, 0};
    bq_highpass(&h1, SR, 1500.0f, 0.7f); bq_highpass(&h2, SR, 1500.0f, 0.7f); bq_reset(&h1); bq_reset(&h2);
    const float ea = onepole_k(SR, 0.001f), er = onepole_k(SR, 0.03f);
    const float ha = onepole_k(SR, 0.0005f), hr = onepole_k(SR, 0.010f), hs = onepole_k(SR, 0.050f);
    const float xa = onepole_k(SR, 0.015f), xr = onepole_k(SR, 0.40f);
    const float thr = 0.003f;
    float env = 0, envs = 0, hf = 0, hfs = 0, f0s = 0, onset_peak = 0, held_f0 = 0, cand_f0 = 0, ref_f = 0;
    int key = 0, state = 0 /* 0 idle, 1 waiting for a lock, 2 sounding, 3 repicked (old note held) */;
    int cand_n = 0, peak_n = 0, stable_n = 0, refractory = 0, hw = 0;
    static float hist[132]; memset(hist, 0, sizeof hist);
    float vel = 0, peak_ref = 1e-4f;
    if (log) log->n = 0;
    for (int i = 0; i < n; i++) {
        const float xi = x[i], a = fabsf(xi);
        env += (a - env) * (a > env ? ea : er);
        envs += (env - envs) * (env > envs ? xa : xr);
        /* pick detector: the attack's broadband click (above 1.5 kHz) against its recent level. A string that is
         * still ringing hides a softer repick in the overall level, not in the click. */
        const float hx = fabsf(bq_run(&h2, bq_run(&h1, xi)));
        hf += (hx - hf) * (hx > hf ? ha : hr);
        hfs += (hf - hfs) * hs;
        int on = key ? env > thr * 0.5f : env > thr * 1.5f;
        hist[hw] = hf; hw = (hw + 1) % 132; const float hf_3ms = hist[hw];      /* the click level 3 ms ago */
        int pick = on && refractory == 0 && hf > 1.6f * hf_3ms + 0.15f * hfs + 0.0008f;
        int attack = (on && !key) || (pick && state >= 2);
        if (refractory > 0) refractory--;
        if (attack || (on && !key)) refractory = (int)(0.045f * SR);
        key = on;
        const float b = bq_run(&f2, bq_run(&f1, xi));
        /* a new pick restarts the tracker: it measures the new note's own periods, not a median with the old */
        synth_hq_track(&trk, b, on && !attack, SR);
        const px_synth_tracking *tk = &trk;
        if (twoband) {
            const float bh = bq_run(&u2, bq_run(&u1, bq_run(&u0, xi)));
            synth_hq_track(&trkh, bh, on && !attack, SR);
            el += (fabsf(b) - el) * bk; eh += (fabsf(bh) - eh) * bk;
            if (!high && el < 0.3f * eh) high = 1; else if (high && el > 0.6f * eh) high = 0;
            const float fl = trk.step * 4.0f * SR, fh = trkh.step * 4.0f * SR;
            /* the high band of a low note hears its harmonics: when its estimate is a whole multiple (2 or more) of the
             * low band's, the low band has the fundamental */
            int harmonic = 0;
            if (fl > 0.0f && fh > 1.5f * fl) { const float r = fh / fl, k = floorf(r + 0.5f); harmonic = fabsf(1200.0f * log2f(r / k)) < 35.0f; }
            const int want = high && fh >= 200.0f && !harmonic;
            /* the band is chosen afresh until a note speaks; while it sounds, a change must last 30 ms */
            if (state != 2) sel = want;
            else if (want != sel) { if (++sel_n > (int)(0.030f * SR)) { sel = want; sel_n = 0; } }
            else sel_n = 0;
            /* each band keeps its own lock counter, so a change of band does not restart the count */
            const px_synth_tracking *both[2] = {&trk, &trkh};
            for (int q = 0; q < 2; q++) {
                const float t = both[q]->target > 0.0f ? 4.0f * both[q]->target * SR : 0.0f, g = both[q]->step * 4.0f * SR;
                if (t > 0 && ref_b[q] > 0 && fabsf(1200.0f * log2f(t / ref_b[q])) < 20.0f) stable_b[q]++;
                else { stable_b[q] = 0; ref_b[q] = t; }
                if (g > 0 && t > 0 && fabsf(1200.0f * log2f(g / t)) > 15.0f && stable_b[q] > 1) stable_b[q] = 1;
                if (attack) { stable_b[q] = 0; ref_b[q] = 0; }
            }
            tk = both[sel];
        }
        float f = tk->step > 0.0f ? 4.0f * tk->step * SR : 0.0f;
        raw_f0[i] = f;
        memset(&c[i], 0, sizeof c[i]);
        if (!on) { state = 0; f0s = 0; continue; }
        if (attack) { state = state >= 2 ? 3 : 1; onset_peak = 0; peak_n = 0; stable_n = 0; ref_f = 0; }
        if (peak_n < (int)(0.006f * SR)) { onset_peak = a > onset_peak ? a : onset_peak; peak_n++; }
        /* a lock: the tracker's target (updated at each period) has stayed within 20 cents for 1.2 of its periods
         * (and 4 ms), and its gliding output has arrived within 15 cents of it */
        const float tf = tk->target > 0.0f ? 4.0f * tk->target * SR : 0.0f;
        if (tf > 0 && ref_f > 0 && fabsf(1200.0f * log2f(tf / ref_f)) < 20.0f) stable_n++;
        else { stable_n = 0; ref_f = tf; }
        if (f > 0 && tf > 0 && fabsf(1200.0f * log2f(f / tf)) > 15.0f) stable_n = stable_n < 1 ? stable_n : 1;
        if (twoband) stable_n = stable_b[sel];
        /* octave guard: a jump to twice or half the held pitch must last 40 ms before it is believed */
        if (f > 0 && held_f0 > 0 && state == 2) {
            float d = fabsf(12.0f * log2f(f / held_f0));
            if (fabsf(d - 12.0f) < 0.5f) {
                if (cand_f0 == 0 || fabsf(12.0f * log2f(f / cand_f0)) > 0.5f) { cand_f0 = f; cand_n = 0; }
                if (++cand_n < (int)(0.040f * SR)) f = f0s;
            } else cand_f0 = 0;
        }
        if ((state == 1 || state == 3) && f > 0 && peak_n >= (int)(0.004f * SR)
            && stable_n >= (int)(1.2f * SR / f) && stable_n >= (int)(0.004f * SR)) {     /* the note speaks */
            float db = 20.0f * log10f(onset_peak + 1e-9f);
            vel = (db + 40.0f) / 34.0f; vel = vel < 0.05f ? 0.05f : vel > 1 ? 1 : vel;
            peak_ref = envs > 1e-5f ? envs : 1e-5f;
            c[i].onset = 1; state = 2; held_f0 = f; f0s = f; cand_f0 = 0;
            if (log && log->n < 512) log->v[log->n++] = (trig_t){i / SR, f, vel, 0};
        }
        if (state == 1) continue;                       /* nothing sounds before the first lock */
        if (state == 2) {
            f0s += (f > 0 ? f - f0s : 0.0f) * 0.5f;     /* the tracker glides already; this only holds through drop-outs */
            if (fabsf(12.0f * log2f(f0s / held_f0)) > 0.6f) {
                if (log && log->n < 512) log->v[log->n++] = (trig_t){i / SR, f0s, vel, 1};
                held_f0 = f0s;                          /* a slur or a slide: same note on, new pitch identity */
            }
        }                                               /* state 3: the old note holds its pitch until the new lock */
        c[i].gate = 1; c[i].f0 = f0s; c[i].vel = vel;
        float rel = envs / peak_ref; rel = rel > 1 ? 1 : rel;
        float e = vel * sqrtf(rel);                     /* bow / breath: half the guitar's decay in dB */
        c[i].expr = e < 0.3f * vel ? 0.3f * vel : e;
    }
}

/* ------------------------------------------------------------------------------------------- sample voice --- */
typedef struct { int instr, dyn; float f0; int n, ls, le; float level_db; float *pcm; } smp_t;
static smp_t BANK[32]; static int NBANK;

static int bank_load(const char *fn)
{
    FILE *f = fopen(fn, "rb"); if (!f) { fprintf(stderr, "no bank %s (run tools/legacy-synth/acoustic_prep.py)\n", fn); return 0; }
    char m[4]; uint32_t cnt; if (fread(m, 1, 4, f) != 4 || memcmp(m, "PXAB", 4) || !rd32(f, &cnt)) { fclose(f); return 0; }
    for (uint32_t k = 0; k < cnt && k < 32; k++) {
        smp_t *s = &BANK[k]; int32_t h[7];
        if (fread(h, 4, 7, f) != 7) break;
        s->instr = h[0]; s->dyn = h[1]; memcpy(&s->f0, &h[2], 4); s->n = h[3]; s->ls = h[4]; s->le = h[5]; memcpy(&s->level_db, &h[6], 4);
        s->pcm = malloc(sizeof(float) * (s->n + 4));
        for (int i = 0; i < s->n; i++) { int16_t v; fread(&v, 2, 1, f); s->pcm[i] = v / 32768.0f; }
        for (int i = 0; i < 4; i++) s->pcm[s->n + i] = s->pcm[s->ls + i];          /* interpolation reads across the loop */
        NBANK = k + 1;
    }
    fclose(f); return NBANK;
}

typedef struct { const smp_t *s; double pos; float gain, fade; int active; } player_t;
typedef struct { int instr; float oct; player_t p[3]; float amp, lp, rel_k, att_k, root_f; int held; } svoice;

static const smp_t *pick(int instr, float f, int dyn)
{
    const smp_t *best = 0; float bd = 1e9f;
    for (int k = 0; k < NBANK; k++) { const smp_t *s = &BANK[k]; if (s->instr != instr || s->dyn != dyn) continue;
        float d = fabsf(hz_midi(f) - hz_midi(s->f0)); if (d < bd) { bd = d; best = s; } }
    return best;
}

static inline float av_hermite(const float *x, double pos)
{
    int i = (int)pos; float t = (float)(pos - i);
    float xm1 = i > 0 ? x[i - 1] : x[i], x0 = x[i], x1 = x[i + 1], x2 = x[i + 2];
    float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

static void sv_init(svoice *v, int instr)
{
    memset(v, 0, sizeof *v); v->instr = instr; v->oct = instr == 0 ? 12.0f : 0.0f;
    v->rel_k = onepole_k(SR, instr == 0 ? 0.030f : 0.070f);     /* trumpet stops quickly; the bow lifts off */
}

static void sv_start(svoice *v, const smp_t *s, double pos)
{
    for (int k = 0; k < 3; k++) if (v->p[k].active) v->p[k].fade = onepole_k(SR, 0.010f);   /* old ones out, 10 ms */
    for (int k = 0; k < 3; k++) if (!v->p[k].active || v->p[k].gain < 1e-3f) {
        v->p[k] = (player_t){s, pos, pos > 0 ? 0.0f : 1.0f, 0.0f, 1}; return; }
    int q = 0; for (int k = 1; k < 3; k++) if (v->p[k].gain < v->p[q].gain) q = k;             /* steal the quietest */
    v->p[q] = (player_t){s, pos, pos > 0 ? 0.0f : 1.0f, 0.0f, 1};
}

static float sv_tick(svoice *v, const ctl_t *c)
{
    float f = c->f0 * powf(2.0f, v->oct / 12.0f);
    if (c->onset && f > 0) {
        const smp_t *s = pick(v->instr, f, c->vel > 0.55f ? 1 : 0);
        if (s) { sv_start(v, s, 0.0); v->root_f = s->f0; }
        v->held = 1;
    } else if (c->gate && f > 0 && v->held && v->root_f > 0 && fabsf(hz_midi(f) - hz_midi(v->root_f)) > 6.0f) {
        /* a slur or slide far from the root: cross into the nearer root's sustain loop (no new attack) */
        const smp_t *s = pick(v->instr, f, c->vel > 0.55f ? 1 : 0);
        if (s && s->f0 != v->root_f) { sv_start(v, s, (double)s->ls); v->root_f = s->f0; }
    }
    if (!c->gate) v->held = 0;
    /* level: expression in dB (24 dB range), released smoothly when the gate falls */
    float target = c->gate ? powf(10.0f, (c->expr - 1.0f) * 24.0f / 20.0f) : 0.0f;
    v->amp += (target - v->amp) * (c->gate ? onepole_k(SR, 0.012f) : v->rel_k);
    float y = 0.0f;
    for (int k = 0; k < 3; k++) {
        player_t *p = &v->p[k]; if (!p->active) continue;
        if (p->fade > 0) { p->gain -= p->gain * p->fade; if (p->gain < 1e-4f) { p->active = 0; continue; } }
        else if (p->gain < 1.0f) { p->gain += (1.0f - p->gain) * onepole_k(SR, 0.020f); }
        const smp_t *s = p->s;
        y += av_hermite(s->pcm, p->pos) * p->gain;
        double ff = f > 0 ? f : s->f0;
        p->pos += (ff / s->f0) * (22050.0 / SR);
        if (p->pos >= s->le) p->pos -= (s->le - s->ls);
    }
    /* expression as brightness too: a softer bow / breath is darker (one-pole, cutoff 6..30 harmonics) */
    float fc = (f > 0 ? f : 200.0f) * (6.0f + 24.0f * c->expr); if (fc > 16000) fc = 16000;
    v->lp += opk(SR, fc) * (y - v->lp);
    return v->lp * v->amp * 1.4f;
}

/* ---------------------------------------------------------------------------------------- waveguide voices --- */
typedef struct { float b[2048]; int w; } dline;
static inline void dl_write(dline *d, float x) { d->b[d->w] = x; d->w = (d->w + 1) & 2047; }
static inline float dl_read(const dline *d, float delay)          /* delay in samples, >= 1, linear */
{ float rp = (float)d->w - delay; while (rp < 0) rp += 2048.0f; int i = (int)rp; float fr = rp - i;
  float a = d->b[i & 2047], b = d->b[(i + 1) & 2047]; return a + (b - a) * fr; }

typedef struct {
    int instr; float oct;
    dline neck, bridge, bore;
    float str_lp, bowv, press, vib_ph, vib_amt, held_t, noise_lp; uint32_t rnd;
    px_bq body[6]; int nbody;
    float lip_y1, lip_y2, lip_a1, lip_a2, dc_x, dc_y, bell_lp, breath, lip_f;
    float out_lp, gain;
} wvoice;

#define BRASS_LIP_RATIO 0.92f
#define BRASS_LIP_PEAK 6.0f
#define BRASS_BORE_OFFSET 10.0f
/* The lip pulls the pitch away from the bore's: a table of drive / sounding ratios, solved at start-up
 * (brass_calibrate), every 2 semitones from D3 to D6. On the pedal it would be a constant table. */
#define BCAL_LO 50
#define BCAL_N 18
static float BCAL[BCAL_N];
static float brass_cal(float f)
{
    if (BCAL[0] == 0.0f) return 1.0f;
    float u = (hz_midi(f) - BCAL_LO) * 0.5f; if (u < 0) u = 0; if (u > BCAL_N - 1.001f) u = BCAL_N - 1.001f;
    int i = (int)u; return BCAL[i] + (BCAL[i + 1] - BCAL[i]) * (u - i);
}

static void wv_init(wvoice *v, int instr)
{
    memset(v, 0, sizeof *v); v->instr = instr; v->oct = instr == 0 ? 12.0f : 0.0f; v->rnd = 777;
    if (instr == 1) {          /* cello body: air mode, main wood modes, bridge hill; no rumble, no fizz */
        bq_highpass(&v->body[0], SR, 55.0f, 0.7f);
        bq_peak(&v->body[1], SR, 105.0f, 2.0f, 5.0f);
        bq_peak(&v->body[2], SR, 205.0f, 2.5f, 6.0f);
        bq_peak(&v->body[3], SR, 420.0f, 1.8f, 3.0f);
        bq_peak(&v->body[4], SR, 2300.0f, 0.9f, 4.0f);
        bq_lowpass(&v->body[5], SR, 5500.0f, 0.7f);
        v->nbody = 6; v->gain = 0.25f;
    } else {                   /* trumpet bell: radiation rises with frequency, then the bell's cutoff */
        bq_highpass(&v->body[0], SR, 250.0f, 0.7f);
        bq_peak(&v->body[1], SR, 1200.0f, 0.8f, 3.0f);
        bq_lowpass(&v->body[2], SR, 7000.0f, 0.7f);
        v->nbody = 3; v->gain = 7.0f;
    }
}

static float wv_tick(wvoice *v, const ctl_t *c)
{
    float f = c->f0 * powf(2.0f, v->oct / 12.0f);
    if (v->instr == 0 && f > 0) f *= brass_cal(f);
    if (f <= 0) f = v->lip_f > 0 ? v->lip_f : 110.0f;
    if (c->onset) { v->held_t = 0; v->vib_amt = 0; }
    v->held_t += 1.0f / SR;
    v->rnd = v->rnd * 1664525u + 1013904223u; float nz = (float)(v->rnd >> 8) / 8388608.0f - 1.0f;
    float y;
    if (v->instr == 1) {
        /* bow velocity follows expression (an attack shaped by pick strength), pressure rises with it */
        float target = c->gate ? 0.03f + 0.22f * c->expr : 0.0f;
        float k = c->gate ? onepole_k(SR, 0.010f + 0.10f * (1.0f - c->vel)) : onepole_k(SR, 0.060f);
        v->bowv += (target - v->bowv) * k;
        float slope = 5.0f - 4.0f * (0.35f + 0.45f * c->expr);
        /* a cellist's vibrato that grows on held notes (on top of the guitar's own) */
        if (v->held_t > 0.35f && c->gate) v->vib_amt += (0.0035f - v->vib_amt) * onepole_k(SR, 0.4f);
        v->vib_ph = wrap1(v->vib_ph + 5.2f / SR);
        float fv = f * (1.0f + v->vib_amt * sinf(TWO_PI * v->vib_ph));
        const float beta = 0.127236f, base = SR / fv - 1.7f;   /* STK subtracts 4; measured here: 1.7 */
        float bridge_d = base * beta, neck_d = base * (1.0f - beta);
        if (bridge_d < 1.0f) bridge_d = 1.0f;
        float bo = dl_read(&v->bridge, bridge_d), no = dl_read(&v->neck, neck_d);
        /* string loss: one-pole low-pass (STK: pole 0.75 - 0.2 * 22050 / fs, gain 0.95) */
        const float pole = 0.75f - 0.2f * 22050.0f / SR;
        v->str_lp = 0.95f * (1.0f - pole) * bo + pole * v->str_lp;
        float bridge_refl = -v->str_lp, nut_refl = -no;
        float string_v = bridge_refl + nut_refl;
        v->noise_lp += 0.3f * (nz - v->noise_lp);
        float bow = v->bowv * (1.0f + 0.04f * v->noise_lp);     /* rosin: a little noise on the bow */
        float dv = bow - string_v;
        float s = (dv + 0.001f) * slope; s = fabsf(s) + 0.75f; s = 1.0f / (s * s * s * s);   /* bow table */
        if (s > 0.98f) s = 0.98f; if (s < 0.01f) s = 0.01f;
        float nv = dv * s;
        dl_write(&v->neck, bridge_refl + nv);
        dl_write(&v->bridge, nut_refl + nv);
        y = v->str_lp;
        /* bow scrape on the attack: band-passed noise that dies once the string has caught */
        if (v->held_t < 0.08f && c->gate) y += 0.05f * v->bowv * nz * (1.0f - v->held_t / 0.08f);
    } else {
        /* breath pressure: fast tongued rise for a hard pick, softer for a quiet one; the lip follows the pitch */
        /* breath stays above the lip's blowing threshold (below about 0.8 the higher notes stop speaking); the
         * dynamics are an output level instead, as in the sample voice */
        float target = c->gate ? 0.85f + 0.25f * c->expr : 0.0f;
        float k = c->gate ? onepole_k(SR, 0.004f + 0.030f * (1.0f - c->vel)) : onepole_k(SR, 0.015f);
        v->breath += (target - v->breath) * k;
        v->lip_f = f;
        /* Lip tension a little under the note, and the lip's peak gain held constant over the range: STK's fixed
         * gain (0.03) makes the resonator's peak grow as 1 / sin(w), and below about E5 the lip then stays saturated
         * (fully open) and the model never speaks. */
        const float r = 0.997f, lip_w = TWO_PI * f * BRASS_LIP_RATIO / SR;
        v->lip_a1 = -2.0f * r * cosf(lip_w); v->lip_a2 = r * r;
        float mouth = 0.3f * (v->breath * (1.0f + 0.012f * nz));
        float bore_d = SR / f * 2.0f + BRASS_BORE_OFFSET; if (bore_d > 2040.0f) bore_d = 2040.0f;
        float bore = 0.85f * dl_read(&v->bore, bore_d);
        float dp = mouth - bore;
        float lip = BRASS_LIP_PEAK * 2.0f * (1.0f - r) * sinf(lip_w) * dp - v->lip_a1 * v->lip_y1 - v->lip_a2 * v->lip_y2;
        v->lip_y2 = v->lip_y1; v->lip_y1 = lip;
        float d2 = lip * lip; if (d2 > 1.0f) d2 = 1.0f;
        float o = d2 * mouth + (1.0f - d2) * bore;
        float dc = o - v->dc_x + 0.99f * v->dc_y; v->dc_x = o; v->dc_y = dc;
        dl_write(&v->bore, dc);
        /* radiation: the bell passes the rising slope (a differentiator, half mixed in); near full breath a soft
         * clip stands in for the bore's nonlinear steepening that makes loud brass brassy */
        const float e2 = c->expr * c->expr, g = 1.0f + 4.0f * e2 * e2, sh = tanhf(g * dc * 15.0f) / (g * 15.0f);
        y = (sh + 4.0f * (sh - v->out_lp)) * powf(10.0f, (c->expr - 1.0f) * 18.0f / 20.0f); v->out_lp = sh;
        if (v->held_t < 0.03f && c->gate) y += 0.02f * v->breath * nz * c->vel;    /* tongue / breath at the start */
    }
    for (int i = 0; i < v->nbody; i++) y = bq_run(&v->body[i], y);
    return y * v->gain;
}

/* ------------------------------------------------------------------- the engine's BRASS and STRINGS, today --- */
static void engine_render(const float *x, float *y, int n, int type)
{
    static px_engine e; px_engine_init(&e, SR);
    px_preset_t p; memset(&p, 0, sizeof p); strcpy(p.name, type == PX_SYN_BRASS ? "BRASS" : "STRINGS");
    for (int b = 0; b < PX_NBLK; b++) { p.order[b] = (uint8_t)b; for (int k = 0; k < PX_NPAR; k++) p.p[b][k] = 50; }
    p.on[PX_B_SYN] = 1; p.type[PX_B_SYN] = (uint8_t)type;
    if (type == PX_SYN_BRASS) { uint8_t q[5] = {20, 30, 50, 40, 0}; memcpy(p.p[PX_B_SYN], q, 5); }
    else { uint8_t q[5] = {45, 50, 25, 50, 0}; memcpy(p.p[PX_B_SYN], q, 5); }
    px_engine_set(&e, &p);
    for (int i = 0; i < n; i += PX_MAX_BLOCK) { int b = n - i < PX_MAX_BLOCK ? n - i : PX_MAX_BLOCK; px_engine_process(&e, x + i, y + i, b); }
}

/* ------------------------------------------------------------------------------------------------ helpers --- */
typedef struct { int instr, kind; } vspec;                 /* kind 0 sample, 1 waveguide */
static const char *INAME[] = {"trumpet", "cello"}, *KNAME[] = {"sample", "waveguide"};
static double voice_ns[2][2]; static long voice_samples[2][2];

static void run_voice(vspec v, const ctl_t *c, float *y, int n)
{
    static svoice sv; static wvoice wv;
    if (v.kind == 0) sv_init(&sv, v.instr); else wv_init(&wv, v.instr);
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < n; i++) y[i] = v.kind == 0 ? sv_tick(&sv, &c[i]) : wv_tick(&wv, &c[i]);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    voice_ns[v.instr][v.kind] += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
    voice_samples[v.instr][v.kind] += n;
}

static char OUT[1024];
static void save(const char *name, const float *y, int n)
{
    char fn[1400]; double r = rms_db(y, n);
    snprintf(fn, sizeof fn, "%s/raw/%s.wav", OUT, name); wav_write(fn, y, n, 1.0f);
    float g = (float)pow(10.0, (-20.0 - r) / 20.0); float pk = peak_abs(y, n) * g; if (pk > 0.99f) g *= 0.99f / pk;
    snprintf(fn, sizeof fn, "%s/matched/%s.wav", OUT, name); wav_write(fn, y, n, g);
    printf("  %-44s rms %6.1f dBFS  peak %6.1f dBFS\n", name, r, 20 * log10(peak_abs(y, n) + 1e-12));
}

/* short-time level (5 ms RMS) in dB */
static void env_db(const float *y, int n, float *e)
{
    double acc = 0; int W = (int)(0.005f * SR);
    for (int i = 0; i < n; i++) { acc += (double)y[i] * y[i]; if (i >= W) acc -= (double)y[i - W] * y[i - W];
        e[i] = (float)(10.0 * log10(acc / W + 1e-14)); }
}

/* the measured pitch of a steady output (autocorrelation over 0.4 s from `start`) */
static float out_pitch(const float *y, int start, int len, float lo, float hi)
{
    int lmin = (int)(SR / hi), lmax = (int)(SR / lo); double best = -1; int bl = lmin;
    double *r = calloc(lmax + 2, sizeof(double));
    for (int L = lmin - 1; L <= lmax + 1; L++) { double s = 0, ea = 0, eb = 0;
        for (int i = 0; i < len; i++) { double a = y[start + i], b = y[start + i + L]; s += a * b; ea += a * a; eb += b * b; }
        r[L - lmin + 1] = s / sqrt(ea * eb + 1e-20); }
    for (int L = lmin; L <= lmax; L++) if (r[L - lmin + 1] > best) { best = r[L - lmin + 1]; bl = L; }
    for (int L = lmin; L < bl; L++) if (r[L - lmin + 1] > 0.92 * best && r[L - lmin + 1] >= r[L - lmin] && r[L - lmin + 1] >= r[L - lmin + 2]) { bl = L; break; }
    double a = r[bl - lmin], b = r[bl - lmin + 1], c = r[bl - lmin + 2], den = a - 2 * b + c;
    double d = den != 0 ? 0.5 * (a - c) / den : 0; free(r);
    return (float)(SR / (bl + d));
}

static void brass_calibrate(void)
{
    const int n = (int)(0.9f * SR); ctl_t *c = calloc(n, sizeof *c); float *y = malloc(sizeof(float) * n);
    float tab[BCAL_N];
    for (int k = 0; k < BCAL_N; k++) {
        float target = midi_hz(BCAL_LO + 2.0f * k), drive = target;
        for (int it = 0; it < 5; it++) {
            for (int i = 0; i < n; i++) c[i] = (ctl_t){drive / 2.0f, 0.7f, 0.7f, 1, i == 0};   /* guitar = sounding - 12 */
            wvoice w; wv_init(&w, 0); for (int i = 0; i < n; i++) y[i] = wv_tick(&w, &c[i]);
            float m = out_pitch(y, (int)(0.5f * SR), (int)(0.3f * SR), drive * 0.6f, drive * 1.6f);
            drive *= target / m;
        }
        tab[k] = drive / target;
    }
    memcpy(BCAL, tab, sizeof tab);
    free(c); free(y);
}

static void note_name(char *b, float m);
/* ------------------------------------------------------------------------------- waveguide tuning check --- */
static void tuning_table(FILE *rep)
{
    fprintf(rep, "\n## Waveguide tuning (steady note, measured output pitch)\n\nThe brass table is solved at expression 0.7; the other "
                 "columns show how far breath (or bow) strength moves the pitch.\n\n"
                 "| voice | target | error at expr 0.3 (cents) | at 0.7 | at 1.0 |\n| --- | --- | --- | --- | --- |\n");
    const float notes[2][4] = {{52, 59, 64, 76}, {36, 43, 52, 60}};   /* trumpet E3..E5 sounding, cello C2..C4 */
    const float ex[3] = {0.3f, 0.7f, 1.0f};
    for (int instr = 0; instr < 2; instr++) for (int k = 0; k < 4; k++) {
        int n = (int)(1.2f * SR); ctl_t *c = calloc(n, sizeof *c); float *y = malloc(sizeof(float) * n);
        float sound = notes[instr][k], guitar = sound - (instr == 0 ? 12 : 0), tf = midi_hz(sound);
        char nm[8]; note_name(nm, sound);
        fprintf(rep, "| %s | %s %.1f Hz |", INAME[instr], nm, tf);
        for (int q = 0; q < 3; q++) {
            for (int i = 0; i < n; i++) c[i] = (ctl_t){midi_hz(guitar), ex[q], ex[q], 1, i == 0};
            wvoice w; wv_init(&w, instr); for (int i = 0; i < n; i++) y[i] = wv_tick(&w, &c[i]);
            float mf = out_pitch(y, (int)(0.6f * SR), (int)(0.3f * SR), tf * 0.7f, tf * 1.4f);
            float pk = peak_abs(y + (int)(0.6f * SR), (int)(0.3f * SR));
            fprintf(rep, " %+.0f%s |", 1200 * log2f(mf / tf), pk < 1e-4f ? " (silent)" : "");
        }
        fprintf(rep, "\n");
        free(c); free(y);
    }
}

/* steady notes at each recorded root and dynamic, for the spectral comparison (tools/legacy-synth/acoustic_compare.py):
 * the waveguide voice, and the engine's current BRASS / STRINGS on a plucked DI note at the same pitch */
static void steady_set(void)
{
    const int n = (int)(1.6f * SR); ctl_t *c = calloc(n, sizeof *c); float *y = malloc(sizeof(float) * n), *di = malloc(sizeof(float) * n);
    for (int k = 0; k < NBANK; k++) {
        const smp_t *s = &BANK[k]; const float sound = s->f0, oct = s->instr == 0 ? 12.0f : 0.0f, ex = s->dyn ? 0.95f : 0.45f;
        char nm[96], fn[1400]; int m = (int)lrintf(hz_midi(sound));
        for (int i = 0; i < n; i++) c[i] = (ctl_t){sound / powf(2.0f, oct / 12.0f), ex, ex, 1, i == 0};
        wvoice w; wv_init(&w, s->instr); for (int i = 0; i < n; i++) y[i] = wv_tick(&w, &c[i]);
        snprintf(nm, sizeof nm, "steady_%s_waveguide_%s_%d", INAME[s->instr], s->dyn ? "loud" : "soft", m);
        snprintf(fn, sizeof fn, "%s/raw/%s.wav", OUT, nm); wav_write(fn, y, n, 1.0f);
        note_t one = {0.05f, 1.5f, hz_midi(sound), s->dyn ? 1.0f : 0.4f, 0, 0, 0, 0, 0, 0};
        synth_di_score(&one, 1, di, n);
        engine_render(di, y, n, s->instr == 0 ? PX_SYN_BRASS : PX_SYN_STRINGS);
        snprintf(nm, sizeof nm, "steady_%s_engine_%s_%d", INAME[s->instr], s->dyn ? "loud" : "soft", m);
        snprintf(fn, sizeof fn, "%s/raw/%s.wav", OUT, nm); wav_write(fn, y, n, 1.0f);
    }
    free(c); free(y); free(di);
}

/* -------------------------------------------------------------------- tracking metrics on the synthetic DI --- */
static int cmpf(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }
static float median(float *v, int n) { if (!n) return NAN; qsort(v, n, sizeof *v, cmpf); return n & 1 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]); }
static float worst(const float *v, int n) { float w = -1; for (int i = 0; i < n; i++) w = v[i] > w ? v[i] : w; return w; }
static const char *num(char *b, float v, const char *f) { if (isnan(v) || v < -1e8f) return "-"; snprintf(b, 24, f, v); return b; }
static void note_name(char *b, float m) { static const char *NN[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int k = (int)lrintf(m); snprintf(b, 8, "%s%d", NN[k % 12], k / 12 - 1); }

static void tracking_metrics(FILE *rep, const char *title, const ctl_t *c, const float *raw_f0, const trig_log *log, int n)
{
    int notes = 0, missed = 0, wrong = 0, octave = 0, nl = 0, nt = 0, nc = 0;
    float locks[64], trigs[64]; float *cents = malloc(sizeof(float) * n);
    fprintf(rep, "\n## Tracking on the synthetic DI: %s\n\nThe note layer in "
                 "`acoustic_voice.c` on a Karplus-Strong DI of the fixture score. Times from the true pick. *Lock*: the tracker's first "
                 "estimate within 50 cents. *Trigger*: the instrument note starts (the note layer waits for a lock).\n\n"
                 "| # | guitar | pick | lock (ms) | trigger (ms) | trigger error (cents) | result |\n| --- | --- | --- | --- | --- | --- | --- |\n", title);
    for (int k = 0; k < NSCORE; k++) {
        const note_t *sn = &SCORE[k]; if (sn->legato) continue;
        notes++;
        int i0 = (int)(sn->t * SR), i1 = (int)((sn->t + sn->dur) * SR); if (i1 > n) i1 = n;
        float lock = NAN, trig = NAN, terr = NAN; int oct = 0;
        for (int i = i0; i < i1; i++) if (raw_f0[i] > 0 && fabsf(1200 * log2f(raw_f0[i] / midi_hz(note_midi_at(sn, (i - i0) / SR)))) < 50) { lock = (i - i0) * 1000 / SR; break; }
        for (int q = 0; q < log->n; q++) if (log->v[q].kind == 0 && log->v[q].t >= sn->t - 0.002f && log->v[q].t < sn->t + sn->dur) {
            trig = (log->v[q].t - sn->t) * 1000; float tf = midi_hz(note_midi_at(sn, log->v[q].t - sn->t));
            terr = 1200 * log2f(log->v[q].f0 / tf); if (fabsf(fabsf(terr) - 1200) < 80) oct = 1; break; }
        const char *res = "ok";
        if (isnan(trig)) { missed++; res = "**missed**"; }
        else if (oct) { octave++; wrong++; res = "**octave**"; }
        else if (fabsf(terr) > 50) { wrong++; res = "**wrong**"; }
        if (!isnan(lock)) locks[nl++] = lock;
        if (!isnan(trig)) trigs[nt++] = trig;
        for (int i = i0 + (i1 - i0) / 4; i < i1 - (i1 - i0) / 4; i++) if (c[i].gate && c[i].f0 > 0)
            cents[nc++] = fabsf(1200 * log2f(c[i].f0 / midi_hz(note_midi_at(sn, (i - i0) / SR))));
        char nm[8], b1[24], b2[24], b3[24]; note_name(nm, sn->midi);
        fprintf(rep, "| %d | %s | %.2f | %s | %s | %s | %s |\n", k + 1, nm, sn->vel, num(b1, lock, "%.1f"), num(b2, trig, "%.1f"),
                num(b3, terr, "%+.0f"), res);
    }
    float tl = worst(locks, nl), tt = worst(trigs, nt), ml = median(locks, nl), mt = median(trigs, nt), mc = median(cents, nc);
    fprintf(rep, "\n%d picked notes: %d missed, %d wrong (%d octave errors). Lock median %.1f ms, worst %.1f ms. Trigger median %.1f ms, "
                 "worst %.1f ms. Sustained pitch error (middle half of each note, slurs and bends included): median %.1f cents.\n",
            notes, missed, wrong, octave, ml, tl, mt, tt, mc);
    printf("  tracking (%s): %d notes, %d missed, %d wrong (%d octave); lock median %.1f / worst %.1f ms; trigger median %.1f / worst %.1f ms\n",
           title, notes, missed, wrong, octave, ml, tl, mt, tt);
    free(cents);
}

/* per voice on a render of the score: audible onset, release, level jumps (5 ms RMS) */
static void voice_metrics(FILE *rep, const char *name, const float *y, int n)
{
    float *e = malloc(sizeof(float) * n); env_db(y, n, e);
    float on[64], rel[64]; int no = 0, nr = 0; float jump = 0, jump_t = 0;
    for (int k = 0; k < NSCORE; k++) {
        const note_t *sn = &SCORE[k];
        int i0 = (int)(sn->t * SR), i1 = (int)((sn->t + sn->dur) * SR);
        if (!sn->legato) {
            float mx = -200; for (int i = i0; i < i1 && i < n; i++) mx = e[i] > mx ? e[i] : mx;
            for (int i = i0; i < i1 && i < n; i++) if (e[i] > mx - 12) { on[no++] = (i - i0) * 1000 / SR; break; }
        }
        int next = k + 1 < NSCORE && SCORE[k + 1].t - (sn->t + sn->dur) < 0.01f;
        if (!next) {
            float lvl = -200; for (int i = i1 - (int)(0.03f * SR); i < i1; i++) lvl = e[i] > lvl ? e[i] : lvl;
            int lim = k + 1 < NSCORE ? (int)(SCORE[k + 1].t * SR) : n;
            for (int i = i1; i < lim && i < n; i++) if (e[i] < lvl - 40 || e[i] < -80) { rel[nr++] = (i - i1) * 1000 / SR; break; }
        }
        /* level jumps inside a held note, away from its attack: the largest 10 ms rise */
        for (int i = i0 + (int)(0.15f * SR); i < i1 - (int)(0.01f * SR) && i + 441 < n; i += 64) {
            float d = e[i + 441] - e[i]; if (d > jump && e[i + 441] > -60) { jump = d; jump_t = (i + 441) / SR; }
        }
    }
    fprintf(rep, "| %s | %.0f | %.0f | %.0f | %.0f | %.1f dB at %.2f s |\n", name, median(on, no), worst(on, no), median(rel, nr), worst(rel, nr), jump, jump_t);
    free(e);
}

/* a recorded phrase against a transcription (tools/legacy-synth/acoustic_truth.py): what sounds during each played note */
static void truth_metrics(FILE *rep, const char *stem, const char *title, const char *csv, const ctl_t *c, const trig_log *log, int n)
{
    FILE *f = fopen(csv, "r"); if (!f) { fprintf(stderr, "no truth file %s\n", csv); return; }
    float a[512], b[512], m[512], v[512]; int k = 0; char line[256];
    while (fgets(line, sizeof line, f) && k < 512) { if (line[0] == '#') continue;
        if (sscanf(line, "%f,%f,%f,%f", &a[k], &b[k], &m[k], &v[k]) == 4) k++; }
    fclose(f);
    int played = 0, ok = 0, octave = 0, wrong = 0, silent = 0, attacks = 0, nl = 0; float lat[512];
    fprintf(rep, "\n### %s against its transcription: %s\n\nNotes of at least 60 ms and velocity 30 in the transcription. A note is "
                 "*ok* when the instrument's pitch is within 50 cents for most of the middle half of the time it is the latest "
                 "note (a mono voice follows the newest note). *Time to pitch*: from the transcribed onset to the first sample "
                 "within 50 cents. *Attack*: the note layer started a new instrument note near the onset (else it followed as a slur).\n\n"
                 "| t (s) | note | vel | result | time to pitch (ms) | attack |\n| --- | --- | --- | --- | --- | --- |\n", stem, title);
    for (int i = 0; i < k; i++) {
        float end = b[i]; for (int j = i + 1; j < k; j++) if (a[j] > a[i] + 0.001f) { if (a[j] < end) end = a[j]; break; }
        if (b[i] - a[i] < 0.06f || v[i] < 30 || end - a[i] < 0.03f) continue;
        played++;
        int i0 = (int)(a[i] * SR), i1 = (int)(end * SR); if (i0 < 0) i0 = 0; if (i1 > n) i1 = n; if (i0 >= i1) continue;
        const float tf = midi_hz(m[i]);
        int good = 0, oct = 0, on = 0, tot = 0; float t_ok = NAN;
        for (int q = i0; q < i1; q++) if (c[q].gate && c[q].f0 > 0 && fabsf(1200 * log2f(c[q].f0 / tf)) < 50) { t_ok = (q - i0) * 1000 / SR; break; }
        for (int q = i0 + (i1 - i0) / 4; q < i1 - (i1 - i0) / 4; q++) { tot++;
            if (!c[q].gate || c[q].f0 <= 0) continue; on++;
            float ce = fabsf(1200 * log2f(c[q].f0 / tf));
            if (ce < 50) good++; else if (fabsf(ce - 1200) < 50) oct++; }
        const char *res;
        if (good * 2 > tot) { ok++; res = "ok"; } else if (oct * 2 > tot) { octave++; res = "**octave**"; }
        else if (on * 2 < tot) { silent++; res = "**silent**"; } else { wrong++; res = "**wrong**"; }
        int att = 0; for (int q = 0; q < log->n; q++) if (log->v[q].kind == 0 && log->v[q].t > a[i] - 0.01f && log->v[q].t < a[i] + 0.12f) att = 1;
        attacks += att; if (!isnan(t_ok)) lat[nl++] = t_ok;
        char nm[8], bt[24]; note_name(nm, m[i]);
        fprintf(rep, "| %.3f | %s | %.0f | %s | %s | %s |\n", a[i], nm, v[i], res, num(bt, t_ok, "%.0f"), att ? "yes" : "slur");
    }
    float ml = median(lat, nl), wl = worst(lat, nl);
    fprintf(rep, "\n%d played notes: %d ok, %d wrong, %d octave errors, %d silent. %d started an instrument attack. Time to pitch: "
                 "median %.0f ms, worst %.0f ms.\n", played, ok, wrong, octave, silent, attacks, ml, wl);
    printf("  %s vs transcription (%s): %d notes, %d ok, %d wrong, %d octave, %d silent; time to pitch median %.0f / worst %.0f ms\n",
           stem, title, played, ok, wrong, octave, silent, ml, wl);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s BANK OUTDIR [DI.wav ...]\n", argv[0]); return 1; }
    if (!bank_load(argv[1])) return 1;
    brass_calibrate();
    snprintf(OUT, sizeof OUT, "%s", argv[2]);
    char cmd[2200]; snprintf(cmd, sizeof cmd, "mkdir -p '%s/raw' '%s/matched'", OUT, OUT); if (system(cmd)) return 1;
    char fn[1400]; snprintf(fn, sizeof fn, "%s/report.md", OUT);
    FILE *rep = fopen(fn, "w"); if (!rep) return 1;
    fprintf(rep, "# Acoustic voice prototype: measurements\n\nGenerated by `tools/legacy-synth/acoustic_voice.c` (host, 44.1 kHz). Listening files: "
                 "`raw/` as rendered, `matched/` each at -20 dBFS RMS.\n");
    const vspec V[4] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    const int n = (int)(SCORE_LEN * SR);
    ctl_t *c = malloc(sizeof(ctl_t) * n); float *y = malloc(sizeof(float) * n), *di = malloc(sizeof(float) * n), *rf = malloc(sizeof(float) * n);
    char nm[256];

    printf("fixture (known pitch, gate, expression; no tracking)\n");
    fixture_controls(c, n);
    for (int v = 0; v < 4; v++) { run_voice(V[v], c, y, n); snprintf(nm, sizeof nm, "fixture_%s_%s", INAME[V[v].instr], KNAME[V[v].kind]); save(nm, y, n); }

    printf("synthetic DI of the same score\n");
    synth_di(di, n); save("synthdi_dry", di, n);
    engine_render(di, y, n, PX_SYN_BRASS); save("synthdi_engine_BRASS", y, n);
    engine_render(di, y, n, PX_SYN_STRINGS); save("synthdi_engine_STRINGS", y, n);
    static trig_log log;
    static const char *TB[2] = {"hq", "2band"}, *TBT[2] = {"the engine's HQ tracker as BASS HQ / PWM HQ feed it", "two-band (HQ tracker + a 200 Hz .. 1 kHz copy)"};
    for (int tb = 0; tb < 2; tb++) { tracked_controls(di, n, c, rf, &log, tb); tracking_metrics(rep, TBT[tb], c, rf, &log, n); }
    fprintf(rep, "\n## The voices on the synthetic DI, tracked\n\n*Onset*: the true pick to the output reaching 12 dB under the note's "
                 "maximum (tracking + the instrument's own attack). *Release*: the end of the note (string muted) to 40 dB down. "
                 "*Jump*: the largest 10 ms level rise inside a held note, away from its attack.\n\n"
                 "| render | onset median (ms) | onset worst (ms) | release median (ms) | release worst (ms) | largest jump |\n| --- | --- | --- | --- | --- | --- |\n");
    for (int tb = 0; tb < 2; tb++) {
        tracked_controls(di, n, c, rf, &log, tb);
        for (int v = 0; v < 4; v++) { run_voice(V[v], c, y, n); snprintf(nm, sizeof nm, "synthdi_%s_%s_%s", TB[tb], INAME[V[v].instr], KNAME[V[v].kind]); save(nm, y, n); voice_metrics(rep, nm, y, n); }
    }
    engine_render(di, y, n, PX_SYN_BRASS); voice_metrics(rep, "synthdi_engine_BRASS", y, n);
    engine_render(di, y, n, PX_SYN_STRINGS); voice_metrics(rep, "synthdi_engine_STRINGS", y, n);
    fixture_controls(c, n);
    fprintf(rep, "\nThe same measures on the fixture renders (no tracking: the instrument alone):\n\n| render | onset median (ms) | onset worst (ms) | release median (ms) | release worst (ms) | largest jump |\n| --- | --- | --- | --- | --- | --- |\n");
    for (int v = 0; v < 4; v++) { run_voice(V[v], c, y, n); snprintf(nm, sizeof nm, "fixture_%s_%s", INAME[V[v].instr], KNAME[V[v].kind]); voice_metrics(rep, nm, y, n); }

    fprintf(rep, "\n## Recorded DI phrases\n\n*Pitch identity changes*: the note layer saw the pitch move more than 0.6 semitone "
                 "without a pick (a slur, a slide, or a bend counted once per 0.6 semitone).\n\n"
                 "| phrase | tracker | instrument notes started | pitch identity changes |\n| --- | --- | --- | --- |\n");
    FILE *truth_rep = tmpfile();
    for (int a = 3; a < argc; a++) {
        char path[1024]; snprintf(path, sizeof path, "%s", argv[a]);
        char *truth = strchr(path, '='); if (truth) *truth++ = 0;          /* DI.wav=truth.csv */
        int m; float *x = wav_read(path, &m); if (!x) continue;
        const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
        char stem[128]; snprintf(stem, sizeof stem, "%s", base); char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
        printf("DI %s\n", stem);
        ctl_t *cc = malloc(sizeof(ctl_t) * m); float *yy = malloc(sizeof(float) * m), *rr = malloc(sizeof(float) * m);
        snprintf(nm, sizeof nm, "%s_dry", stem); save(nm, x, m);
        engine_render(x, yy, m, PX_SYN_BRASS); snprintf(nm, sizeof nm, "%s_engine_BRASS", stem); save(nm, yy, m);
        engine_render(x, yy, m, PX_SYN_STRINGS); snprintf(nm, sizeof nm, "%s_engine_STRINGS", stem); save(nm, yy, m);
        for (int tb = 0; tb < 2; tb++) {
            tracked_controls(x, m, cc, rr, &log, tb);
            int starts = 0, slurs = 0; for (int q = 0; q < log.n; q++) { if (log.v[q].kind) slurs++; else starts++; }
            fprintf(rep, "| %s | %s | %d | %d |\n", stem, TB[tb], starts, slurs);
            if (truth) truth_metrics(truth_rep, stem, TBT[tb], truth, cc, &log, m);
            for (int v = 0; v < 4; v++) { run_voice(V[v], cc, yy, m); snprintf(nm, sizeof nm, "%s_%s_%s_%s", stem, TB[tb], INAME[V[v].instr], KNAME[V[v].kind]); save(nm, yy, m); }
        }
        free(x); free(cc); free(yy); free(rr);
    }

    rewind(truth_rep); { char buf[4096]; size_t r; while ((r = fread(buf, 1, sizeof buf, truth_rep)) > 0) fwrite(buf, 1, r, rep); }
    fclose(truth_rep);
    tuning_table(rep);
    steady_set();

    fprintf(rep, "\n## Cost on this host (not the pedal)\n\nWall time per output sample over every render above, `-O2`, one core of the build machine. "
                 "The pedal (AC791N, 240 MHz class, single-precision FPU) is far slower; port and time it there before deciding.\n\n"
                 "| voice | ns / sample | share of real time |\n| --- | --- | --- |\n");
    for (int v = 0; v < 4; v++) { double ns = voice_ns[V[v].instr][V[v].kind] / voice_samples[V[v].instr][V[v].kind];
        fprintf(rep, "| %s %s | %.1f | %.3f %% |\n", INAME[V[v].instr], KNAME[V[v].kind], ns, ns * SR / 1e7); }

    long bytes[2] = {0, 0}; for (int k = 0; k < NBANK; k++) bytes[BANK[k].instr] += 2L * BANK[k].n;
    fprintf(rep, "\n## Memory\n\n| item | bytes |\n| --- | --- |\n"
                 "| trumpet samples, PCM16 22.05 kHz (3 roots x 2 dynamics) | %ld |\n| cello samples, PCM16 22.05 kHz (3 roots x 2 dynamics) | %ld |\n"
                 "| both at 4 bits / sample (ADPCM estimate, before block headers) | %ld |\n"
                 "| bowed waveguide: delay lines at C2 (65.4 Hz), float | %d |\n| brass waveguide: bore at E3 (164.8 Hz), float | %d |\n"
                 "| one waveguide voice as written here (2048-sample lines, body filters) | %zu |\n",
            bytes[0], bytes[1], (bytes[0] + bytes[1]) / 4, (int)(SR / 65.4f) * 4, (int)(2 * SR / 164.8f + 3) * 4, sizeof(wvoice));
    fclose(rep);
    printf("report: %s\n", fn);
    return 0;
}
