/* OpenPocket — v49: the note tracker every tracked voice shares (polyphonic, built on the HQ octave's ERB bank)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POLY OCTAVE HQ's analysis (43 complex ERB band-pass filters, 65 Hz .. 4 kHz, 1.5x ERB bandwidth, at 22.05 kHz:
 * docs/research/POLYPHONIC-PITCH-RESEARCH.md) read as a spectrum with a frequency in every band:
 *  - each band's instantaneous frequency, from its phase advance against the band centre, accumulated over a frame
 *    of PXT_HOP quarter-rate samples (1.45 ms) and weighted by the band's power: one arctangent per band per frame;
 *  - partials: the bands above the floor, neighbours that hear the same partial merged;
 *  - notes: harmonic salience (Klapuri, ISMIR 2006: every partial votes for the fundamentals it is a harmonic of,
 *    weight (f0 + 27) / (h f0 + 320)), candidates from the partials' first 5 sub-harmonics, the best one taken and
 *    its harmonics removed, repeated for up to PXT_NOTES notes;
 *  - picks: the frame's positive change of log band power summed over the bands (spectral flux), which hears a
 *    repick of a ringing string and each string of a strum.
 * Its outputs, every frame: t->mono (the strongest note's frequency, its share of the partials' level as
 * confidence), t->notes[] (lowest first), t->onset (a pick in this frame, its strength), t->level.
 * The note layer (px_track_notes.h) turns them into gate, onset, pitch, pick strength and expression for a voice.
 * Cost: the band filters (as POLY OCTAVE HQ's) + 10 products a band a sample + per frame the partials and salience.
 */
#ifndef PX_TRACK_H
#define PX_TRACK_H

#define PXT_HOP     16                 /* v49: quarter-rate (11.025 kHz) samples per frame: 1.45 ms */
#define PXT_PART    16                 /* partials considered per frame */
#define PXT_NOTES   6                  /* notes found per frame */
#ifndef PXT_FMIN
#define PXT_FMIN    60.0f              /* v72: back to 60 Hz (38 Hz in v71 let guitar notes drop an octave); was, from below a bass's E1 (41 Hz; v49..v67: 60 Hz, B1, which
                                          played E1..A#1 an octave up: tools/research, docs/archive/SYNTH-REBUILD.md) .. */
#endif
#define PXT_FMAX    1400.0f            /* .. F6 */
#ifndef PXT_OCT_A
#define PXT_OCT_A   0.05f              /* the octave-below check: its fundamental at least this of the strongest partial */
#define PXT_OCT_S   0.6f               /* and this much of the salience */

#endif
#ifndef PXT_IFK
#define PXT_IFK     0.75f              /* the phase advance's memory per frame (0.75: about 6 ms) */
#endif
#ifndef PXT_REFINE_D
#define PXT_REFINE_D 0.3f              /* the waveform refines a low note when it repeats this well */
#define PXT_REFINE_F 330.0f            /* .. below this */
#endif
#ifndef PXT_CONF
#define PXT_CONF    0.12f              /* the strongest note counts above this confidence */
#endif
#ifndef PXT_CH_S
#define PXT_CH_S    0.6f               /* v49: a chord: notes this strong against the strongest .. */
#define PXT_CH_ON   8                  /* .. in this many frames (the counter rises PXT_CH_OFF a chord frame, falls 1) */
#define PXT_CH_OFF  4
#ifndef PXT_CH_TRIG
#define PXT_CH_TRIG 0                  /* (1: a new note in a chord starts on its bass; off: it hurt single notes) */
#endif
#ifndef PXT_CH_HOLDMODE
#define PXT_CH_HOLDMODE 0              /* (1: hold the note while a chord sounds; it hurt single notes) */
#endif
#ifndef PXT_CH_SLUR
#define PXT_CH_SLUR 10                 /* in a chord, another note without a pick takes over after this many frames (15 ms) */
#endif
#define PXT_CH_HOLD 15                 /* frames its note may be missing before it takes another (22 ms) */
#endif
#ifndef PXT_MINWAIT
/* An opt-in shortcut (off by default, PX_MONO_FASTCONF 0): a note whose pitch is already stable AND whose frame confidence is at least PX_MONO_FASTCONF may speak after PX_MONO_FASTWAIT frames instead of the
 * full wait (PXT_MINWAIT / minwait_lo): a clear pitch is rarely an upper partial of the pick transient. Experiment: tests/fasttrack (docs/FASTTRACK-FASTSTART.md). */
