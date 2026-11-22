/* Host-only deterministic float render and exact sample-frame capture. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "px_dsp.c"
#include "ft_ctrl.h"
#include "ordered.h"
#include "ft_wav.h"

enum { STEAL = 256, PATCH_CHANGE = 512 };
typedef struct { uint32_t events; float hz, velocity, expression; } frame;
_Static_assert(sizeof(frame) == 16, "frame format");
static px_engine engine;
static px_track tracker;
static pxt_layer track_layer;
static ft_ctrl controller;
static const uint8_t changed[] = {80, 50, 50, 50, 50};

static void capture(const char *path, bool experimental)
{
    long n;
    float *input = ft_rd(path, &n);
    if (!input) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    const uint8_t controls[] = {0, 50, 50, 50, 50};
    px_s2_reset(&engine, PX_SYN_ANALOG, controls);
    pxt_reset(&engine, &tracker, &track_layer);
    tracker.sensitive_picks = 1;
    ft_ctrl_init(&controller, FS);
    float previous = 0;
    for (long i = 0; i < n; ++i) {
        frame f;
        if (experimental) {
            ft_ctrl_out o;
            const int framed = pxt_run(&engine, &tracker, input[i]);
            ft_ctrl_run(&controller, &tracker, input[i], framed, &o);
            f = (frame){o.ev, o.f0, o.vel, o.vel};
        } else {
            px_mono_out o;
            px_mono_run(&engine, &px_s2_st.mono, input[i], &o);
            uint32_t events = 0;
            if (o.onset)
                events |= !o.retrigger || fabsf(o.f0 - previous) > previous * 0.0293f
                    ? FTC_NOTE_ON : FTC_REPICK;
            if (o.slur)
                events |= FTC_LEGATO;
            if (o.release)
                events |= FTC_NOTE_OFF | (o.damp ? FTC_DAMP : 0);
            f = (frame){events, o.f0, o.vel, o.expr};
        }
        if (!(f.events & (FTC_NOTE_ON | FTC_REPICK | FTC_LEGATO)) && f.hz > 0 && f.hz != previous)
            f.events |= FTC_PITCH_UPDATE;
        previous = f.hz;
        assert(fwrite(&f, sizeof f, 1, stdout) == 1);
    }
    free(input);
}

static int synth_type(const char *name)
{
    const struct { const char *name; int id; } types[] = {
        {"analog", PX_SYN_ANALOG}, {"arcade", PX_SYN_ARCADE}, {"split", PX_SYN_ARCADE_SPLIT},
        {"instrument", PX_SYN_INSTRUMENT}, {"ensemble", PX_SYN_ENSEMBLE}, {"choir", PX_SYN_CHOIR},
    };
    for (unsigned i = 0; i < sizeof types / sizeof *types; ++i)
        if (!strcmp(name, types[i].name))
            return types[i].id;
    fprintf(stderr, "unknown synth type %s\n", name);
    exit(1);
}

static void raw_legacy(const char *path)
{
    long n;
    float *input = ft_rd(path, &n);
    assert(input);
    const uint8_t controls[] = {0, 50, 50, 50, 50};
    px_s2_reset(&engine, PX_SYN_ANALOG, controls);
    for (long i = 0; i < n; ++i) {
        float output;
        px_synth_run(&engine, &input[i], &output, 1);
        assert(fwrite(&output, sizeof output, 1, stdout) == 1);
    }
    free(input);
}

static void apply_frame(px_s2 *s, frame f)
{
    if (f.events & (FTC_NOTE_OFF | FTC_DAMP)) {
        s->pend_off = 1;
        if (f.events & FTC_DAMP) {
            s->damped = 1;
#ifdef FT_DECLICK_OVERLAY
            dc_clear();
#endif
        }
    }
    if (f.events & (FTC_NOTE_ON | FTC_REPICK)) {
        s->pend_on = 1;
        s->pend_re = (f.events & FTC_REPICK) != 0;
        s->vel = f.velocity;
        s->pend_off = 0;
        s->damped = 0;
    }
    s->f0 = f.hz;
    s->expr_a = f.expression;
}

static uint64_t now_ns(void)
{
#ifdef __APPLE__
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000u + time.tv_nsec;
#endif
}

static void render(int argc, char **argv)
{
    assert(argc == 11);
    FILE *input = fopen(argv[2], "rb");
    FILE *trace = fopen(argv[3], "w");
    assert(input && trace);
    const int type = synth_type(argv[4]);
    const uint8_t controls[] = {atoi(argv[5]), atoi(argv[6]), 50, 50, 50};
    const float tau = strtof(argv[7], NULL);
    const int policy = atoi(argv[8]);
    const int measure = atoi(argv[9]);
    const int require_silence = atoi(argv[10]);
    px_s2 *s = &px_s2_st;
    px_s2_reset(&engine, type, controls);
    if (policy >= 0)
        s->follow = policy;
#ifdef FT_DECLICK_OVERLAY
    dc_coefficient = declick_coefficient(FS, tau);
#else
    assert(tau == 0);
#endif
    fprintf(trace, "render,output,starts,release,voices,damped,corrected_slots,env,ns,ordered_starts,ordered_releases,ordered_damps,ordered_gate,raw_mix_hash,stage_before,env_before,replacement_slots\n");
    ordered_state ordered = {0};
    frame f;
    uint32_t pending = 0;
    long i = 0;
    while (fread(&f, sizeof f, 1, input) == 1) {
        ordered_deliver(&ordered, f.events, f.hz);
        apply_frame(s, f);
        pending |= f.events;
        assert(fwrite(s->ring + s->rpos, sizeof(float), 1, stdout) == 1);
        ++s->rpos;
        if (++s->cnt == CTL) {
            unsigned age = trk[0].v[0].age;
            const int stage_before = s->stage;
            const float env_before = s->env;
            const int release = s->pend_off;
            if (pending & PATCH_CHANGE)
                px_s2_set(s, PX_SYN_ARCADE, changed);
#ifdef FT_DECLICK_OVERLAY
            dc_steal = (pending & STEAL) != 0;
#endif
            if (pending & STEAL) {
                /* Explicit test-only slot replacement. Other ensemble slots
                   continue, with their existing phases and envelope stages. */
                memset(&trk[0].v[0], 0, sizeof trk[0].v[0]);
                s->last_amp = 0;
                s->env = 0;
                s->stage = 0;
                px_s2_noteon(s, 0, &s->env, &s->stage, s->v0semis);
                s->pend_on = 0;
            }
            int corrected = 0;
            int replaced = 0;
