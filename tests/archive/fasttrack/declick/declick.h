#ifndef FT_DECLICK_H
#define FT_DECLICK_H

#include <stdbool.h>

typedef struct {
    float last;
    float correction;
    bool pending;
    bool valid;
} declick_state;

/* Compute once, away from rendering. Zero disables correction. */
float declick_coefficient(float sample_rate, float tau_ms);
void declick_reset(declick_state *state);
void declick_replace(declick_state *state);

/* Return the additive contribution in the same units as sample.
 * limit is a per-voice representable range, and never clips the raw voice. */
float declick_step(declick_state *state, float coefficient, float sample, float limit);

#endif