#ifndef PX_MONO_FASTCONF
#define PX_MONO_FASTCONF 0.0f
#endif
#ifndef PX_MONO_FASTWAIT
#define PX_MONO_FASTWAIT 8
#endif
#define PXT_MINWAIT 5                  /* v49: frames a note above PXT_MW_F waits after its pick (7 ms; see the trigger) */
#define PXT_MW_F    200.0f
#endif
#ifndef PXT_SETTLE
#define PXT_SETTLE  55                 /* v49: frames after a note speaks in which its fundamental may still arrive (80 ms) */
#endif
#ifndef PXT_SLUR
#define PXT_SLUR    4                  /* frames a steady other note needs to take over (6 ms) */
#define PXT_SLUR_OCT 20                /* an octave away (29 ms) */
#endif

#ifndef PXT_NB
#define PXT_NB      15                 /* the bands it reads: those up to 790 Hz (the fundamentals and first partials of the
                                          guitar's range: on the test sets as accurate as all 29, at half the cost) */
#endif
/* Host experiments can adjust these without changing the default firmware tracker. */
#ifndef PXT_SECOND_REL
#define PXT_SECOND_REL 0.20f
#endif
#ifndef PXT_SHARED_RESIDUAL
#define PXT_SHARED_RESIDUAL 0
#endif
#define PXT_DEC     256                /* decimated history (power of two) */
#ifndef PXT_PD1
#define PXT_PD1     0.3f               /* the octave check: the chosen period this poor (normalised difference) .. */
#define PXT_PD2     0.25f              /* .. and a multiple this good, */
#define PXT_PDR     0.6f               /* .. and this much better: the note is the lower one */
#endif
#ifndef PXT_OCT_CHECK_MAX
#define PXT_OCT_CHECK_MAX 700.0f
#endif
typedef struct { float f, a; } pxt_partial;
typedef struct {
    /* the filter bank (coefficients: erb_band, made by erb_prepare: POLY OCTAVE HQ's bank, at 11.025 kHz) */
    px_erb_complex z1[PX_ERB_BANDS], z2[PX_ERB_BANDS], prev[PX_ERB_BANDS], acc[PX_ERB_BANDS];
    float pw[PX_ERB_BANDS], lg[PX_ERB_BANDS];
    float input[32], in2[32]; unsigned pos, pos2, n;
    /* per frame */
    float freq[PX_ERB_BANDS], pow[PX_ERB_BANDS];
    pxt_partial part[PXT_PART]; int npart;
    float notes[PXT_NOTES], sal[PXT_NOTES]; int nnotes;
    float mono, conf, level, onset, flux_avg;
    int frame, refractory;
    int sensitive_picks, weak_pick, weak_refractory;
    /* the waveform at 1/16 of the rate (2.76 kHz, 93 ms): the low notes' octave check (pxt_period) */
    px_bq d1, d2; float dec[PXT_DEC]; int dw, dn;
    float fmin;                       /* Synth2: the lowest pitch it looks for; 0: PXT_FMIN (60 Hz). pxt_set_range sets it (a bass patch: 38 Hz) */
} px_track;
static inline float pxt_fmin(const px_track *t) { return t->fmin > 0.0f ? t->fmin : PXT_FMIN; }
/* one tracker, two ranges: guitar (the default, 60 Hz, B1) or bass (38 Hz, E1 with room for a down-tuned bass). The 38 Hz range is worse for
 * guitar on the pedal (v71: notes dropped an octave), so only a bass sound selects it. Takes effect on the next frame. */
static inline void pxt_set_range(px_track *t, float fmin_hz) { t->fmin = fmin_hz; }

/* band centre rotation e^{-i w_c} per half-rate sample, and centre frequency: computed once (shared by all trackers) */
static px_erb_complex pxt_rot[PX_ERB_BANDS];
static float pxt_fc[PX_ERB_BANDS], pxt_wn[PX_ERB_BANDS], pxt_rate;   /* (wn: 1 / bandwidth, relative to the first band) */
static px_bq pxt_dlp[2];                                   /* the decimation's low-pass (1 kHz, 4th order) */

static void pxt_prepare(px_engine *e)
{
    erb_prepare(e);
    if (pxt_rate == erb_rate) return;
    pxt_rate = erb_rate;                                   /* (a quarter of the input rate) */
    bq_lowpass(&pxt_dlp[0], pxt_rate, 1000.0f, 0.54f); bq_lowpass(&pxt_dlp[1], pxt_rate, 1000.0f, 1.31f);
    for (int i = 0; i < PX_ERB_BANDS; ++i) {
        const float center = erb_fc[i], w = 2.0f * PI_F * center / pxt_rate;
        pxt_fc[i] = center; pxt_rot[i].r = cosf(w); pxt_rot[i].i = -sinf(w); pxt_wn[i] = erb_bw[0] / erb_bw[i];
    }
}

static inline float pxt_atan2(float y, float x)           /* |error| < 0.005 rad */
{
    const float ax = px_abs(x), ay = px_abs(y);
    if (ax < 1e-30f && ay < 1e-30f) return 0.0f;
    const float a = ax > ay ? ay / ax : ax / ay, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.0f) r = 3.14159274f - r;
    return y < 0.0f ? -r : r;
}