#ifdef FT_DECLICK_OVERLAY
            dc_repick = s->pend_re;
            dc_steal = 0;
#endif
            const uint64_t start = measure ? now_ns() : 0;
            px_s2_block(s);
            const uint64_t elapsed = measure ? now_ns() - start : 0;
#ifdef FT_DECLICK_OVERLAY
            corrected = dc_applied;
            replaced = dc_replaced;
#endif
            int voices = 0;
            for (int v = 0; v < s->nv; ++v)
                voices += trk[0].v[v].active != 0;
            uint32_t hash = 2166136261u;
            for (int offset = 0; offset < CTL; ++offset)
                hash = (hash ^ (uint32_t)s->out[offset]) * 16777619u;
            fprintf(trace, "%ld,%ld,%d,%d,%d,%d,%d,%.9g,%llu,%u,%u,%u,%d,%u,%d,%.9g,%d\n", i, i + 1,
                    age != trk[0].v[0].age || (pending & STEAL), release, voices,
                    s->damped, corrected, s->env, (unsigned long long)elapsed,
                    ordered.starts, ordered.releases, ordered.damps, ordered.gate, hash, stage_before, env_before, replaced);
            ordered.starts = ordered.releases = ordered.damps = 0;
            s->cnt = s->rpos = 0;
            pending = 0;
        }
        ++i;
    }
    assert(!ferror(input));
    fclose(input);
    fclose(trace);
    if (require_silence)
        for (int v = 0; v < s->nv; ++v)
            assert(!trk[0].v[v].active);
}

int main(int argc, char **argv)
{
    assert(argc > 1);
    px_engine_init(&engine, FS);
    if (!strcmp(argv[1], "capture")) {
        assert(argc == 4);
        capture(argv[3], !strcmp(argv[2], "controller"));
    } else if (!strcmp(argv[1], "raw-legacy")) {
        assert(argc == 3);
        raw_legacy(argv[2]);
    } else if (!strcmp(argv[1], "test-order")) {
        ordered_state state = {0};
        for (unsigned i = 0; i < 128; ++i) {
            ordered_deliver(&state, FTC_NOTE_ON, 110);
            ordered_deliver(&state, FTC_DAMP | FTC_NOTE_OFF, 110);
            assert(!state.gate);
            ordered_deliver(&state, FTC_NOTE_ON, 220);
            assert(state.gate && state.pitch == 220);
        }
        ordered_deliver(&state, FTC_LEGATO, 330);
        ordered_deliver(&state, FTC_PITCH_UPDATE, 340);
        ordered_deliver(&state, FTC_NOTE_OFF, 340);
        assert(state.starts == 256 && state.damps == 128 && state.releases == 129);
        assert(!state.gate && state.pitch == 340);
        puts("ordered host delivery: 385 ordered starts, damps and releases retained");
    } else {
        assert(!strcmp(argv[1], "render"));
        render(argc, argv);
    }
}
