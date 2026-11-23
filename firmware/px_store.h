/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the user's presets in flash.
 *
 * 52 slots of 112 bytes (px_preset_t, 94 B, + a "saved" mark), kept in RAM (st_slot) and written as one whole copy
 * on each save, alternately to copy A [0xF0000, 0xF2000) and copy B [0xF2000, 0xF4000), each two 4 KiB sectors:
 *   sector 0: header (16 B) + slots 0..33        sector 1: slots 34..51
 * Format 7 (v75: the BST block, a tenth block: the preset grew from 86 to 94 bytes, the slot stays 112). A format 6 copy (v70..v74, nine
 * blocks) is converted in RAM when it is read (st_migrate6: the BST block is added off, after the gate in the order, and a RANGEMASTER, EP BOOST
 * or MAXIMIZER in the SFX block moves there) and written as format 7 by the next save. The multi-effects firmware's presets (formats 2..5, same
 * slot size) are not converted: such a copy gives its settings (MIDI, USB AUDIO, STOMP mode) and every slot starts as its placeholder
 * (px_preset_factory). The first save writes format 7; later updates keep the presets.
 * The header goes last, with a sequence number and a CRC16 of every slot: at boot the valid copy with the highest
 * sequence wins, so a power cut during a save leaves the previous copy. Bytes 0xF00..0xF7F of each sector stay
 * erased: the update loader treats a valid record there as an update record (loader/ldr_core.c).
 * The update loader writes only the app area [0x4000, 0xA3000): presets survive firmware updates.
 * During a write the audio is muted (the output buffers are zeros), the audio IRQ held and the second core idle
 * (its loop runs from RAM): the flash cannot be read through XIP while it is being written.
 */
#pragma once

#define ST_A       0xF0000u
#define ST_B       0xF2000u
#define ST_NSLOT   52u
#define ST_SLOT    112u
#define ST_S0N     34u                          /* slots in sector 0: 16 + 34 * 112 = 0xEF0 < 0xF00 */
#define ST_VER     7u          /* 7: v75 (ten blocks); 6: v70..v74 OpenPocket Synth (nine blocks, converted); 2..5: the multi-effects firmware (settings kept, presets not) */
#define ST_V6_NB   9u          /* a format 6 preset: nine blocks: the name (14), on, type, 5 settings each, then the order: 14 + 9 * (1 + 1 + 5 + 1) = 86 bytes, its mark at 86 */
#define ST_V6_SIZE (14u + ST_V6_NB * (3u + PX_NPAR))
#define ST_VER_MIN 2u
typedef char st_size_check[(sizeof(px_preset_t) < ST_SLOT && ST_NSLOT == 52u) ? 1 : -1];
#define ST_MAGIC   0x5350504Fu                  /* "OPPS" */
#define ST_MARK    0x5Au                        /* slot holds a saved preset (else: the factory one) */
#ifndef ST_IRQ_OFF
#define ST_IRQ_OFF() __asm__ volatile("cli" ::: "memory")
#endif

/* pad[0]: the MIDI settings (px_midi.h, px_midi_cfg_pack); 0 = the defaults, which is what every earlier save wrote.
 * pad[1] (v42): bit 0 = USB AUDIO on (px_midi.h, px_ua_on); the other bits 0. 0 = off, as every earlier save wrote.
 * pad[2] (v48): bit 0 = STOMP mode (firmware/ui.h); 0 = PATCH mode, as every earlier save wrote.
 * v48: each slot's last byte (ST_SLOT - 1, after the preset and its mark) holds the footswitches' assignment in
 * STOMP mode (firmware/ui.h, ui_fs): low 4 bits the left footswitch, high 4 the right; 0..9 a block (v75; 0..8 before), 12 BYPASS,
 * 15 nothing (st_migrate6 clears the 9..11 that format 6 let through). Earlier saves left it 0xFF (erased): nothing assigned. The format is unchanged. */
