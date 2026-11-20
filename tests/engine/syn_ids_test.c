/* Synth2: the SYNTH block's type ids (docs/research/synth2/SYNTH2-PLAN.md). Every id of the v29..v73 catalogue keeps its number; only
 * POLY OCT FUZZ (16) runs; the 27 others are tombstones. Checks:
 *  - the catalogue size and POLY OCT FUZZ's id are the v73 ones; the ids Synth2 appends (ANALOG 28, ARCADE 29) come after STRING DUO (27)
 *  - a tombstone has an empty name (the UI skips it), no controls, and px_type_retired says so
 *  - px_block_canonical leaves a stored tombstone untouched (the type and its five settings are kept)
 *  - a tombstone on the block is bypassed: its output is bit for bit the output with the block off (and no NaN, whatever
 *    its settings), for every id 0..255 (an id beyond the catalogue is a tombstone too, never another sound)
 *  - POLY OCT FUZZ does run (the output differs from the block off) */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static px_engine e;
static float in[2048], out[2048];
static int bad;
#define CHECK(c, m) do { if (!(c)) { bad++; printf("FAIL %s\n", m); } } while (0)
static void render(const px_preset_t *p, float *o)
{
    px_engine_init(&e, 44100.0f); px_engine_set(&e, p);
    for (int at = 0; at < 2048; at += 64) px_engine_process(&e, in + at, o + at, 64);
}
int main(void)
{
    for (int i = 0; i < 2048; i++) in[i] = 0.3f * sinf(i * 6.2831853f * 110.0f / 44100.0f) + 0.2f * sinf(i * 6.2831853f * 165.0f / 44100.0f);
    CHECK(PX_SYN_OCT_FUZZ == 16, "POLY OCT FUZZ keeps id 16");
    CHECK(PX_SYN_X_STRING_DUO == 27, "STRING DUO was the last legacy id (27)");
    CHECK(px_ntypes(PX_B_SYN) >= 28 && PX_SYN_ANALOG == 28 && PX_SYN_ARCADE == 29 && PX_SYN_ARCADE_SPLIT == 30 && PX_SYN_INSTRUMENT == 31 && PX_SYN_ENSEMBLE == 32 && PX_SYN_CHOIR == 33, "the catalogue never shrinks; Synth2's ANALOG and ARCADE are the first appended ids (28, 29)");
    px_preset_t off; px_preset_factory(&off, 7);
    for (int z = 0; z < PX_NBLK; z++) off.on[z] = 0;
    static float ref[2048]; render(&off, ref);
    for (int t = 0; t < 256; t++) {
        px_preset_t p = off; p.on[PX_B_SYN] = 1; p.type[PX_B_SYN] = (uint8_t)t;
        for (int k = 0; k < 5; k++) p.p[PX_B_SYN][k] = (uint8_t)(10 + 17 * k + t % 7);
        px_preset_t q = p; px_block_canonical(PX_B_SYN, &q.type[PX_B_SYN], q.p[PX_B_SYN]);
        CHECK(!memcmp(&p, &q, sizeof p), "px_block_canonical keeps a stored type and its settings");
        render(&p, out);
        int nan = 0, same = !memcmp(out, ref, sizeof out);
        for (int i = 0; i < 2048; i++) if (out[i] != out[i] || fabsf(out[i]) > 50.0f) nan = 1;
        CHECK(!nan, "no NaN / runaway");
        if (t == PX_SYN_OCT_FUZZ) { CHECK(!same, "POLY OCT FUZZ runs"); CHECK(!px_type_retired(PX_B_SYN, t), "POLY OCT FUZZ is live"); CHECK(px_type_name(PX_B_SYN, t)[0] && px_nparams(PX_B_SYN, t) == 5, "POLY OCT FUZZ keeps its name and 5 controls"); }
        else if (t == PX_SYN_ANALOG || t == PX_SYN_ARCADE || t == PX_SYN_ARCADE_SPLIT || t == PX_SYN_INSTRUMENT || t == PX_SYN_ENSEMBLE || t == PX_SYN_CHOIR) { CHECK(!px_type_retired(PX_B_SYN, t) && px_nparams(PX_B_SYN, t) == 5 && px_type_name(PX_B_SYN, t)[0], "ANALOG and ARCADE are live"); }
        else {
            CHECK(same, "a tombstone is bypassed: the output is the block off, bit for bit");
            CHECK(px_type_retired(PX_B_SYN, t), "a tombstone says so");
            if (t < px_ntypes(PX_B_SYN)) CHECK(!px_type_name(PX_B_SYN, t)[0] && px_nparams(PX_B_SYN, t) == 0, "a tombstone has no name and no controls");
        }
    }
    printf("%s: %d problems\n", bad ? "FAILED" : "syn ids ok", bad);
    return bad != 0;
}
