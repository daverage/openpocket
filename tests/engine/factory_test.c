/* The factory bank (v75: 52 presets, engine/px_dsp.c FACT):
 *  - every slot has a name (letters, digits and a few signs, at most 13), the names are different, the order is a permutation, every setting 0..100
 *  - no retired type anywhere, every type is a live one; a preset's blocks that are on are the ones it names (INIT: only the gate)
 *  - every preset plays as loud as the guitar alone (BS.1770 over the five bundled loops, tools/research/preset_levels.c): the mean within 0.5 dB, and the loudest peak under -3 dBFS
 *  - px_preset_canonical changes none of them (a factory preset is already clean) */
#define main level_audit_main                          /* (its loudness helpers) */
#include "../../tools/research/level_audit.c"
#undef main
static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    static const char *LOOPS[5] = {"arpeggio", "chords", "chug", "lead", "riff"};
    CHECK(px_preset_count() == 52, "52 factory presets");
    float *in[5], *out; int len[5];
    for (int l = 0; l < 5; l++) {
        char path[128]; snprintf(path, sizeof path, "assets/loops/%s.wav", LOOPS[l]);
        in[l] = load(path, &len[l]); if (!in[l]) { printf("no %s\n", path); return 1; }
        for (int i = 0; i < len[l]; i++) in[l][i] *= 0.15f;
    }
    int mx = 0; for (int l = 0; l < 5; l++) if (len[l] > mx) mx = len[l];
    out = malloc(sizeof(float) * (size_t)mx);
    double lo[5], dummy = 0; px_preset_t p, q;
    memset(&p, 0, sizeof p); px_order_default(p.order); p.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    for (int l = 0; l < 5; l++) { render(&p, in[l], out, len[l], &dummy); lo[l] = lufs(out, len[l]); }
    char names[52][14];
    for (int i = 0; i < 52; i++) {
        px_preset_factory(&p, i); memcpy(names[i], p.name, 14);
        int ok = p.name[0] != 0 && strlen(p.name) <= 13;
        for (const char *c = p.name; *c; c++) if (!((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ' ' || *c == '+' || *c == '-')) ok = 0;
        CHECK(ok, "slot %d: a valid name ('%s')", i + 1, p.name);
        for (int j = 0; j < i; j++) CHECK(strcmp(names[j], p.name), "slot %d: the name '%s' is not unique (slot %d)", i + 1, p.name, j + 1);
        CHECK(px_order_valid(p.order), "slot %d: the order is a permutation", i + 1);
        for (int b = 0; b < PX_NBLK; b++) {
            CHECK(!px_type_retired(b, p.type[b]) && p.type[b] < px_ntypes(b), "slot %d (%s): block %d holds a live type (%d)", i + 1, p.name, b, p.type[b]);
            for (int k = 0; k < PX_NPAR; k++) CHECK(p.p[b][k] <= 100, "slot %d: setting out of range", i + 1);
        }
        q = p; px_preset_canonical(&q);
        CHECK(!memcmp(&p, &q, sizeof p), "slot %d (%s): px_preset_canonical changes a factory preset", i + 1, p.name);
        if (i == 51) { int n = 0; for (int b = 0; b < PX_NBLK; b++) n += p.on[b]; CHECK(n == 1 && p.on[PX_B_GATE] && p.p[PX_B_GATE][PX_PATCH_LEVEL] == 50, "INIT: only the gate, LEVEL 0 dB"); }
        double mean = 0, pk = 0;
        for (int l = 0; l < 5; l++) {
            render(&p, in[l], out, len[l], &dummy);
            mean += (lufs(out, len[l]) - lo[l]) / 5;
            for (int k = 0; k < len[l]; k++) { if (!isfinite(out[k])) { CHECK(0, "slot %d: not finite", i + 1); break; } if (fabs(out[k]) > pk) pk = fabs(out[k]); }
        }
        CHECK(fabs(mean) < 0.5, "slot %d (%s): %.2f dB against the guitar alone", i + 1, p.name, mean);
        CHECK(20 * log10(pk + 1e-9) < -3.0, "slot %d (%s): peak %.1f dBFS", i + 1, p.name, 20 * log10(pk + 1e-9));
    }
    if (bad) { printf("%d problems\n", bad); return 1; }
    printf("FACTORY BANK ok: 0 problems\n"); return 0;
}
