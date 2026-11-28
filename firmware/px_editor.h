/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the web editor's protocol (docs/EDITOR-PROTOCOL.md): SysEx F0 7D 4F 50 cmd args.. F7.
 *
 * The editor edits PRESET, the very sound the pedal's screen shows and the engine plays (audio_poll applies any change,
 * ui_render redraws what changed), and the 52 slots of the store (px_store.h). Frames come through usb.c's sx_frame
 * (ota_frame_get), replies leave through ota_wire_send, as the FM-1 editor (felucca's editor.c) does; ed_service runs
 * before ota_service, which would take any frame.
 * While watched, a change made on the pedal (knob, buttons, footswitches) is pushed as a STATE frame; a change the
 * editor made itself is not echoed back.
 * The including file provides: PRESET, cur_preset, load_preset, ui_blk / ui_mode / ui_item (ui.h), st_gen, the
 * store (px_store.h), BLK_COL (ui_colors.h), ota_frame_get / ota_frame_done / ota_wire_send, ms_now, OP_VERSION,
 * FELUCCA_ID_PTR, ed_load / ed_flags (the DSP load for PING); (v42) px_ua_on (px_midi.h), st_usb, and with
 * OP_UAUDIO ed_ua_stat(k), USB audio's state (k = 0) and its ED_UA_STATS counts (px_uaudio.h).
 * An image with a log channel also defines ED_DIAG_LINES
 * and ed_diag_line(i, out), which writes line i (at most ED_DIAG_LEN - 1 characters); without them DIAG gets no reply.
 */
#pragma once

#define ED_H0 0x7Du
#define ED_H1 0x4Fu
#define ED_H2 0x50u
#define ED_PROTO 16u                             /* 11: Valve Level; 10: per-preset valve; 9: v75, GLOBAL (the global settings, TUNER); 8: v75, SHED (the blocks switched off for the load); 7: v75, ten blocks (BST); 6: v70 OpenPocket Synth, no NAM models or IRs (their commands get no reply); 5: v48, FOOTSWITCH; 4: v42, USB_AUDIO; 3: v38, cabinet IRs (IR_LIST .. IR_ERASE); 2: v37, NAM models */
#define ED_NAME_MAX 13u
#define ED_P_SIZE PX_PRESET_BASE_SIZE          /* the name, then per block on, type, 5 settings and its place in the order: v75: 10 blocks, 94 bytes; v70: 9, 86 */
#if OP_UADEV
#define ED_UA_STATS 40u                         /* (the measurement option's figures after the 20, px_uaudio.h) */
#else
#define ED_UA_STATS 20u                         /* USB_AUDIO's counts after the state (ed_ua_stat 1..20) */
#endif
#ifndef ED_STATS
#define ED_STATS 6u                             /* PING's figures after the flags (ed_stat): the Bluetooth test image has 8 */
#endif
typedef char ed_size_check[sizeof(px_preset_t) == ED_P_SIZE + 4u ? 1 : -1];
enum { ED_INFO = 1, ED_CAT_BLOCK, ED_CAT_TYPE, ED_TEXTS, ED_STATE, ED_SET, ED_ORDER, ED_NAME, ED_PUT, ED_SELECT,
       ED_FOCUS, ED_LIST, ED_SLOT_GET, ED_SLOT_PUT, ED_COMMIT, ED_SAVE, ED_WATCH, ED_PING,
       ED_NAM_LIST, ED_NAM_WRITE, ED_NAM_COMMIT, ED_NAM_ERASE, ED_DIAG,   /* 19..22: NAM retired (v70) */
       ED_IR_LIST, ED_IR_WRITE, ED_IR_COMMIT, ED_IR_ERASE,     /* v38: 24..27, retired (v70) */
       ED_USB_AUDIO,                                           /* v42: 28 */
       ED_FOOTSWITCH,                                          /* v48: 29 */
       ED_SHED,                                                /* v75: 30 */
       ED_GLOBAL, ED_TUNER, ED_SCENE };                                  /* v75: 31, 32 */

static uint8_t ed_out[1024];
static uint32_t ed_n;
static struct { uint8_t on; uint32_t last_ms, hash, push_ms; } ed_w;

static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0; ed_out[1] = ED_H0; ed_out[2] = ED_H1; ed_out[3] = ED_H2; ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_str(const char *s, uint32_t max)
{
    for (uint32_t i = 0; s && s[i] && i < max; i++) ed_b((uint8_t)s[i]);
    ed_b(0);
}
static void ed_send(void) { ed_out[ed_n++] = 0xF7; ota_wire_send(ed_out, ed_n); }
static void ed_preset(const px_preset_t *p)
{
    const uint8_t *b = (const uint8_t *)p;
    for (uint32_t i = 0; i < sizeof *p; i++) ed_b(b[i]);
}

static void ed_scene_record(const px_scene_record *scene)
{
    const uint8_t *bytes = (const uint8_t *)scene;
    ed_b(scene->start);
    if (scene->start)
        for (unsigned i = 1; i < sizeof *scene; i++) ed_b(bytes[i]);
}

static int ed_take_scene(px_scene_record *scene, const uint8_t *bytes, uint32_t n)
{
    memset(scene, 0, sizeof *scene);
    if (!n || (n == 1 && !bytes[0])) return 1;
    if (n != sizeof *scene || !bytes[0] || !px_scene_start_valid(bytes[0])) return 0;
    memcpy(scene, bytes, sizeof *scene);
    for (unsigned i = 0; i < PX_SCENES; i++) {
        for (unsigned j = 0; j < PX_SCENE_NAME - 1u; j++)
            if ((uint8_t)scene->name[i][j] < 32u && scene->name[i][j]) return 0;
        scene->name[i][PX_SCENE_NAME - 1u] = 0;
    }
    for (unsigned i = 0; i < PX_SCENES - 1u; i++) {
        const px_scene_sound *sound = &scene->other[i];
        if (sound->volume > 100u || sound->valve_drive > 100u ||
            sound->valve_level > PX_VALVE_LEVEL_MAX || sound->valve_on > 1u ||
            sound->on[0] > 127u || sound->on[1] >= (1u << (PX_NBLK - 7))) return 0;
        for (unsigned b = 0; b < PX_NBLK; b++)
            for (unsigned j = 0; j < PX_NPAR; j++)
                if (sound->p[b][j] > 100u) return 0;
    }
    return 1;
}

/* a received preset: every field in range, the name cleaned; 0 = invalid */
static int ed_take_preset(const uint8_t *a, uint32_t n, px_preset_t *q)
{
    uint32_t i, k;
    if (n < sizeof *q) return 0;
    memcpy(q, a, sizeof *q);
    if (q->valve_on > 1u || q->valve_drive > 100u || q->valve_level > PX_VALVE_LEVEL_MAX || q->patch_volume > 100u) return 0;
    for (i = 0; i < 14u; i++) {
        uint8_t c = (uint8_t)q->name[i];
        if (i >= ED_NAME_MAX || !c) { for (; i < 14u; i++) q->name[i] = 0; break; }
        if (c < 32u || c > 126u) q->name[i] = '?';
    }
    for (i = 0; i < PX_NBLK; i++) {
        if (q->on[i] > 1u || q->type[i] >= (uint8_t)px_ntypes((int)i)) return 0;
        for (k = 0; k < PX_NPAR; k++) if (q->p[i][k] > 100u) return 0;
    }
    if (!px_order_valid(q->order)) return 0;
    px_preset_canonical(q);                            /* v75: a RANGEMASTER, EP BOOST or MAXIMIZER in SFX (an older library file) moves to BST */
    return 1;
}

/* the sound differs from what its slot holds (the pedal's "*") */
static uint32_t ed_state_hash(void)
{
    const uint8_t *b = (const uint8_t *)&PRESET;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof PRESET; i++) h = (h ^ b[i]) * 16777619u;
    h = (h ^ cur_preset) * 16777619u;
    h = (h ^ ui_blk) * 16777619u;
    h = (h ^ (ui_fs | (uint32_t)ui_stomp << 8 | (uint32_t)ui_byp << 9)) * 16777619u;   /* v48: footswitches, STOMP, BYPASS */
    for (uint32_t i = 0; i < 4u; i++) h = (h ^ ui_gs[i]) * 16777619u;                  /* v75: their gestures */
    h = (h ^ (ui_scene_start | ui_scene << 2 | ui_scene_mode << 4)) * 16777619u;
    h = (h ^ ui_scene_revision) * 16777619u;
    return (h ^ st_gen) * 16777619u;
}
static void ed_state_frame(void)
{
    ed_begin(ED_STATE);
    ed_b(cur_preset);
    ed_b((ui_dirty() ? 1u : 0u) | (st_saved(cur_preset) ? 2u : 0u));
    ed_b(ui_blk);
    ed_preset(&PRESET);
    ed_b(ui_scene_start); ed_b(ui_scene); ed_b(ui_scene_mode);
    if (ui_scene_start)
        for (unsigned i = 0; i < PX_SCENES; i++) ed_str(ui_scene_names[i], PX_SCENE_NAME - 1u);
    ed_b(ui_scene_start ? ui_scene_count : 0u);
}
static void ed_ui_fix(void)                       /* the block page after a type change, a new preset... */
{
    if (ui_mode == M_EDIT && ui_item >= 2u + (uint32_t)px_nparams((int)ui_blk, PRESET.type[ui_blk])) ui_mode = M_BLOCK;
    if (ui_item >= 2u + (uint32_t)px_nparams((int)ui_blk, PRESET.type[ui_blk])) ui_item = 0;
}