/* salience of f0 over the partials, and which partials it explains (mask) */
static float pxt_salience(const pxt_partial *p, int n, float f0, uint32_t *mask)
{
    float s = 0.0f; uint32_t m = 0;
    const float inv = 1.0f / f0;
    for (int j = 0; j < n; j++) {
        const float r = p[j].f * inv;
        const int h = (int)(r + 0.5f);
        if (h < 1 || h > 16) continue;
        const float d = r - (float)h;                      /* within 3 % of the harmonic's own frequency */
        if (d > 0.03f * (float)h || d < -0.03f * (float)h) continue;
        s += p[j].a * (f0 + 27.0f) / ((float)h * f0 + 320.0f);
        m |= 1u << j;
    }
    if (mask) *mask = m;
    return s;
}

/* how badly the decimated waveform repeats at lag (samples, fractional) over the last w samples: 0 exactly .. 1 not */
static float pxt_period(const px_track *t, float lag, int w)
{
    const int li = (int)lag; const float fr = lag - (float)li;
    float num = 0.0f, den = 1e-20f;
    for (int n = 0; n < w; n++) {
        const int i = t->dw - 1 - n;
        const float a = t->dec[i & (PXT_DEC - 1)];
        const float b0 = t->dec[(i - li) & (PXT_DEC - 1)], b1 = t->dec[(i - li - 1) & (PXT_DEC - 1)];
        const float b = b0 + (b1 - b0) * fr, d = a - b;
        num += d * d; den += a * a + b * b;
    }
    return num / den;
}

/* a low note's frequency from the waveform: the period within 6 % of f's that repeats best, interpolated between
 * lags (YIN's step 5, de Cheveigne & Kawahara 2002) - the sparse low bands place a partial between two of them, and
 * its band frequency is then off by tens of cents. 0 when nothing there repeats well. */
static float pxt_refine(const px_track *t, float f)
{
    const float rate = pxt_rate / 4.0f, P0 = rate / f;
    int w = (int)(2.0f * P0); if (w < 24) w = 24; if (w > 120) w = 120;
    int lo = (int)(P0 * 0.94f), hi = (int)(P0 * 1.06f) + 1;
    if (lo < 3) lo = 3;
    if (hi + 1 + w > PXT_DEC - 2) return 0.0f;
    float best = 2.0f, d[64]; int bl = -1;
    if (hi - lo + 3 > 64) return 0.0f;
    for (int L = lo - 1; L <= hi + 1; L++) d[L - lo + 1] = pxt_period(t, (float)L, w);
    for (int L = lo; L <= hi; L++) if (d[L - lo + 1] < best) { best = d[L - lo + 1]; bl = L; }
    if (bl < 0 || best > PXT_REFINE_D) return 0.0f;
    const float a = d[bl - lo], b = d[bl - lo + 1], c = d[bl - lo + 2], den = a - 2.0f * b + c;
    const float off = den > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
    return rate / ((float)bl + (off > 0.5f ? 0.5f : off < -0.5f ? -0.5f : off));
}