typedef struct { uint32_t magic, seq; uint16_t crc; uint8_t ver, nslot, last, pad[3]; } st_hdr_t;   /* 16 B */
static uint8_t st_slot[ST_NSLOT][ST_SLOT] __attribute__((aligned(4)));
static uint32_t st_seq, st_cur;                 /* sequence of the copy in use, its base (0: none yet) */
static uint8_t st_ok;                           /* the store can be written (the flash is the expected part) */
static uint8_t st_last;                         /* the slot to start on (the last one saved) */
static uint8_t st_midi;                         /* the MIDI settings byte (header pad[0]) */
static uint8_t st_usb;                          /* the USB settings byte (header pad[1]) */
static uint8_t st_ui;                           /* v48: the pedal settings byte (header pad[2]): bit 0 STOMP */

/* v75: the global settings (px_global_t, and the tuner's reference A): 16 bytes in sector 0's spare gap after its slots (0xEF0 .. 0xEFF, which no format
 * has used: erased, so an older store reads as "the defaults"), written with every copy, so they travel with the presets and a power cut keeps
 * the old or the new. Not in the header's CRC (which covers the slots); the block has its own check byte.
 *   [0] 'G'  [1] 1 (version)  [2] tuner A: 0 = 440, else 414 + n Hz  [3] input high-pass Hz / 5  [4..8] output EQ, half-dB steps, 24 flat  [9] limiter  [10] tails
 *   [11] hush  [12] lights (0xFF: 0)  [13..14] 0xFF  [15] check: the XOR of [0..14] and 0xA5 */
#define ST_CFG_OFF 0xEF0u
typedef struct { px_global_t g; uint8_t tn, lights; } st_cfg_t;   /* lights: bits 0..1 the tuner's lights (0 blink, 1 needle, 2 strobe), bit 2 lights off (px_lights.h) */
static st_cfg_t st_cfg;
typedef char st_cfg_gap_check[(16u + ST_S0N * ST_SLOT == ST_CFG_OFF && ST_CFG_OFF + 16u <= 0xF00u) ? 1 : -1];
static void st_cfg_defaults(void) { px_global_default(&st_cfg.g); st_cfg.g.tails = 1; st_cfg.tn = 0; st_cfg.lights = 1; }   /* (the tuner's lights: the needle, so they move: v76) */   /* (tails on unless the player turns them off) */
static void st_cfg_pack(uint8_t *r)
{
    memset(r, 0xFF, 16);
    r[0] = 'G'; r[1] = 1; r[2] = st_cfg.tn; r[3] = (uint8_t)(st_cfg.g.hpf / 5u);
    for (uint32_t i = 0; i < 5u; i++) r[4 + i] = st_cfg.g.eq[i];
    r[9] = st_cfg.g.lim; r[10] = st_cfg.g.tails; r[11] = st_cfg.g.hush; r[12] = st_cfg.lights;
    uint8_t c = 0xA5u; for (uint32_t i = 0; i < 15u; i++) c ^= r[i];
    r[15] = c;
}
static void st_cfg_unpack(const uint8_t *r)     /* a block that is not ours, or damaged: the defaults */
{
    uint8_t c = 0xA5u; for (uint32_t i = 0; i < 15u; i++) c ^= r[i];
    st_cfg_defaults();
    if (r[0] != 'G' || r[1] != 1u || r[15] != c) return;
    px_global_t g; px_global_default(&g);
    g.hpf = (uint8_t)(r[3] * 5u); for (uint32_t i = 0; i < 5u; i++) g.eq[i] = r[4 + i] > 48u ? 24u : r[4 + i];
    g.lim = r[9] > 24u ? 0u : r[9]; g.tails = r[10] ? 1u : 0u; g.hush = r[11] > 100u ? 0u : r[11];
    if (g.hpf > 140u) g.hpf = 0;
    st_cfg.g = g; st_cfg.tn = r[2] > 52u ? 0u : r[2]; st_cfg.lights = (r[12] == 0xFFu || (r[12] & 3u) == 3u || r[12] > 7u) ? 1u : r[12];
}
static float st_tn_hz(void) { return st_cfg.tn ? 414.0f + (float)st_cfg.tn : 440.0f; }