static void ed_handle(const uint8_t *a, uint32_t n)   /* a: the args, after the command byte */
{
    uint32_t cmd = a[-1], i, k;
    int rc;
    px_preset_t q;
    ed_begin(cmd);
    switch (cmd) {
    case ED_INFO:
        ed_b(ED_PROTO); ed_str(OP_VERSION, 15); ed_str(FELUCCA_ID_PTR, 27);
        ed_b(PX_NBLK); ed_b(PX_NPAR); ed_b(ST_NSLOT); ed_b(ED_NAME_MAX);
        break;
    case ED_CAT_BLOCK:
        if (n < 1u || a[0] >= PX_NBLK) return;
        ed_b(a[0]); ed_str(px_block_label(a[0]), 15); ed_str(px_block_name(a[0]), 31); ed_b((uint32_t)px_ntypes(a[0]));
        { uint32_t c = BLK_COL[a[0]]; ed_b(c); ed_b(c >> 7); ed_b(c >> 14); }
        break;
    case ED_CAT_TYPE: {
        if (n < 2u || a[0] >= PX_NBLK || a[1] >= (uint32_t)px_ntypes(a[0])) return;
        int np = px_nparams(a[0], a[1]);
        uint32_t mask = 0;
        char t[12];
        for (k = 0; k < (uint32_t)np; k++)
            for (i = 0; i <= 100u && !(mask >> k & 1u); i++)
                if (px_param_text(a[0], a[1], (int)k, (int)i, t)) mask |= 1u << k;
        ed_b(a[0]); ed_b(a[1]); ed_str(px_type_name(a[0], a[1]), 31); ed_b((uint32_t)np); ed_b(mask);
        for (k = 0; k < (uint32_t)np; k++) ed_str(px_param_name(a[0], a[1], (int)k), 15);
        break;
    }
    case ED_TEXTS: {
        if (n < 3u || a[0] >= PX_NBLK || a[1] >= (uint32_t)px_ntypes(a[0]) || a[2] >= PX_NPAR) return;
        char t[12];
        ed_b(a[0]); ed_b(a[1]); ed_b(a[2]);
        for (i = 0; i <= 100u; i++) {
            t[8] = 0;
            ed_str(px_param_text(a[0], a[1], a[2], (int)i, t) ? t : "", 8);
        }
        break;
    }
    case ED_STATE:
        ed_state_frame();
        break;
    case ED_SET: {
        if (n < 4u || a[0] > PX_PATCH_ACTION) return;
        uint32_t b = a[0], v = a[3];
        if (b == PX_PATCH_ACTION) {
            if (a[1] || a[2]) return;
            v = PRESET.patch_volume = (uint8_t)(v > 100u ? 100u : v);
            ed_b(b); ed_b(a[1]); ed_b(a[2]); ed_b(v);
            break;
        } else if (b == PX_VALVE_ACTION) {
            if (a[1] == 0u && a[2] == 0u) v = PRESET.valve_drive = (uint8_t)(v > 100u ? 100u : v);
            else if (a[1] == 0u && a[2] == 1u) v = PRESET.valve_level = (uint8_t)(v > PX_VALVE_LEVEL_MAX ? PX_VALVE_LEVEL_MAX : v);
            else if (a[1] == 1u) v = PRESET.valve_on = v != 0u;
            else return;
            ed_b(b); ed_b(a[1]); ed_b(a[2]); ed_b(v);
            break;
        }
        if (a[1] == 0u) {
            if (a[2] >= PX_NPAR) return;
            PRESET.p[b][a[2]] = (uint8_t)(v > 100u ? 100u : v);
            v = PRESET.p[b][a[2]];
        } else if (a[1] == 1u) {
            PRESET.on[b] = (uint8_t)(v ? 1u : 0u);
            v = PRESET.on[b];
        } else if (a[1] == 2u) {
            uint32_t nt = (uint32_t)px_ntypes((int)b);
            const uint8_t was_t = PRESET.type[b];
            PRESET.type[b] = (uint8_t)(v < nt ? v : nt - 1u);
            if (b == PX_B_COMP && PRESET.type[b] == PX_CMP_VALVE) PRESET.type[b] = was_t;
            px_block_canonical((int)b, &PRESET.type[b], PRESET.p[b]);
            if (PRESET.type[b] != was_t) px_type_defaults((int)b, PRESET.type[b], PRESET.p[b]);   /* v76: a new type starts at its own settings */
            v = PRESET.type[b];
            ed_ui_fix();
        } else return;
        ed_b(b); ed_b(a[1]); ed_b(a[2]); ed_b(v);
        break;
    }
    case ED_ORDER:
        if (n < PX_NBLK) return;
        rc = px_order_valid(a) ? 0 : 1;
        if (!rc) memcpy(PRESET.order, a, PX_NBLK);
        ed_b((uint32_t)rc);
        break;
    case ED_NAME:
        for (i = 0; i < 14u; i++) q.name[i] = 0;
        for (i = 0; i < n && a[i] && i < ED_NAME_MAX; i++) q.name[i] = (char)(a[i] >= 32u && a[i] <= 126u ? a[i] : '?');
        memcpy(PRESET.name, q.name, 14);
        ed_str(PRESET.name, 14);
        break;
    case ED_PUT: {
        if (n < ED_P_SIZE) return;
        rc = ed_take_preset(a, n, &q) ? 0 : 1;
        px_scene_record scene;
        if (!rc && n > sizeof q && !ed_take_scene(&scene, a + sizeof q, n - sizeof q)) rc = 1;
        if (!rc) {
            PRESET = q;
            if (n > sizeof q) {
                if (a[14u + PX_NBLK + PX_B_PITCH] == PX_PIT_DROP) px_scene_retire_pitch(&scene);
                ui_scene_load(&scene);
            }
            ed_ui_fix();
        }
        ed_b((uint32_t)rc);
        break;
    }
    case ED_SELECT:
        if (n < 1u) return;
        rc = a[0] < ST_NSLOT ? 0 : 1;
        if (!rc) { cur_preset = a[0]; load_preset(cur_preset); ed_ui_fix(); }
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
    case ED_FOCUS:
        if (n < 1u || a[0] >= PX_NBLK) return;
        if (ui_mode == M_CHAIN || ui_mode == M_BLOCK || ui_mode == M_EDIT) {
            if (ui_blk != a[0]) { ui_blk = a[0]; ui_item = 0; }
            ui_mode = M_BLOCK;
        }
        ed_b(a[0]);
        break;
    case ED_LIST: {
        if (n < 2u) return;
        uint32_t s = a[0], c = a[1];
        if (s >= ST_NSLOT) s = ST_NSLOT;
        if (c > 26u) c = 26u;
        if (s + c > ST_NSLOT) c = ST_NSLOT - s;
        ed_b(s); ed_b(c);
        for (k = s; k < s + c; k++) { st_get(k, &q); ed_b(st_saved(k) ? 1u : 0u); ed_str(q.name, 14); }
        break;
    }
    case ED_SLOT_GET: {
        if (n < 1u || a[0] >= ST_NSLOT) return;
        st_get(a[0], &q);
        ed_b(a[0]); ed_b(st_saved(a[0]) ? 1u : 0u); ed_preset(&q);
        px_scene_record scene;
        st_scene_of(a[0], &scene);
        ed_scene_record(&scene);
        break;
    }
    case ED_SLOT_PUT: {
        if (n < 2u || a[0] >= ST_NSLOT) return;
        rc = 0;
        if (a[1] & 1u) {
            if (n < 2u + ED_P_SIZE || !ed_take_preset(a + 2, n - 2u, &q)) rc = 1;
            else {
                px_scene_record scene;
                const uint32_t base = 2u + sizeof q;
                if (!ed_take_scene(&scene, n > base ? a + base : a, n > base ? n - base : 0u)) rc = 1;
                else {
                    if (a[2u + 14u + PX_NBLK + PX_B_PITCH] == PX_PIT_DROP) px_scene_retire_pitch(&scene);
                    st_pack_preset(a[0], &q);
                    st_scene[a[0]] = scene;
                }
            }
        } else {
            memset(st_slot[a[0]], 0xFF, ST_SLOT);
            memset(&st_scene[a[0]], 0, sizeof st_scene[a[0]]);
        }
        if (!rc && (a[1] & 2u)) rc = st_commit() ? 2 : 0;
        st_gen++;
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
    }
    case ED_COMMIT:
        rc = st_commit() ? 2 : 0;
        st_gen++;
        ed_b((uint32_t)rc);
        break;
    case ED_SAVE:
        if (n < 1u || a[0] >= ST_NSLOT) return;
        rc = ui_save_patch(a[0]) ? 2 : 0;   /* v48: with its footswitches; v75: and their gestures */
        if (!rc) cur_preset = a[0];
        st_gen++;
        if (!rc) load_preset(cur_preset);
        ed_b(a[0]); ed_b((uint32_t)rc);
        break;
    case ED_WATCH:
        ed_w.on = n >= 1u && a[0] ? 1u : 0u;
        ed_b(ed_w.on);
        break;
    case ED_PING:
        ed_b(ed_load() > 127u ? 127u : ed_load()); ed_b(ed_flags());
        for (k = 0; k < 2u; k++) ed_b(ed_stat(k) > 127u ? 127u : ed_stat(k));   /* v40.1: the peaks, the late blocks, the clips */
        for (k = 2; k < 4u; k++) { ed_b(ed_stat(k)); ed_b(ed_stat(k) >> 7); }
        for (k = 4; k < ED_STATS; k++) { ed_b(ed_stat(k) >> 2); ed_b(ed_stat(k) >> 9); }   /* free bytes / 4: the stacks (BLE image: its heap, its tasks) */
#ifdef ED_PROF
        ed_b(ED_PROF);                                 /* v40.2: where the DSP time goes (ed_prof), after their count */
        for (k = 0; k < ED_PROF; k++) { ed_b(ed_prof(k)); ed_b(ed_prof(k) >> 7); }
#endif
        break;
    case ED_USB_AUDIO:                                 /* v42: the USB AUDIO setting (no argument: read), its streams */
#if OP_UAUDIO
        rc = 0;
        i = n >= 1u && a[0] ? 1u : 0u;
#if OP_UADEV
        if (n >= 1u) i = uad_set(a, n);                /* the measurement option's modes (px_uaudio.h) */
#endif
        if (n >= 1u && i != px_ua_on) {
            px_ua_on = (uint8_t)i;                     /* (px_usb.h presents the USB device anew, after this reply) */
            st_usb = (uint8_t)((st_usb & ~1u) | px_ua_on);
            rc = st_commit() ? 2 : 0;
        }
        ed_b(px_ua_on); ed_b((uint32_t)rc); ed_b(ed_ua_stat(0));
        ed_b(ED_UA_STATS);
        for (k = 1; k <= ED_UA_STATS; k++) { i = ed_ua_stat(k); ed_b(i); ed_b(i >> 7); ed_b(i >> 14); ed_b(i >> 21); }
#else
        ed_b(0); ed_b(1); ed_b(0); ed_b(0);            /* (this image has no USB audio) */
#endif
        break;
    case ED_FOOTSWITCH:                                /* v48: the current preset's footswitches, STOMP mode, BYPASS */
        if (n >= 3u) {                                 /* (each: a value to set, or 127 to leave it) */
            for (k = 0; k < 2u; k++) if (a[k] != 127u) ui_fs_put((int)k, ui_fs_valid(a[k]) ? a[k] : UI_FS_NONE);   /* (v75: a block, BYPASS, or nothing: 10 and 11 are not blocks) */
            if (a[2] != 127u) {
                ui_scene_mode = 0;
                ui_stomp = a[2] != 0u;
                if (!ui_stomp) ui_byp = 0;
            }
        }
        if (n >= 7u) for (k = 0; k < 4u; k++) if (a[3 + k] != 127u) ui_gs[k] = ui_ga_valid(a[3 + k]) ? a[3 + k] : 0xFFu;   /* v75: the gestures: an action, 15 nothing, 127 leave it */
        ed_b(ui_fs_get(0)); ed_b(ui_fs_get(1)); ed_b(ui_stomp); ed_b(ui_byp);
        for (k = 0; k < 4u; k++) ed_b(ui_ga_valid(ui_gs[k]) ? ui_gs[k] : 15u);
        break;
    case ED_SCENE: {
        rc = 0;
        if (n) {
            if (a[0] == 1u && n == 2u) {
                ui_scene_enable(a[1] != 0u);
            } else if (a[0] == 7u && n == 2u && ui_scene_start && (a[1] == 2u || a[1] == 3u)) {
                ui_scene_set_count(a[1]);
            } else if (ui_scene_start && n >= 2u && (a[0] == 6u || a[1] < ui_scene_count)) {
                if (a[0] == 2u && n == 2u) ui_scene_select(a[1]);
                else if (a[0] == 3u && n == 2u) ui_scene_default(a[1]);
                else if (a[0] == 4u && n == 2u)
                    ui_scene_copy(a[1]);
                else if (a[0] == 5u && n <= 2u + PX_SCENE_NAME - 1u) {
                    char name[PX_SCENE_NAME] = {0};
                    for (k = 2; k < n; k++)
                        name[k - 2] = a[k] >= 32u && a[k] <= 126u ? (char)a[k] : '?';
                    ui_scene_name(a[1], name);
                } else if (a[0] == 6u && n == 2u && a[1] <= 1u) {
                    ui_scene_switches(a[1]);
                } else rc = 1;
            } else rc = 1;
        }
        ed_b((unsigned)rc);
        if (!n) {
            px_scene_record scene;
            ui_scene_pack(&q, &scene);
            ed_preset(&q);
            ed_scene_record(&scene);
        }
        break;
    }
    case ED_SHED:                                      /* v75 (protocol 8): the blocks the load safeguard switched off (px_shed.h) */
        if (n >= 1u && a[0] == 1u) ed_shed_clear();    /* 1: bring them back (they fade in at the next block) */
        { const uint32_t m = ed_shed_mask(), c = ed_shed_count(); ed_b(m); ed_b(m >> 7); ed_b(c); ed_b(c >> 7); }
        break;
    case ED_GLOBAL:                                    /* v75 (protocol 9): the global settings; 11 values to set them (in RAM: COMMIT writes them with the store), none to read */
        if (n >= 11u) {
            st_cfg.tn = a[0] > 52u ? 0u : a[0];
            st_cfg.g.hpf = (uint8_t)(a[1] * 5u > 140u ? 140u : a[1] * 5u);
            for (k = 0; k < 5u; k++) st_cfg.g.eq[k] = a[2 + k] > 48u ? 24u : a[2 + k];
            st_cfg.g.lim = a[7] > 24u ? 24u : a[7]; st_cfg.g.tails = a[8] ? 1u : 0u;
            st_cfg.lights = (a[10] > 7u || (a[10] & 3u) == 3u) ? 0u : a[10];
            audio_globals(&st_cfg.g); ui_tn_ref = st_tn_hz();
        }
        ed_b(st_cfg.tn); ed_b(st_cfg.g.hpf / 5u);
        for (k = 0; k < 5u; k++) ed_b(st_cfg.g.eq[k]);
        ed_b(st_cfg.g.lim); ed_b(st_cfg.g.tails); ed_b(0); ed_b(st_cfg.lights);
        break;
    case ED_TUNER: {                                   /* v75: what the tuner hears now, for the editor's stage view: the frequency in 0.01 Hz (21 bits in three 7-bit values) and whether the pedal's tuner page is open;
                                                          1 opens the pedal's tuner page (the output is muted, as from the pedal), 0 closes it */
        if (n >= 1u) {
            if (a[0] == 1u && (ui_mode == M_CHAIN || ui_mode == M_BLOCK || ui_mode == M_EDIT || ui_mode == M_LIST)) tuner_open();
            else if (a[0] == 0u && ui_mode == M_TUNER) tuner_close();
        }
        const uint32_t f = (uint32_t)(ed_tuner_hz() * 100.0f + 0.5f);
        ed_b(f); ed_b(f >> 7); ed_b(f >> 14); ed_b(ui_mode == M_TUNER ? 1u : 0u); ed_b(st_cfg.tn);
        break;
    }
#ifdef ED_DIAG_LINES
    case ED_DIAG: {                                    /* read only: nothing here changes the pedal */
        static char l[ED_DIAG_LEN];
        ed_b(ED_DIAG_LINES);
        for (i = 0; i < ED_DIAG_LINES; i++) { ed_diag_line(i, l); ed_str(l, sizeof l - 1u); }
        break;
    }
#endif
    default:
        return;
    }
    ed_w.hash = ed_state_hash();                       /* the editor's own change: no push back */
    ed_send();
}

/* main loop, before ota_service: editor frames; anything else stays for ota_service */
static void ed_service(uint32_t now)
{
    const uint8_t *p;
    uint32_t n;
    if (ed_w.on && now - ed_w.last_ms > 5000u) ed_w.on = 0;    /* the editor went away */
    if (ed_w.on && now - ed_w.push_ms >= 40u) {
        uint32_t h = ed_state_hash();
        if (h != ed_w.hash) { ed_w.hash = h; ed_w.push_ms = now; ed_state_frame(); ed_send(); }
    }
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_H0 || p[1] != ED_H1 || p[2] != ED_H2)
        return;
    {
        static uint8_t f[4u + 2u + sizeof(px_preset_t) + sizeof(px_scene_record)]; /* SLOT_PUT with scenes */
        uint32_t k = n > sizeof f ? 0u : n, i;
        for (i = 0; i < k; i++) f[i] = p[i];
        ota_frame_done();                              /* free the frame buffer before replying */
        ed_w.last_ms = now;
        if (k) ed_handle(f + 4, k - 4u);
    }
}