/* one frame: partials, notes, picks */
static __attribute__((noinline)) void pxt_frame(px_track *t)   /* (per frame: stays in the flash) */
{
    const float inv_hop = 1.0f / (float)PXT_HOP, k_if = pxt_rate / (2.0f * PI_F);
    float pmax = 0.0f, flux = 0.0f, lvl = 0.0f;
    for (int i = 0; i < PXT_NB; i++) {
        const float p = t->pw[i] * inv_hop;
        t->pow[i] = p; lvl += p;
        if (p > pmax) pmax = p;
        t->freq[i] = pxt_fc[i] + pxt_atan2(t->acc[i].i, t->acc[i].r) * k_if   /* (the sum's angle: the mean advance a sample) */;
        const float l = logf(p + 1e-12f);
        if (l > t->lg[i]) flux += l - t->lg[i];
        t->lg[i] = t->lg[i] + 0.35f * (l - t->lg[i]);       /* (a short memory: the last few frames) */
        t->acc[i].r *= PXT_IFK; t->acc[i].i *= PXT_IFK; t->pw[i] = 0.0f;   /* (the phase advance: a leaky average over a few
                                                            frames, so two partials beating in a band average out) */
    }
    t->level = lvl;
    /* picks: the flux against its own running level */
    t->onset = 0.0f;
    if (t->refractory > 0) t->refractory--;
    const float strong_pick = 6.0f + 2.5f * t->flux_avg;
    /* Provisional candidates get their own cooldown so they cannot mask a strong pick. */
    if (t->weak_refractory > 0) t->weak_refractory--;
    if (lvl > 1e-9f && t->refractory == 0) {
        if (flux > strong_pick) {
            t->onset = flux;
            t->weak_pick = 0;
            t->refractory = 20;
        } else if (t->sensitive_picks && t->weak_refractory == 0 && flux > 6.0f + 1.5f * t->flux_avg) {
            t->onset = flux;
            t->weak_pick = 1;
            t->weak_refractory = 20;
        }
    }
    t->flux_avg += 0.05f * (flux - t->flux_avg);
    /* partials: local maxima of band power above the floor, at the band's own frequency */
    t->npart = 0;
    const float floor_p = pmax * 1e-3f > 1e-10f ? pmax * 1e-3f : 1e-10f;
    for (int i = 0; i < PXT_NB; i++) {
        const float p = t->pow[i];
        if (p < floor_p) continue;
        if (i > 0 && t->pow[i - 1] > p) continue;
        if (i + 1 < PX_ERB_BANDS && t->pow[i + 1] >= p) continue;
        const float f = t->freq[i];
        if (f < pxt_fmin(t) * 0.97f) continue;
        pxt_partial q = {f, sqrtf(p * pxt_wn[i])};     /* (power per Hz: wider bands do not make louder partials) */
        if (t->npart < PXT_PART) t->part[t->npart++] = q;
        else {                                            /* keep the strongest */
            int w = 0; for (int j = 1; j < PXT_PART; j++) if (t->part[j].a < t->part[w].a) w = j;
            if (q.a > t->part[w].a) t->part[w] = q;
        }
    }
    /* notes: the best candidate, its harmonics out, again */
    pxt_partial p[PXT_PART]; int n = t->npart;
    for (int j = 0; j < n; j++) p[j] = t->part[j];
    float total = 0.0f; for (int j = 0; j < n; j++) total += p[j].a;
    t->nnotes = 0; t->mono = 0.0f; t->conf = 0.0f;
    float first = 0.0f;
    for (int k = 0; k < PXT_NOTES && n > 0; k++) {
        float best = 0.0f, bf = 0.0f; uint32_t bm = 0;
        for (int j = 0; j < n; j++) for (int h = 1; h <= 5; h++) {
            const float f0 = p[j].f / (float)h;
            if (f0 < pxt_fmin(t) || f0 > PXT_FMAX) continue;
#if PXT_SHARED_RESIDUAL
            int duplicate = 0;
            for (int j = 0; j < t->nnotes; ++j)
                if (px_abs(f0 / t->notes[j] - 1.0f) < 0.035f) duplicate = 1;
            if (duplicate) continue;
#endif
            uint32_t m; const float s = pxt_salience(p, n, f0, &m);
            if (s > best * 1.0001f) { best = s; bf = f0; bm = m; }
        }
        if (best <= 0.0f || (k > 0 && best < PXT_SECOND_REL * first)) break;
        /* a weak fundamental (common on a guitar pickup): the note an octave (a twelfth) below wins when its own
         * fundamental is there and it explains nearly as much */
        {   float amax = 0.0f; for (int j = 0; j < n; j++) if (p[j].a > amax) amax = p[j].a;
            float nb = best, nf = bf; uint32_t nm = bm;             /* the lowest that passes, of /2 .. /5 */
            for (int d = 2; d <= 5; d++) {
                const float lo = bf / (float)d;
                if (lo < pxt_fmin(t)) break;
#if PXT_SHARED_RESIDUAL
                int duplicate = 0;
                for (int j = 0; j < t->nnotes; ++j)
                    if (px_abs(lo / t->notes[j] - 1.0f) < 0.035f) duplicate = 1;
                if (duplicate) continue;
#endif
                uint32_t m; const float sl = pxt_salience(p, n, lo, &m);
                float a1 = 0.0f;
                for (int j = 0; j < n; j++) { const float r = p[j].f / lo; if (r > 0.95f && r < 1.05f && p[j].a > a1) a1 = p[j].a; }   /* (5 %: the sparse low bands misplace a partial) */
                if (a1 > PXT_OCT_A * amax && sl > PXT_OCT_S * best) { nb = sl; nf = lo; nm = m; }
            }
            best = nb; bf = nf; bm = nm;
        }
        /* the note's frequency: from its lowest explaining partial (the best-resolved one) */
        /* low notes: the waveform decides between the note and its octave or twelfth below (a guitar pickup's weak
         * fundamental makes the 2nd or 3rd harmonic the strongest partial) */
        float mono_f = 0.0f;                                   /* (the one-note pitch: the chord list keeps bf) */
        if (k == 0 && bf < PXT_OCT_CHECK_MAX) {
            const float P = pxt_rate / 4.0f / bf;              /* (the history: every 4th quarter-rate sample) */
            int w = (int)(2.0f * P); if (w < 24) w = 24; if (w > 120) w = 120;
            const float d1 = pxt_period(t, P, w);
#ifdef PXT_LOCAL_PERIOD_FIRST
            /* The spectral estimate may be off enough to miss a nearby
             * period minimum. Compare that before accepting a longer lag. */
            if (d1 > PXT_PD1 && bf < PXT_REFINE_F)
                mono_f = pxt_refine(t, bf);
#endif
            if (d1 > PXT_PD1 && mono_f <= 0.0f) {
                int kb = 0; float db = 1.0f;
                for (int q = 2; q <= 4; q++) {
                    if ((float)q * P + (float)w > (float)(PXT_DEC - 4) || bf / (float)q < pxt_fmin(t)) break;
                    const float dq = pxt_period(t, (float)q * P, w);
                    if (dq < PXT_PD2 && dq < PXT_PDR * d1 && dq < db) { db = dq; kb = q; }
                }
                if (kb) {                                          /* refined on its lowest explaining partial */
                    const float lo = bf / (float)kb; uint32_t m; pxt_salience(p, n, lo, &m);
                    int bh = 99; mono_f = lo;
                    for (int j = 0; j < n; j++) if (m >> j & 1u) { const int h = (int)(p[j].f / lo + 0.5f); if (h < bh) { bh = h; mono_f = p[j].f / (float)h; } }
                }
            }
        }
        {   int bh = 99; float rf = bf;
            for (int j = 0; j < n; j++) if (bm >> j & 1u) { const int h = (int)(p[j].f / bf + 0.5f); if (h < bh) { bh = h; rf = p[j].f / (float)h; } }
            bf = rf; }
        if (k == 0) {
            first = best; t->mono = mono_f > 0.0f ? mono_f : bf; t->conf = total > 0.0f ? best / total : 0.0f;
            if (mono_f > 0.0f && total > 0.0f) { const float sm = pxt_salience(p, n, mono_f, 0) / total; if (sm > t->conf) t->conf = sm; }
            if (t->mono < PXT_REFINE_F) { const float r = pxt_refine(t, t->mono); if (r > 0.0f) t->mono = r; }
        }
        t->notes[t->nnotes] = bf; t->sal[t->nnotes] = best; t->nnotes++;
        int w = 0;
        for (int j = 0; j < n; ++j) {
            if (!(bm >> j & 1u)) p[w++] = p[j];
#if PXT_SHARED_RESIDUAL
            else if (p[j].f > 1.5f * bf) {
                /* Retain some overlapping upper-harmonic evidence for the next candidate. */
                p[j].a *= 0.35f;
                p[w++] = p[j];
            }
#endif
        }
        n = w;
    }
    for (int a = 1; a < t->nnotes; a++)                   /* lowest first */
        for (int b = a; b > 0 && t->notes[b] < t->notes[b - 1]; b--) {
            float x = t->notes[b]; t->notes[b] = t->notes[b - 1]; t->notes[b - 1] = x;
            x = t->sal[b]; t->sal[b] = t->sal[b - 1]; t->sal[b - 1] = x;
        }
    t->frame++;
}

