/* Included only in the generated host adapter, after px_s2 and CTL. */
#include "declick.h"

static declick_state dc_state[PX_S2_NV];
static float dc_neighbor[PX_S2_NV];
static int32_t dc_voice[CTL];
static float dc_tail[CTL];
static float dc_coefficient;
static int dc_repick, dc_steal, dc_applied, dc_replaced;

static void dc_clear(void)
{
    memset(dc_state, 0, sizeof dc_state);
    memset(dc_neighbor, 0, sizeof dc_neighbor);
}

static void dc_mix(px_s2 *s, int vi)
{
    const float gain = s->level * PX_S2_OUT / 32768.0f;
    declick_state *state = &dc_state[vi];
    float previous = state->last;
    float max_step = 0.0f;
    /* A repick needs a step clearly above nearby waveform motion. Candidate
       NOTE_ON/steal replacements exercise the original H1 rule directly. */
    if (state->pending && dc_repick &&
        fabsf(previous - (float)dc_voice[0] * gain) <= 3 * dc_neighbor[vi] + 0.001f)
        state->pending = false;
    const bool replacing = state->pending;
    for (int i = 0; i < CTL; ++i) {
        s->out[i] += dc_voice[i];
        const float sample = (float)dc_voice[i] * gain;
        const float correction = declick_step(state, dc_coefficient, sample, 1);
        if (i == 0 && correction != 0) {
            dc_applied |= 1 << vi;
            if (replacing)
                dc_replaced |= 1 << vi;
        }
        dc_tail[i] += correction;
        if (i > 0)
            max_step = fmaxf(max_step, fabsf(state->last - previous));
        previous = state->last;
    }
    dc_neighbor[vi] = max_step;
}
