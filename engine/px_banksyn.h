/* SPDX-License-Identifier: GPL-3.0-only */
/* Waveform-following synths: nothing here estimates a note. Three families, all following the guitar sample by sample
 * (tests/experiments/organ_bank, docs/research/SQUARE-FOLLOWER-VOICES.md):
 *  - PADS (polyphonic): POLY OCTAVE HQ's 26-band bank. Each band holds the guitar near one partial as a complex signal
 *    z = |z| u; its harmonic k is |z| Re(u^k) (16' = the octave-down half phase). Drawbar weights mix them; the post
 *    stage gives the instrument its envelope, chorus, formants or filter.
 *  - LEADS (one note): the same bank; the lowest band holding real level is the note's fundamental, and the phase step
 *    of that band from one bank sample to the next is its frequency right now (no period count, no confirmation). That
 *    drives a band-limited saw / pulse and a resonant filter.
 *  - SQUARE family (one note): the original SFX SQUARE SYNTH's crossings (700 Hz low-pass, +-10 % of a 2/150 ms
 *    follower), square and sub, shaped after the fact.
 * Included by px_dsp.c (needs its bank, filters and helpers). */

static float opk(float sr, float hz) { float k = 2.0f * PI_F * hz / sr; return k > 0.9f ? 0.9f : k; }   /* one-pole */
static inline float bs_atan2(float y, float x)         /* |error| < 1e-5 rad (v75's pv_atan2) */
{
    const float ax = x < 0.0f ? -x : x, ay = y < 0.0f ? -y : y, mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx < 1e-30f) return 0.0f;
    const float a = mn / mx, s = a * a;
    float r = ((((-0.0117212f * s + 0.05265332f) * s - 0.11643287f) * s + 0.19354346f) * s - 0.33262347f) * s + 0.99997726f;
    r *= a;
    if (ay > ax) r = 1.5707963f - r;
    if (x < 0.0f) r = 3.1415927f - r;
    return y < 0.0f ? -r : r;
}

#ifndef PS_GAIN_TABLE
#define PS_GAIN_TABLE 1.767f, 1.774f, 1.775f, 1.788f, 1.774f, 1.768f, 1.771f, 1.780f, 1.754f, 1.759f, 1.819f, 1.759f, 2.436f, \
    1.763f, 1.788f, 1.771f, 1.770f, 1.751f, 1.786f, 1.753f, 1.774f, 1.774f, 1.771f, 1.766f, 1.689f   /* -12 .. +12 (fast-play DI) */
#endif
enum { BS_PAD, BS_BRASS, BS_TWELVE, BS_BELL, BS_LEAD, BS_SQUARE };
typedef struct {
    uint8_t family;           /* BS_* */
    uint8_t wave;             /* LEAD: 0 saw, 1 pulse (PWM), 2 two detuned saws; SQUARE: 0 brass, 1 bass, 2 flute */
    uint8_t formant;          /* PAD: the choir's vowel filters */
    int8_t  oct;              /* LEAD: octave of the oscillator (-1, -2: bass) */
    float w[8];               /* PAD: 16' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' */
    float gain;               /* level at LEVEL 50, matched by ear-level measurement (host, fast-play DI) */
} bs_voice;