/* Shared half-band stages. Return 1 at the quarter-rate sample. */
static inline int pxt_decimate(px_track *t, float input, float *output)
{
    const unsigned pos = t->pos;
    t->input[pos] = input;
    t->pos = (pos + 1) & 31;
    if (pos & 1) return 0;
    const unsigned p2 = t->pos2;
    t->in2[p2] = erb_halfband(t->input, pos, erb_hb);
    t->pos2 = (p2 + 1) & 31;
    if (p2 & 1) return 0;
    *output = erb_halfband(t->in2, p2, erb_hb);
    return 1;
}

/* one input sample (44.1 kHz). Returns 1 when a frame was completed (results in t). */
static __attribute__((noinline)) int pxt_run(const px_engine *e, px_track *t, float input)   /* (in RAM on the pedal) */
{
    float x;
    if (!pxt_decimate(t, input, &x)) return 0;
    (void)e;
    if (t->d1.b0 == 0.0f) { t->d1 = pxt_dlp[0]; t->d2 = pxt_dlp[1]; }
    {   const float y = bq_run(&t->d2, bq_run(&t->d1, x));
        if (++t->dn >= 4) { t->dn = 0; t->dec[t->dw] = y; t->dw = (t->dw + 1) & (PXT_DEC - 1); } }
    for (int i = 0; i < PXT_NB; ++i) {
        const px_erb_band *b = &erb_band[i];
        const float real = b->b0 * x + t->z1[i].r, imag = t->z1[i].i;
        t->z1[i].r = b->b1.r * x - (b->a1.r * real - b->a1.i * imag) + t->z2[i].r;
        t->z1[i].i = b->b1.i * x - (b->a1.r * imag + b->a1.i * real) + t->z2[i].i;
        t->z2[i].r = b->b2.r * x - (b->a2.r * real - b->a2.i * imag);
        t->z2[i].i = b->b2.i * x - (b->a2.r * imag + b->a2.i * real);
        /* z conj(previous z) e^{-i w_c}: the phase advance against the band centre, power weighted */
        const float pr = t->prev[i].r, pi = t->prev[i].i;
        const float wr = real * pr + imag * pi, wi = imag * pr - real * pi;
        t->acc[i].r += wr * pxt_rot[i].r - wi * pxt_rot[i].i;
        t->acc[i].i += wr * pxt_rot[i].i + wi * pxt_rot[i].r;
        t->pw[i] += real * real + imag * imag;
        t->prev[i].r = real; t->prev[i].i = imag;
    }
    if (++t->n < PXT_HOP) return 0;
    t->n = 0;
    pxt_frame(t);
    return 1;
}

