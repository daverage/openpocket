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
static int in_store(uint32_t off, uint32_t n) { return off >= 0xF0000u && off + n <= 0xF4000u; }

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
    p.p[PX_B_AMP][0] = 99; strcpy(p.name, "MY LEAD");
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
        p.order[0] = (uint8_t)(i % 2 ? PX_B_SYN : PX_B_GATE); p.order[4] = (uint8_t)(i % 2 ? PX_B_GATE : PX_B_SYN);
        CHECK(st_save((uint32_t)(i % 2 ? ST_NSLOT - 1u : 0), &p) == 0, "repeated save");
    }
    reboot();
    st_get(0, &q); CHECK(q.p[PX_B_DELAY][0] == 24 && q.order[0] == PX_B_GATE, "slot 1: the newest of 25 saves");
    st_get(ST_NSLOT - 1u, &q);
    CHECK(q.p[PX_B_DELAY][0] == 23 && q.order[0] == PX_B_SYN && px_order_valid(q.order), "last slot (second sector): its newest save, with its block order");
    loader_bytes_clear();

    /* v48: a slot's footswitch assignment and the STOMP setting come back after a reboot; an earlier save has none */
    CHECK(st_fs_of(5) == 0xFFu, "a slot saved without footswitches: nothing assigned");
    CHECK(st_save_fs(5, &p, 0xC5u) == 0, "save slot 6 with its footswitches (left AMP, right BYPASS)");
    st_ui = 1u; CHECK(st_save_fs(5, &p, 0xC5u) == 0, "and with STOMP mode on");
    reboot();
    st_get(5, &q);
    CHECK(st_fs_of(5) == 0xC5u && !memcmp(&q, &p, sizeof q) && st_ui == 1u, "after a reboot: the footswitches, the sound and STOMP mode");
    CHECK(st_fs_of(6) == 0xFFu, "a factory slot: nothing assigned");
    st_ui = 0u;

    /* factory reset of a slot */
    CHECK(st_reset(ST_NSLOT - 1u) == 0, "restore the last slot to factory");
    reboot();
    st_get(ST_NSLOT - 1u, &q); px_preset_factory(&f, (int)ST_NSLOT - 1);
    CHECK(!st_saved(ST_NSLOT - 1u) && !memcmp(&q, &f, sizeof q), "the last slot is factory again after a reboot");

    /* a power cut at every step of a save: after it, the store is the old one or the new one, never broken */
    {
        static uint8_t snap[0x4000];
        px_preset_t before, newp;
        st_get(7, &before);
        newp = before; newp.p[PX_B_REVERB][2] = 77; strcpy(newp.name, "CUT TEST");
        memcpy(snap, nor + 0xF0000, sizeof snap);
        uint32_t seq0 = st_seq, cur0 = st_cur;
        int olds = 0, news = 0, bad = 0;
        for (int cut = 0; cut < 9000; cut += 7) {
            memcpy(nor + 0xF0000, snap, sizeof snap);
            st_seq = seq0; st_cur = cur0;
            reboot();
            ops = 0; cut_at = cut; dead = 0;
            st_save(7, &newp);
            cut_at = -1; dead = 0;
            reboot();
            st_get(7, &q);
            if (!memcmp(&q, &newp, sizeof q)) news++;
            else if (!memcmp(&q, &before, sizeof q)) olds++;
            else bad++;
            st_get(0, &q);
            if (q.p[PX_B_DELAY][0] != 24) bad++;           /* other slots intact */
        }
        printf("power cut at %d points of a save: old kept %d, new %d, broken %d\n", (9000 + 6) / 7, olds, news, bad);
        CHECK(bad == 0 && olds > 0 && news > 0, "a power cut never breaks the store");
    }
    CHECK(nor[0xEFFFF] == 0x11 && nor[0xF4000] == 0x22, "flash outside the store untouched");

    /* v70: a store written by the multi-effects firmware (format 5): its settings stay, its presets do not (every slot
     * its placeholder); the first save writes format 6 and from then on the presets survive updates */
    {
        memset(nor + 0xF0000, 0xFF, 0x4000);
        reboot();
        px_preset_factory(&q, 5);
        strcpy(q.name, "OLD AMP SOUND");
        st_midi = 0x15;                                        /* (some MIDI settings) */
        CHECK(st_save(5, &q) == 0, "format 5 test store written");
        st_hdr_t h;
        uint32_t base = st_cur;
        memcpy(&h, nor + base, sizeof h);
        h.ver = 5;
        memcpy(nor + base, &h, sizeof h);
        reboot();
        st_get(5, &f); px_preset_factory(&q, 5);
        CHECK(!st_saved(5) && !memcmp(&q, &f, sizeof q) && cur_preset == 0, "format 5 store: the slot is its placeholder, the pedal starts on 1");
        CHECK(st_midi == 0x15, "format 5 store: the settings are kept");
        strcpy(q.name, "MY SYNTH");
        CHECK(st_save(9, &q) == 0, "the first save after it");
        reboot();
        st_get(9, &f);
        CHECK(st_saved(9) && !strcmp(f.name, "MY SYNTH") && !st_saved(5), "after the first save: format 6, the new preset there, the old one gone");
        memcpy(&h, nor + st_cur, sizeof h);
        CHECK(h.ver == ST_VER, "the store is format 6 now");
        loader_bytes_clear();
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
    loader_bytes_clear();
    printf(fails ? "%d FAILED\n" : "store tests passed\n", fails);
    return fails != 0;
}