static const bs_voice BS_VOICES[] = {
    /* PX_SYN_BS_STRINGS */ {BS_PAD, 0, 0, 0, {0.45f, 1.0f, 0.55f, 0, 0, 0, 0, 0}, 0.871f},
    /* PX_SYN_BS_CHOIR   */ {BS_PAD, 0, 1, 0, {0.35f, 1.0f, 0.45f, 0.15f, 0, 0, 0, 0}, 0.933f},
    /* PX_SYN_BS_BRASS   */ {BS_BRASS, 0, 0, 0, {0.30f, 1.0f, 0.60f, 0.45f, 0.20f, 0, 0, 0}, 0.759f},
    /* PX_SYN_BS_PIPE    */ {BS_PAD, 0, 0, 0, {0.90f, 0.80f, 0.70f, 0.30f, 0.50f, 0.20f, 0, 0.25f}, 0.556f},
    /* PX_SYN_BS_TWELVE  */ {BS_TWELVE, 0, 0, 0, {0, 0, 1.0f, 0, 0, 0, 0, 0}, 1.334f},
    /* PX_SYN_BS_BELL    */ {BS_BELL, 0, 0, 0, {0, 0.25f, 0.30f, 0.60f, 0.45f, 0.40f, 0, 0.30f}, 1.496f},
    /* PX_SYN_BS_SAW     */ {BS_LEAD, 0, 0, 0, {0}, 0.389f},
    /* PX_SYN_BS_PULSE   */ {BS_LEAD, 1, 0, 0, {0}, 0.313f},
    /* PX_SYN_BS_SUBBASS */ {BS_LEAD, 0, 0, -1, {0}, 0.372f},
    /* PX_SYN_BS_ANALOG  */ {BS_LEAD, 2, 0, 0, {0}, 0.462f},
    /* PX_SYN_SQ_BRASS   */ {BS_SQUARE, 0, 0, 0, {0}, 0.195f},
    /* PX_SYN_SQ_BASS    */ {BS_SQUARE, 1, 0, 0, {0}, 0.229f},
    /* PX_SYN_SQ_FLUTE   */ {BS_SQUARE, 2, 0, 0, {0}, 0.398f},
};
static inline int bs_index(int t) { return t - PX_SYN_BS_STRINGS; }
static inline int px_syn_bs(int t) { return t >= PX_SYN_BS_STRINGS && t < PX_SYN_COUNT; }

/* the choir's vowels: three formants (Hz) and their levels */
static const float BS_VOWEL[5][6] = {
    {800, 1150, 2900, 1.0f, 0.50f, 0.10f},   /* AH */
    {450, 800, 2830, 1.0f, 0.35f, 0.08f},    /* OH */
    {325, 700, 2530, 1.0f, 0.25f, 0.05f},    /* OO */
    {400, 1700, 2600, 1.0f, 0.35f, 0.12f},   /* EH */
    {270, 2140, 2950, 1.0f, 0.20f, 0.12f},   /* EE */
};
static const float BS_STOPS[4][8] = {
    {0.0f, 1.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.9f, 0.8f, 0.7f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f},
    {0.9f, 0.8f, 0.7f, 0.3f, 0.5f, 0.2f, 0.0f, 0.25f},
    {0.5f, 0.7f, 0.6f, 0.5f, 0.6f, 0.4f, 0.3f, 0.45f},
};