/* ---- the note layer: what a one-note voice plays (gate, onset, pitch, pick strength, expression), per sample ----
 * A pick (the tracker's flux, or the level rising from silence) waits for the pitch: the strongest note held within
 * 30 cents for two frames (3 ms) speaks. While it sounds it follows the found note nearest to it (bends, slides,
 * vibrato); a steady other note without a pick (a hammer-on, a slur) takes over after 15 ms, an octave jump after
 * 60 ms. Pick strength: the first 6 ms of the attack, -40 .. -6 dBFS -> 0 .. 1; expression: the level against the
 * attack's, half its fall in dB. */
typedef struct {
    float env, envs, onset_peak, peak_ref, vel, expr, f0, stab_f, cand_f;
    int key, state, peak_n, stab_n, cand_n, wait_n;   /* state 0 off, 1 waiting for a pitch, 2 sounding, 3 repicked */
    int chord_n, miss_n;                              /* v49: chord frames (hysteresis); frames its note was not found */
    int since, sub_n; float sub_f;                    /* v49: frames since the note spoke; its sub-multiple candidate */
    int gate, onset;                                  /* outputs: gate on; a new note this sample */
    float k_ea, k_er, k_xa, k_xr, sr;
    int minwait; float minwait_f;                     /* Synth2: frames a note above minwait_f Hz waits after its pick (0 = PXT_MINWAIT, PXT_MW_F) */
    int weak_pick;
    int stabreq, minwait_lo;                          /* Synth2: stable frames a note needs before it speaks (0 = 2); frames any note below minwait_f waits (0 = none) */
} pxt_layer;

static void pxt_layer_init(pxt_layer *L, float sr)
{
    memset(L, 0, sizeof *L);
    L->sr = sr; L->k_ea = onepole_k(sr, 0.001f); L->k_er = onepole_k(sr, 0.03f);
    L->k_xa = onepole_k(sr, 0.015f); L->k_xr = onepole_k(sr, 0.40f);
}
static inline int pxt_near(float a, float b, float c)   /* within c cents (c = 30, 50, 250 ...) */
{
    if (a <= 0.0f || b <= 0.0f) return 0;
    const float r = a / b, k = c * 0.000577623f;          /* ln 2 / 1200 */
    return r > 1.0f - k && r < 1.0f + k * (1.0f + 0.5f * k);
}
static inline float pxt_pick_velocity(float peak)
{
    const float v = (8.685890f * logf(peak + 1e-9f) + 40.0f) * (1.0f / 34.0f);
    return v < 0.05f ? 0.05f : v > 1.0f ? 1.0f : v;
}
/* The ERB peak can follow an upper partial through a pick. Check the
 * waveform before sending a high candidate to the synth. Require clear
 * evidence for a longer period so a real high note keeps its pitch. Return
 * zero when neither the high candidate nor a lower period is convincing. */
static float pxt_pick_fundamental(const px_track *t, float f)
{
    if (f < 300.0f) return f;
    const float lag = (pxt_rate * 0.25f) / f;
    const int window = 20;
    const float error = pxt_period(t, lag, window);
    if (error < 0.5f) return f;
    for (int h = 2; h <= 4; h++) {
        const float lower = f / (float)h;
        if (lower < pxt_fmin(t)) break;
        if (pxt_period(t, lag * (float)h, window) < 0.3f) {
            const float refined = pxt_refine(t, lower);
            return refined > 0.0f ? refined : lower;
        }
    }
    return error < 0.7f ? f : 0.0f;
}
/* Confirm a weak flux candidate from the samples since its pick. A much better
 * double/triple period means the candidate could be an overtone. */
