/* SPDX-License-Identifier: GPL-3.0-only
 * Global settings page. Shares the store and live audio settings with the USB editor. */
#pragma once

enum { GL_EQ, GL_HPF, GL_LIMITER, GL_TAILS, GL_TUNER_LIGHTS, GL_BUTTON_LIGHTS, GL_DIAG, GL_ROWS };
static uint8_t gl_row, gl_eq_page, gl_edit, gl_dirty, gl_redraw;
static int gl_shown[GL_ROWS];

static unsigned global_value(unsigned row)
{
    if (gl_eq_page) return st_cfg.g.eq[row];
    switch (row) {
    case GL_HPF: return st_cfg.g.hpf;
    case GL_LIMITER: return st_cfg.g.lim;
    case GL_TAILS: return st_cfg.g.tails;
    case GL_TUNER_LIGHTS: return st_cfg.lights & 3u;
    case GL_BUTTON_LIGHTS: return (st_cfg.lights & 4u) == 0;
    default: return 0;
    }
}

static void global_db(char *s, int steps)
{
    unsigned magnitude = (unsigned)(steps < 0 ? -steps : steps);
    if (steps) b_str(s, steps < 0 ? "-" : "+");
    b_num(s, magnitude / 2u);
    if (magnitude & 1u) b_str(s, ".5");
    b_str(s, " DB");
}

static void global_text(char *s, unsigned row, unsigned value)
{
    if (gl_eq_page) { global_db(s, (int)value - 24); return; }
    switch (row) {
    case GL_HPF: if (value) { b_num(s, value); b_str(s, " HZ"); } else b_str(s, "OFF"); break;
    case GL_LIMITER: if (value) global_db(s, -(int)value); else b_str(s, "OFF"); break;
    case GL_TAILS: case GL_BUTTON_LIGHTS: b_str(s, value ? "ON" : "OFF"); break;
    case GL_TUNER_LIGHTS: b_str(s, value == 2u ? "STROBE" : value == 1u ? "NEEDLE" : "BLINK"); break;
    default: b_str(s, ">"); break;
    }
}

static void global_draw(void)
{
    static const char *const ROWS[GL_ROWS] = {"OUTPUT EQ", "INPUT HIGH-PASS", "LIMITER", "TAILS",
                                            "TUNER LIGHTS", "BUTTON LIGHTS", "DIAGNOSTICS"};
    static const char *const BANDS[5] = {"80 HZ", "250 HZ", "1 KHZ", "3.5 KHZ", "9 KHZ"};
    if (gl_redraw) {
        gl_redraw = 0;
        lcd_rect(0, 0, 240, 240, UI_BG);
        ft_box(0, 4, 240, gl_eq_page ? "OUTPUT EQ" : "GLOBAL", &FT_MD, BLK, UI_ACC, 1);
        lcd_rect(0, F_Y, 240, 240u - F_Y, UI_PANEL);
        ft_box(0, F_Y + 3u, 240, gl_eq_page ? "PUSH: EDIT   HOME: BACK" : "PUSH: EDIT   HOME: SAVE/EXIT", &FT_SM, UI_DIM, UI_PANEL, 1);
        for (unsigned i = 0; i < GL_ROWS; i++) gl_shown[i] = -1;
    }
    const unsigned count = gl_eq_page ? 5u : GL_ROWS;
    for (unsigned i = 0; i < count; i++) {
        unsigned value = global_value(i);
        int selected = i == gl_row;
        int state = (int)value | selected << 8 | (selected && gl_edit) << 9;
        if (gl_shown[i] == state) continue;
        gl_shown[i] = state;
        uint32_t y = 33u + i * (gl_eq_page ? 32u : 23u);
        uint16_t bg = selected ? C_CARD : UI_BG;
        char s[16] = "";
        global_text(s, i, value);
        lcd_rect(0, y, 240, gl_eq_page ? 29u : 22u, bg);
        if (selected) lcd_rect(0, y, 3, gl_eq_page ? 29u : 22u, UI_ACC);
        ft_box(8, y + 4u, 141, gl_eq_page ? BANDS[i] : ROWS[i], &FT_SM, selected ? UI_TXT : UI_DIM, bg, 0);
        ft_box(151, y + 4u, 80, s, &FT_SM, selected && gl_edit ? UI_ACC : UI_TXT, bg, 2);
    }
}

static void global_open(void)
{
    ui_tn_have = 0;
    a_tmute = 0;
    ug.fs = ug.fs_b = -1;
    ug.gt[0] = ug.gt[1] = 0;
    ui_mode = M_GLOBAL;
    gl_row = gl_eq_page = gl_edit = 0;
    gl_redraw = 1;
}

static unsigned global_step(const uint8_t *values, unsigned count, unsigned current, int dd)
{
    if (dd > 0) {
        for (unsigned i = 0; i < count; i++) if (values[i] > current) return values[i];
    } else {
        for (unsigned i = count; i > 0; i--) if (values[i - 1] < current) return values[i - 1];
    }
    return current;
}

static int global_clamp(int value, int max) { return value < 0 ? 0 : value > max ? max : value; }

static void global_turn(int dd, int fast)
{
    if (!gl_edit) {
        gl_row = (uint8_t)global_clamp((int)gl_row + dd, (gl_eq_page ? 5 : GL_ROWS) - 1);
        return;
    }
    static const uint8_t HPF[] = {0, 20, 25, 30, 40, 55, 70, 80, 100, 120, 140};
    static const uint8_t LIMITER[] = {0, 1, 2, 4, 6, 8, 12, 18, 24};
    const unsigned before = global_value(gl_row);
    if (gl_eq_page) st_cfg.g.eq[gl_row] = (uint8_t)global_clamp((int)before + dd, 48);
    else switch (gl_row) {
    case GL_HPF: st_cfg.g.hpf = (uint8_t)global_step(HPF, sizeof HPF, before, dd); break;
    case GL_LIMITER: st_cfg.g.lim = (uint8_t)global_step(LIMITER, sizeof LIMITER, before, dd); break;
    case GL_TAILS: st_cfg.g.tails = dd > 0; break;
    case GL_TUNER_LIGHTS: st_cfg.lights = (uint8_t)((st_cfg.lights & 4u) | global_clamp((int)before + dd, 2)); break;
    case GL_BUTTON_LIGHTS: st_cfg.lights = (uint8_t)((st_cfg.lights & 3u) | (dd > 0 ? 0u : 4u)); break;
    }
    if (before != global_value(gl_row)) {
        gl_dirty = 1;
        if (gl_eq_page || gl_row <= GL_TAILS) audio_globals(&st_cfg.g);
    }
}

static void global_save(void)
{
    if (!gl_dirty) return;
    if (!st_commit()) { gl_dirty = 0; return; }
    ft_box(0, F_Y + 3u, 240, "GLOBAL SAVE FAILED", &FT_SM, WHT, RED, 1);
    for (int i = 0; i < 7; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
}

static void global_push(void)
{
    if (!gl_eq_page && gl_row == GL_EQ) { gl_eq_page = gl_redraw = 1; gl_row = 0; }
    else if (!gl_eq_page && gl_row == GL_DIAG) { global_save(); diag_open(); }
    else gl_edit ^= 1u;
}

static void global_back(void)
{
    if (gl_eq_page) { gl_eq_page = gl_edit = 0; gl_row = GL_EQ; gl_redraw = 1; return; }
    global_save();
    ui_home();
    uc.screen = -1;
    ui_invalidate();
}