/* the bank at 11.025 kHz: PADS mix the drawbars; LEADS pick the lowest band and measure its phase step */
static PX_HOT float bs_bank(px_engine *e, float x)
{
    px_erb_octave *s = &e->erb_syn;
    px_bsyn *b = &e->bs;
    float y = 0.0f, top = 0.0f;
    const int lead = b->family == BS_LEAD;
    for (int i = 0; i < ERB_O_BANDS; ++i) {
        const px_erb_band *f = &erb_oband[i];
        const float real = f->b0 * x + s->z1[i].r, imag = s->z1[i].i;
        s->z1[i].r = f->b1.r * x - (f->a1.r * real - f->a1.i * imag) + s->z2[i].r;
        s->z1[i].i = f->b1.i * x - (f->a1.r * imag + f->a1.i * real) + s->z2[i].i;
        s->z2[i].r = f->b2.r * x - (f->a2.r * real - f->a2.i * imag);
        s->z2[i].i = f->b2.i * x - (f->a2.r * imag + f->a2.i * real);
        const float power = real * real + imag * imag + 1e-24f, inv = erb_inverse_root(power), mag = power * inv;
        if (lead) {
            float *env = &s->low_env[i];
            *env += (mag - *env) * (mag > *env ? 0.05f : 0.004f);
            if (*env > top) top = *env;
            if (i == b->sel) {                        /* the phase step of the chosen band: its frequency now */
                const float cr = real * b->pr + imag * b->pi, ci = imag * b->pr - real * b->pi;
                if (b->pr != 0.0f || b->pi != 0.0f) {
                    const float hz = bs_atan2(ci, cr) * (erb_rate / (2.0f * PI_F));
                    if (hz > 25.0f) b->hz_raw = hz;
                }
                b->pr = real; b->pi = imag;
            }
            continue;
        }
        const float ur = real * inv, ui = imag * inv;
        const float *w = b->w;
        /* 16': |z| cos(arg z / 2) with the sign kept across the negative real axis (erb_bank's octave down) */
        const unsigned negative = imag < 0.0f;
        unsigned phase = s->half_phase[i];
        if (real < 0.0f && (phase & 1) != negative) phase ^= 2;
        s->half_phase[i] = (uint8_t)((phase & 2) | negative);
        if (w[0] != 0.0f) {
            const float hc = 0.5f + 0.5f * ur, sq = hc > 0.0f ? power * hc + 1e-30f : 1e-30f, sub = sq * erb_inverse_root(sq);
            y += w[0] * 1.45f * ((phase & 2) ? -sub : sub);
        }
        y += w[1] * real;
        /* harmonics k = 2..8 while k fc stays under the bank's 5.5 kHz limit */
        float pr = ur, pi = ui;
        const float lim = 4600.0f / erb_ofc[i];
        for (int k = 2; k <= 8 && (float)k < lim; ++k) {
            const float nr = pr * ur - pi * ui; pi = pr * ui + pi * ur; pr = nr;
            const float wk = k <= 6 ? w[k] : (k == 8 ? w[7] : 0.0f);
            if (wk != 0.0f) y += wk * 0.6f * mag * pr;
        }
    }
    if (lead) {                                        /* the lowest band with a quarter of the loudest band's level */
        int sel = b->sel;
        const float floor = top * 0.25f + 2e-4f;
        int low = -1;
        for (int i = 0; i < ERB_O_BANDS; ++i) if (s->low_env[i] > floor) { low = i; break; }
        if (low >= 0 && (sel < 0 || low < sel - 1 || s->low_env[sel] < top * 0.12f)) sel = low;
        if (sel != b->sel) { b->sel = sel; b->pr = b->pi = 0.0f; }   /* a new band: its first step is measured next sample */
        b->level = top;
    }
    return y;
}

/* the rate chain around bs_bank (erb_octaves2's): 44.1 -> 11.025 kHz and back */
static PX_HOT float bs_chain(px_engine *e, float input)
{
    px_erb_octave *s = &e->erb_syn;
    const unsigned pos = s->pos;
    s->input[pos] = input;
    if ((pos & 1) == 0) {
        const unsigned p2 = s->pos2;
        s->in2[p2] = erb_halfband(s->input, pos, erb_hb);
        if ((p2 & 1) == 0) s->up2[p2] = bs_bank(e, erb_halfband(s->in2, p2, erb_hb));
        s->up[pos] = (p2 & 1) ? s->up2[(p2 - 15) & 31] : 2.0f * erb_halfband(s->up2, p2, erb_hb);
        s->pos2 = (p2 + 1) & 31;
    }
    s->pos = (pos + 1) & 31;
    if (pos & 1) return s->up[(pos - 15) & 31];
    return 2.0f * erb_halfband(s->up, pos, erb_hb);
}

/* a band-limited saw, phase 0..1, step dt (PolyBLEP) */
static inline float bs_saw(float ph, float dt)
{
    float y = 2.0f * ph - 1.0f;
    if (ph < dt) { const float t = ph / dt; y -= t + t - t * t - 1.0f; }
    else if (ph > 1.0f - dt) { const float t = (ph - 1.0f) / dt; y -= t * t + t + t + 1.0f; }
    return y;
}

/* a state-variable low-pass (Chamberlin, twice per sample for stability up to ~8 kHz) */
static inline float bs_svf(px_bsyn *b, float x, float hz, float damp)
{
    const float w = PI_F * hz * (1.0f / 88200.0f);
    float f = 2.0f * w * (1.0f - w * w * (1.0f / 6.0f)); if (f > 0.9f) f = 0.9f;   /* 2 sin(w), w small */
    for (int r = 0; r < 2; ++r) {
        const float hp = x - b->lo - damp * b->bp;
        b->bp += f * hp; b->lo += f * b->bp;
    }
    return b->lo;
}

