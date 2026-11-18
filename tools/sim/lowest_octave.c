/* Host-only Lowest Note shifter benchmark. Read and write 44.1 kHz mono LE float32.
 * Modes: full, range HZ, lowest, oracle LOW_HZ [UPPER_HZ], focused, focused-oracle LOW_HZ.
 * Synthesized variants: sub, sub-fast, sub-harmonic, sub-refined, sub-exclusive,
 * sub-low, sub-low-smooth, sub-low-hold, sub-fit, sub-fit-hybrid, sub-oracle LOW_HZ.
 * The oracle bypasses note selection to expose leakage from the mask itself.
 */
#define PX_RESONATOR_TEST 1
#define PX_LOWEST_HOST 1
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lowest_verify.h"
#include "../../engine/px_lowest_sub.h"

static px_engine engine;
static px_track tracker;
static px_erb_octave octave;

static int candidate_trace(FILE *file, double time, float proposed, float selected,
                           const float *notes, int count)
{
    if (fprintf(file, "%.8f,%.5f,%.5f", time, proposed, selected) < 0) return 0;
    for (int i = 0; i < PXT_NOTES; ++i)
        if (fprintf(file, ",%.5f", i < count ? notes[i] : 0.0f) < 0) return 0;
    return fputc('\n', file) != EOF;
}

static float band_limit(float hz)
{
    for (int i = 0; i < PX_ERB_BANDS - 1; ++i) {
        if (hz <= erb_fc[i + 1])
            return (float)i + (hz - erb_fc[i]) / (erb_fc[i + 1] - erb_fc[i]);
    }
    return ERB_FULL;
}

static float overlap(float center, float width, float hz)
{
    const float d = px_abs(center - hz) / width;
    return d < 1.0f ? 1.0f - d : 0.0f;
}

static void build_mask(float mask[PX_ERB_BANDS], float low, const float *notes, int count)
{
    const float weights[] = {1.0f, 0.42f, 0.20f};
    for (int i = 0; i < PX_ERB_BANDS; ++i) {
        float bass = 0.0f, other = 0.0f;
        if (low > 0.0f) {
            const float width = erb_bw[i] * 0.9f;
            for (int h = 1; h <= 3; ++h) {
                const float value = weights[h - 1] * overlap(erb_fc[i], width, low * (float)h);
                if (value > bass) bass = value;
                for (int n = 0; n < count; ++n) {
                    if (notes[n] < low * 1.03f) continue;
                    const float leak = overlap(erb_fc[i], width, notes[n] * (float)h);
                    if (leak > other) other = leak;
                }
            }
        }
        mask[i] = bass * (1.0f - 0.85f * other);
    }
}

/* A deliberately narrow analysis band tests whether the broad shared bank limits separation.
 * Changing the selected note restarts the shifter. A production version would need a handoff
 * between two states rather than this hard reset. */
