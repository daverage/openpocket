/* OpenPocket Synth2: the ANALOG and ARCADE types' patches. A patch is a fixed table entry: Felucca's ANALOG parameters (E0..E7), its amplitude
 * envelope and filter-envelope amount, a transposition and a level trim. The block's PATCH setting (0..100) selects one in fixed bands of 10
 * (slot = value / 10, 100 = slot 9), so a patch keeps its band whatever is added later; a slot with no patch plays the last one defined.
 * The other settings are macros around the patch's own values (px_synth_adapter.h), 50 = the patch as written.
 * These are sound-design starting points built from Felucca's ANALOG engine; they do not model any particular vintage instrument. */
#ifndef PX_SYNTH_PATCHES_H
#define PX_SYNTH_PATCHES_H
#include <stdint.h>
typedef struct {
    const char *name;            /* at most 7 characters (the screen's value text) */
    int8_t e[8];                 /* Felucca ANALOG: WAVE, DTN, MIX, NOIS, CUT, RES, DRV, KTR; LOFI: CHIP, WAVE, DUTY, CRSH, SWP, VIB, ARP, TONE (third_party/felucca-engines) */
    uint8_t env[4];              /* ATK DEC SUS REL, 0..127 */
    int8_t fenv;                 /* envelope -> filter cutoff, -64..63 */
    int8_t semis;                /* transposition from the guitar's note */
    uint8_t trim;                /* level trim, 64 = 1.0 */
    uint8_t pwm;                 /* pulse-width sweep (the WAVE PWM and SYNC), 0 none .. 127 */
    uint8_t noise;               /* 1: the adapter's own xorshift noise voice (the Arcade Poly noise of the synth branch) instead of Felucca's engine */
    uint8_t eng;                 /* INSTRUMENT only: 0 Felucca ANALOG, 1 Felucca WHEEL (e[] is then REG, SUB, BODY, TOP, PERC, CLICK, DRV, ROTR) */
} px_patch;
enum { PX_PATCH_SLOTS = 10 };
/* Fix pass 4: CLASSIC, FATSAW, PULSE, PWM and SWEEP have the second oscillator at MIX 16 (was 64, 64, 40, 30, 64) and their trims lowered by the loudness this adds (0.6-1.8 dB): detuned
 * near-equal oscillators beat at 1-3 Hz, 6-10 dB deep, a phaser-like swell on a steady note; at 16 it is 3-4.4 dB. The other ANALOG patches are as written. */
static const px_patch PX_ANALOG_PATCHES[PX_PATCH_SLOTS] = {
    /* name       WAVE DTN MIX NOIS CUT RES DRV KTR     ATK DEC SUS REL  fenv semis trim pwm */
    {"CLASSIC", {0, 10, 16, 0, 90, 30, 10, 64}, {4, 70, 100, 50},  20,   0, 54,  0},   /* saw lead (Felucca's SAW LEAD) */
    {"FATSAW",  {0, 28, 16, 0, 85, 20, 20, 64}, {6, 70, 110, 55},  15,   0, 47,  0},   /* two detuned saws */
    {"PULSE",   {1,  8, 16, 0, 75, 25, 10, 64}, {3, 70, 100, 45},  15,   0, 52,  0},   /* square lead */
    {"PWM",     {4,  6, 16, 0, 80, 25,  0, 64}, {4, 70, 100, 50},  10,   0, 56, 70},   /* pulse width sweeping */
    {"SQ BASS", {1,  0,  0, 0, 50, 70, 40, 64}, {0, 60,  40, 30},  30, -12, 58,  0},   /* square bass (Felucca's SQR BASS), an octave down */
    {"SUB",     {3,  0,  0, 0, 40,  0, 20,  0}, {0, 60, 100, 20},   0, -12, 68,  0},   /* deep sine sub, an octave down */
    {"MOOG",    {6,  0, 40, 0, 35, 85, 30, 80}, {0, 55,  50, 35},  45, -12, 58,  0},   /* saw + sub square, resonant filter swept by each note */
    {"ACID",    {0,  0,  0, 0, 50,100, 25, 64}, {0, 55,  20, 30},  48, -12, 54,  0},   /* Felucca's ACID: saw, full resonance, a fast sweep */
    {"SWEEP",   {0, 14, 16, 0, 30, 50, 10, 64}, {50, 90, 100, 70}, 60,   0, 49,  0},   /* slow filter-sweep lead */
    {"SYNC",    {5, 30, 64, 0, 70, 25, 10, 64}, {3, 70, 100, 50},  25,   0, 56, 40},   /* hard-synced saw */
};
/* ARCADE: Felucca LOFI. CHIP 0 4BIT 1 4B/2 2 8BIT 3 1BIT 4 STEP (the stepped chip envelope: CRSH is then its DCY); WAVE 0 PLS 1 TRI 2 SAW 3 NOIS 4 WRAM (DUTY / 8 = the wave table);
 * ARP 0 OFF 1 OCT 2 MAJ 3 MIN. fenv is unused (LOFI's TONE is its filter). */
