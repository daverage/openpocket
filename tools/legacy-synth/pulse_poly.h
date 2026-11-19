/* Host-only two-note oscillator prototype. Candidates arrive in frequency order, not salience order. */
typedef struct {
    float pitch, pending, salience, gain;
    int stable, missing, active, retrigger;
    px_synth_tracking osc;
} pulse_poly_voice;

static void pulse_poly_frame(pulse_poly_voice voices[2], const px_track *track)
{
    unsigned used = 0;
    float strongest = 0;
    for (int k = 0; k < track->nnotes; ++k)
        if (track->sal[k] > strongest) strongest = track->sal[k];

    /* Keep existing voices first. A stronger second note should not steal an established note. */
    for (int i = 0; i < 2; ++i) {
        pulse_poly_voice *v = &voices[i];
        if (!v->active) continue;
        int match = -1;
        float distance = 0.045f;
        for (int k = 0; k < track->nnotes; ++k) {
            const float d = px_abs(track->notes[k] / v->pitch - 1.0f);
            if (!(used & (1u << k)) && d < distance) { match = k; distance = d; }
        }
        if (match < 0) {
            if (++v->missing >= 8) { v->active = 0; v->stable = 0; }
            continue;
        }
        used |= 1u << match;
        v->missing = 0;
        v->pitch += 0.4f * (track->notes[match] - v->pitch);
        /* Global flux only supplies timing. This voice must show its own rise in harmonic salience. */
        if (track->onset > 0 && track->sal[match] > 2.0f * v->salience) v->retrigger = 1;
        v->salience += 0.15f * (track->sal[match] - v->salience);
    }
    for (int i = 0; i < 2; ++i) {
        pulse_poly_voice *v = &voices[i];
        if (v->active) continue;
        int best = -1;
        float score = 0.38f * strongest;
        for (int k = 0; k < track->nnotes; ++k) {
            if ((used & (1u << k)) || track->sal[k] <= score) continue;
            int harmonic = 0;
            for (int j = 0; j < 2; ++j) if (voices[j].active) {
                float ratio = track->notes[k] / voices[j].pitch;
                if (ratio < 1.0f) ratio = 1.0f / ratio;
                if (px_abs(ratio - 2.0f) < 0.045f || px_abs(ratio - 3.0f) < 0.065f)
                    harmonic = track->sal[k] < 0.72f * strongest;
            }
            if (!harmonic) { best = k; score = track->sal[k]; }
        }
        if (best < 0 || track->level < 1e-7f) { v->stable = 0; continue; }
        used |= 1u << best;
        if (pxt_near(track->notes[best], v->pending, 45.0f)) ++v->stable;
        else { v->pending = track->notes[best]; v->stable = 1; }
        if (v->stable >= 5 && v->gain < 0.1f) {
            v->pitch = track->notes[best];
            v->salience = score;
            v->missing = 0;
            v->active = 1;
        }
    }
}

static float pulse_poly_sample(px_engine *engine, pulse_poly_voice voices[2])
{
    float sum = 0;
    for (int i = 0; i < 2; ++i) {
        pulse_poly_voice *v = &voices[i];
        if (v->retrigger) { v->gain *= 0.25f; v->retrigger = 0; }
        v->gain += ((v->active ? 1.0f : 0.0f) - v->gain) * (v->active ? 0.012f : 0.003f);
        v->osc.step = 0.25f * v->pitch / engine->sr;
        v->osc.phase = wrap1(v->osc.phase + v->osc.step);
        sum += v->gain * synth_hq_pulse(&v->osc, 4, 0.4f);
    }
    return 0.28f * syn_ladder(engine, 0.45f * sum, 2400.0f);
}
