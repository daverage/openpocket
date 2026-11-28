/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — host test of the preset store (firmware/px_store.h) on a simulated NOR flash:
 * save / reload, alternate copies, a power cut at every step of a save, the bytes the update loader reads
 * (0xF00.. of each sector) kept erased, writes only inside the store.
 *   cc -O1 -Iengine tests/test_store.c engine/px_dsp.c -lm -o build/test_store && build/test_store */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "../firmware/px_midi.h"

static uint8_t nor[0x100000];
static int fails, ops, cut_at = -1, dead;          /* power cut: after cut_at flash operations nothing more happens */
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } } while (0)
static int in_store(uint32_t off, uint32_t n) { return (off >= 0xF0000u && off + n <= 0xF4000u) || (off >= 0xE8000u && off + n <= 0xEC000u); }

/* ---- the firmware pieces px_store.h uses ---- */
static uint32_t ota_crc16(const uint8_t *p, uint32_t n, uint32_t c)
{
    while (n--) { c ^= (uint32_t)*p++ << 8; for (int i = 0; i < 8; i++) c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1; c &= 0xFFFFu; }
    return c;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int fl_erase4k(uint32_t off, uint32_t *t)
{
    (void)t;
    if (!in_store(off, 0x1000) || (off & 0xFFF)) { printf("FAIL erase outside the store %x\n", off); fails++; return -1; }
    if (dead || (cut_at >= 0 && ops++ >= cut_at)) { dead = 1; return 0; }
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    if (!in_store(off, n)) { printf("FAIL write outside the store %x\n", off); fails++; return -1; }
    for (uint32_t i = 0; i < n; i++) {                 /* a cut can land in the middle of a write */
        if (dead || (cut_at >= 0 && ops++ >= cut_at)) { dead = 1; return 0; }
        nor[off + i] &= s[i];
    }
    return 0;
}
static volatile uint8_t a_mute;
static void fm1_wdt_feed(void) {}
static void fm1_delay_ms(int ms) { (void)ms; }
#define FM1_IRQ_ALNK0 11
static void fm1_irq_mask(int n) { (void)n; }
static void fm1_irq_unmask(int n) { (void)n; }
static void a_collect(void) {}
static uint8_t px_flash_ok = 1;
static uint32_t cur_preset;
static px_preset_t PRESET;
static void st_get(uint32_t k, px_preset_t *p);
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
#define ST_IRQ_OFF() ((void)0)
static px_global_t a_gl;                        /* v75: the global settings the engine takes at boot, and the tuner's A (ui.h) */
static float ui_tn_ref = 440.0f;
#include "../firmware/px_store.h"

static void reboot(void) { memset(st_slot, 0, sizeof st_slot); st_cur = st_seq = 0; st_last = 0; st_midi = 0x55; mid_cfg.chan = 9; st_usb = 0xA5; px_ua_on = 7; px_store_boot(); }
static void loader_bytes_clear(void)
{
    for (uint32_t s = 0xF0000u; s < 0xF4000u; s += 0x1000u)
        for (uint32_t i = 0xF00; i < 0xF80; i++)
            if (nor[s + i] != 0xFF) { printf("FAIL byte %x not erased (update loader record zone)\n", s + i); fails++; return; }
}

int main(void)
{
    px_preset_t p, q, f;
    memset(nor, 0xFF, sizeof nor);
    nor[0xEFFFF] = 0x11; nor[0xF4000] = 0x22;          /* neighbours: must not move */
    reboot();
    CHECK(!st_saved(5) && cur_preset == 0, "empty flash: factory presets, start on slot 1");
    px_preset_factory(&f, 5);
    st_get(5, &q);
    CHECK(!memcmp(&q, &f, sizeof q), "empty flash: slot 6 is its factory preset");

    /* save, reboot, the sound is there and the pedal starts on it */
    px_preset_factory(&p, 5);
    p.p[PX_B_SYN][0] = 99; strcpy(p.name, "MY LEAD");
    CHECK(st_save(5, &p) == 0, "save slot 6");
    reboot();
    st_get(5, &q);
    CHECK(st_saved(5) && !memcmp(&q, &p, sizeof q), "after a reboot: slot 6 holds the saved sound");
    CHECK(cur_preset == 5 && !memcmp(&PRESET, &p, sizeof p), "the pedal starts on the slot saved last");
    st_get(4, &q); px_preset_factory(&f, 4);
    CHECK(!memcmp(&q, &f, sizeof q), "other slots stay factory");

    /* many saves: the copies alternate, the newest wins; the last slot (in sector 1) works too */
    for (int i = 0; i < 25; i++) {
        p.p[PX_B_DELAY][0] = (uint8_t)i;
        px_order_default(p.order); if (i % 2) { int s = 0; while (p.order[s] != PX_B_SYN) s++; p.order[0] = PX_B_SYN; p.order[s] = PX_B_GATE; }   /* the synth first, or the default order */
        CHECK(st_save((uint32_t)(i % 2 ? ST_NSLOT - 1u : 0), &p) == 0, "repeated save");
    }
    reboot();
    st_get(0, &q); CHECK(q.p[PX_B_DELAY][0] == 24 && q.order[0] == PX_B_GATE, "slot 1: the newest of 25 saves");
    st_get(ST_NSLOT - 1u, &q);
    CHECK(q.p[PX_B_DELAY][0] == 23 && q.order[0] == PX_B_SYN && px_order_valid(q.order), "last slot (second sector): its newest save, with its block order");
    loader_bytes_clear();

    /* v48: a slot's footswitch assignment and the STOMP setting come back after a reboot; an earlier save has none */
    CHECK(st_fs_of(5) == 0xFFu, "a slot saved without footswitches: nothing assigned");
    CHECK(st_save_fs(5, &p, 0xC5u) == 0, "save slot 6 with its footswitches (left SFX, right BYPASS)");
    st_ui = 1u; CHECK(st_save_fs(5, &p, 0xC5u) == 0, "and with STOMP mode on");
    reboot();
    st_get(5, &q);
    CHECK(st_fs_of(5) == 0xC5u && !memcmp(&q, &p, sizeof q) && st_ui == 1u, "after a reboot: the footswitches, the sound and STOMP mode");
    CHECK(st_fs_of(6) == 0xFFu, "a factory slot: nothing assigned");
    st_ui = 0u;

    /* Older format 7 saves leave the Valve bytes erased and assign its controls to COMP. */
    {
        const uint32_t k = 7;
        px_preset_factory(&f, k);
        f.type[PX_B_COMP] = PX_CMP_VALVE;
        f.on[PX_B_COMP] = 1;
        f.p[PX_B_COMP][0] = 82;
        const uint8_t gestures[4] = {PX_B_COMP, PX_B_REVERB, 15u, 15u};
        st_pack_preset(k, &f);
        st_slot[k][ST_SLOT - 1u] = (uint8_t)(PX_B_REVERB << 4 | PX_B_COMP);
        memcpy(&st_slot[k][ST_GS_OFF], gestures, sizeof gestures);
        st_slot[k][ST_VALVE_OFF] = st_slot[k][ST_VALVE_OFF + 1u] = st_slot[k][ST_VALVE_OFF + 2u] = 0xFF;
        CHECK(st_commit() == 0, "write a legacy COMP Valve preset");
        reboot();
        st_get(k, &q);
        CHECK(q.valve_on && q.valve_drive == 82 && q.valve_level == PX_VALVE_LEVEL_UNITY && !q.on[PX_B_COMP] && q.type[PX_B_COMP] == PX_CMP_SQUASH,
              "older stored COMP Valve becomes the dedicated input stage");
        uint8_t migrated[4];
        st_gs_of(k, migrated);
        CHECK(st_fs_of(k) == (PX_B_REVERB << 4 | PX_VALVE_ACTION) && migrated[0] == PX_VALVE_ACTION && migrated[1] == PX_B_REVERB,
              "legacy press and gesture assignments follow Valve; other assignments stay");
        st_pack_preset(k, &q);
        st_slot[k][ST_VALVE_OFF + 2u] = 0xFF;
        st_get(k, &q);
        CHECK(q.valve_on && q.valve_drive == 82 && q.valve_level == PX_VALVE_LEVEL_UNITY,
              "earlier dedicated Valve presets retain their Drive and default to unity Level");
        st_reset(k);
    }

    /* v75: the global settings (st_cfg): defaults when the store never had them, kept across a save and a reboot, damaged -> the defaults */
    {
        reboot();
        CHECK(st_cfg.g.hpf == 0 && st_cfg.g.lim == 0 && st_cfg.g.tails == 1 && st_cfg.tn == 0 && st_cfg.g.eq[0] == 24 && st_cfg.g.eq[4] == 24 && ui_tn_ref == 440.0f,
              "global settings: a store that never had them reads the defaults (tails on)");
        st_cfg.g.hpf = 60; st_cfg.g.eq[2] = 30; st_cfg.g.eq[4] = 10; st_cfg.g.lim = 6; st_cfg.g.tails = 0; st_cfg.tn = 29;
        px_preset_t gp; px_preset_factory(&gp, 2);
        CHECK(st_save(2, &gp) == 0, "global settings: saved with a preset");
        reboot();
        CHECK(st_cfg.g.hpf == 60 && st_cfg.g.eq[2] == 30 && st_cfg.g.eq[4] == 10 && st_cfg.g.lim == 6 && st_cfg.g.tails == 0 && st_cfg.tn == 29 &&
              a_gl.hpf == 60 && ui_tn_ref == 443.0f, "global settings: kept after a reboot, the engine's copy and the tuner's A (443) set");
        /* a torn or damaged block reads as the defaults, the presets stay */
        nor[st_cur + ST_CFG_OFF + 3] ^= 0x04u;
        reboot();
        CHECK(st_cfg.g.hpf == 0 && st_cfg.tn == 0 && st_saved(2), "global settings: a damaged block reads the defaults, the presets are kept");
        st_cfg.g.hpf = 25; CHECK(st_save(2, &gp) == 0, "(saved again)"); reboot();
        CHECK(st_cfg.g.hpf == 25, "global settings: a save writes a good block again");
        st_cfg.g.hpf = 0; st_cfg.tn = 0; st_cfg.g.eq[2] = 24; st_cfg.g.eq[4] = 24; st_cfg.g.lim = 0; st_cfg.g.tails = 1; st_save(2, &gp); st_reset(2);
        reboot();
    }

    /* Current format transition preserves patch levels and all assignment bytes. */
    {
        px_preset_t old, converted;
        px_preset_factory(&old, 3); old.p[PX_B_GATE][2] = 63;
        old.type[PX_B_GATE] = PX_GATE_SUPPRESSOR;
        const uint8_t gs[4] = {1, 2, 9, 12};
        CHECK(st_save_fsg(3, &old, 0xC9, gs) == 0, "transition source saved");
        px_preset_t hum_only = old; hum_only.on[PX_B_GATE] = 0;
        hum_only.type[PX_B_EQ] = 3; hum_only.on[PX_B_EQ] = 1;
        hum_only.p[PX_B_EQ][0] = 50; hum_only.p[PX_B_GATE][2] = 77;
        const uint8_t hum_gs[4] = {PX_B_EQ, 15, 15, 15};
        CHECK(st_save_fsg(4, &hum_only, 0xF5, hum_gs) == 0, "hum-only transition source saved");
        st_hdr_t hdr; memcpy(&hdr, nor + st_cur, sizeof hdr);
        hdr.ver = 7; memcpy(nor + st_cur, &hdr, sizeof hdr);
        static uint8_t source[0x4000]; memcpy(source, nor + ST_A, sizeof source);
        for (int cut = 0; cut < 6000; cut += 53) {
            memcpy(nor + ST_A, source, sizeof source);
            cut_at = cut; dead = ops = 0; reboot();
            cut_at = -1; dead = 0; reboot(); st_get(3, &converted);
            CHECK(st_saved(3) && converted.patch_volume == 63 && st_fs_of(3) == 0xC9, "power cut during transition retains patch and level");
        }
        memcpy(nor + ST_A, source, sizeof source);
        reboot(); st_get(3, &converted);
        px_preset_t migrated_hum; st_get(4, &migrated_hum);
        CHECK(migrated_hum.patch_volume == 77 && migrated_hum.p[PX_B_GATE][0] == 0 && migrated_hum.p[PX_B_GATE][2] == 78 && !migrated_hum.on[PX_B_EQ], "hum-only patch retains its level without enabling broadband gating");
        uint8_t hg[4]; st_gs_of(4, hg);
        CHECK(st_fs_of(4) == 0xF0 && hg[0] == PX_B_GATE, "HUM assignments follow integrated processing");
        CHECK(converted.patch_volume == 63 && converted.p[PX_B_GATE][2] == 0, "transition moves volume out of gate");
        CHECK(!strcmp(old.name, converted.name) && !memcmp(old.order, converted.order, PX_NBLK), "transition preserves name and order");
        uint8_t got[4]; st_gs_of(3, got);
        CHECK(st_fs_of(3) == 0xC9 && !memcmp(got, gs, 4), "transition preserves assignments");
        memcpy(&hdr, nor + st_cur, sizeof hdr);
        CHECK(hdr.ver == 8, "transition committed once to new format");
        CHECK(converted.p[PX_B_GATE][0] == 61 && converted.p[PX_B_GATE][1] == 8, "transition preserves nominal Smart Gate threshold and release scales");
        reboot(); st_get(3, &q);
        CHECK(!memcmp(&converted, &q, sizeof q), "transition is not repeated");
    }

    loader_bytes_clear();

    /* the MIDI settings ride in the header of the copy: saved with it, back after a power cycle */
    {
        px_preset_t q;
        px_preset_factory(&q, 4);
        mid_cfg.chan = 7; mid_cfg.send = 0; mid_cfg.clock = 1;
        st_midi = px_midi_cfg_pack(&mid_cfg);
        CHECK(st_commit() == 0, "settings: a commit without a new sound");
        reboot();
        CHECK(mid_cfg.chan == 7 && !mid_cfg.send && mid_cfg.clock, "settings: channel 7, send off, clock on survive a power cycle");
        CHECK(st_save(4, &q) == 0, "settings: a preset save");
        reboot();
        CHECK(mid_cfg.chan == 7 && !mid_cfg.send && mid_cfg.clock, "settings: still there after a preset save (the header is rewritten with them)");
        {   /* a store written before the settings existed has zero there: the defaults, not the last boot's values */
            st_hdr_t h;
            uint32_t base = st_cur;
            memcpy(&h, nor + base, sizeof h);
            h.pad[0] = 0;
            memcpy(nor + base, &h, sizeof h);
            reboot();
            CHECK(mid_cfg.chan == 0 && mid_cfg.send && !mid_cfg.clock, "settings: a header with 0 there (every earlier OpenPocket) = omni, send on, clock off");
        }
        {   /* a power cut during the commit of settings: the old settings or the new, the presets intact */
            uint32_t olds = 0, news = 0, bad = 0;
            for (int cut = 0; cut < 9000; cut += 11) {
                uint8_t keep[0x100000];
                memcpy(keep, nor, sizeof nor);
                mid_cfg.chan = 12; mid_cfg.send = 1; mid_cfg.clock = 0;
                st_midi = px_midi_cfg_pack(&mid_cfg);
                ops = 0; dead = 0; cut_at = cut;
                st_commit();
                cut_at = -1; dead = 0;
                reboot();
                if (mid_cfg.chan == 12) news++; else if (mid_cfg.chan == 0) olds++; else bad++;
                st_get(4, &q);
                if (!st_saved(4)) bad++;
                memcpy(nor, keep, sizeof nor);
            }
            CHECK(bad == 0 && olds > 0 && news > 0, "settings: a power cut during a settings save leaves the old or the new, never a broken store");
        }
    }
    /* v42: the USB AUDIO setting rides in the header too (its second spare byte) */
    {
        px_preset_t q;
        px_preset_factory(&q, 5);
        reboot();
        CHECK(!px_ua_on && !st_usb, "USB audio: off in a store that never set it (every earlier OpenPocket wrote 0 there)");
        mid_cfg.chan = 3; st_midi = px_midi_cfg_pack(&mid_cfg);
        st_usb = 1;
        CHECK(st_commit() == 0, "USB audio: a commit of the setting");
        reboot();
        CHECK(px_ua_on == 1 && st_usb == 1, "USB audio: on survives a power cycle");
        CHECK(mid_cfg.chan == 3, "USB audio: the MIDI settings beside it are kept");
        CHECK(st_save(5, &q) == 0, "USB audio: a preset save");
        reboot();
        CHECK(px_ua_on == 1 && st_saved(5), "USB audio: still on after a preset save");
        {   /* only bit 0 is the setting */
            st_hdr_t h;
            memcpy(&h, nor + st_cur, sizeof h);
            h.pad[1] = 0xFE;
            memcpy(nor + st_cur, &h, sizeof h);
            reboot();
            CHECK(px_ua_on == 0, "USB audio: the byte's other bits do not switch it on");
        }
        st_usb = 0;
        CHECK(st_commit() == 0, "USB audio: a commit of off");
        reboot();
        CHECK(!px_ua_on && !st_usb, "USB audio: off survives a power cycle");
    }
    {
        const unsigned slot = 51;
        px_preset_t patch;
        px_preset_factory(&patch, slot);
        px_scene_record first = {0}, next, got;
        first.start = PX_SCENE_TWO | 1u;
        memcpy(first.name[1], "Rhythm", 7);
        for (unsigned i = 0; i < PX_SCENES - 1u; i++) px_scene_capture(&first.other[i], &patch);
        first.other[0].volume = 12;
        CHECK(st_save_scene(slot, &patch, 0xFFu, 0, &first) == 0, "scene bank saves");
        reboot();
        st_scene_of(slot, &got);
        CHECK(!memcmp(&first, &got, sizeof got), "two-scene bank and hidden C survive reboot");
        next = first;
        next.start = 3;
        next.other[0].volume = 78;
        static uint8_t snapshot[sizeof nor];
        memcpy(snapshot, nor, sizeof nor);
        for (int cut = 0; cut < 13800; cut += 47) {
            cut_at = cut;
            dead = ops = 0;
            const int rc = st_save_scene(slot, &patch, 0xFFu, 0, &next);
            if (rc) {
                st_scene_of(slot, &got);
                CHECK(!memcmp(&first, &got, sizeof got), "failed scene save rolls back the in-memory slot");
            }
            cut_at = -1;
            reboot();
            st_scene_of(slot, &got);
            CHECK(!memcmp(&first, &got, sizeof got) || !memcmp(&next, &got, sizeof got),
                  "power loss leaves one complete scene bank");
            memcpy(nor, snapshot, sizeof nor);
            reboot();
        }
        for (uint32_t sector = ST_SCENE_A; sector < ST_SCENE_B + 0x2000u; sector += 0x1000u)
            for (uint32_t offset = 0xF00; offset < 0xF80; offset++)
                CHECK(nor[sector + offset] == 0xFF, "scene sectors keep the loader record area erased");
        CHECK(st_reset(slot) == 0, "factory reset clears the scene bank");
        reboot();
        st_scene_of(slot, &got);
        CHECK(!got.start && !st_saved(slot), "factory patch returns without scenes");
    }
    loader_bytes_clear();
    printf(fails ? "%d FAILED\n" : "store tests passed\n", fails);
    return fails != 0;
}