static const px_patch PX_ARCADE_PATCHES[PX_PATCH_SLOTS] = {
    /* name       CHIP WAVE DUTY CRSH SWP VIB ARP TONE    ATK DEC SUS REL  fenv semis trim pwm */
    {"ARCADE",  {0, 0, 32,  0,  0, 20, 0, 127}, {0, 60,  90, 30},  0,   0, 64, 0},   /* 4-bit pulse lead with a little vibrato (Felucca's PULSE LD) */
    {"CHIP",    {0, 0, 64,  0,  0,  0, 0, 127}, {0, 60, 100, 25},  0,   0, 64, 0},   /* chiptune pulse, 37.5 % */
    {"8BIT BS", {2, 1,  0,  0,  0,  0, 0, 110}, {0, 50,  70, 20},  0, -12, 92, 0},   /* 8-bit triangle bass, an octave down */
    {"WAVE",    {0, 4, 92,  0,  0, 18, 0, 110}, {0, 70,  90, 30},  0,   0, 96, 0},   /* wave-RAM lead ("ah", Felucca's WAVE LEAD) */
    {"STEP",    {4, 0, 40, 34,  0, 16, 0, 127}, {0, 64, 127, 55},  0,   0, 76, 0},   /* stepped-envelope lead, a 0.5 s staircase decay */
    {"ARP",     {2, 0, 96,  0,  0,  0, 2, 110}, {0, 60,  80, 40},  0,   0, 60, 0},   /* 8-bit major arpeggio on every note */
    {"ZAP",     {0, 0, 64,  0, 60,  0, 0, 127}, {0, 40,   0, 20},  0,   0, 80, 0},   /* retro game: the pitch sweeps down on every pick */
    {"PLUCK",   {4, 0, 64, 12,  0,  0, 0, 127}, {0, 64, 127, 30},  0,   0, 76, 0},   /* digital pluck: stepped one-shot decay */
    {"NOISE",   {0, 3,  0, 40,  0,  0, 0, 100}, {0, 40,  90, 30},  0,   0,140, 0, 1},   /* crushed noise, rate follows the note; xorshift as in Arcade Poly, sustains while held */
    {"MIN ARP", {2, 0, 64,  0,  0,  0, 3, 110}, {0, 60,  80, 40},  0,   0, 60, 0},   /* 8-bit minor arpeggio */
};
/* INSTRUMENT: Felucca ANALOG again, patches for sustained instrument colours, played by the same mono tracker (one note at a time: NOT polyphonic,
 * a chord does not make a string section). The first three are Felucca's own ANALOG presets (src/eng_analog.c at the pin: SOFT PAD, STRINGS, BRASS)
 * with E0..E7, ATK/DEC/SUS/REL and the filter-envelope amount copied as they stand; Felucca's chorus / reverb "FX" and pattern fields are not used
 * (OpenPocket's MOD, DELAY and REVERB blocks come after the SYNTH block for that). Fix pass 2: MIX (the second oscillator's level) is 16, not Felucca's 64: with no chorus after it, the two detuned saws beat at 1-3 Hz at 8-10 dB depth (a phaser-like swell on a steady note); at 16 it is 3-4 dB. SLOW STR and BRS LEAD are variations of STRINGS and BRASS (marked).
 * The slot is PATCH / 10 as for ANALOG: slots 0..4 (PATCH 0-49) are the ANALOG-engine patches, slots 5..9 (PATCH 50-100) the WHEEL organs (stage B). All ten
 * are defined and final, a defined slot is never reassigned, reordered or renamed, and the table is full. Slots 5..9 played BRSLEAD in builds 907-911, so a preset
 * saved then with PATCH 50 or more changes sound. Names: at most 7 characters. */