static void bs_setup(px_engine *e, int t, const uint8_t *q)
{
    px_bsyn *b = &e->bs;
    const int octave_mix = t == PX_SYN_BS_STRINGS_OCT || t == PX_SYN_BS_CHOIR_OCT;
    if (octave_mix) t = t == PX_SYN_BS_STRINGS_OCT ? PX_SYN_BS_STRINGS : PX_SYN_BS_CHOIR;
    const bs_voice *v = &BS_VOICES[bs_index(t)];
    const float k0 = f01(q[0]), k1 = f01(q[1]), k2 = f01(q[2]), k3 = f01(q[3]), k4 = f01(q[4]);
    b->family = v->family; b->wave = v->wave; b->oct = v->oct; b->formant = v->formant;
    memcpy(b->w, v->w, sizeof b->w);
    const float sr = e->sr;
    b->lvl = v->gain * 2.0f * (k4 < 0.5f ? 4.0f * k4 * k4 : db2lin(12.0f * (k4 - 0.5f)));
    b->tone_hz = 600.0f + 7000.0f * k0 * k0;
    b->att = onepole_k(sr, 0.004f + 0.9f * k1 * k1);           /* ATTACK 4 ms .. 0.9 s */
    b->rel = onepole_k(sr, 0.03f + 2.5f * k2 * k2);            /* RELEASE 30 ms .. 2.5 s */
    b->chorus = octave_mix ? 0.5f : k3;
    if (octave_mix) {
        /* Scale only the existing octave down and octave up weights.
         * This runs on edits, so the audio loop has the same work at 50–100%. */
        b->w[0] *= 2.0f * k3;
        b->w[2] *= 2.0f * k3;
    }
    b->k0 = k0; b->k1 = k1; b->k2 = k2; b->k3 = k3;
    if (t == PX_SYN_BS_CHOIR) {
        const int vw = q[0] * 5 / 101;
        for (int j = 0; j < 3; ++j) bq_bandpass(&b->fm[j], sr, BS_VOWEL[vw][j], 6.0f + 4.0f * (float)j);
        for (int j = 0; j < 3; ++j) b->fmg[j] = BS_VOWEL[vw][3 + j] * 3.0f;
        b->tone_hz = 7000.0f;
    }
    if (t == PX_SYN_BS_PIPE) {                                 /* STOPS, SWELL, RELEASE, TONE */
        memcpy(b->w, BS_STOPS[q[0] * 4 / 101], sizeof b->w);
        b->tone_hz = 900.0f + 6000.0f * k3 * k3;
        b->chorus = 0.0f;
    }
    if (t == PX_SYN_BS_BRASS) b->att = onepole_k(sr, 0.01f + 0.2f * k1);   /* BITE: how fast the section speaks */
    if (t == PX_SYN_BS_TWELVE) {                               /* OCTAVE, DETUNE, TONE, DRY */
        b->w[2] = 0.3f + 1.2f * k0; b->tone_hz = 1500.0f + 6000.0f * k2 * k2;
        b->att = onepole_k(sr, 0.002f); b->rel = onepole_k(sr, 0.08f);
        b->chorus = 0.8f * k1;                                 /* DETUNE: the doubled courses' drift */
    }
    if (t == PX_SYN_BS_BELL) b->decay = 1.0f - 1.0f / ((0.2f + 3.0f * k1 * k1) * sr);   /* DECAY 0.2 .. 3.2 s */
    if (v->family == BS_LEAD || v->family == BS_SQUARE) {      /* CUTOFF, RESONANCE, ENVELOPE, SUB, LEVEL */
        b->cut = 120.0f + 5000.0f * k0 * k0;
        b->damp = 1.6f - 1.45f * k1;
        b->envamt = k2;
        b->sub = k3;
        b->att = onepole_k(sr, 0.0015f);
        b->rel = onepole_k(sr, v->family == BS_SQUARE ? 0.15f : 0.06f);
    }
    if (v->family == BS_SQUARE) bq_lowpass(&b->sqlp, sr, 700.0f, 0.7f);
    erb_prepare(e);
}

