/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the pedal's screen on the PC: firmware/ui.h + ui_text.h + fonts.h drawn into a 240x240 RGB565
 * frame buffer, exactly as the pedal streams it (window + pixels). Writes PPM screenshots of a script of actions.
 *   cc -O1 -Iengine -Ifirmware -o build/uisim tests/uisim.c engine/px_dsp.c firmware/px_math.c -lm
 *   build/uisim OUTDIR        -> OUTDIR/NN_name.ppm ; also checks that every pixel is drawn on each full page
 * The frame buffer starts (and is re-poisoned before each full page) in magenta: any pixel left magenta after a
 * full redraw is a gap in the page's layout. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "../firmware/px_midi.h"

#include "simlcd.h"
#define POISON 0xF81F

static void fm1_wdt_feed(void) { }
static void fm1_delay_ms(int ms) { (void)ms; }

static px_preset_t PRESET;
static uint32_t cur_preset = 0;
static uint32_t master_vol = 80;
static px_preset_t saved[52];
static int saved_ok[52];
static void st_get(uint32_t k, px_preset_t *p) { if (saved_ok[k]) *p = saved[k]; else px_preset_factory(p, (int)k); }
static int st_save(uint32_t k, const px_preset_t *p) { saved[k] = *p; saved_ok[k] = 1; return 0; }
static int st_reset(uint32_t k) { saved_ok[k] = 0; return 0; }
static int st_saved(uint32_t k) { return saved_ok[k]; }
static uint8_t saved_fs[52], ui_fs = 0xFFu;                 /* v48: footswitch assignments */
static uint8_t st_fs_of(uint32_t k) { return saved_ok[k] ? saved_fs[k] : 0xFFu; }
static uint8_t saved_gs[52][4];                             /* v75: footswitch gestures */
static void st_gs_of(uint32_t k, uint8_t *out) { for (int i = 0; i < 4; i++) out[i] = saved_ok[k] ? saved_gs[k][i] : 0xFFu; }
static int st_save_fsg(uint32_t k, const px_preset_t *p, uint8_t fs, const uint8_t *gs) { st_save(k, p); saved_fs[k] = fs; for (int i = 0; i < 4; i++) saved_gs[k][i] = gs ? gs[i] : 0xFFu; return 0; }
static int st_save_fs(uint32_t k, const px_preset_t *p, uint8_t fs) { return st_save_fsg(k, p, fs, 0); }
static void load_preset(uint32_t i);
static int a_level_db(int ch) { return ch ? -84 : -62; }
static uint8_t a_pf[PX_NBLK] = {0, 0, 0, 64, 0, 3, 0, 5, 7};   /* v38: CPU per block (the tuner's footer) */
static uint32_t a_wcut = 2;                            /* v39: wet blocks cut short */
static volatile uint8_t a_tmute;
static int bat_mv = 3920, bat_usb_mv = 0;
static int32_t bat_raw[5] = {612, 1, 0, 3, 91};   /* bat_read's raw values (the tuner page shows them) */
static int bat_pct = -1;                /* (-1: the gauge from bat_mv) */

static void b_num(char *d, uint32_t v) { char t[12]; int n = 0; while (*d) d++; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *d++ = t[--n]; *d = 0; }
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static int midi_saved;
static void midi_persist(void) { midi_saved++; }
#include "ui_text.h"
#include "ui.h"
static struct { px_global_t g; uint8_t lights; } st_cfg = {{0, {24, 24, 24, 24, 24}, 0, 1}, 1};
static void audio_globals(const px_global_t *g) { (void)g; }
static int st_commit(void) { return 0; }
#include "ui_global.h"
static void load_preset(uint32_t i) { st_get(i, &PRESET); ui_fs = st_fs_of(i); st_gs_of(i, ui_gs); }   /* (v75: and its gestures) */

