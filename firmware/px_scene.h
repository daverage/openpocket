/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#define PX_SCENES 3u
#define PX_SCENE_NAME 10u
#define PX_SCENE_TWO 4u

typedef struct {
    uint8_t on[2];
    uint8_t p[PX_NBLK][PX_NPAR];
    uint8_t valve_on, valve_drive, valve_level, volume;
} px_scene_sound;

typedef struct {
    /* Zero means scenes are disabled, otherwise the default scene plus one.
     * PX_SCENE_TWO marks an enabled patch with C hidden. Its sound stays in other[].
     * The default sound lives in the ordinary preset record. */
    uint8_t start;
    char name[PX_SCENES][PX_SCENE_NAME];
    px_scene_sound other[PX_SCENES - 1u];
} px_scene_record;

static inline int px_scene_start_valid(unsigned start)
{
    return start <= PX_SCENES || start == (PX_SCENE_TWO | 1u) || start == (PX_SCENE_TWO | 2u);
}

static inline void px_scene_retire_pitch(px_scene_record *record)
{
    for (unsigned i = 0; i < PX_SCENES - 1u; i++)
        record->other[i].on[PX_B_PITCH / 7u] &= (uint8_t)~(1u << (PX_B_PITCH % 7u));
}

static inline void px_scene_capture(px_scene_sound *s, const px_preset_t *p)
{
    s->on[0] = s->on[1] = 0;
    for (unsigned b = 0; b < PX_NBLK; b++)
        s->on[b / 7u] |= (uint8_t)((p->on[b] != 0) << (b % 7u));
    memcpy(s->p, p->p, sizeof s->p);
    s->valve_on = p->valve_on;
    s->valve_drive = p->valve_drive;
    s->valve_level = p->valve_level;
    s->volume = p->patch_volume;
}

static inline void px_scene_apply(px_preset_t *p, const px_scene_sound *s)
{
    for (unsigned b = 0; b < PX_NBLK; b++)
        p->on[b] = (s->on[b / 7u] >> (b % 7u)) & 1u;
    memcpy(p->p, s->p, sizeof p->p);
    p->valve_on = s->valve_on;
    p->valve_drive = s->valve_drive;
    p->valve_level = s->valve_level;
    p->patch_volume = s->volume;
}