enum { PX_INST_DEFINED = 10 };
static const px_patch PX_INSTRUMENT_PATCHES[PX_PATCH_SLOTS] = {
    /* name       WAVE DTN MIX NOIS CUT RES DRV KTR     ATK DEC SUS REL  fenv semis trim pwm */
    {"SOFTPAD", {0, 20, 16, 4, 60, 10,  0, 32}, {80, 90, 110, 95},  10,   0, 64, 0},   /* Felucca SOFT PAD */
    {"STRINGS", {0, 25, 16, 0, 70, 10,  0, 32}, {70, 90, 115, 90},   5,   0, 64, 0},   /* Felucca STRINGS */
    {"BRASS",   {0, 10, 16, 0, 45, 20, 10, 64}, {35, 70,  90, 45},  40,   0, 64, 0},   /* Felucca BRASS: the filter opens on every note (fenv 40) */
    {"SLOWSTR", {0, 25, 16, 0, 62, 10,  0, 32}, {100, 90, 115, 105}, 4,   0, 64, 0},   /* variation of STRINGS: longer attack and release, a darker filter */
    {"BRSLEAD", {0, 10, 16, 0, 55, 25, 10, 64}, {15, 70,  95, 35},  48,   0, 62, 0},   /* variation of BRASS: faster attack, brighter, more filter movement */
    /* Stage B: Felucca WHEEL (a tonewheel organ: nine drawbars, percussion, key click, drive and a built-in rotary). e[] = REG SUB BODY TOP PERC CLICK DRV ROTR; ROTR 0 off 1 slow 2 fast.
     * ATK is 8 (7 ms): an organ speaks at once, the ATTACK setting runs 2-28 ms around it. Upstream's registration list is Felucca's (src/eng_wheel.c DRW_REG). */
    {"ORGAN",   {15, 0, 0, 0, 0, 30, 20, 1}, {8, 64, 127, 45},   0,   0, 98, 0, 0, 1},   /* Felucca FULL ORGAN: all nine bars, a little drive, slow rotary */
    {"JAZZORG", { 4, 0, 0, 0, 2, 50,  8, 1}, {8, 64, 127, 40},   0,   0, 106, 0, 0, 1},   /* Felucca JAZZ PERC: three bars and the third-harmonic (2 2/3') percussion, slow rotary */
    {"ROCKORG", { 7, 0, 0, 0, 0, 70,100, 2}, {8, 64, 127, 40},   0,   0, 73, 0, 0, 1},   /* Felucca ROCK DRIVE: body and the top bars, driven, fast rotary */
    {"REEDORG", {10, 0, 0, 0, 0, 20, 10, 0}, {8, 64, 127, 40},   0,   0, 86, 0, 0, 1},   /* the REED registration (a falling slope), no rotary: a reed organ */
    {"SOFTORG", { 1, 0,-2, 0, 0, 10,  0, 1}, {8, 64, 127, 55},   0,   0, 125, 0, 0, 1},   /* Felucca SOFT FLUTE: one soft flute bar, slow rotary */
};
static inline int px_inst_slot(int value) { int s = value / 10; return s < 0 ? 0 : (s >= PX_INST_DEFINED ? PX_INST_DEFINED - 1 : s); }
/* ENSEMBLE (Synth2 stage F): the INSTRUMENT patches played by several voices from the one tracked note (the mono tracker: the note you play, not the chord). The VOICING setting picks a
 * stack of up to PX_S2_NV voices: each has an interval (semitones), a small fixed detune (cents, scaled by SPREAD), a level (64 = 1.0) and an attack stretch (64 = the patch's own).
 * Like the INSTRUMENT patches the slots are PATCH / 10 and fixed forever (a defined slot is never reassigned or reordered); all ten are defined. Names: at most 7 characters. */
