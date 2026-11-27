/* SIGNATURE through the engine: tone, harmonics, transient lift and clean gain. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"

enum { RATE = 44100, BLOCK = 64, SAMPLES = 512 * BLOCK };
static float input[SAMPLES], output[SAMPLES];
static px_engine engine;
static int failures;
#define CHECK(result, message) do { if (!(result)) { puts("FAIL " message); ++failures; } } while (0)

static void render(const px_preset_t *preset, float hz, int onset)
{
    px_engine_init(&engine, RATE);
    px_engine_set(&engine, preset);
    for (int i = 0; i < SAMPLES; i++)
        input[i] = i < onset ? 0.0f : 0.12f * sinf(6.2831853f * hz * (i - onset) / RATE);
    for (int i = 0; i < SAMPLES; i += BLOCK)
        px_engine_process(&engine, input + i, output + i, BLOCK);
}

static float magnitude(float hz, int start, int count)
{
    double real = 0.0, imag = 0.0;
    for (int i = start; i < start + count; i++) {
        const double phase = 6.283185307179586 * hz * i / RATE;
        real += output[i] * cos(phase);
        imag += output[i] * sin(phase);
    }
    return (float)(2.0 * sqrt(real * real + imag * imag) / count);
}

static float sustained(const px_preset_t *preset, float hz)
{
    render(preset, hz, 0);
    return magnitude(hz, SAMPLES - RATE / 5, RATE / 5);
}

int main(void)
{
    px_preset_t preset;
    px_preset_base(&preset);
    memset(preset.on, 0, sizeof preset.on);
    preset.on[PX_B_BST] = 1;
    preset.type[PX_B_BST] = PX_BST_SIGNATURE;
    uint8_t *q = preset.p[PX_B_BST];
    px_type_defaults(PX_B_BST, PX_BST_SIGNATURE, q);
    const uint8_t defaults[] = {60, 55, 25, 30, 50};
    CHECK(memcmp(q, defaults, sizeof defaults) == 0, "voicing defaults");

    q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 0;
    const float lean = sustained(&preset, 350.0f);
    q[0] = 100;
    CHECK(sustained(&preset, 350.0f) > lean * 2.4f, "BODY raises the low mids");
    q[0] = 50;
    const float low = sustained(&preset, 100.0f);
    const float high = sustained(&preset, 4800.0f);
    q[1] = 100;
    CHECK(sustained(&preset, 100.0f) < low * 0.65f, "FOCUS tightens the bass");
    CHECK(sustained(&preset, 4800.0f) > high * 1.2f, "FOCUS adds definition");

    q[1] = 0;
    const float clean = sustained(&preset, 440.0f);
    const float clean_second = magnitude(880.0f, SAMPLES - RATE / 5, RATE / 5);
    q[2] = 100;
    const float colored = sustained(&preset, 440.0f);
    const float second = magnitude(880.0f, SAMPLES - RATE / 5, RATE / 5);
    CHECK(second > clean_second * 10.0f && second / colored > 0.02f &&
          second / colored < 0.06f && fabsf(colored / clean - 1.0f) < 0.01f,
          "RICHNESS adds a restrained second harmonic at unity small-signal gain");
    q[4] = 100;
    const float boosted = sustained(&preset, 440.0f);
    const float boosted_second = magnitude(880.0f, SAMPLES - RATE / 5, RATE / 5);
    CHECK(fabsf(boosted / colored - 3.9811f) < 0.04f &&
          fabsf(boosted_second / second - boosted / colored) < 0.04f,
          "LEVEL adds 12 dB independently of harmonic balance");

    q[2] = 0; q[4] = 50;
    const int onset = 4096, attack_window = RATE / 100;
    render(&preset, 900.0f, onset);
    const float attack = magnitude(900.0f, onset, attack_window);
    const float tail = magnitude(900.0f, SAMPLES - RATE / 5, RATE / 5);
    q[3] = 100;
    render(&preset, 900.0f, onset);
    CHECK(magnitude(900.0f, onset, attack_window) > attack * 1.15f, "PUNCH lifts the attack");
    CHECK(fabsf(magnitude(900.0f, SAMPLES - RATE / 5, RATE / 5) / tail - 1.0f) < 0.001f,
          "PUNCH returns to unity on sustain");

    if (!failures) puts("signature_test: ok");
    return failures != 0;
}