static void focus(float hz, float target[PX_ERB_BANDS])
{
    memset(erb_band, 0, sizeof erb_band);
    memset(target, 0, sizeof(float) * PX_ERB_BANDS);
    memset(&octave, 0, sizeof octave);
    if (hz <= 0.0f) return;
    const float width = fminf(20.0f, fmaxf(6.0f, 0.13f * hz));
    const float omega = PI_F * width / erb_rate;
    const float a0 = 1.0f / (1.0f + 0.70710678f * sinf(omega));
    const float half = sinf(0.5f * omega);
    const float b0 = a0 * half * half;
    const float a1 = -2.0f * cosf(omega) * a0, a2 = 2.0f * a0 - 1.0f;
    const float phase = 2.0f * PI_F * hz / erb_rate;
    const float c = cosf(phase), s = sinf(phase), c2 = c * c - s * s, s2 = 2.0f * c * s;
    erb_band[0] = (px_erb_band){b0, {2.0f * b0 * c, 2.0f * b0 * s}, {b0 * c2, b0 * s2},
        {a1 * c, a1 * s}, {a2 * c2, a2 * s2}};
    target[0] = 1.0f;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    const int sub_fit_hybrid = !strcmp(argv[1], "sub-fit-hybrid");
    const int sub_fit_only = !strcmp(argv[1], "sub-fit");
    const int sub_fit = sub_fit_only || sub_fit_hybrid;
    const int sub_low_hold = !strcmp(argv[1], "sub-low-hold");
    const int sub_low_smooth = sub_low_hold || !strcmp(argv[1], "sub-low-smooth");
    const int sub_low = sub_low_smooth || !strcmp(argv[1], "sub-low");
    const int sub_exclusive = !strcmp(argv[1], "sub-exclusive");
    const int sub_refined = sub_exclusive || !strcmp(argv[1], "sub-refined");
    const int sub_verify = sub_low || sub_refined || !strcmp(argv[1], "sub-harmonic");
    const int sub_fast = !strcmp(argv[1], "sub-fast");
    const int sub = sub_fit || sub_verify || sub_fast || !strcmp(argv[1], "sub") || !strcmp(argv[1], "sub-oracle");
    const int focused = !strcmp(argv[1], "focused") || !strcmp(argv[1], "focused-oracle");
    const int tracked = !strcmp(argv[1], "lowest") || !strcmp(argv[1], "focused") || (sub && strcmp(argv[1], "sub-oracle"));
    const int fixed = !strcmp(argv[1], "range");
    const int oracle = !strcmp(argv[1], "oracle") || !strcmp(argv[1], "focused-oracle") || !strcmp(argv[1], "sub-oracle");
    const int full = !strcmp(argv[1], "full");
    if ((!tracked && !fixed && !oracle && !full) || ((fixed || oracle) && argc < 3)) return 2;

    const float rate = 44100.0f;
    px_engine_init(&engine, rate);
    erb_prepare(&engine);
    pxr_reset(rate);
    lowest_fit fit = {0};
    px_lowest_sub hybrid;
    int fit_ready = 0;
    if (sub_fit_only) lf_init(&fit, rate);
    if (sub_fit_hybrid) pxl_reset(&hybrid, rate);
    lowest_verify verification = {0};
    lowest_verify low_verification = {0};
    if (sub_verify) lv_init(&verification, rate);
    if (sub_low_smooth) lv_init(&low_verification, rate);
    const float low_smoothing = expf(-4.0f * PXR_HOP / (0.016f * rate));
    const float argument = argc > 2 ? strtof(argv[2], NULL) : 0.0f;
    if ((fixed || oracle) && (!(argument > 0.0f) || !isfinite(argument))) return 2;
    const float range = fixed ? band_limit(argument) : ERB_FULL;
    float target[PX_ERB_BANDS] = {0};
    if (oracle) {
        const float upper = argc > 3 ? strtof(argv[3], NULL) : 0.0f;
        if (focused) focus(argument, target);
        else if (!sub) build_mask(target, argument, &upper, upper > argument);
    } else if (!tracked) {
        for (int i = 0; i < PX_ERB_BANDS; ++i) target[i] = 1.0f;
    }
    memcpy(erb_host_mask, target, sizeof target);

    const char *trace_path = tracked || sub ? getenv("PX_LOWEST_TRACE") : NULL;
    FILE *trace = trace_path ? fopen(trace_path, "w") : NULL;
    if (trace_path && !trace) return 1;
    const char *candidate_path = tracked ? getenv("PX_LOWEST_CANDIDATES") : NULL;
    FILE *candidates = candidate_path ? fopen(candidate_path, "w") : NULL;
    if (candidate_path && !candidates) return 1;
    unsigned long frame = 0;
    float input[128], output[128], env = 0.0f;
    const float env_step = onepole_k(rate, 0.006f);
    const float fade_step = onepole_k(rate, 0.005f);
    float selected = sub && oracle ? argument : 0.0f, pending = 0.0f;
    float sub_pitch = 0.0f, sub_phase = 0.0f, sub_gain = 0.0f, sub_target = 0.0f;
    const float sub_attack = onepole_k(rate, 0.003f);
    const float sub_release = onepole_k(rate, 0.008f);
    const float sub_swap = onepole_k(rate, 0.0015f);
    const float normalize = 2.0f * onepole_k(rate * 0.25f, 0.0226644f);
    int stable = 0, missing = 0;
    float incumbent = 0.0f;
    int weak = 0;
    float refined = 0.0f, output_pitch = 0.0f;
    const float pitch_step = onepole_k(rate, 0.004f);
    unsigned sub_age = 0;
    size_t samples;
    while ((samples = fread(input, sizeof *input, 128, stdin)) != 0) {
        for (size_t k = 0; k < samples; ++k) {
            const float x = input[k];
            ++frame;
            if (sub_fit_hybrid) {
                output[k] = pxl_run(&hybrid, x);
                if (hybrid.reported) {
                    const double time = (double)frame / rate;
                    if (trace && fprintf(trace, "%.8f,%.5f,%.6f\n", time, hybrid.pitch, hybrid.gain) < 0)
                        return 1;
                    if (candidates && !candidate_trace(candidates, time, hybrid.proposed, hybrid.selected,
                                                        hybrid.track.notes, hybrid.track.nnotes))
                        return 1;
                }
                continue;
            }
            if (tracked || sub) {
                env += env_step * (px_abs(x) - env);
                if (sub_fit_only && lf_run(&fit, x, rate)) fit_ready = 1;
                const int report = sub_fit_only ? (frame & 127u) == 0 : pxr_run(&tracker, x);
                if (report) {
                    const float maximum = sub_verify ? lv_observe(&verification, rate, 0.0f) : 0.0f;
                    const float low_maximum = sub_low_smooth
                                              ? lv_observe(&low_verification, rate, low_smoothing)
                                              : maximum;
                    if (tracked && (!sub_fit_only || fit_ready)) {
                        float candidate = 0.0f;
                        if (env > 0.001f)
                            candidate = sub_verify ? lv_select(&verification, &tracker, maximum)
                                                   : tracker.nnotes ? tracker.notes[0] : 0.0f;
                        if (sub_fit_only) {
                            candidate = fit.first_hz;
                            if (fit.second_hz > 0.0f && fit.second_hz < candidate) candidate = fit.second_hz;
                            if (env <= 0.001f) candidate = 0.0f;
                            fit_ready = 0;
                        }
                        if (sub_low && env > 0.001f) {
                            const float low = lv_low_candidate(sub_low_smooth ? &low_verification : &verification,
                                                               low_maximum);
                            if (low > 0.0f) candidate = low;
                            if (sub_low_hold && incumbent > 0.0f) {
                                if (lv_audible_low(&verification, &low_verification, incumbent, maximum, low_maximum))
                                    weak = 0;
                                else
                                    ++weak;
                                if (weak >= 3 || (low > 0.0f && low < incumbent)) incumbent = 0.0f;
                                if (incumbent > 0.0f) candidate = incumbent;
                            }
                            if (sub_low_hold && low > 0.0f && candidate == low && pending == low) {
                                incumbent = low;
                                weak = 0;
                            }
                        } else {
                            incumbent = 0.0f;
                            weak = 0;
                        }
                        if (candidate > 0.0f && candidate == pending) { if (stable < 5) ++stable; }
                        else { pending = candidate; stable = candidate > 0.0f; }
                        /* A higher octave needs more evidence than the next note of a bend. */
                        const int octave_jump = sub_verify && selected > 0.0f &&
                                                candidate > 1.92f * selected && candidate < 2.08f * selected;
                        if (candidate > 0.0f && stable >= (octave_jump ? 5 : 2)) {
                            missing = 0;
                            if (candidate != selected && sub_refined) refined = candidate;
                            if (focused && candidate != selected) focus(candidate, target);
                            selected = candidate;
                            if (!focused && !sub) build_mask(target, selected, tracker.notes, tracker.nnotes);
                        } else {
                            if (missing < 5) ++missing;
                            if (missing >= (octave_jump ? 5 : 3)) {
                                selected = 0.0f;
                                memset(target, 0, sizeof target);
                            }
                        }
                    }
                    if (sub_refined && selected > 0.0f) {
                        const float fine = sub_exclusive ? lv_refine_exclusive(&verification, &tracker, selected, maximum)
                                                         : lv_refine(&verification, selected, maximum);
                        if (fine > 0.0f) refined = fine;
                    }
                    if (sub) {
                        sub_target = 0.0f;
                        if (sub_fit_only && selected > 0.0f && env > 0.001f) {
                            if (selected == fit.first_hz) sub_target = fit.first_gain;
                            if (selected == fit.second_hz) sub_target = fit.second_gain;
                        } else if (selected > 0.0f && env > 0.001f) {
                            for (int i = 0; i < PXR_BANDS; ++i) {
                                if (px_abs(pxr_test.hz[i] - selected) > 0.027f * selected) continue;
                                const px_erb_complex z = pxr_test.state[i];
                                sub_target = normalize * sqrtf(z.r * z.r + z.i * z.i);
                                if (sub_fast) {
                                    /* Briefly compensate for the resonator's slow level rise, limited by the
                                     * input envelope. Return to note-specific level as the resonator settles. */
                                    const float boost = 1.0f - fminf(1.0f, sub_age / (0.024f * rate));
                                    sub_target = fminf(1.7f * env, sub_target * (1.0f + boost));
                                }
                                break;
                            }
                        }
                    }
                    if (trace) {
                        int written = sub
                            ? fprintf(trace, "%.8f,%.5f,%.6f\n", (double)frame / rate, output_pitch, sub_gain)
                            : fprintf(trace, "%.8f,%.5f\n", (double)frame / rate, selected);
                        if (written < 0) return 1;
                    }
                    if (candidates) {
                        const float pair[] = {fit.second_hz > 0.0f ? fminf(fit.first_hz, fit.second_hz) : fit.first_hz,
                                              fit.second_hz > 0.0f ? fmaxf(fit.first_hz, fit.second_hz) : 0.0f};
                        if (!candidate_trace(candidates, (double)frame / rate, pending, selected,
                                             sub_fit_only ? pair : tracker.notes,
                                             sub_fit_only ? (fit.second_hz > 0.0f ? 2 : 1) : tracker.nnotes))
                            return 1;
                    }
                }
                if (!sub) for (int i = 0; i < PX_ERB_BANDS; ++i)
                    erb_host_mask[i] += fade_step * (target[i] - erb_host_mask[i]);
            }
            if (sub) {
                /* Release the old oscillator before switching to an unrelated pitch. */
                const int nearby = sub_refined && selected > 0.0f && sub_pitch > 0.0f &&
                                   px_abs(selected / sub_pitch - 1.0f) < 0.085f;
                if (selected != sub_pitch && (sub_gain < 0.001f || nearby)) {
                    sub_pitch = selected;
                    if (!nearby) output_pitch = selected;
                    sub_age = 0;
                } else if (sub_age < (unsigned)(0.024f * rate) + 1u) {
                    ++sub_age;
                }
                const int switching = selected > 0.0f && selected != sub_pitch;
                const float wanted = sub_pitch == selected ? sub_target : 0.0f;
                const float step = switching ? sub_swap : (wanted > sub_gain ? sub_attack : sub_release);
                sub_gain += step * (wanted - sub_gain);
                if (sub_refined && sub_pitch == selected && refined > 0.0f)
                    output_pitch += pitch_step * (refined - output_pitch);
                sub_phase = wrap1(sub_phase + 0.5f * output_pitch / rate);
                output[k] = sub_gain * lfo_sin(sub_phase);
            } else {
                output[k] = erb_octaves(&engine, &octave, x, 0.0f, 1.0f, range);
            }
        }
        if (fwrite(output, sizeof *output, samples, stdout) != samples) return 1;
    }
    if (trace && fclose(trace) != 0) return 1;
    if (candidates && fclose(candidates) != 0) return 1;
    return ferror(stdin) ? 1 : 0;
}
