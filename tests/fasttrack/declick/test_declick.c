#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "declick.h"

int main(void)
{
    float taus[] = {0.5f, 1, 1.5f, 2, 3};
    for (unsigned i = 0; i < sizeof taus / sizeof *taus; ++i) {
        declick_state state = {0};
        float k = declick_coefficient(44100, taus[i]);
        float expected = powf(k, 44100 * taus[i] / 1000);
        assert(fabsf(expected - expf(-1)) < 0.00002f);
        assert(declick_step(&state, k, 0.8f, 1) == 0);
        declick_replace(&state);
        float correction = declick_step(&state, k, -0.4f, 1);
        assert(fabsf(-0.4f + correction - 0.8f) < 1e-6f);
        assert(fabsf(declick_step(&state, k, -0.4f, 1) - correction * k) < 1e-6f);
        float last = state.last;
        declick_replace(&state);
        assert(fabsf(0.2f + declick_step(&state, k, 0.2f, 1) - last) < 1e-6f);
        for (unsigned j = 0; j < 4096; ++j)
            declick_step(&state, k, 0, 1);
        assert(state.correction == 0);
        declick_reset(&state);
        declick_replace(&state);
        assert(declick_step(&state, k, 0.3f, 1) == 0);
    }
    declick_state a = {0}, b = {0};
    float k = declick_coefficient(48000, 1.5f);
    declick_step(&a, k, 3, 1);
    declick_replace(&a);
    assert(declick_step(&a, k, -0.5f, 1) == 1.5f);
    assert(declick_step(&b, k, 0.25f, 1) == 0);
    declick_reset(&a);
    assert(declick_step(&a, k, 0, 1) == 0);
    assert(declick_coefficient(0, 1) == 0 && declick_coefficient(48000, 0) == 0);
    printf("synthetic jumps, all 5 time constants, replacement during tail, isolation, reset and overrange passed (%zu bytes/voice)\n", sizeof a);
}