static void bs_reset(px_engine *e)
{
    memset(&e->erb_syn, 0, sizeof e->erb_syn);
    const float lvl = e->bs.lvl;
    memset(&e->bs, 0, sizeof e->bs);
    e->bs.lvl = lvl; e->bs.sel = -1; e->bs.since = 1 << 20;   /* (the first pick counts) */ e->bs.hz = 110.0f; e->bs.sqprev = -1.0f; e->bs.sqff = 1.0f;
}

/* over a block: the SYN input in, the synth out (no guitar unless the type mixes some in) */
static void bs_run(px_engine *e, int t, float *xb, int n, float strim)
{
    px_bsyn *b = &e->bs;
    const float sr = e->sr;
    for (int k = 0; k < n; ++k) {
        const float x = xb[k], a = x < 0.0f ? -x : x;
        /* the guitar's own envelopes: fast / slow (picks), the recent peak (the gate closes 22 dB under it) */
        b->fast += (a - b->fast) * 0.0224f; b->slow += (a - b->slow) * 0.0011f;
        b->peak = b->fast > b->peak ? b->fast : b->peak * 0.99995f;
        const int pick = b->fast > 1.8f * b->slow && b->fast > 2e-3f && b->since > 2646;
        if (pick) { b->since = 0; b->hit = 1.0f; }
        if (b->since < (1 << 30)) b->since++;
        const int open = b->fast > 1.5e-3f && b->fast > 0.08f * b->peak;
        float y = 0.0f;
        if (b->family <= BS_BELL) {
            float w = bs_chain(e, x);
            /* hold: the wet levelled by a slow follower (an organ / bowed tone does not decay as a string does) */
            const float wa = w < 0.0f ? -w : w;
            b->agc += (wa - b->agc) * (wa > b->agc ? 0.004f : 0.0003f);
            float g = 0.12f / (b->agc + 0.02f); if (g > 6.0f) g = 6.0f;
            if (b->family == BS_TWELVE) g = 1.0f;
            const float target = (open ? 1.0f : 0.0f);
            b->env += (target - b->env) * (target > b->env ? b->att : b->rel);
            float amp = b->env;
            if (b->family == BS_BRASS) {               /* the section speaks: brightness opens on the attack, then settles */
                b->hit *= 0.99993f;
                const float c = b->tone_hz * (0.5f + 0.5f * b->env) * (1.0f + 1.5f * b->hit);
                w = bs_svf(b, px_tanh(w * g * 1.5f), c, 0.9f) * 0.8f; g = 1.0f;
            } else if (b->family == BS_BELL) {         /* struck: each pick rings and decays */
                if (pick) b->bell = 1.0f;
                b->bell *= b->decay;
                amp = b->bell * (0.3f + 0.7f * b->env);
            }
            y = w * g * amp;
            if (b->formant) y = bq_run(&b->fm[0], y) * b->fmg[0] + bq_run(&b->fm[1], y) * b->fmg[1] + bq_run(&b->fm[2], y) * b->fmg[2];
            if (b->family == BS_TWELVE) y = y + x * (0.3f + 1.4f * b->k3);   /* DRY: the guitar's own six strings */
        } else if (b->family == BS_LEAD) {
            (void)bs_chain(e, x);
            /* the band's frequency, smoothed by ~1 ms; held while nothing sounds */
            if (b->sel >= 0) b->hz += (b->hz_raw - b->hz) * 0.02f;
            b->env += (a - b->env) * (a > b->env ? b->att : b->rel);
            const float gate = open ? 1.0f : 0.0f;
            b->gate += (gate - b->gate) * (gate > b->gate ? 0.05f : 0.002f);
            float hz = b->hz * (b->oct == -1 ? 0.5f : b->oct == -2 ? 0.25f : 1.0f);
            if (hz > 4000.0f) hz = 4000.0f;
            const float dt = hz / sr;
            b->ph += dt; if (b->ph >= 1.0f) { b->ph -= 1.0f; b->subff = b->subff > 0.0f ? -1.0f : 1.0f; }
            float osc;
            if (b->wave == 1) {                         /* pulse: two saws apart by the width, which moves slowly (PWM) */
                b->lfo += 0.6f / sr; if (b->lfo >= 1.0f) b->lfo -= 1.0f;
                const float pw = 0.5f + 0.35f * (4.0f * px_abs(b->lfo - 0.5f) - 1.0f);
                float p2 = b->ph + pw; if (p2 >= 1.0f) p2 -= 1.0f;
                osc = 0.8f * (bs_saw(b->ph, dt) - bs_saw(p2, dt));
            } else if (b->wave == 2) {                  /* two saws, one 7 cents up */
                b->ph2 += dt * 1.004f; if (b->ph2 >= 1.0f) b->ph2 -= 1.0f;
                osc = 0.6f * (bs_saw(b->ph, dt) + bs_saw(b->ph2, dt * 1.004f));
            } else osc = bs_saw(b->ph, dt);
            osc += b->sub * (b->subff >= 0.0f ? 0.7f : -0.7f);    /* SUB: a square an octave below */
            float lvl = b->env * 6.0f; lvl = lvl / (1.0f + lvl);   /* dynamics, softly levelled */
            b->hit *= 0.9998f;
            const float c = b->cut * (1.0f + 6.0f * b->envamt * (lvl + b->hit));
            y = bs_svf(b, osc, c > 12000.0f ? 12000.0f : c, b->damp) * lvl * b->gate * 0.5f;
        } else {                                         /* SQUARE family: the original Square's crossings */
            b->env += (a - b->env) * (a > b->env ? b->att : b->rel);
            const float s = bq_run(&b->sqlp, x), h = b->env * 0.1f;
            if (b->sqprev <= 0.0f && s > h) { b->sqprev = 1.0f; b->sqff = -b->sqff; }
            else if (b->sqprev > 0.0f && s < -h) b->sqprev = -1.0f;
            float osc = b->sqprev + b->sub * b->sqff;
            b->hit *= 0.9997f;
            float lvl = b->env * 6.0f; lvl = lvl / (1.0f + lvl);
            if (b->wave == 2) {                          /* flute: the square rounded by a leaky integrator, levelled, a breath */
                b->tri += (osc - b->tri) * 0.03f;
                b->tagc += (px_abs(b->tri) - b->tagc) * 0.001f;
                osc = b->tri / (b->tagc + 0.05f) * 0.5f;
                /* the breath: all of it on ENVELOPE (0 = a clean note), a soft puff on the pick and a little under the note,
                 * the noise darkened by a one-pole (~1.8 kHz) so it reads as air, not hiss */
                b->noise = b->noise * 1664525u + 1013904223u;
                b->nlp += (((float)(int32_t)b->noise * (1.0f / 2147483648.0f)) - b->nlp) * 0.25f;
                osc += b->nlp * 0.12f * b->envamt * (0.25f + 0.75f * b->hit * b->hit);
            }
            const float base = b->wave == 1 ? b->cut * 0.4f : b->cut;
            const float c = base * (1.0f + (b->wave == 1 ? 3.0f : 8.0f) * b->envamt * (b->wave == 0 ? b->hit + 0.3f * lvl : lvl));
            y = bs_svf(b, osc, c > 12000.0f ? 12000.0f : c, b->damp) * lvl * 0.5f;
            if (b->wave == 0) y += 0.15f * x;            /* a little guitar through, for the pick */
        }
        b->lp += (y - b->lp) * (b->family <= BS_BELL ? opk(sr, b->tone_hz) : 1.0f);
        y = b->lp;
        /* CHORUS (pads): three moving taps 7..20 ms */
        if (b->chorus > 0.0f && b->family <= BS_BELL) {
            b->cbuf[b->cw & (BS_CHORUS_N - 1)] = y;
            float acc = 0.0f;
            for (int j = 0; j < 3; ++j) {
                b->cph[j] += (0.23f + 0.17f * (float)j) / sr; if (b->cph[j] >= 1.0f) b->cph[j] -= 1.0f;
                const float d = (7.0f + 4.0f * (float)j + 4.0f * lfo_tri(b->cph[j])) * 0.001f * sr;
                const int di = (int)d; const float fr = d - (float)di;
                const float d0 = b->cbuf[(b->cw - di) & (BS_CHORUS_N - 1)], d1 = b->cbuf[(b->cw - di - 1) & (BS_CHORUS_N - 1)];
                acc += d0 + fr * (d1 - d0);
            }
            b->cw++;
            y = y * (1.0f - 0.5f * b->chorus) + acc * 0.33f * b->chorus;
        }
        xb[k] = y * b->lvl * strim;
    }
}