static uint32_t st_slot_off(uint32_t base, uint32_t k)
{
    return k < ST_S0N ? base + 16u + k * ST_SLOT : base + 0x1000u + (k - ST_S0N) * ST_SLOT;
}
static uint32_t st_crc(void)
{
    uint32_t k, c = 0;
    for (k = 0; k < ST_NSLOT; k++)
        c = ota_crc16(st_slot[k], ST_SLOT, c);
    return c;
}
/* reads copy `base` (format h->ver) into st_slot; returns its CRC as stored on flash */
static uint32_t st_read_copy(uint32_t base, const st_hdr_t *h)
{
    uint32_t k;
    (void)h;
    for (k = 0; k < ST_NSLOT; k++)
        if (ota_fread(st_slot_off(base, k), st_slot[k], ST_SLOT)) return ~0u;
    return st_crc();
}

/* v75: format 6 slots (nine blocks, 86 bytes + the mark) -> the ten-block preset, in place; the footswitch byte at the end of the slot is kept */
static void st_migrate6(void)
{
    const uint32_t o_on = 14u, o_ty = o_on + ST_V6_NB, o_p = o_ty + ST_V6_NB, o_ord = o_p + ST_V6_NB * PX_NPAR;   /* the format 6 layout */
    for (uint32_t k = 0; k < ST_NSLOT; k++) {
        uint8_t *s = st_slot[k], o[ST_V6_SIZE];
        px_preset_t q;
        if (s[ST_V6_SIZE] != ST_MARK) continue;
        memcpy(o, s, ST_V6_SIZE);
        memset(&q, 0, sizeof q);
        memcpy(q.name, o, 14);
        for (uint32_t b = 0; b < ST_V6_NB; b++) {
            q.on[b] = o[o_on + b]; q.type[b] = o[o_ty + b];
            for (uint32_t i = 0; i < PX_NPAR; i++) q.p[b][i] = o[o_p + b * PX_NPAR + i];
        }
        q.type[PX_B_BST] = PX_BST_RANGEMASTER; q.p[PX_B_BST][0] = 50;
        unsigned seen = 0, ok = 1, n = 0;
        for (uint32_t i = 0; i < ST_V6_NB; i++) { if (o[o_ord + i] >= ST_V6_NB || (seen >> o[o_ord + i] & 1u)) ok = 0; else seen |= 1u << o[o_ord + i]; }
        if (ok) for (uint32_t i = 0; i < ST_V6_NB; i++) { q.order[n++] = o[o_ord + i]; if (o[o_ord + i] == PX_B_GATE) q.order[n++] = PX_B_BST; }
        else px_order_default(q.order);
        px_preset_canonical(&q);
        {   /* the footswitch byte: format 6 accepted 9..11 as a block, which are not blocks (9 is BST now): those nibbles become "nothing" */
            uint8_t f = s[ST_SLOT - 1u], l = f & 15u, r = f >> 4;
            if (l >= ST_V6_NB && l != 12u) l = 15u;
            if (r >= ST_V6_NB && r != 12u) r = 15u;
            memset(s, 0xFF, ST_SLOT - 1u);
            s[ST_SLOT - 1u] = (uint8_t)(l | r << 4);
        }
        memcpy(s, &q, PX_PRESET_BASE_SIZE);
        s[PX_PRESET_BASE_SIZE] = ST_MARK;
    }
}

/* boot: the newest valid copy into st_slot (call with the flash readable; it may turn interrupts on) */
static void st_load(void)
{
    static const uint32_t BASE[2] = {ST_A, ST_B};
    uint32_t i, k, best = 0, bseq = 0;
    st_hdr_t h;
    memset(st_slot, 0xFF, sizeof st_slot);
    st_hdr_t bh;
    memset(&bh, 0, sizeof bh);
    for (i = 0; i < 2u; i++) {
        if (ota_fread(BASE[i], &h, sizeof h) || h.magic != ST_MAGIC) continue;
        if (h.ver < ST_VER_MIN || h.ver > ST_VER || h.nslot != ST_NSLOT) continue;
        if (st_read_copy(BASE[i], &h) == h.crc && (!best || (int32_t)(h.seq - bseq) > 0)) {
            best = BASE[i];
            bseq = h.seq;
            st_last = h.last;
            st_midi = h.pad[0];
            st_usb = h.pad[1]; st_ui = h.pad[2];
            bh = h;
        }
    }
    uint8_t cfgraw[16];                           /* v75: the global settings of the winning copy */
    memset(cfgraw, 0xFF, sizeof cfgraw);
    if (best) ota_fread(best + ST_CFG_OFF, cfgraw, sizeof cfgraw);
    st_cfg_unpack(cfgraw);
    (void)k;
    if (best) {                                   /* read the winner again (the loop left the other in st_slot) */
        st_read_copy(best, &bh);
        st_cur = best;
        st_seq = bseq;
    } else {
        memset(st_slot, 0xFF, sizeof st_slot);
        st_last = 0;
        st_midi = 0;
        st_usb = 0; st_ui = 0;
    }
    if (st_last >= ST_NSLOT) st_last = 0;
    if (best && bh.ver == 6u) st_migrate6();
    else if (best && bh.ver < ST_VER) {            /* the multi-effects firmware's presets: placeholders instead */
        memset(st_slot, 0xFF, sizeof st_slot);
        st_last = 0;
    }
}