#ifndef PX_S2_NV
#define PX_S2_NV 5
#endif
typedef struct { int8_t semis, cents; uint8_t lvl, atk; } px_vc;
typedef struct { const char *name; uint8_t nv; px_vc v[PX_S2_NV]; uint8_t trim; } px_stack;   /* trim: 64 = 1.0, the stack's own level (set by ear and measurement) */
static const px_stack PX_STACKS[PX_PATCH_SLOTS] = {
    {"UNISON",  3, {{0, 0, 64, 64}, {0, -11, 36, 72}, {0, 10, 36, 80}}, 70},                                   /* the note and two drifting copies: a thicker, chorused single line */
    {"OCT UP",  2, {{0, -4, 64, 64}, {12, 5, 56, 80}}, 68},                                                    /* the note and the octave above (violins over the cellos) */
    {"OCT DN",  2, {{0, -4, 64, 64}, {-12, 5, 64, 72}}, 65},                                                   /* the note and the octave below */
    {"OCTAVES", 3, {{-12, -5, 56, 72}, {0, 0, 64, 64}, {12, 6, 52, 84}}, 67},                                  /* sub, note and octave above */
    {"FIFTH",   2, {{0, -4, 64, 64}, {7, 5, 56, 76}}, 67},                                                     /* a parallel fifth */
    {"POWER",   3, {{-12, -4, 56, 72}, {0, 0, 64, 64}, {7, 5, 56, 76}}, 66},                                   /* sub octave, root and fifth */
    {"MAJOR",   3, {{0, -3, 64, 64}, {4, 4, 56, 74}, {7, -5, 56, 84}}, 67},                                    /* a parallel major triad */
    {"MINOR",   3, {{0, -3, 64, 64}, {3, 4, 56, 74}, {7, -5, 56, 84}}, 67},                                    /* a parallel minor triad */
    {"MAJ 7",   4, {{0, -3, 64, 64}, {4, 4, 52, 72}, {7, -4, 52, 80}, {11, 5, 48, 88}}, 56},                  /* a major seventh chord */
    {"OPEN",    5, {{-12, -4, 52, 72}, {0, 0, 60, 64}, {7, 4, 52, 76}, {12, -5, 48, 84}, {16, 5, 44, 92}}, 58}, /* an open major voicing: sub octave, root, fifth, octave, tenth */
};
static inline int px_stack_slot(int value) { int s = value / 10; return s < 0 ? 0 : (s >= PX_PATCH_SLOTS ? PX_PATCH_SLOTS - 1 : s); }
/* ARCADE SPLIT: the note (MIDI) the lower and upper ranges meet at, from the SPLIT setting 0..100: E2 (40) .. E5 (76); notes from it up play UPPER */
static inline int px_split_note(int v) { v = v < 0 ? 0 : v > 100 ? 100 : v; return 40 + (v * 36 + 50) / 100; }
static inline int px_patch_slot(int value) { int s = value / 10; return s < 0 ? 0 : (s >= PX_PATCH_SLOTS ? PX_PATCH_SLOTS - 1 : s); }
#endif
