/* The load safeguard in stages (firmware/px_shed.h, v75) against simulated loads and the real engine:
 *  - 3 blocks over the limit: nothing; 4: the heaviest block is switched off; 12 blocks of grace; then the next one
 *  - the load falls after the first: no second block, no BYPASS
 *  - still over after three blocks are shed: BYPASS after 8 in a row
 *  - no block timings (nothing to rank): the old behaviour, BYPASS after 8
 *  - the gate is never shed; a new preset starts again */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
#include "../firmware/px_shed.h"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
static px_engine e;
static px_shed_t sh;
static float in[128], out[128];
static int bypass;
static void setup(void)
{
    px_preset_t p; px_preset_base(&p); for (int k = 0; k < PX_NBLK; k++) p.on[k] = 0;
    p.on[PX_B_BST] = 1; p.type[PX_B_BST] = PX_BST_RANGEMASTER; p.on[PX_B_MOD] = 1; p.on[PX_B_EQ] = 1; p.on[PX_B_GATE] = 1;
    px_engine_init(&e, 44100.0f); px_engine_set(&e, &p); memset(&sh, 0, sizeof sh); bypass = 0;
    for (int i = 0; i < 128; i++) in[i] = 0.1f * sinf(6.2831853f * 220.0f * (float)i / 44100.0f);
    for (int i = 0; i < 20; i++) { px_engine_process(&e, in, out, 64); px_engine_process(&e, in + 64, out + 64, 64); }
    e.pfs[PX_B_GATE] = 900.0f; e.pfs[PX_B_BST] = 90.0f; e.pfs[PX_B_MOD] = 40.0f; e.pfs[PX_B_EQ] = 10.0f;     /* (no clock on the host: the costs by hand) */
}
/* one audio block at a load */
static int block(uint32_t pct)
{
    px_engine_process(&e, in, out, 64); px_engine_process(&e, in + 64, out + 64, 64);
    const int r = px_shed_judge(&sh, &e, pct, 85u);
    if (r == PX_SHED_BYPASS) bypass = 1;
    return r;
}
int main(void)
{
    setup();
    for (int i = 0; i < 3; i++) block(95);
    CHECK(e.shed == 0 && !bypass, "3 blocks over: nothing yet");
    block(60); for (int i = 0; i < 3; i++) block(95);
    CHECK(e.shed == 0, "a block under the limit starts the count again");
    CHECK(block(95) == PX_SHED_DID && e.shed == (1u << PX_B_BST), "the 4th block in a row: the heaviest (BST) is shed (mask %x)", e.shed);
    for (int i = 0; i < 12; i++) CHECK(block(95) == PX_SHED_NONE, "block %d of the grace: not judged", i + 1);
    for (int i = 0; i < 3; i++) block(95);
    CHECK(block(95) == PX_SHED_DID && e.shed == ((1u << PX_B_BST) | (1u << PX_B_MOD)), "still over: the next heaviest (MOD) goes (mask %x)", e.shed);
    for (int i = 0; i < 12 + 3; i++) block(95);
    CHECK(block(95) == PX_SHED_DID && e.shed == ((1u << PX_B_BST) | (1u << PX_B_MOD) | (1u << PX_B_EQ)), "and the third (EQ); the gate is never shed (mask %x)", e.shed);
    CHECK(sh.total == 3 && !bypass, "three shed, no BYPASS yet (total %u)", sh.total);
    for (int i = 0; i < 12 + 7; i++) { block(95); }
    CHECK(!bypass, "(grace over, 7 blocks over: not yet)");
    block(95); CHECK(bypass, "8 blocks over with nothing left to shed: BYPASS");

    setup();
    for (int i = 0; i < 4; i++) block(95);
    CHECK(e.shed == (1u << PX_B_BST), "again: BST shed");
    for (int i = 0; i < 60; i++) block(60);
    CHECK(e.shed == (1u << PX_B_BST) && !bypass && sh.total == 1, "the load fell: one block shed, no more, no BYPASS (total %u)", sh.total);
    px_engine_unshed(&e); px_shed_reset(&sh);
    CHECK(e.shed == 0, "unshed brings it back");

    setup(); memset(e.pfs, 0, sizeof e.pfs);
    for (int i = 0; i < 7; i++) block(95);
    CHECK(e.shed == 0 && !bypass, "no timings: nothing to rank, 7 blocks over: not yet");
    block(95); CHECK(e.shed == 0 && bypass, "no timings: BYPASS after 8, as before");

    setup();
    for (int i = 0; i < 4; i++) block(95);
    { px_preset_t p; px_preset_factory(&p, 3); px_engine_set(&e, &p); px_shed_reset(&sh); }
    CHECK(e.shed == 0 && sh.npre == 0, "a new preset: nothing shed, the count starts again");
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("LOAD SHEDDING (policy) ok: 0 problems\n"); return 0;
}
