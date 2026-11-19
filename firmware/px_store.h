/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the user's presets in flash.
 *
 * 52 slots of 112 bytes (px_preset_t, 110 B, + a "saved" mark), kept in RAM (st_slot) and written as one whole copy
 * on each save, alternately to copy A [0xF0000, 0xF2000) and copy B [0xF2000, 0xF4000), each two 4 KiB sectors:
 *   sector 0: header (16 B) + slots 0..33        sector 1: slots 34..51
 * Format 6 (v70, OpenPocket Synth). The multi-effects firmware's presets (formats 2..5, same slot size) are not
 * converted: such a copy gives its settings (MIDI, USB AUDIO, STOMP mode) and every slot starts as its placeholder
 * (px_preset_factory). The first save writes format 6; later updates keep the presets.
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
#define ST_VER     6u          /* 6: v70 OpenPocket Synth; 2..5: the multi-effects firmware (settings kept, presets not) */
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
 * STOMP mode (firmware/ui.h, ui_fs): low 4 bits the left footswitch, high 4 the right; 0..11 a block, 12 BYPASS,
 * 15 nothing. Earlier saves left it 0xFF (erased): nothing assigned. The format is unchanged. */
typedef struct { uint32_t magic, seq; uint16_t crc; uint8_t ver, nslot, last, pad[3]; } st_hdr_t;   /* 16 B */
static uint8_t st_slot[ST_NSLOT][ST_SLOT] __attribute__((aligned(4)));
static uint32_t st_seq, st_cur;                 /* sequence of the copy in use, its base (0: none yet) */
static uint8_t st_ok;                           /* the store can be written (the flash is the expected part) */
static uint8_t st_last;                         /* the slot to start on (the last one saved) */
static uint8_t st_midi;                         /* the MIDI settings byte (header pad[0]) */
static uint8_t st_usb;                          /* the USB settings byte (header pad[1]) */
static uint8_t st_ui;                           /* v48: the pedal settings byte (header pad[2]): bit 0 STOMP */

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
    if (best && bh.ver < ST_VER) {                 /* the multi-effects firmware's presets: placeholders instead */
        memset(st_slot, 0xFF, sizeof st_slot);
        st_last = 0;
    }
}

static int st_saved(uint32_t k) { return k < ST_NSLOT && st_slot[k][sizeof(px_preset_t)] == ST_MARK; }

/* the preset in slot k: the saved one, else the factory one */
static void st_get(uint32_t k, px_preset_t *p)
{
    if (st_saved(k)) { memcpy(p, st_slot[k], sizeof *p); if (!px_order_valid(p->order)) px_order_default(p->order); }
    else px_preset_factory(p, (int)k);
    for (int b = 0; b < PX_NBLK; b++) px_block_canonical(b, &p->type[b], p->p[b]);   /* v64: every block's retired types */
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
    if (!rc && fl_write(base, (const uint8_t *)&h, sizeof h)) rc = -4;      /* header last */
    if (!rc) {                                    /* read back */
        st_hdr_t r;
        static uint8_t chk[ST_SLOT];
        if (ota_fread(base, &r, sizeof r) || memcmp(&r, &h, sizeof h)) rc = -5;
        for (k = 0; !rc && k < ST_NSLOT; k++)
            if (ota_fread(st_slot_off(base, k), chk, ST_SLOT) || memcmp(chk, st_slot[k], ST_SLOT)) rc = -5;
    }
    st_audio_hold(0);
    if (!rc) { st_cur = base; st_seq = h.seq; }
    return rc;
}

/* v48: slot k's footswitch assignment (0xFF: nothing, also for a slot never saved) */
static uint8_t st_fs_of(uint32_t k) { return st_saved(k) ? st_slot[k][ST_SLOT - 1u] : 0xFFu; }
typedef char st_fs_check[(sizeof(px_preset_t) + 1u < ST_SLOT) ? 1 : -1];   /* the mark, then this byte */

/* save p, with its footswitch assignment fs, to slot k (becomes the start slot) */
static int st_save_fs(uint32_t k, const px_preset_t *p, uint8_t fs)
{
    if (k >= ST_NSLOT) return -1;
    memset(st_slot[k], 0xFF, ST_SLOT);
    memcpy(st_slot[k], p, sizeof *p);
    st_slot[k][sizeof *p] = ST_MARK;
    st_slot[k][ST_SLOT - 1u] = fs;
    st_last = (uint8_t)k;
    return st_commit();
}
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
}