/* ---- POLY SHIFTER on the bank: every band moved by the same ratio, no frames, nothing tracked ----
 * Each band holds the guitar near one partial: z = |z| u. Its phase step from one bank sample to the next is that partial's
 * frequency now; the band's output phasor w turns by the step x RATIO, and the band gives |z| Re(w): the partial at RATIO x its
 * frequency, with its own envelope. A chord's every partial moves alike (J. Laroche's and E. Thuillier's filter-bank shifters do
 * this per band). Bands whose shifted frequency would pass the bank's limit fade out. ATTACK: on a pick the output phasors start
 * again from the input's (a sharper attack). */
static inline void ps_sincos(float a, float *s, float *c)   /* a in [-2 pi, 2 pi]: by the half angle, 7th order */
{
    while (a > 3.1415927f) a -= 6.2831853f;
    while (a < -3.1415927f) a += 6.2831853f;
    const float h = 0.5f * a, h2 = h * h;
    const float sh = h * (1.0f - h2 * (1.0f / 6.0f - h2 * (1.0f / 120.0f - h2 * (1.0f / 5040.0f))));
    const float ch = 1.0f - h2 * (0.5f - h2 * (1.0f / 24.0f - h2 * (1.0f / 720.0f)));
    *s = 2.0f * sh * ch; *c = ch * ch - sh * sh;
}
/* the wet level against the dry for SHIFT semitones (measured on guitar loops, tests/engine/pshift_test.c) */
static float ps_gain(float semis)
{
    static const float G[25] = {
        PS_GAIN_TABLE
    };
    float i = semis + 12.0f; if (i < 0.0f) i = 0.0f; if (i > 24.0f) i = 24.0f;
    const int k = (int)i; const float f = i - (float)k;
    return k >= 24 ? G[24] : G[k] + f * (G[k + 1] - G[k]);
}
static int ps_octave(float ratio) { return ratio == 2.0f ? 1 : ratio == 0.5f ? -1 : 0; }