static int pxt_confirm_pick(const px_track *t, float f, int frames)
{
    const float lag = (pxt_rate * 0.25f) / f;
    const int recent = (frames - 1) * (PXT_HOP / 4);
    const int window = (int)(0.5f * lag) + 1;
    if (window + (int)lag + 1 > recent || window + (int)lag + 1 >= PXT_DEC) return 0;
    const float error = pxt_period(t, lag, window);
    if (error > PXT_REFINE_D) return 0;
    for (int h = 2; h <= 3; h++) {
        const float longer = lag * (float)h;
        if ((int)longer + window + 1 > recent || (int)longer + window + 1 >= PXT_DEC ||
            f < h * pxt_fmin(t)) break;
        if (pxt_period(t, longer, window) + 0.12f < error) return 0;
    }
    return 1;
}
/* x: the guitar sample; framed: pxt_run returned 1 for it (t holds a new frame) */
static __attribute__((noinline)) void pxt_layer_run(pxt_layer *L, const px_track *t, float x, int framed)
{
    const float a = px_abs(x), thr = 0.003f;
    L->env += (a - L->env) * (a > L->env ? L->k_ea : L->k_er);
    L->envs += (L->env - L->envs) * (L->env > L->envs ? L->k_xa : L->k_xr);
    const int on = L->key ? L->env > thr * 0.5f : L->env > thr * 1.5f;
    const int rise = on && !L->key;
    L->key = on;
    L->onset = 0;
    if (!on) { L->state = 0; L->gate = 0; return; }
    if (rise || (framed && t->onset > 0.0f && L->state >= 2 &&
                 (L->state != 3 || !t->weak_pick))) {
        L->weak_pick = !rise && t->weak_pick;
        L->state = L->state >= 2 ? 3 : 1; L->onset_peak = 0.0f; L->peak_n = 0; L->stab_n = 0; L->wait_n = 0;
    }
    if (L->peak_n < (int)(0.006f * L->sr)) { if (a > L->onset_peak) L->onset_peak = a; L->peak_n++; }
    if (framed) {
        float m = t->conf > PXT_CONF ? t->mono : 0.0f;
        /* v49: a chord (two notes or more of comparable salience): a one-note voice holds one chord tone instead of
         * jumping between them - it keeps its note while the tracker still finds it, else takes the lowest strong one */
        float smax = 0.0f, low = 0.0f; int strong = 0;
        for (int k = 0; k < t->nnotes; k++) if (t->sal[k] > smax) smax = t->sal[k];
        for (int k = 0; k < t->nnotes; k++) if (t->sal[k] >= PXT_CH_S * smax) { strong++; if (low == 0.0f) low = t->notes[k]; }
        if (strong >= 2) { if (L->chord_n < PXT_CH_ON + PXT_CH_OFF) L->chord_n += PXT_CH_OFF; }
        else if (L->chord_n > 0) L->chord_n--;
        const int chord = L->chord_n >= PXT_CH_ON;
#if PXT_CH_TRIG
        if (chord && low > 0.0f && (L->state == 1 || L->state == 3)) m = low;   /* (a new note in a chord: its bass) */
#endif
        if (L->state == 1 || L->state == 3) {
            L->wait_n++;
            if (m > 0.0f && pxt_near(m, L->stab_f, 30.0f)) L->stab_n++; else { L->stab_f = m; L->stab_n = m > 0.0f; }
            /* v49: a note above PXT_MW_F waits PXT_MINWAIT frames after the pick: a low note's fundamental is still on
             * its way through the low bands and its high partials would speak first */
            int early = (L->stab_f > (L->minwait_f > 0.0f ? L->minwait_f : PXT_MW_F) && L->wait_n < (L->minwait ? L->minwait : PXT_MINWAIT)) || L->wait_n < L->minwait_lo;
            if (PX_MONO_FASTCONF > 0.0f && early && t->conf >= PX_MONO_FASTCONF && L->wait_n >= PX_MONO_FASTWAIT && L->stab_n >= (L->stabreq ? L->stabreq : 2)) early = 0;   /* (opt-in shortcut above) */
            if (L->weak_pick && L->wait_n > 28) L->state = 2;  /* expire the provisional repick after ~41 ms */
            else if (!early && (L->stab_n >= (L->stabreq ? L->stabreq : 2) || (L->wait_n > 55 && L->stab_n >= 1)) &&
                     (!L->weak_pick || pxt_confirm_pick(t, L->stab_f, L->wait_n))) {
                const float pitch = t->sensitive_picks ? pxt_pick_fundamental(t, L->stab_f) : L->stab_f;
                if (pitch <= 0.0f) return;
                L->vel = pxt_pick_velocity(L->onset_peak);
                L->peak_ref = L->envs > 1e-5f ? L->envs : 1e-5f;
                L->f0 = pitch;
                L->state = 2; L->onset = 1; L->cand_n = 0; L->since = 0; L->sub_n = 0;
            }
#if PXT_CH_HOLDMODE
        } else if (L->state == 2 && chord) {
            float best = 0.0f, bd = 1e9f;                  /* the found note nearest the one it plays */
            for (int k = 0; k < t->nnotes; k++) { const float q = t->notes[k] / L->f0, d = q > 1.0f ? q : 1.0f / q; if (d < bd) { bd = d; best = t->notes[k]; } }
            if (best > 0.0f && bd < 1.03f) { L->f0 += 0.6f * (best - L->f0); L->miss_n = 0; }
            else if (++L->miss_n >= PXT_CH_HOLD && low > 0.0f) { L->f0 = low; L->miss_n = 0; }
            L->cand_n = 0;
#endif
        } else if (L->state == 2 && L->since < PXT_SETTLE && m > 0.0f && m < 0.55f * L->f0 &&
                   pxt_near(L->f0 / (float)(int)(L->f0 / m + 0.5f), m, 50.0f)) {
            /* v49: just after a note speaks, the low bands are still settling: a low note can speak on a high partial
             * first (heard on the bowed string as a squeak). Its fundamental arriving - the pitch at a whole fraction,
             * 1/2 .. 1/8, held two frames - takes over at once */
            /* A lower estimate can be a multiple of a period that still fits the waveform. */
            if (t->sensitive_picks && pxt_period(t, (pxt_rate * 0.25f) / L->f0, 20) < 0.2f) {
                L->sub_n = 0;
            } else if (pxt_near(m, L->sub_f, 50.0f)) { if (++L->sub_n >= 2) { L->f0 = m; L->sub_n = 0; L->cand_n = 0; } }
            else { L->sub_f = m; L->sub_n = 1; }
        } else if (L->state == 2) {
            /* follow the strongest note while it stays within 120 cents a frame (bends, slides, vibrato); another
             * steady strongest note without a pick (a hammer-on, a slur) takes over after 15 ms, an octave after 60 */
            if (m > 0.0f && pxt_near(m, L->f0, 120.0f)) { L->f0 += 0.6f * (m - L->f0); L->cand_n = 0; }
            else if (m > 0.0f) {
                if (pxt_near(m, L->cand_f, 50.0f)) L->cand_n++;          /* (a leaky vote: one odd frame does not reset it) */
                else if (L->cand_n > 0) L->cand_n--;
                else { L->cand_f = m; L->cand_n = 1; }
                const int octave = pxt_near(m, 2.0f * L->f0, 50.0f) || pxt_near(m, 0.5f * L->f0, 50.0f);
                if (L->cand_n >= (octave ? PXT_SLUR_OCT : chord ? PXT_CH_SLUR : PXT_SLUR)) {
                    const float pitch = t->sensitive_picks ? pxt_pick_fundamental(t, L->cand_f) : L->cand_f;
                    if (pitch > 0.0f) L->f0 = pitch;
                    L->cand_n = 0;
                }
            }
        }
    }
    if (framed && L->state == 2 && L->since < 1000) L->since++;
    if (L->state == 1) { L->gate = 0; return; }           /* nothing sounds before the first pitch */
    L->gate = 1;
    float rel = L->envs / L->peak_ref; if (rel > 1.0f) rel = 1.0f;
    const float ex = L->vel * sqrtf(rel);
    L->expr = ex < 0.3f * L->vel ? 0.3f * L->vel : ex;
}

