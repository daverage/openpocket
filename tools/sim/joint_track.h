/* Host experiment: compare a one-note fit with a bounded two-note fit.
 * Uses OpenPocket's existing partials and the harmonic weighting in pxt_salience.
 * A partial can support either note, but its contribution is counted only once. */
#define JOINT_SHORTLIST 12
#ifndef JOINT_PENALTY
#define JOINT_PENALTY 0.23f
#endif

typedef struct {
    float pitch, score;
    float contribution[PXT_PART];
} joint_candidate;

static joint_candidate joint_measure(const px_track *track, float pitch)
{
    joint_candidate candidate = {.pitch = pitch};
    const float inverse = 1.0f / pitch;
    for (int i = 0; i < track->npart; ++i) {
        const pxt_partial p = track->part[i];
        const float harmonic = p.f * inverse;
        const int order = (int)(harmonic + 0.5f);
        if (order < 1 || order > 12) continue;
        const float error = px_abs(harmonic - (float)order);
        const float tolerance = 0.033f * (float)order;
        if (error >= tolerance) continue;
        const float fit = 1.0f - error / tolerance;
        const float support = p.a * (pitch + 27.0f) / (order * pitch + 320.0f);
        candidate.contribution[i] = support * (0.35f + 0.65f * fit);
        candidate.score += candidate.contribution[i];
    }
    return candidate;
}

static void joint_offer(const px_track *track, joint_candidate *candidates, int *count, float pitch)
{
    const joint_candidate candidate = joint_measure(track, pitch);
    if (candidate.score <= 0) return;
    int slot = -1, weakest = 0;
    for (int j = 0; j < *count; ++j) {
        if (px_abs(pitch / candidates[j].pitch - 1.0f) < 0.022f) {
            if (candidate.score > candidates[j].score) candidates[j] = candidate;
            return;
        }
        if (candidates[j].score < candidates[weakest].score) weakest = j;
    }
    if (*count < JOINT_SHORTLIST) slot = (*count)++;
    else if (candidate.score > candidates[weakest].score) slot = weakest;
    if (slot >= 0) candidates[slot] = candidate;
}

static void joint_candidates(px_track *track)
{
    joint_candidate candidates[JOINT_SHORTLIST];
    int count = 0;
    for (int i = 0; i < track->npart; ++i) {
        for (int h = 1; h <= 5; ++h) {
            const float pitch = track->part[i].f / h;
            if (pitch >= PXT_FMIN && pitch <= PXT_FMAX)
                joint_offer(track, candidates, &count, pitch);
        }
    }
    track->nnotes = 0;
    if (!count) return;

    int first = 0, second = -1;
    for (int i = 1; i < count; ++i)
        if (candidates[i].score > candidates[first].score) first = i;
    const float single_score = candidates[first].score;
    float best = single_score * (1.0f + JOINT_PENALTY);
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            const joint_candidate *a = &candidates[i], *b = &candidates[j];
            float combined = 0, distinct_a = 0, distinct_b = 0;
            for (int k = 0; k < track->npart; ++k) {
                const float x = a->contribution[k], y = b->contribution[k];
                combined += x > y ? x : y;
                if (y == 0) distinct_a += x;
                if (x == 0) distinct_b += y;
            }
            const float required_evidence = 0.10f * single_score;
            if (combined > best && distinct_a > required_evidence && distinct_b > required_evidence) {
                best = combined;
                first = i;
                second = j;
            }
        }
    }
    if (second >= 0 && candidates[second].pitch < candidates[first].pitch) {
        const int swap = first;
        first = second;
        second = swap;
    }
    track->notes[0] = candidates[first].pitch;
    track->sal[0] = candidates[first].score;
    track->nnotes = 1;
    if (second >= 0) {
        track->notes[1] = candidates[second].pitch;
        track->sal[1] = candidates[second].score;
        track->nnotes = 2;
    }
}
