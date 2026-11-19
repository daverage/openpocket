/* OpenPocket, SPDX-License-Identifier: GPL-3.0-or-later. */
#ifndef PX_LOWEST_SUB_H
#define PX_LOWEST_SUB_H

#include "px_lowest_evidence.h"
#include "px_lowest_fit.h"

typedef struct {
    lowest_fit fit;
    lowest_verify verify;
    px_track track;
    float rate, inv_rate, env_step, attack, release, swap, normalize;
    float env, proposed, selected, pitch, phase, gain, target;
    float fit_previous, fit_confirmed;
    float anchor_level, release_hz;
    int stable, missing, anchor_missing;
#ifdef PX_LOWEST_HOST
    int reported;
#endif
} px_lowest_sub;

static void pxl_reset(px_lowest_sub *sub, float rate)
{
    memset(sub, 0, sizeof *sub);
    sub->rate = rate;
    sub->inv_rate = 1.0f / rate;
    sub->env_step = onepole_k(rate, 0.006f);
    sub->attack = onepole_k(rate, 0.003f);
    sub->release = onepole_k(rate, 0.005f);
    sub->swap = onepole_k(rate, 0.0015f);
    sub->normalize = 2.0f * onepole_k(rate * 0.25f, 0.0226644f);
    pxr_reset(rate);
    lv_init(&sub->verify, rate);
    lf_init(&sub->fit, rate);
}

static void pxl_fit_note(px_lowest_sub *sub)
{
    lowest_fit *fit = &sub->fit;
    float low = 0.0f;
    const float loudest = fit->first_gain > fit->second_gain ? fit->first_gain : fit->second_gain;
    const float floor = 0.16f * loudest;
    if (fit->first_hz < PXT_FMIN && fit->first_root > floor) low = fit->first_hz;
    if (fit->second_hz < PXT_FMIN && fit->second_root > floor &&
        (low <= 0.0f || fit->second_hz < low)) low = fit->second_hz;
    /* A single strong fit can also clean up semitone and octave swaps in the
     * fast detector. Keep the resonator when a second fit makes this ambiguous. */
    if (low <= 0.0f && fit->second_hz == 0.0f && fit->first_share > 0.74f &&
        fit->first_root > floor)
        low = fit->first_hz;
    const int held = fit->held_hz > 0.0f && fit->held_gain > 0.025f * loudest;
    /* A steep loss of note-specific level can start the fade while the
     * harmonic window still holds the pitch and the upper string rings. */
    const float anchor_hz = fit->anchor * (sub->rate / (LF_SIZE * LF_DECIMATE));
    sub->anchor_level += 0.18f * (fit->held_gain - sub->anchor_level);
    sub->release_hz = anchor_hz > 0.0f && fit->first_hz != anchor_hz &&
                      sub->anchor_level > 0.004f && fit->held_gain < 0.60f * sub->anchor_level
                      ? anchor_hz : 0.0f;
    if (held) {
        sub->anchor_missing = 0;
        if (low <= 0.0f || low >= fit->held_hz) low = fit->held_hz;
    } else if (fit->anchor > 0.0f && ++sub->anchor_missing >= 4) {
        fit->anchor = 0.0f;
        sub->anchor_missing = 0;
    }
    sub->fit_confirmed = low == sub->fit_previous ? low : 0.0f;
    sub->fit_previous = low;
    /* Use the isolated-note check to establish a fresh hold. */
    if (fit->anchor == 0.0f && sub->fit_confirmed > 0.0f &&
        sub->fit_confirmed < PXT_FMIN && sub->fit_confirmed == fit->first_hz &&
        fit->second_hz == 0.0f && fit->first_share > 0.74f &&
        fit->first_root >= 0.40f * fit->first_gain) {
        fit->anchor = fit->first.pitch;
    }
}

static __attribute__((noinline)) void pxl_pick(px_lowest_sub *sub)
{
    const float maximum = lv_observe(&sub->verify, sub->rate, 0.0f);
    float candidate = 0.0f;
    if (sub->env > 0.001f) {
        candidate = sub->fit_confirmed > 0.0f ? sub->fit_confirmed :
                    lv_select(&sub->verify, &sub->track, maximum);
    }
    if (candidate > 0.0f && candidate == sub->proposed) {
        if (sub->stable < 5) ++sub->stable;
    } else {
        sub->proposed = candidate;
        sub->stable = candidate > 0.0f;
    }
    const int octave_jump = sub->selected > 0.0f &&
                            candidate > 1.92f * sub->selected && candidate < 2.08f * sub->selected;
    if (candidate > 0.0f && sub->stable >= (octave_jump ? 5 : 2)) {
        sub->selected = candidate;
        sub->missing = 0;
    } else {
        if (sub->missing < 5) ++sub->missing;
        if (sub->missing >= (octave_jump ? 5 : 3)) sub->selected = 0.0f;
    }

    sub->target = 0.0f;
    if (sub->selected <= 0.0f ||
        sub->selected == sub->release_hz ||
        sub->env <= 0.001f) return;
    if (sub->selected == sub->fit_confirmed) {
        if (sub->selected == sub->fit.held_hz) sub->target = sub->fit.held_gain;
        if (sub->selected == sub->fit.first_hz) sub->target = sub->fit.first_gain;
        if (sub->selected == sub->fit.second_hz) sub->target = sub->fit.second_gain;
        return;
    }
    for (int i = 0; i < PXR_BANDS; ++i) {
        if (px_abs(pxr_test.hz[i] - sub->selected) > 0.027f * sub->selected) continue;
        sub->target = sub->normalize * sub->verify.magnitude[i];
        break;
    }
}

static __attribute__((noinline)) float pxl_run(px_lowest_sub *sub, float input)
{
    sub->env += sub->env_step * (px_abs(input) - sub->env);
    if (lf_run(&sub->fit, input, sub->rate)) pxl_fit_note(sub);
    const int report = pxr_run(&sub->track, input);
    if (report) pxl_pick(sub);
#ifdef PX_LOWEST_HOST
    sub->reported = report;
#endif

    if (sub->selected != sub->pitch && sub->gain < 0.001f) sub->pitch = sub->selected;
    const int switching = sub->selected > 0.0f && sub->selected != sub->pitch;
    const float target = sub->pitch == sub->selected ? sub->target : 0.0f;
    const float step = switching ? sub->swap : (target > sub->gain ? sub->attack : sub->release);
    sub->gain += step * (target - sub->gain);
    sub->phase = wrap1(sub->phase + 0.5f * sub->pitch * sub->inv_rate);
    return sub->gain * lfo_sin(sub->phase);
}

#endif