#define ST_VALVE_OFF (PX_PRESET_BASE_SIZE + 5u)
static int st_saved(uint32_t k) { return k < ST_NSLOT && st_slot[k][PX_PRESET_BASE_SIZE] == ST_MARK; }

/* the preset in slot k: the saved one, else the factory one */
static void st_get(uint32_t k, px_preset_t *p)
{
    if (st_saved(k)) {
        memcpy(p, st_slot[k], PX_PRESET_BASE_SIZE);
        p->valve_on = st_slot[k][ST_VALVE_OFF] == 1u;
        p->valve_drive = st_slot[k][ST_VALVE_OFF + 1u] <= 100u ? st_slot[k][ST_VALVE_OFF + 1u] : PX_VALVE_DEFAULT;
        if (!px_order_valid(p->order)) px_order_default(p->order);
    }
    else px_preset_factory(p, (int)k);
    px_preset_canonical(p);                            /* v64: every block's retired types; v75: the pedals that moved to BST */
}

/* audio off the flash: muted output, IRQ held, the second core between jobs */
static void st_audio_hold(int on)
{
    if (on) {
        a_mute = 1;
        for (int i = 0; i < 12; i++) { fm1_wdt_feed(); fm1_delay_ms(1); }   /* > 2 blocks: the DMA plays zeros */
        fm1_irq_mask(FM1_IRQ_ALNK0);
        a_collect();
    } else {
        fm1_irq_unmask(FM1_IRQ_ALNK0);
        a_mute = 0;
    }
}

/* write the whole store as a new copy (the one not in use); 0 = ok */
static int st_commit(void)
{
    uint32_t base = st_cur == ST_A ? ST_B : ST_A, k;
    st_hdr_t h;
    int rc = 0;
    if (!st_ok) return -1;
    memset(&h, 0, sizeof h);
    h.magic = ST_MAGIC; h.seq = st_seq + 1u; h.crc = (uint16_t)st_crc(); h.ver = ST_VER; h.nslot = ST_NSLOT; h.last = st_last; h.pad[0] = st_midi; h.pad[1] = st_usb; h.pad[2] = st_ui;
    st_audio_hold(1);
    if (fl_erase4k(base, &k) || fl_erase4k(base + 0x1000u, &k)) rc = -2;
    for (k = ST_S0N; !rc && k < ST_NSLOT; k++)
        if (fl_write(st_slot_off(base, k), st_slot[k], ST_SLOT)) rc = -3;
    if (!rc && fl_write(base + 16u, st_slot[0], ST_S0N * ST_SLOT)) rc = -3;   /* sector 0's slots: st_slot's rows are contiguous */
    uint8_t cfgraw[16];
    st_cfg_pack(cfgraw);
    if (!rc && fl_write(base + ST_CFG_OFF, cfgraw, sizeof cfgraw)) rc = -3;  /* v75: the global settings, before the header */
    if (!rc && fl_write(base, (const uint8_t *)&h, sizeof h)) rc = -4;      /* header last */
    if (!rc) {                                    /* read back */
        st_hdr_t r;
        static uint8_t chk[ST_SLOT];
        if (ota_fread(base, &r, sizeof r) || memcmp(&r, &h, sizeof h)) rc = -5;
        uint8_t cr[16]; if (!rc && (ota_fread(base + ST_CFG_OFF, cr, sizeof cr) || memcmp(cr, cfgraw, sizeof cr))) rc = -5;
        for (k = 0; !rc && k < ST_NSLOT; k++)
            if (ota_fread(st_slot_off(base, k), chk, ST_SLOT) || memcmp(chk, st_slot[k], ST_SLOT)) rc = -5;
    }
    st_audio_hold(0);
    if (!rc) { st_cur = base; st_seq = h.seq; }
    return rc;
}