static void ps_reset(px_engine *e)
{
    memset(&e->erb_pitch, 0, sizeof e->erb_pitch);
    px_pshift *p = &e->psh;
    memset(p->upr, 0, sizeof p->upr); memset(p->upi, 0, sizeof p->upi);
    for (int i = 0; i < ERB_O_BANDS; ++i) { p->wr[i] = 1.0f; p->wi[i] = 0.0f; }
    p->fast = p->slow = 0.0f; p->since = 1 << 20; p->r = p->rt;
    p->last = p->correction = 0.0f; p->octave = ps_octave(p->rt);
}
static PX_HOT float ps_bank(px_engine *e, float x, int pick, int octave)
{
    px_erb_octave *s = &e->erb_pitch;
    px_pshift *p = &e->psh;
    const float r = p->r;
    if (octave) {
        /* The octave kernels keep all bands phase-coherent and use the same
         * level and range as a full-wet POLY OCTAVE HQ voice. */
        return erb_bank(e, s, x, octave > 0 ? octave_up_level(1.0f, 1) : 0.0f,
                        octave < 0 ? octave_down_level(1.0f, 1) : 0.0f, 0.0f, ERB_FULL);
    }
    float y = 0.0f;
    for (int i = 0; i < ERB_O_BANDS; ++i) {
        const px_erb_band *f = &erb_oband[i];
        const float real = f->b0 * x + s->z1[i].r, imag = s->z1[i].i;
        s->z1[i].r = f->b1.r * x - (f->a1.r * real - f->a1.i * imag) + s->z2[i].r;
        s->z1[i].i = f->b1.i * x - (f->a1.r * imag + f->a1.i * real) + s->z2[i].i;
        s->z2[i].r = f->b2.r * x - (f->a2.r * real - f->a2.i * imag);
        s->z2[i].i = f->b2.i * x - (f->a2.r * imag + f->a2.i * real);
        const float power = real * real + imag * imag + 1e-24f, inv = erb_inverse_root(power);
        const float ur = real * inv, ui = imag * inv;
        const float top = erb_ofc[i] * r;              /* the shifted band's place: fade out from 4.2 to 5 kHz */
        const float wgt = top < 4200.0f ? 1.0f : (top > 5000.0f ? 0.0f : (5000.0f - top) * (1.0f / 800.0f));
        if (wgt > 0.0f) {
            if (pick || (p->upr[i] == 0.0f && p->upi[i] == 0.0f)) {
                p->wr[i] = 1.0f; p->wi[i] = 0.0f;
            }
            else {
                const float cr = ur * p->upr[i] + ui * p->upi[i], ci = ui * p->upr[i] - ur * p->upi[i];   /* u conj(u_prev) */
                float sn, cs; ps_sincos((r - 1.0f) * bs_atan2(ci, cr), &sn, &cs);
                const float nr = p->wr[i] * cs - p->wi[i] * sn, ni = p->wr[i] * sn + p->wi[i] * cs;
                const float k = 1.5f - 0.5f * (nr * nr + ni * ni);    /* (keep |w| = 1) */
                p->wr[i] = nr * k; p->wi[i] = ni * k;
            }
            y += wgt * (real * p->wr[i] - imag * p->wi[i]);
        }
        p->upr[i] = ur; p->upi[i] = ui;
    }
    return y;
}
/* one 44.1 kHz sample in, the shifted voice out (erb_octaves2's rate chain) */
static PX_HOT float ps_run(px_engine *e, float input)
{
    px_erb_octave *s = &e->erb_pitch;
    px_pshift *p = &e->psh;
    const float a = input < 0.0f ? -input : input;
    p->fast += (a - p->fast) * 0.0224f; p->slow += (a - p->slow) * 0.0011f;
    if (p->fast > (3.0f - 1.6f * p->attack) * p->slow && p->fast > 2e-3f && p->since > 2646 && p->attack > 0.0f) p->since = 0;   /* a pick: the bank's next sample restarts the phasors */
    if (p->since < (1 << 20)) p->since++;
    const float error = p->rt - p->r;
    p->r = fabsf(error) < 1e-5f ? p->rt : p->r + error * 0.002f;                   /* (a turned SHIFT glides over ~10 ms) */
    const unsigned pos = s->pos;
    s->input[pos] = input;
    if ((pos & 1) == 0) {
        const unsigned p2 = s->pos2;
        s->in2[p2] = erb_halfband(s->input, pos, erb_hb);
        if ((p2 & 1) == 0) {
            const int octave = ps_octave(p->r);
            const int changed = octave != p->octave;
            const int onset = p->since >= 1 && p->since <= 4;
            const float raw = ps_bank(e, erb_halfband(s->in2, p2, erb_hb), onset || changed, octave) *
                              (octave ? p->octave_gain : p->gain);
            /* De-click the handoff between phase integration and the octave kernels. */
            if (changed) p->correction = p->last - raw;
            s->up2[p2] = p->last = raw + p->correction;
            p->correction *= 0.975f;
            p->octave = octave;
        }
        s->up[pos] = (p2 & 1) ? s->up2[(p2 - 15) & 31] : 2.0f * erb_halfband(s->up2, p2, erb_hb);
        s->pos2 = (p2 + 1) & 31;
    }
    s->pos = (pos + 1) & 31;
    if (pos & 1) return s->up[(pos - 15) & 31];
    return 2.0f * erb_halfband(s->up, pos, erb_hb);
}