/* v49: the engine's trackers. One for the SYN block (its tracked voices: TRUMPET .. CELLO SECTION, BASS SYNTH HQ, PWM SYNTH
 * HQ; one SYN type plays at a time) and one for the SFX block's SQUARE SYNTH HQ, which can play beside it. Out of the
 * engine (it has no room), in the low RAM on the pedal. */
static px_track pxa_trk PX_LO(pxa_trk), pxs_trk PX_LO(pxs_trk);
static pxt_layer pxa_lay, pxs_lay;
static void pxt_reset(px_engine *e, px_track *t, pxt_layer *L)
{
    pxt_prepare(e);
    const float range = t->fmin;                       /* a reset starts a new note's state, not a new range */
    memset(t, 0, sizeof *t);
    t->fmin = range;
    pxt_layer_init(L, e->sr);
}

/* v49: an HQ synth's oscillator (synth_hq_pulse's quarter-rate phase) from the note layer's pitch, gliding as
 * synth_hq_track's did (5.7 ms); no note, no pitch */
static inline void pxt_drive(px_synth_tracking *o, const pxt_layer *L, float sr)
{
    const float target = L->gate && L->f0 > 0.0f ? 0.25f * L->f0 / sr : 0.0f;
    if (target == 0.0f) o->step = 0.0f;
    else if (o->step == 0.0f) o->step = target;
    else o->step += (target - o->step) * (175.0f / sr);
    o->phase = wrap1(o->phase + o->step);
}

#endif