static void shot(const char *dir, const char *name)
{
    static int n;
    char p[512];
    snprintf(p, sizeof p, "%s/%02d_%s.ppm", dir, ++n, name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    int holes = 0;
    for (int i = 0; i < 240 * 240; i++) holes += fb[i] == POISON;
    printf("%-28s %6lu px sent%s", name, px_sent, holes ? "" : "\n");
    if (holes) printf("   %d pixels never drawn (magenta)\n", holes);
    px_sent = 0;
}
static void poison(void) { for (int i = 0; i < 240 * 240; i++) fb[i] = POISON; }
static void status(void) { ui_status("DSP 42%", 0); }
static void full(void) { poison(); uc.screen = -1; ui_invalidate(); ui_render(); status(); }


/* ---- the design review's quick wins (F1, F2, F3, F4, F13): text that fits, and the state each screen shows ---- */
static int count_col(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t col)
{
    int n = 0;
    for (uint32_t j = y; j < y + h; j++) for (uint32_t i = x; i < x + w; i++) n += fb[j * 240u + i] == col;
    return n;
}
static void play_again(void) { poison(); uc.screen = -1; ui_invalidate(); ui_render(); }
static int quick_wins(const char *d)
{
    char c[16];
    /* F1: every load text the main loop can build fits the box it is drawn in (it used to cut "BYP 2C 150%" to "BYP 2C 15") */
    for (uint32_t l = 0; l <= 999u; l++)
        for (int m = 0; m < 8; m++) {
            int safe = m & 1, bypass = (m >> 1) & 1, late = (m >> 2) & 1;
            ui_load_text(c, l, safe, bypass, late);
            if (ft_width(&FT_SM, c) > ST_W) { printf("F1: \"%s\" is %u px, the box %u\n", c, ft_width(&FT_SM, c), ST_W); return 1; }
        }
    ui_load_text(c, 150, 0, 1, 0);
    if (strcmp(c, "PEAK 150%")) { printf("F1: a peak of 150%% reads \"%s\"\n", c); return 1; }
    ui_load_text(c, 88, 0, 0, 1);
    if (strcmp(c, "DSP 88%!")) { printf("F11: a late block reads \"%s\"\n", c); return 1; }
    if (ft_width(&FT_SM, "KNOB: SELECT  PUSH: EDIT") > ST_X - 2u) { printf("F1: the longest hint no longer fits beside the load figure\n"); return 1; }
    /* F1: overload is a red banner with an instruction, on PLAY (top bar) and on a block page (footer) */
    load_preset(9); cur_preset = 9; ui_blk = PX_B_SYN; ui_home(); play_again(); ui_status("DSP 42%", UI_ST_NORM);
    if (count_col(0, 0, 240, 3, C_ALERT)) { printf("F1: PLAY shows the banner without an overload\n"); return 1; }
    ui_status("PEAK 150%", UI_ST_BYPASS); ui_render(); shot(d, "play_overload_banner");
    if (count_col(0, 0, 240, S_H, C_ALERT) < 240 * 5 || count_col(0, F_Y + 3u, 240, 15, WHT) == 0) { printf("F1: PLAY overload: no red bar, or no instruction in the footer\n"); return 1; }
    ui_push(); ui_render(); shot(d, "edit_overload_banner");
    if (count_col(0, F_Y, 240, 18, C_ALERT) < 240 * 3 || count_col(0, F_Y + 3u, 240, 15, WHT) == 0) { printf("F1: block page overload: the footer is not red\n"); return 1; }
    ui_status("DSP 42%", UI_ST_NORM); ui_render();
    if (count_col(0, F_Y, 240, 18, C_ALERT)) { printf("F1: the footer stays red after the overload ends\n"); return 1; }
    ui_home(); play_again(); ui_status("DSP 42%", UI_ST_NORM);
    if (count_col(0, 0, 240, S_H, C_ALERT)) { printf("F1: the top bar stays red after the overload ends\n"); return 1; }
    /* F3: a footswitch on a block page goes back to PLAY (on the block it was on); the band carries the preset number */
    ui_blk = PX_B_SYN; ui_mode = M_EDIT; ui_item = 2;
    ui_preset_step(1);
    if (ui_mode != M_CHAIN || ui_item != 0 || ui_blk != PX_B_SYN || cur_preset != 10) { printf("F3: a footswitch on a block page must return to PLAY\n"); return 1; }
    ui_preset_step(-1);
    if (cur_preset != 9) { printf("F3: the footswitch must still change the preset\n"); return 1; }
    ui_enter_block(PX_B_SYN); poison(); uc.screen = -1; ui_invalidate(); ui_render();
    int band = count_col(8, 6, 24, 19, BLK);
    cur_preset = 11; load_preset(11); ui_render(); shot(d, "edit_band_after_midi_change");   /* (a Program Change keeps the page) */
    if (!band || count_col(8, 6, 24, 19, BLK) == band) { printf("F3: the band must show the new preset number after a MIDI change\n"); return 1; }
    ui_home(); cur_preset = 9; load_preset(9);
    /* F4: while EDITED shows, the footer names SAVE */
    play_again(); ui_status("DSP 42%", UI_ST_NORM);
    if (count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: SAVE is named without an edit\n"); return 1; }
    PRESET.p[PX_B_SYN][1] ^= 7u; ui_render(); shot(d, "play_edited_save_hint");
    if (!count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: EDITED shows, but the footer does not name SAVE\n"); return 1; }
    load_preset(9); ui_render();
    if (count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: the SAVE hint stays after the edit is gone\n"); return 1; }
    /* F13: the label of an off tile reads (contrast of at least 2.5 against the tile; the old one was 1.5 to 3.2) */
    for (int b = 0; b < PX_NBLK; b++) {
        uint16_t fg = ft_mix(BLK_COL[b], C_OFF, 11);
        if (fg == C_OFF || fg == BLK_COL[b]) { printf("F13: block %d: an off label must differ from its tile and from an on tile\n", b); return 1; }
    }
    return 0;
}

/* ---- the full overhaul (v42): the design review's F4 to F17, through ui.h's own controls (ui_buttons, ui_turn, ui_action) ---- */
static uint32_t T = 100000u;                            /* the simulated clock (ms); ui_buttons runs every 5 ms as the main loop */
static void hold(uint32_t mask, uint32_t ms) { for (uint32_t t = 0; t < ms; t += 5u) { ui_buttons(T, mask); T += 5u; } }
static void tap(int b) { hold(1u << b, 40); hold(0, 5); }
#define CHECK(c, m) do { if (!(c)) { printf("%s\n", m); return 1; } } while (0)
/* the diagnostics as pictures (v42): each figure beside its graphic, colours by state, two pages turned by the knob */
static int px_at(uint32_t x, uint32_t y, uint16_t c) { return fb[y * 240u + x] == c; }
static int diagnostics(const char *d)
{
    char b[48];
    ui_dg.load = 61; ui_dg.peak = 70; ui_dg.cores = 2; ui_dg.split = 1; ui_dg.guard = 93; ui_dg.cl[0] = 52; ui_dg.cl[1] = 56;
    ui_dg.late = 0; ui_dg.bypass = 0;
    diag_draw(); shot(d, "diagnostics");
    CHECK(dg_page == 0 && count_col(10, 60, 220, 6, GRN) && !count_col(10, 60, 220, 6, UI_ACC), "diag: a load of 61 % is a green bar");
    CHECK(px_at(10 + dg_w(93, 100, 220) - 1, 58, WHT) && px_at(10 + dg_w(93, 100, 220) - 1, 67, WHT), "diag: the guard's tick at 93 %");
    CHECK(count_col(88, 93, 144, 6, GRN) && count_col(88, 109, 144, 6, GRN), "diag: a bar for each core");
    {
        uint8_t top[DG_BLK];
        int n = dg_top(top, DG_BLK);
        CHECK(n == 4 && top[0] == PX_B_SYN && top[1] == PX_B_REVERB && top[2] == PX_B_DELAY && top[3] == PX_B_EQ, "diag: CPU per block, costliest first");
        CHECK(count_col(52, 165, 148, 5, UI_ACC) && !count_col(52, 180, 148, 5, UI_ACC), "diag: the heaviest block highlighted, the others not");
    }
    CHECK(count_col(2, 122, 76, 18, GRN) && !count_col(2, 122, 76, 18, RED), "diag: no late block: a green chip");
    ui_dg.load = 86; diag_draw();
    CHECK(count_col(10, 60, 220, 6, UI_ACC) && !count_col(10, 60, 220, 6, GRN), "diag: above 80 % the load turns amber");
    ui_dg.late = 1; bat_raw[2] = 3; diag_draw(); shot(d, "diagnostics_late");
    CHECK(count_col(10, 60, 220, 6, RED) && count_col(2, 122, 76, 18, RED), "diag: a late block: the load and its chip in red");
    ui_dg.late = 0; bat_raw[2] = 0; ui_dg.load = 61; ui_dg.split = 0; diag_draw();
    CHECK(!count_col(88, 93, 144, 6, GRN), "diag: no layer split: no core bars (CPU0 --)");
    ui_dg.split = 1;
    /* every figure fits its box at its widest */
    dg_pct(b, "DSP ", 999); CHECK(ft_width(&FT_MD, b) <= 120u, "diag: DSP 999% fits");
    dg_pct(b, "PEAK ", 999); CHECK(ft_width(&FT_SM, b) <= 104u, "diag: PEAK 999% fits");
    CHECK(ft_width(&FT_SM, "CPU1 100%") <= 78u && ft_width(&FT_SM, "LATE 9999") <= 70u && ft_width(&FT_SM, "CLIP 9999") <= 70u
          && ft_width(&FT_SM, "BYPASS OFF") <= 76u && ft_width(&FT_SM, "PK 999%") <= 64u && ft_width(&FT_SM, "CORES 2C") <= 80u
          && ft_width(&FT_SM, "GUARD 93%") <= 80u, "diag: the chips and core figures fit");
    CHECK(ft_width(&FT_SM, "SOFT CLIP: 99999 BLOCKS") <= 204u && ft_width(&FT_SM, "RAW  C9 4095  CH 1") <= DG_W
          && ft_width(&FT_SM, "R -199 DB") <= 74u && ft_width(&FT_SM, "CUT 9999") <= 84u, "diag: page 2's figures fit");
    /* the knob turns the page; both pages are drawn whole */
    poison(); ui_turn(1, 0); CHECK(dg_page == 1, "diag: the knob turns to page 2");
    diag_draw(); shot(d, "diagnostics_2");
    CHECK(count_col(82, 58, 150, 8, GRN) && count_col(82, 58, 150, 8, ui_dark(RED)), "diag: an input meter, green, with its clip zone");
    CHECK(!px_at(DG_X, 112, RED), "diag: no recent soft clip: the marker is dark");
    bat_raw[3]++; diag_draw(); CHECK(px_at(DG_X + 2, 114, RED), "diag: a new soft clip lights the marker");
    for (int i = 0; i < 25; i++) diag_draw();
    CHECK(!px_at(DG_X + 2, 114, RED), "diag: the marker goes dark again");
    CHECK(count_col(DG_X + 3, 159, 40, 16, GRN), "diag: the battery's cell is filled");
    poison(); ui_turn(-1, 0); diag_draw(); shot(d, "diagnostics_1_again");
    CHECK(dg_page == 0, "diag: and back to page 1");
    return 0;
}
static int overhaul(const char *d)
{
    char c[16];
    load_preset(9); cur_preset = 9; ui_blk = PX_B_SYN; ui_mode = M_CHAIN; ui_item = 0; full();
    /* F6: a click of PUSH on PLAY opens the block at its release, no wait; a 2nd one within 350 ms makes it MOVE */
    hold(1u << UB_PUSH, 60); CHECK(ui_mode == M_CHAIN, "F6: PUSH acts at its release");
    hold(0, 5); CHECK(ui_mode == M_BLOCK && ui_blk == PX_B_SYN, "F6: one click opens the block at once");
    hold(1u << UB_PUSH, 60); hold(0, 5); CHECK(ui_mode == M_MOVE && ui_blk == PX_B_SYN, "F6: a 2nd click within 350 ms: MOVE");
    tap(UB_HOME); CHECK(ui_mode == M_CHAIN, "MOVE: HOME puts the block back");
    tap(UB_PUSH); hold(0, 400); tap(UB_PUSH); CHECK(ui_mode == M_EDIT, "F6: a 2nd click after 350 ms edits the model, as on any block page");
    tap(UB_HOME);
    tap(UB_PUSH); ui_turn(1, 0); ui_turn(-1, 0); tap(UB_PUSH); CHECK(ui_mode == M_EDIT, "F6: the knob between the clicks: no MOVE");
    tap(UB_HOME);
    hold(1u << UB_PUSH, 850); CHECK(ui_mode == M_SAVE, "hold PUSH: SAVE");
    hold(0, 5); CHECK(ui_mode == M_SAVE, "the release of that hold stays on SAVE");
    tap(UB_HOME); CHECK(ui_mode == M_CHAIN, "HOME leaves SAVE");
    hold(1u << UB_FX, 850); hold(0, 5); CHECK(ui_mode == M_MIDI, "hold FX: MIDI");
    tap(UB_HOME); CHECK(ui_mode == M_CHAIN, "HOME leaves MIDI");
    /* F5: both footswitches: the first acts at once, the other within 150 ms takes that step back and opens the tuner */
    hold(1u << UB_FSUP, 60); CHECK(cur_preset == 10, "F5: the first footswitch acts at its press");
    poison();
    hold(1u << UB_FSUP | 1u << UB_FSDN, 60);
    CHECK(ui_mode == M_TUNER && cur_preset == 9 && a_tmute, "F5: both footswitches: the step back, the tuner, muted");
    tuner_draw(108.0f); shot(d, "tuner_flat");
    CHECK(count_col(10, 76, 46, 23, UI_ACC) && !count_col(184, 76, 46, 23, UI_ACC), "F12: flat: an arrow on the left, toward in tune");
    CHECK(count_col(0, 30, 240, 30, UI_BG) == 240 * 30 && count_col(0, 206, 240, 16, UI_BG) == 240 * 16, "F12: no diagnostics on the tuner page");
    tuner_draw(112.5f); CHECK(!count_col(10, 76, 46, 23, UI_ACC) && count_col(184, 76, 46, 23, UI_ACC), "F12: sharp: the arrow on the right");
    tuner_draw(110.0f); CHECK(!count_col(10, 76, 46, 23, UI_ACC) && !count_col(184, 76, 46, 23, UI_ACC), "F12: in tune: no arrow");
    hold(0, 300);
    hold(1u << UB_FSDN, 40); hold(1u << UB_FSDN | 1u << UB_FSUP, 40); hold(0, 300);
    CHECK(ui_mode == M_CHAIN && cur_preset == 9 && !a_tmute, "F5: both footswitches leave the tuner, the preset as it was");
    tap(UB_FSUP); hold(0, 200); tap(UB_FSDN); hold(0, 200);
    CHECK(ui_mode == M_CHAIN && cur_preset == 9, "F5: footswitches 200 ms apart are two presses");
    tap(UB_FSUP); tap(UB_FSDN); hold(0, 200);
    CHECK(ui_mode == M_CHAIN && cur_preset == 9, "F5: one footswitch released before the other goes down: two presses");
    /* F12: hold HOME for the tuner, then release and hold again for global settings. */
    hold(1u << UB_HOME, 750); CHECK(ui_mode == M_TUNER, "hold HOME 0.7 s: the tuner");
    hold(1u << UB_HOME, 700); CHECK(ui_mode == M_TUNER, "F12: one continuous hold stays on the tuner");
    hold(0, 5); hold(1u << UB_HOME, 750); hold(0, 5);
    CHECK(ui_mode == M_GLOBAL && !a_tmute, "F12: a second hold opens Global and restores audio");
    poison();
    ui_render(); shot(d, "global");
    tap(UB_PUSH); poison(); ui_render(); shot(d, "global_output_eq");
    tap(UB_PUSH); ui_turn(1, 0); CHECK(st_cfg.g.eq[0] == 25, "Global: adjust the selected output EQ band in half-dB steps");
    tap(UB_HOME); CHECK(ui_mode == M_GLOBAL && !gl_eq_page, "Global: HOME returns from EQ to the list");
    ui_turn(GL_DIAG, 0); tap(UB_PUSH);
    CHECK(ui_mode == M_DIAG && !a_tmute, "F12: diagnostics opens from the Global list");
    if (diagnostics(d)) return 1;
    tap(UB_FX); CHECK(ui_mode == M_CHAIN, "any control leaves the diagnostics");
    full();
    /* F11: the load figure in plain words, amber when high, red when a block ran late */
    CHECK(ui_load_kind(85, 0, 0, 0, 0) == UI_ST_HIGH && ui_load_kind(80, 0, 0, 0, 0) == UI_ST_NORM && ui_load_kind(85, 1, 0, 0, 0) == UI_ST_NORM
          && ui_load_kind(85, 0, 0, 1, 0) == UI_ST_LATE && ui_load_kind(85, 0, 1, 1, 0) == UI_ST_BYPASS && ui_load_kind(5, 0, 1, 1, 1) == UI_ST_LOADING,
          "F11: the load's kind");
    ui_load_text(c, 42, 0, 0, 0); CHECK(!strcmp(c, "DSP 42%"), "F11: the load reads DSP 42%");
    ui_status("DSP 85%", UI_ST_HIGH); ui_render(); shot(d, "play_dsp_high");
    CHECK(count_col(ST_X, 3, ST_W, 15, UI_ACC) && !count_col(ST_X, 3, ST_W, 15, RED), "F11: a high load in amber");
    ui_status("DSP 91%!", UI_ST_LATE); ui_render(); CHECK(count_col(ST_X, 3, ST_W, 15, RED), "F11: a late block in red");
    ui_status("DSP 42%", UI_ST_NORM); ui_render();
    /* F8: HOME on PLAY: the preset list; the knob browses without loading, PUSH loads, HOME goes back */
    poison(); tap(UB_HOME); CHECK(ui_mode == M_LIST, "F8: HOME on PLAY: the preset list"); ui_render(); shot(d, "preset_list");
    for (int i = 0; i < 3; i++) ui_turn(1, 0);
    ui_render(); shot(d, "preset_list_browsed");
    CHECK(cur_preset == 9 && ls_at == 12, "F8: the knob browses without loading");
    tap(UB_PUSH); CHECK(cur_preset == 12 && ui_mode == M_CHAIN, "F8: PUSH loads the preset shown and returns to PLAY");
    tap(UB_HOME); ui_turn(-1, 0); tap(UB_HOME); CHECK(ui_mode == M_CHAIN && cur_preset == 12, "F8: HOME leaves the list without loading");
    tap(UB_HOME); tap(UB_FSUP); CHECK(ui_mode == M_CHAIN && cur_preset == 13, "F8: a footswitch on the list steps and shows PLAY");
    { const int was = PRESET.on[PX_B_SYN]; tap(UB_HOME); tap(UB_AMP); CHECK(ui_mode == M_BLOCK && ui_blk == PX_B_SYN && PRESET.on[PX_B_SYN] == was, "F8: the AMP button opens SYNTH, without switching it"); }
    { tap(UB_HOME); tap(UB_CAB); CHECK(ui_mode == M_BLOCK && ui_blk == PX_B_PITCH, "F8: the CAB button opens OCTAVE"); }
    ui_home(); full();
    /* F4: one unsaved edit survives a preset change and comes back with its slot */
    load_preset(9); cur_preset = 9; full();
    PRESET.p[PX_B_SYN][1] ^= 7u;
    uint8_t ed = PRESET.p[PX_B_SYN][1];
    ui_preset_step(1); CHECK(cur_preset == 10 && !ui_dirty(), "F4: the next preset plays as saved");
    ui_preset_step(-1); ui_render(); shot(d, "play_edit_kept");
    CHECK(ui_dirty() && PRESET.p[PX_B_SYN][1] == ed, "F4: back on the edited preset: the edit is back, EDITED");
    ui_preset_step(1); ui_preset_step(1); ui_preset_step(-1); ui_preset_step(-1);
    CHECK(ui_dirty() && PRESET.p[PX_B_SYN][1] == ed, "F4: the edit waits while other presets play");
    ui_preset_step(1); PRESET.p[PX_B_SYN][2] ^= 3u; ui_preset_step(-1);
    CHECK(!ui_dirty(), "F4: an edit elsewhere takes the place of the first");
    ui_preset_step(1); CHECK(ui_dirty(), "F4: that edit is kept in turn");
    load_preset(10); PRESET.p[PX_B_SYN][1] ^= 5u; { px_preset_t q = PRESET; ui_preset_step(1); st_save(10, &q); st_gen++; } ui_preset_step(-1);
    CHECK(!ui_dirty() && st_saved(10), "F4: a slot saved over meanwhile: the kept edit is dropped");
    st_reset(10); st_gen++; load_preset(9); cur_preset = 9;
    /* F9: the block page's selection goes in the order drawn: ON at the top, MODEL, the settings */
    ui_blk = PX_B_SYN; ui_mode = M_BLOCK; ui_item = 0;
    ui_turn(-1, 0); CHECK(ui_item == 1u, "F9: back from MODEL: ON");
    ui_turn(1, 0); CHECK(ui_item == 0u, "F9: on from ON: MODEL");
    ui_turn(1, 0); CHECK(ui_item == 2u, "F9: on from MODEL: the first setting");
    ui_item = 1; ui_turn(-1, 0); CHECK((int)ui_item == ui_nitems() - 1, "F9: back from ON: the last setting");
    ui_turn(1, 0); CHECK(ui_item == 1u, "F9: on from the last setting: ON");
    /* F10: FX steps through its blocks in the preset's chain order, as the tiles read */
    load_preset(9); cur_preset = 9; ui_home(); ui_blk = PX_B_MOD;   /* (from a block outside the FX group) */
    for (int i = 0, last = -1; i < UI_NFX; i++) {
        tap(UB_FX);
        int pos = ui_pos_of(ui_blk);
        CHECK(ui_mode == M_BLOCK && ui_is_fx(ui_blk) && pos > last, "F10: FX goes on along the chain");
        last = pos;
    }
    tap(UB_FX); CHECK(ui_mode == M_VALVE, "F10: the Valve page follows the utility blocks");
    poison(); ui_invalidate(); ui_render(); shot(d, "valve_input_off");
    tap(UB_PUSH); ui_turn(8, 0); ui_render(); shot(d, "valve_input_on");
    CHECK(PRESET.valve_on && PRESET.valve_drive == PX_VALVE_DEFAULT + 8, "F10: PARA switches Valve and changes Drive");
    tap(UB_FX); CHECK(ui_mode == M_VALVE && ui_item == 1u, "F10: FX selects Valve Level");
    ui_turn(3, 0); ui_render(); shot(d, "valve_input_level");
    CHECK(PRESET.valve_level == PX_VALVE_LEVEL_UNITY + 3u && PRESET.valve_drive == PX_VALVE_DEFAULT + 8,
          "F10: PARA changes Level independently of Drive");
    tap(UB_PUSH); CHECK(!PRESET.valve_on, "F10: PARA also switches Valve on the Level page");
    tap(UB_FX); CHECK(ui_mode == M_PATCH, "F10: independent patch settings after Valve Level");
    poison(); ui_invalidate(); ui_render(); shot(d, "patch_settings");
    uint8_t gateControls[PX_NPAR]; memcpy(gateControls, PRESET.p[PX_B_GATE], PX_NPAR);
    ui_turn(4, 0); ui_render();
    CHECK(!memcmp(gateControls, PRESET.p[PX_B_GATE], PX_NPAR), "patch volume does not alter gate controls");
    tap(UB_FX); CHECK(ui_mode == M_BLOCK && ui_pos_of(ui_blk) < 3, "F10: after Valve, FX starts again from the first utility block");
    ui_home(); list_open();
    tap(UB_FX); CHECK(ui_mode == M_PATCH, "patch list FX opens patch volume independently of effects");
    ui_render(); shot(d, "patch_volume_from_list");
    /* F17: SYN's page is SYNTH. v50 gave NOISE GATE a second model (SMART GATE), so no block has a single
     * model any more: check the other side, a block of several models shows its arrows */
    CHECK(!strcmp(ui_block_name(PX_B_SYN), "SYNTH"), "F17: SYN's page is SYNTH");
    ui_blk = PX_B_GATE; ui_mode = M_BLOCK; ui_item = 0; full(); shot(d, "edit_gate_arrows");
    CHECK(px_ntypes(PX_B_GATE) == 2 && count_col(8, E_TYPE_Y, 20, 23, UI_ACC), "F17: NOISE GATE: model arrows");
    /* F15, F16: SAVE says a copy keeps its name; RESTORE FACTORY says RESTORE and does nothing on a factory sound */
    ui_home(); load_preset(9); cur_preset = 9; full();
    poison(); save_open(); save_knob(1); save_draw(); shot(d, "save_copy_keeps_name");
    CHECK(count_col(0, 186, 240, 15, UI_DIM), "F15: SAVE to another slot says the name stays");
    while (sv_dest != SV_FACTORY) save_knob(-1);
    save_draw(); shot(d, "save_restore_nothing");
    CHECK(sv_dest == SV_FACTORY && !st_saved(9), "F16: RESTORE FACTORY");
    tap(UB_PUSH); CHECK(ui_mode == M_SAVE && !st_saved(9), "F16: PUSH does nothing when there is nothing to restore");
    tap(UB_HOME); CHECK(ui_mode == M_CHAIN, "HOME leaves SAVE");
    full();
    return 0;
}

/* v75: footswitch gestures: a double tap (STOMP mode) and a hold, per footswitch, each an action; the press's own toggle is taken back for a hold */
static int gestures(void)
{
    int bad = 0;
#define GCHECK(c, what) do { if (!(c)) { printf("gestures: %s\n", what); bad++; } } while (0)
    load_preset(9); cur_preset = 9; ui_mode = M_CHAIN; ui_item = 0; ui_stomp = 1; ui_byp = 0;
    ui_fs = 0xFFu; ui_fs_put(0, PX_B_BST); ui_fs_put(1, PX_B_EQ);
    PRESET.on[PX_B_BST] = 0; PRESET.on[PX_B_EQ] = 0; PRESET.on[PX_B_COMP] = 0;
    ui_gs[0] = PX_B_COMP; ui_gs[1] = UI_GA_NEXT; ui_gs[2] = UI_GA_TUNER; ui_gs[3] = UI_GA_MODE;     /* left: double COMP, hold next preset; right: double tuner, hold mode */
    hold(0, 500);
    tap(UB_FSDN); hold(0, 500);
    GCHECK(PRESET.on[PX_B_BST] == 1 && PRESET.on[PX_B_COMP] == 0, "a single tap: its own toggle, no gesture");
    tap(UB_FSDN); hold(0, 500);                                   /* BST off again */
    tap(UB_FSDN); hold(0, 120); tap(UB_FSDN); hold(0, 500);
    GCHECK(PRESET.on[PX_B_BST] == 0 && PRESET.on[PX_B_COMP] == 1, "a double tap: the two toggles cancel, the action runs (COMP on)");
    tap(UB_FSDN); hold(0, 450); tap(UB_FSDN); hold(0, 500);
    GCHECK(PRESET.on[PX_B_BST] == 0 && PRESET.on[PX_B_COMP] == 1, "two taps 450 ms apart: two single taps, no gesture");
    { const uint32_t was = cur_preset; PRESET.on[PX_B_BST] = 0;
      hold(1u << UB_FSDN, 800); hold(0, 300);
      GCHECK(PRESET.on[PX_B_BST] == 0 && cur_preset != was, "a hold: its own toggle is taken back, the next preset plays"); }
    load_preset(9); cur_preset = 9; ui_stomp = 1; ui_fs = 0xFFu; ui_fs_put(0, PX_B_BST); ui_fs_put(1, PX_B_EQ); ui_gs[0] = 0xFF; ui_gs[1] = 0xFF; ui_gs[2] = UI_GA_TUNER; ui_gs[3] = 0xFF;
    PRESET.on[PX_B_BST] = 0; hold(0, 500);
    hold(1u << UB_FSDN, 900); hold(0, 300);
    GCHECK(PRESET.on[PX_B_BST] == 1, "no hold gesture set: the press's toggle stands");
    PRESET.on[PX_B_BST] = 0; PRESET.on[PX_B_EQ] = 0; hold(0, 400);
    tap(UB_FSUP); hold(0, 120); tap(UB_FSUP); hold(0, 200);
    GCHECK(ui_mode == M_TUNER && PRESET.on[PX_B_EQ] == 0, "a double tap on the right: the tuner, the toggles cancelled");
    hold(1u << UB_FSUP, 60); hold(0, 300);
    GCHECK(ui_mode == M_CHAIN, "any switch leaves the tuner");
    ui_gs[0] = PX_B_COMP; PRESET.on[PX_B_COMP] = 0; PRESET.on[PX_B_BST] = 0; hold(0, 400);
    hold(1u << UB_FSDN | 1u << UB_FSUP, 60); hold(0, 300);
    GCHECK(ui_mode == M_TUNER && PRESET.on[PX_B_COMP] == 0, "both footswitches: the tuner, no gesture");
    hold(1u << UB_FSDN, 60); hold(0, 400);
    /* PATCH mode: a hold works (its step taken back), a double tap does not */
    ui_stomp = 0; load_preset(9); cur_preset = 9; ui_gs[0] = PX_B_COMP; ui_gs[1] = UI_GA_TUNER; ui_gs[2] = 0xFF; ui_gs[3] = 0xFF; PRESET.on[PX_B_COMP] = 0; hold(0, 400);
    hold(1u << UB_FSDN, 800); hold(0, 300);
    GCHECK(ui_mode == M_TUNER && cur_preset == 9, "PATCH mode: a hold takes its preset step back and opens the tuner");
    hold(1u << UB_FSDN, 60); hold(0, 400);
    ui_gs[1] = 0xFF; tap(UB_FSUP); hold(0, 120); tap(UB_FSUP); hold(0, 400);
    GCHECK(PRESET.on[PX_B_COMP] == 0, "PATCH mode: a double tap runs no gesture");
    /* the gestures are part of the preset: EDITED until they are saved, kept with the slot */
    ui_stomp = 1; load_preset(9); cur_preset = 9; ui_gs[0] = 0xFF; ui_gs[1] = 0xFF; ui_gs[2] = 0xFF; ui_gs[3] = 0xFF;
    GCHECK(!ui_dirty(), "unchanged gestures: not edited");
    ui_gs[1] = UI_GA_TUNER; GCHECK(ui_dirty(), "a gesture set: the preset reads EDITED");
    st_save_fsg(9, &PRESET, ui_fs, ui_gs); load_preset(9);
    GCHECK(ui_gs[1] == UI_GA_TUNER && !ui_dirty(), "saved with the slot: back after loading it, not edited");
    GCHECK(ui_ga_valid(PX_VALVE_ACTION) && !ui_ga_valid(11) && !ui_ga_valid(20) && !ui_ga_valid(0xFF) &&
           ui_ga_valid(PX_B_BST) && ui_ga_valid(12) && ui_ga_valid(UI_GA_MODE), "action codes include Valve alongside the blocks and other actions");
    st_reset(9); st_gen++; load_preset(9); cur_preset = 9; ui_stomp = 0; ui_byp = 0; hold(0, 300);
#undef GCHECK
    return bad;
}
int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : ".";
    load_preset(9);
    cur_preset = 9;
    ui_blk = PX_B_SYN;
    full(); shot(d, "play");
    ui_knob(1, 0); ui_render(); shot(d, "play_knob_next_block");
    ui_knob(-1, 0); ui_push(); poison(); ui_render(); status(); shot(d, "edit_amp_model");
    ui_knob(1, 0); ui_knob(1, 0); ui_render(); shot(d, "edit_amp_gain_selected");
    ui_push(); for (int i = 0; i < 8; i++) ui_knob(1, 0); ui_render(); shot(d, "edit_amp_gain_editing");
    ui_push(); ui_home(); poison(); ui_render(); status(); shot(d, "play_edited");
    ui_enter_block(PX_B_DELAY); poison(); ui_render(); status(); shot(d, "edit_delay");
    ui_knob(1, 0); ui_render(); shot(d, "edit_delay_onoff_selected");
    ui_enter_block(PX_B_DELAY); ui_render(); shot(d, "edit_delay_off");
    ui_home(); ui_blk = PX_B_GATE; ui_mode = M_BLOCK; full(); shot(d, "edit_gate");
    ui_home(); cur_preset = 13; load_preset(13); ui_blk = PX_B_REVERB; poison(); ui_render(); status(); shot(d, "play_smooth_lead");
    ui_move_start(); ui_knob(-1, 0); ui_render(); shot(d, "play_move");
    ui_move_cancel(); cur_preset = 23; load_preset(23); master_vol = 100; bat_mv = 3640; full(); ui_status("PEAK 97%", UI_ST_BYPASS); ui_render(); shot(d, "play_acoustic_bypass_vol100_bat12");
    bat_usb_mv = 5000; ui_render(); shot(d, "play_usb");
    bat_usb_mv = 0; bat_mv = 3920; master_vol = 80;
    save_open(); save_draw(); shot(d, "save");
    save_knob(1); save_draw(); shot(d, "save_other_slot");
    save_close(); ui_render(); status();
    tuner_open(); tuner_draw(110.5f); shot(d, "tuner_a_flat");
    {   /* v37: no battery reading: the icon shows an empty cell and "--" (it was hidden) */
        int m = bat_mv; bat_mv = 0; tuner_close(); poison(); ui_render(); status(); shot(d, "play_battery_unknown"); bat_mv = m;
        tuner_open(); tuner_draw(110.5f);
    }
    tuner_draw(110.0f); shot(d, "tuner_a_ok");
    tuner_close(); poison(); ui_render(); status(); shot(d, "play_after_tuner");
    px_midi_cfg_unpack(&mid_cfg, 0);
    midi_open(); midi_draw(); shot(d, "midi_defaults");
    midi_knob(1); midi_knob(1); midi_draw(); shot(d, "midi_channel_2");
    midi_push(); midi_knob(1); mid_usb_up = 1; mid_last[0] = PXM_CC; mid_last[1] = 25; mid_last[2] = 127; midi_draw(); shot(d, "midi_send_off_host_cc");
    midi_push(); midi_knob(1); mid_last[0] = PXM_PC; mid_last[1] = 11; midi_draw(); shot(d, "midi_clock_on_pc");
    mid_c1_off = 2; midi_draw(); shot(d, "midi_core2_off_late"); mid_c1_off = 0; midi_draw();   /* v41: the 2nd core retired */
    /* v42: the fourth row, USB AUDIO: the setting follows the row only when the page is left */
    midi_push(); midi_knob(1); midi_draw(); shot(d, "midi_usb_audio_on");
    if (px_ua_on) { printf("midi: USB AUDIO must not change while the page is open\n"); return 1; }
    midi_push(); poison(); uc.screen = -1; ui_invalidate(); ui_render(); status();
    if (midi_saved != 1 || ui_mode != M_CHAIN) { printf("midi: leaving the page after a change must save once and return to the chain\n"); return 1; }
    if (!px_ua_on) { printf("midi: leaving the page must set USB AUDIO as the row showed\n"); return 1; }
    midi_open(); midi_close();
    if (midi_saved != 1) { printf("midi: leaving without a change must not save\n"); return 1; }
    midi_open(); midi_push(); midi_push(); midi_push(); midi_knob(-1); midi_close();
    if (midi_saved != 2 || px_ua_on) { printf("midi: USB AUDIO off again must save once more\n"); return 1; }
    mid_cfg.chan = 16; midi_open(); midi_knob(1); midi_draw(); shot(d, "midi_channel_wraps_to_omni");
    if (mid_cfg.chan != 0) { printf("midi: channel 16 + 1 must wrap to omni\n"); return 1; }
    midi_knob(-1); if (mid_cfg.chan != 16) { printf("midi: omni - 1 must wrap to 16\n"); return 1; }
    return quick_wins(d) || overhaul(d) || gestures();
}
