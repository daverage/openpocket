/* Throwaway host experiment: ERB-spaced complex filters and octave phase scaling.
 * Filter design cross-checked against Thuillier's thesis and Steven Schulteis's
 * Terrarium Poly Octave. See docs/research/POLYPHONIC-PITCH-RESEARCH.md for references. */
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { BAND_COUNT = 43, SAMPLE_RATE = 44100, HALF_BAND_TAPS = 31 };
static const double pi = 3.14159265358979323846;

typedef struct {
    float gain;
    float complex b1, b2, a1, a2;
    float complex state1, state2;
    float previous_imag;
    int half_phase_inverted;
} band;

static double center_frequency(int index)
{
    /* Equal steps on the ERB-rate scale between 65 and 4,000 Hz. */
    const double start = 1.0 + 0.00437 * 65.0;
    const double end = 1.0 + 0.00437 * 4000.0;
    return (start * pow(end / start, index / (double)(BAND_COUNT - 1)) - 1.0) / 0.00437;
}

static band make_band(int index, double width, double rate)
{
    const double center = center_frequency(index);
    const double spacing = 0.5 * (center_frequency(index + 1) - center_frequency(index - 1));
    const double omega = pi * width * spacing / rate;
    const double alpha = sin(omega) / sqrt(2.0);
    const double inverse_a0 = 1.0 / (1.0 + alpha);
    const double gain = 0.5 * (1.0 - cos(omega)) * inverse_a0;
    const double angle = 2.0 * pi * center / rate;
    const double complex shift = cos(angle) + I * sin(angle);
    const double complex shift2 = shift * shift;
    return (band) {
        .gain = gain,
        .b1 = 2.0 * gain * shift,
        .b2 = gain * shift2,
        .a1 = -2.0 * cos(omega) * inverse_a0 * shift,
        .a2 = (1.0 - alpha) * inverse_a0 * shift2,
    };
}

static float octave_sample(band *filter, float input, int down)
{
    const float complex analytic = filter->gain * input + filter->state1;
    filter->state1 = filter->b1 * input - filter->a1 * analytic + filter->state2;
    filter->state2 = filter->b2 * input - filter->a2 * analytic;
    const float real = crealf(analytic), imag = cimagf(analytic);
    const float real2 = real * real, imag2 = imag * imag;
    const float magnitude = sqrtf(real2 + imag2 + 1e-24f);
    if (down) {
        /* Unwrap the square root by changing its sign at the negative real axis. */
        if (real < 0.0f && ((imag < 0.0f) != (filter->previous_imag < 0.0f)))
            filter->half_phase_inverted ^= 1;
        filter->previous_imag = imag;
        const float half_cosine = sqrtf(fmaxf(0.0f, 0.5f + 0.5f * real / magnitude));
        const float output = magnitude * half_cosine;
        return filter->half_phase_inverted ? -output : output;
    }
    /* z² / |z| doubles phase while retaining magnitude. */
    return (real2 - imag2) / magnitude;
}

typedef struct {
    float samples[32];
    unsigned pos;
} history;

static void push(history *h, float sample)
{
    h->pos = (h->pos + 1) & 31;
    h->samples[h->pos] = sample;
}

static float half_band(const history *h, const float *coeff)
{
    const unsigned center = HALF_BAND_TAPS / 2;
    float output = 0.5f * h->samples[(h->pos - center) & 31];
    for (unsigned offset = 1; offset <= center; offset += 2) {
        const float left = h->samples[(h->pos - center + offset) & 31];
        const float right = h->samples[(h->pos - center - offset) & 31];
        output += coeff[offset / 2] * (left + right);
    }
    return output;
}

static float octave(band *filters, float input, int down)
{
    float output = 0.0f;
    for (int b = 0; b < BAND_COUNT; b++) output += octave_sample(&filters[b], input, down);
    return output;
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        fputs("Usage: erb-octave-prototype bandwidth-multiplier rate-divisor [down] < input.f32 > output.f32\n", stderr);
        return 1;
    }
    const double width = strtod(argv[1], NULL);
    const int divisor = atoi(argv[2]);
    const int down = argc == 4 && argv[3][0] == 'd';
    if (!(width >= 0.5 && width <= 4.0) || (divisor != 1 && divisor != 2)) return 1;

    band filters[BAND_COUNT];
    for (int i = 0; i < BAND_COUNT; i++) filters[i] = make_band(i, width, SAMPLE_RATE / divisor);

    float coeff[HALF_BAND_TAPS / 4 + 1];
    for (unsigned offset = 1; offset <= HALF_BAND_TAPS / 2; offset += 2) {
        const double window = 0.54 + 0.46 * cos(2.0 * pi * offset / (HALF_BAND_TAPS - 1));
        coeff[offset / 2] = window * sin(pi * offset / 2) / (pi * offset);
    }
    history input_history = {0}, output_history = {0};
    unsigned phase = 0;

    float samples[128];
    size_t count;
    while ((count = fread(samples, sizeof samples[0], 128, stdin)) > 0) {
        for (size_t i = 0; i < count; i++) {
            if (divisor == 1) {
                samples[i] = octave(filters, samples[i], down);
            } else {
                push(&input_history, samples[i]);
                float shifted = 0.0f;
                if (phase == 0) shifted = octave(filters, half_band(&input_history, coeff), down);
                push(&output_history, shifted);
                samples[i] = 2.0f * half_band(&output_history, coeff);
                phase ^= 1;
            }
        }
        if (fwrite(samples, sizeof samples[0], count, stdout) != count) return 1;
    }
    return ferror(stdin) ? 1 : 0;
}
