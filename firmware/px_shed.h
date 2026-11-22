/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the DSP load safeguard, in stages (v75). One judgement per audio block, from the block's load:
 *   over the limit for SHED_AT blocks in a row  ->  the heaviest block is switched off (px_engine_shed: a fade, the preset unchanged); up to SHED_MAX
 *                                                   blocks per preset, then SHED_WAIT blocks in which the load is not judged (the fade, then it settles)
 *   still over for 8 blocks in a row (or nothing left to shed)  ->  BYPASS, the dry guitar (what the safeguard always did)
 * Host-testable: tests/shedsim.c runs it against simulated loads. px_audio.h calls px_shed_judge from dsp_load. */
#pragma once
#include <stdint.h>
#include "px_dsp.h"

#define SHED_AT   4u                                    /* blocks over the limit in a row before a block is shed */
#define SHED_MAX  3u                                    /* blocks shed per preset before the BYPASS */
#define SHED_WAIT 12u                                   /* blocks to wait after a shed (the fade is 16 ticks = 8 blocks) */
#define SHED_BYP  8u                                    /* blocks over the limit in a row before the BYPASS */
typedef struct { uint8_t over, grace, npre; uint32_t total; } px_shed_t;
enum { PX_SHED_NONE, PX_SHED_DID, PX_SHED_BYPASS };

static void px_shed_reset(px_shed_t *s) { s->over = 0; s->grace = 0; s->npre = 0; }
/* pct: this block's load in per cent of its time; lim: the limit (85). Returns what was done. */
static int px_shed_judge(px_shed_t *s, px_engine *e, uint32_t pct, uint32_t lim)
{
    if (s->grace) { s->grace--; s->over = 0; return PX_SHED_NONE; }
    if (pct <= lim) { s->over = 0; return PX_SHED_NONE; }
    s->over++;
    if (s->over >= SHED_AT && s->npre < SHED_MAX) {
        const int b = px_engine_heaviest(e);
        if (b >= 0) { px_engine_shed(e, b); s->total++; s->npre++; s->grace = (uint8_t)SHED_WAIT; s->over = 0; return PX_SHED_DID; }
    }
    return s->over >= SHED_BYP ? PX_SHED_BYPASS : PX_SHED_NONE;
}
