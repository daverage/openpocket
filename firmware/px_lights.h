/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — what the button lights show (issue #8). px_leds.h drives them; this decides them, once per main-loop
 * pass, from the preset being played:
 *   FX                          lit while Valve or any block of the FX button's group is on (gate, BST, comp, SFX, EQ)
 *   AMP, CAB                    v70: lit while SYNTH (AMP) or OCTAVE (CAB) is on
 *   MOD, DLY, REV               lit while their block is on
 *   HOME                        lit while the pedal plays, blinks (2 Hz) while the tuner is open (v70)
 *   (all)                       dark while BYPASS is engaged, except the tuner's blink (v70)
 * The bits are the LED map's, read on a BlackBox (2026-11-06): 0 HOME, 1 FX, 2 AMP, 3 MOD, 4 DLY, 5 REV, 6 CAB,
 * 7 none: the panel's order (a Pocket X may differ: not checked there yet). On the wire a 0 lights, so the byte
 * goes out inverted (as the stock firmware sends it), and only when it changes. Confirmed on a BlackBox: each
* light follows its block, HOME blinks in the tuner.
 * v75 (the global settings, px_store.h st_cfg.lights): TUNER LIGHTS, how the seven lights show the tuner: BLINK (HOME, as above), NEEDLE (one light moves along the
 * row from HOME to CAB with the cents, the three in the middle (AMP MOD DLY) steady when in tune), STROBE (one light chases along the row, toward the side the note is
 * out to, faster the further out: the steady middle three when in tune); and LIGHTS OFF (all dark outside the tuner). On / off lights only: no brightness.
 * Included by openpocket.c (after ui.h: UI_FXB) and by tests/lightsim.c. */
#pragma once
#include <stdint.h>
#include "px_leds.h"

enum { LT_HOME, LT_FX, LT_AMP, LT_MOD, LT_DLY, LT_REV, LT_CAB };      /* the light on each bit */
#define LT_BLINK_MS 250u                                             /* the tuner's HOME blink: half a period */
static uint8_t lt_style;               /* v75: the tuner's lights: 0 HOME blinks, 1 needle, 2 strobe */
static uint8_t lt_off;                 /* v75: 1: dark outside the tuner */
static int lt_c;                       /* v75: the tuner's reading: the cents off, and whether it hears a note */
static uint8_t lt_have;

/* the lit lights, 1 = lit. v70: HOME is lit while the pedal plays (not bypassed) and blinks in the tuner; with BYPASS
 * engaged every light is off (the tuner's blink still shows) */
static uint8_t lights_of(const px_preset_t *p, int tuner, int bypass, uint32_t now)
{
    uint8_t v = 0;
    if (tuner && lt_have && lt_style) {                               /* v75: the row as a needle or a strobe */
        const int c = lt_c, a = c < 0 ? -c : c;
        if (a < 4) return (uint8_t)(1u << LT_AMP | 1u << LT_MOD | 1u << LT_DLY);
        if (lt_style == 1) { int i = 3 + (c + (c >= 0 ? 6 : -6)) / 12; if (i < 0) i = 0; if (i > 6) i = 6; return (uint8_t)(1u << i); }
        const uint32_t period = 500u - 9u * (uint32_t)(a > 50 ? 50 : a);                             /* 500 .. 50 ms a step */
        int pos = (int)((now / period) % 7u); if (c < 0) pos = 6 - pos;
        return (uint8_t)(1u << pos);
    }
    if (lt_off && !tuner) return 0;                                   /* v75: LIGHTS OFF */
    const uint8_t home = tuner ? !((now / LT_BLINK_MS) & 1u) : !bypass;
    if (home) v |= 1u << LT_HOME;
    if (bypass) return v;
    if (p->valve_on) v |= 1u << LT_FX;
    for (int i = 0; i < UI_NFX; i++)
        if (p->on[UI_FXB[i]]) v |= 1u << LT_FX;
    if (p->on[PX_B_SYN]) v |= 1u << LT_AMP;           /* v70: the AMP button is SYNTH, CAB is OCTAVE */
    if (p->on[PX_B_MOD]) v |= 1u << LT_MOD;
    if (p->on[PX_B_DELAY]) v |= 1u << LT_DLY;
    if (p->on[PX_B_REVERB]) v |= 1u << LT_REV;
    if (p->on[PX_B_PITCH]) v |= 1u << LT_CAB;
    return v;
}
static void lights_show(uint8_t lit)                                 /* written only when the byte changes */
{
    uint8_t wire = (uint8_t)~lit;
    if (wire != leds_byte) leds_write(wire);
}
static void lights_update(const px_preset_t *p, int tuner, int bypass, uint32_t now) { lights_show(lights_of(p, tuner, bypass, now)); }
static void lights_off(void) { lights_show(0u); }
