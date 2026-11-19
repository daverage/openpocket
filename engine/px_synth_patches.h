/* OpenPocket Synth2: the ANALOG type's patches. A patch is a fixed table entry: Felucca's ANALOG parameters (E0..E7), its amplitude
 * envelope and filter-envelope amount, a transposition and a level trim. The block's PATCH setting (0..100) selects one in fixed bands of 10
 * (slot = value / 10, 100 = slot 9), so a patch keeps its band whatever is added later; a slot with no patch plays the last one defined.
 * The other settings are macros around the patch's own values (px_synth_adapter.h), 50 = the patch as written.
 * These are sound-design starting points built from Felucca's ANALOG engine; they do not model any particular vintage instrument. */
#ifndef PX_SYNTH_PATCHES_H
#define PX_SYNTH_PATCHES_H
#include <stdint.h>
typedef struct {
    const char *name;            /* at most 7 characters (the screen's value text) */
    int8_t e[8];                 /* Felucca ANALOG: WAVE, DTN, MIX, NOIS, CUT, RES, DRV, KTR (engine/../third_party eng_analog.c) */
    uint8_t env[4];              /* ATK DEC SUS REL, 0..127 */
    int8_t fenv;                 /* envelope -> filter cutoff, -64..63 */
    int8_t semis;                /* transposition from the guitar's note */
    uint8_t trim;                /* level trim, 64 = 1.0 */
    uint8_t pwm;                 /* pulse-width sweep (the WAVE PWM and SYNC), 0 none .. 127 */
} px_patch;
enum { PX_PATCH_SLOTS = 10 };
static const px_patch PX_ANALOG_PATCHES[PX_PATCH_SLOTS] = {
    /* name       WAVE DTN MIX NOIS CUT RES DRV KTR     ATK DEC SUS REL  fenv semis trim pwm */
    {"CLASSIC", {0, 10, 64, 0, 90, 30, 10, 64}, {4, 70, 100, 50},  20,   0, 64,  0},   /* saw lead (Felucca's SAW LEAD) */
    {"FATSAW",  {0, 28, 64, 0, 85, 20, 20, 64}, {6, 70, 110, 55},  15,   0, 56,  0},   /* two detuned saws */
    {"PULSE",   {1,  8, 40, 0, 75, 25, 10, 64}, {3, 70, 100, 45},  15,   0, 60,  0},   /* square lead */
    {"PWM",     {4,  6, 30, 0, 80, 25,  0, 64}, {4, 70, 100, 50},  10,   0, 60, 70},   /* pulse width sweeping */
    {"SQ BASS", {1,  0,  0, 0, 50, 70, 40, 64}, {0, 60,  40, 30},  30, -12, 58,  0},   /* square bass (Felucca's SQR BASS), an octave down */
    {"SUB",     {3,  0,  0, 0, 40,  0, 20,  0}, {0, 60, 100, 20},   0, -12, 68,  0},   /* deep sine sub, an octave down */
    {"MOOG",    {6,  0, 40, 0, 35, 85, 30, 80}, {0, 55,  50, 35},  45, -12, 58,  0},   /* saw + sub square, resonant filter swept by each note */
    {"ACID",    {0,  0,  0, 0, 50,100, 25, 64}, {0, 55,  20, 30},  48, -12, 54,  0},   /* Felucca's ACID: saw, full resonance, a fast sweep */
    {"SWEEP",   {0, 14, 64, 0, 30, 50, 10, 64}, {50, 90, 100, 70}, 60,   0, 60,  0},   /* slow filter-sweep lead */
    {"SYNC",    {5, 30, 64, 0, 70, 25, 10, 64}, {3, 70, 100, 50},  25,   0, 56, 40},   /* hard-synced saw */
};
static inline int px_patch_slot(int value) { int s = value / 10; return s < 0 ? 0 : (s >= PX_PATCH_SLOTS ? PX_PATCH_SLOTS - 1 : s); }
#endif
