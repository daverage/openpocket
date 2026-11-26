/* Noise Suppressor control-path regressions. Run with both PX_RUNS modes. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "px_dsp.c"
static px_engine e, other;
static int bad;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s\n", msg); ++bad; } } while (0)
static void patch(px_preset_t *p) {
    px_preset_base(p); memset(p->on, 0, sizeof p->on);
    p->on[PX_B_GATE] = 1; p->type[PX_B_GATE] = PX_GATE_SUPPRESSOR;
    p->p[PX_B_GATE][0] = 40; p->p[PX_B_GATE][1] = 20;
    p->p[PX_B_GATE][2] = p->p[PX_B_GATE][3] = 0;
}
int main(void) {
    px_preset_t p, q; patch(&p);
    CHECK(px_nparams(PX_B_GATE, PX_GATE_NORMAL) == 2, "plain gate has two controls");
    CHECK(px_nparams(PX_B_GATE, PX_GATE_SUPPRESSOR) == 4, "suppressor has four controls");
    float x[64], y[64], z[64];
    /* THRESH 0 is transparent, including when HUSH/HUM are independently off. */
    p.p[0][0] = 0; q = p; q.on[0] = 0;
    px_engine_init(&e, 44100); px_engine_set(&e, &p);
    px_engine_init(&other, 44100); px_engine_set(&other, &q);
    for (int b = 0; b < 1000; ++b) {
        for (int k = 0; k < 64; ++k) x[k] = 0.02f * sinf((b*64+k)*0.017f);
        px_engine_process(&e, x, y, 64); px_engine_process(&other, x, z, 64);
        for (int k = 0; k < 64; ++k) CHECK(y[k] == z[k], "threshold zero bypass is exact");
    }
    /* Valve, boost and order must not affect clean detector state or envelopes. */
    patch(&p); q = p; q.valve_on = 1; q.valve_drive = 100;
    q.on[PX_B_BST] = 1; q.type[PX_B_BST] = PX_BST_EPBOOST; q.p[PX_B_BST][0] = 100;
    q.order[0] = PX_B_BST; q.order[1] = PX_B_GATE;
    px_engine_init(&e, 44100); px_engine_set(&e, &p);
    px_engine_init(&other, 44100); px_engine_set(&other, &q);
    for (int b = 0; b < 2000; ++b) {
        for (int k = 0; k < 64; ++k) {
            float t = (b*64+k)/44100.0f;
            x[k] = 0.08f * expf(-t*2) * sinf(t*6.2831853f*61.735f);
        }
        px_engine_process(&e, x, y, 64); px_engine_process(&other, x, z, 64);
        CHECK(e.suppressor.gain == other.suppressor.gain && e.suppressor.peak == other.suppressor.peak, "clean sidechain independent of Valve and boost");
    }
    /* Direct control measurements avoid mistaking audio harmonics for noise. */
    patch(&p); px_engine_init(&e, 44100); px_engine_set(&e, &p);
    float gain = 1, hf = 1, last = 1, maxstep = 0, minnote = 1;
    int transitions = 0, was = 0;
    for (int k = 0; k < 44100*8; ++k) {
        float t = k/44100.0f;
        float v = t < 4 ? 0.03f*expf(-t*0.7f)*sinf(t*6.2831853f*41.203f) : 0;
        suppressor_detect(&e, v, &gain, &hf);
        if (t > 0.1f && t < 3.9f && gain < minnote) minnote = gain;
        if (was != e.suppressor.open) ++transitions;
        was = e.suppressor.open;
        if (fabsf(gain-last) > maxstep) maxstep = fabsf(gain-last);
        CHECK(isfinite(gain) && gain >= 0.000099f && gain <= 1, "finite bounded gain"); last = gain;
    }
    CHECK(minnote > 0.999f, "low E1 decaying sustain retained above threshold");
    CHECK(transitions <= 2, "no gate chatter during low-note decay");
    CHECK(gain < 0.0002f, "idle reaches approximately 80 dB attenuation");
    CHECK(maxstep < 0.001f, "closing envelope has no jumps");
    for (int k = 0; k < 128; ++k) suppressor_detect(&e, 0.03f, &gain, &hf);
    CHECK(gain > 0.999f, "new attack reopens within 3 ms from current gain");
    /* Sustained notes never become the learned noise floor. */
    CHECK(e.suppressor.floor < e.suppressor.sensitivity * 0.5f, "learning remains capped below threshold");
    /* Gentle swell opens without requiring a transient classifier. */
    px_engine_init(&e, 44100); px_engine_set(&e, &p);
    for (int k = 0; k < 44100*2; ++k) {
        float t = k/44100.0f;
        suppressor_detect(&e, 0.005f*(t*0.5f)*sinf(t*6.2831853f*65.406f), &gain, &hf);
    }
    CHECK(gain > 0.999f, "slow quiet volume swell opens");
    /* Switching the block off disables its input HUM even when knobs stay put. */
    p.p[PX_B_GATE][2] = 26; px_engine_set(&e, &p);
    q = p; q.on[PX_B_GATE] = 0; px_engine_knobs(&e, &q);
    CHECK(e.hum_mode == 0, "block toggle disables input HUM without a knob change");
    q.on[PX_B_GATE] = 1; px_engine_knobs(&e, &q);
    CHECK(e.hum_mode == 1 && e.suppressor.active, "block toggle restores suppression and HUM");
    p.p[PX_B_GATE][2] = 0; px_engine_set(&e, &p);
    q = p; q.patch_volume = 73;
    CHECK(px_engine_change(&e, &q) == PX_CH_KNOBS, "patch volume is a live independent setting");
    printf("suppressor: min sustain %.6f, closing step %.8f, transitions %d, state %zu bytes; %d problems\n", minnote, maxstep, transitions, sizeof(e.suppressor), bad);
    return bad != 0;
}
