#include "declick.h"
#include <math.h>

float declick_coefficient(float sample_rate, float tau_ms)
{
    if (!(isfinite(sample_rate) && isfinite(tau_ms) && sample_rate > 0 && tau_ms > 0))
        return 0;
    return expf(-1000.0f / sample_rate / tau_ms);
}

void declick_reset(declick_state *state)
{
    *state = (declick_state){0};
}

void declick_replace(declick_state *state)
{
    state->pending = state->valid;
}

float declick_step(declick_state *state, float coefficient, float sample, float limit)
{
    if (state->pending) {
        state->correction = coefficient > 0 ? state->last - sample : 0;
        state->pending = false;
    }
    float correction = state->correction;
    if (correction != 0) {
        /* Allow existing overrange samples, but bound any newly added excursion. */
        const float bound = fmaxf(limit, fabsf(sample));
        correction = fmaxf(-bound - sample, fminf(bound - sample, correction));
        state->correction = correction * coefficient;
        if (fabsf(state->correction) < 1e-7f)
            state->correction = 0;
    }
    state->last = sample + correction;
    state->valid = true;
    return correction;
}
