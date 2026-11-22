/* Load shedding (v75: px_engine_heaviest / px_engine_shed / px_engine_unshed; the firmware's policy is tested in tests/shedsim.c):
 *  - a shed block plays as if it were off (after its fade), the preset is unchanged, no click on the way out or back in
 *  - switching the block, or changing its type, makes it the user's again; moving a knob does not; a new preset clears it; unshed brings it back
 *  - px_engine_heaviest: the most expensive running block, never the gate, never one already shed, and not the delay / reverb while the wet lanes run */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
#define FS 44100.0f
enum { N = 44100 * 2 };
static float x[N], a[N], b[N];
static void chain(px_preset_t *p, int wah, int eq)
{
    px_preset_base(p); for (int k = 0; k < PX_NBLK; k++) p->on[k] = 0;
    p->on[PX_B_BST] = (uint8_t)wah; p->type[PX_B_BST] = PX_BST_VOXWAH; p->p[PX_B_BST][0] = 70; p->p[PX_B_BST][1] = 80; p->p[PX_B_BST][2] = 40;
    p->on[PX_B_EQ] = (uint8_t)eq; p->type[PX_B_EQ] = PX_EQ_5BAND; p->p[PX_B_EQ][1] = 80;
}
static void run(px_engine *e, int from, int to) { for (int at = from; at + 64 <= to; at += 64) px_engine_process(e, x + at, a + at, 64); }
static float rms(const float *s, int i0, int i1) { double q = 0; for (int i = i0; i < i1; i++) q += (double)s[i] * s[i]; return (float)sqrt(q / (i1 - i0)); }
static float diff(const float *s, const float *t, int i0, int i1) { double q = 0; for (int i = i0; i < i1; i++) q += (double)(s[i] - t[i]) * (s[i] - t[i]); return (float)sqrt(q / (i1 - i0)); }
int main(void)
{
    static px_engine e, ref;
    px_preset_t on, off;
    chain(&on, 1, 1); chain(&off, 0, 1);
    unsigned s = 7u; for (int i = 0; i < N; i++) { s = s * 1664525u + 1013904223u; x[i] = 0.1f * sinf(6.2831853f * 330.0f * (float)i / FS) + 0.05f * ((float)(s >> 9) / 8388608.0f - 1.0f); }

    px_engine_init(&e, FS); px_engine_set(&e, &on);
    run(&e, 0, 22050);
    CHECK(px_engine_heaviest(&e) == -1, "no clock, no costs: nothing to shed (%d)", px_engine_heaviest(&e));
    e.pfs[PX_B_BST] = 90.0f; e.pfs[PX_B_EQ] = 30.0f; e.pfs[PX_B_GATE] = 500.0f; e.pfs[PX_B_REVERB] = 900.0f;
    CHECK(px_engine_heaviest(&e) == PX_B_BST, "the heaviest running block: BST (the gate and a block that is off do not count; got %d)", px_engine_heaviest(&e));

    /* shed it: after the fade the sound is the chain without it */
    const px_preset_t before = e.cur;
    px_engine_shed(&e, PX_B_BST);
    CHECK(e.shed == (1u << PX_B_BST) && !memcmp(&before, &e.cur, sizeof before), "shed: marked, the preset untouched");
    CHECK(px_engine_heaviest(&e) == PX_B_EQ, "the next heaviest once BST is shed: EQ (%d)", px_engine_heaviest(&e));
    float worst = 0, steady = 0;
    run(&e, 22050, 44100); for (int i = 22051; i < 44100; i++) { const float d = fabsf(a[i] - a[i - 1]); if (d > worst) worst = d; }
    px_engine_init(&ref, FS); px_engine_set(&ref, &off);
    for (int at = 0; at + 64 <= N; at += 64) px_engine_process(&ref, x + at, b + at, 64);
    run(&e, 44100, N);
    for (int i = 44100; i < N; i++) { const float d = fabsf(b[i] - b[i - 1]); if (d > steady) steady = d; }
    CHECK(diff(a, b, 66150, N) < 0.02f * rms(b, 66150, N), "shed BST sounds like BST off (difference %.5f of %.5f)", diff(a, b, 66150, N), rms(b, 66150, N));
    CHECK(worst < 4.0f * steady + 0.01f, "no click while it fades out (largest step %.4f, steady %.4f)", worst, steady);

    /* a knob does not bring it back; switching or retyping does */
    { px_preset_t q = e.cur; q.p[PX_B_BST][0] = 20; px_engine_knobs(&e, &q);
      CHECK(e.shed == (1u << PX_B_BST), "a knob move keeps it shed"); q.p[PX_B_BST][0] = 70; px_engine_knobs(&e, &q); }
    { px_preset_t q = e.cur; q.on[PX_B_BST] = 0; px_engine_knobs(&e, &q); CHECK(e.shed == 0, "switching the block off clears the shed"); q.on[PX_B_BST] = 1; px_engine_knobs(&e, &q); }
    px_engine_shed(&e, PX_B_BST);
    { px_preset_t q = e.cur; q.type[PX_B_BST] = PX_BST_EPBOOST; px_engine_knobs(&e, &q); CHECK(e.shed == 0 && e.cur.type[PX_B_BST] == PX_BST_EPBOOST, "another type clears the shed"); }
    { px_preset_t q = e.cur; q.type[PX_B_BST] = PX_BST_VOXWAH; px_engine_knobs(&e, &q); }

    /* unshed: it fades back in, from rest, with no click, and plays again */
    px_engine_shed(&e, PX_B_BST); run(&e, 0, 22050);
    px_engine_unshed(&e);
    CHECK(e.shed == 0, "unshed clears the set");
    worst = 0; run(&e, 22050, N); for (int i = 22051; i < N; i++) { const float d = fabsf(a[i] - a[i - 1]); if (d > worst) worst = d; }
    { static px_engine on_e; static float c[N]; px_engine_init(&on_e, FS); px_engine_set(&on_e, &on); for (int at = 0; at + 64 <= N; at += 64) px_engine_process(&on_e, x + at, c + at, 64);
      CHECK(diff(a, c, 66150, N) < 0.03f * rms(c, 66150, N), "after unshed the block plays as it did (difference %.5f of %.5f)", diff(a, c, 66150, N), rms(c, 66150, N)); }
    CHECK(worst < 4.0f * steady + 0.05f, "no click while it fades back in (largest step %.4f)", worst);

    /* a new preset clears it; the gate cannot be shed */
    px_engine_shed(&e, PX_B_BST); px_engine_shed(&e, PX_B_GATE);
    CHECK(e.shed == (1u << PX_B_BST), "the gate is never shed");
    px_engine_set(&e, &on); CHECK(e.shed == 0, "a new preset clears the shed set");

    /* the wet lanes: the delay and reverb are off the chain's deadline */
    px_engine_init(&e, FS); { px_preset_t q = on; q.on[PX_B_REVERB] = 1; q.on[PX_B_BST] = 0; px_engine_set(&e, &q); }
    run(&e, 0, 22050); e.pfs[PX_B_REVERB] = 900.0f; e.pfs[PX_B_EQ] = 30.0f;
    CHECK(px_engine_heaviest(&e) == PX_B_REVERB, "lanes off: the reverb counts (%d)", px_engine_heaviest(&e));
    e.wl_on = 1; CHECK(px_engine_heaviest(&e) == PX_B_EQ, "lanes on: the reverb does not (%d)", px_engine_heaviest(&e));
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("LOAD SHEDDING (engine) ok: 0 problems\n"); return 0;
}