/* v48: slot k's footswitch assignment (0xFF: nothing, also for a slot never saved) */
static int st_legacy_valve(uint32_t k)
{
    return st_saved(k) && st_slot[k][14u + PX_NBLK + PX_B_COMP] == PX_CMP_VALVE;
}
static uint8_t st_fs_of(uint32_t k)
{
    if (!st_saved(k)) return 0xFFu;
    uint8_t fs = st_slot[k][ST_SLOT - 1u];
    if (st_legacy_valve(k)) {
        if ((fs & 15u) == PX_B_COMP) fs = (uint8_t)((fs & 0xF0u) | PX_VALVE_ACTION);
        if ((fs >> 4) == PX_B_COMP) fs = (uint8_t)((fs & 15u) | (PX_VALVE_ACTION << 4));
    }
    return fs;
}
typedef char st_fs_check[(sizeof(px_preset_t) + 1u < ST_SLOT) ? 1 : -1];   /* the mark, then this byte */

/* v75: slot k's footswitch gestures: bytes 95..98 of the slot (left double tap, left hold, right double tap, right hold: an action code, 0xFF nothing), in
 * the 15 bytes the slot kept free between the preset's mark and the footswitch byte */
#define ST_GS_OFF (PX_PRESET_BASE_SIZE + 1u)
typedef char st_gs_check[(ST_VALVE_OFF + 2u <= ST_SLOT - 1u) ? 1 : -1];
static void st_gs_of(uint32_t k, uint8_t *out)
{
    int old_valve = st_legacy_valve(k);
    for (uint32_t i = 0; i < 4u; i++) {
        uint8_t action = st_saved(k) ? st_slot[k][ST_GS_OFF + i] : 0xFFu;
        out[i] = old_valve && action == PX_B_COMP ? PX_VALVE_ACTION : action;
    }
}
static void st_pack_preset(uint32_t k, const px_preset_t *p)
{
    memset(st_slot[k], 0xFF, ST_SLOT);
    memcpy(st_slot[k], p, PX_PRESET_BASE_SIZE);
    st_slot[k][PX_PRESET_BASE_SIZE] = ST_MARK;
    st_slot[k][ST_VALVE_OFF] = p->valve_on;
    st_slot[k][ST_VALVE_OFF + 1u] = p->valve_drive;
}
/* save p, with its footswitch assignment fs and gestures gs (4 bytes, or 0: none), to slot k (becomes the start slot) */
static int st_save_fsg(uint32_t k, const px_preset_t *p, uint8_t fs, const uint8_t *gs)
{
    if (k >= ST_NSLOT) return -1;
    st_pack_preset(k, p);
    if (gs) memcpy(&st_slot[k][ST_GS_OFF], gs, 4);
    st_slot[k][ST_SLOT - 1u] = fs;
    st_last = (uint8_t)k;
    return st_commit();
}
static int st_save_fs(uint32_t k, const px_preset_t *p, uint8_t fs) { return st_save_fsg(k, p, fs, 0); }
static int st_save(uint32_t k, const px_preset_t *p) { return st_save_fs(k, p, 0xFFu); }

/* slot k back to its factory preset */
static int st_reset(uint32_t k)
{
    if (k >= ST_NSLOT) return -1;
    memset(st_slot[k], 0xFF, ST_SLOT);
    return st_commit();
}

/* boot, after px_flash_boot and before px_usb_start, which presents the USB device as the settings say (interrupts off
 * until audio_start) */
static void px_store_boot(void)
{
    st_ok = px_flash_ok;
    if (st_ok) st_load();
    ST_IRQ_OFF();                                 /* (ota_fread turned them on) */
    cur_preset = st_last;
    load_preset(cur_preset);
    px_midi_cfg_unpack(&mid_cfg, st_midi);
    px_ua_on = st_usb & 1u;
    a_gl = st_cfg.g; ui_tn_ref = st_tn_hz();        /* v75: the global settings: the engine takes them at audio_start, the tuner its A */
}
