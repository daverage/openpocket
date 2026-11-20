/* Synth2 fix pass 1: what the mono tracker tells the synth around each note start. For every onset: its time, the f0 it sounds at, the
 * f0 changes of more than 5 % in the next 150 ms (and when), and the f0 150 ms later. Usage: note_on_diag IN.wav [bass]
 *   cc -O2 -Iengine -Ifirmware tools/sim/note_on_diag.c firmware/px_math.c -lm -o build/note_on_diag */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
static float *rd(const char *p, long *n)
{
    FILE *in = fopen(p, "rb"); if (!in) return 0;
    unsigned char h[12]; if (fread(h, 1, 12, in) != 12) return 0;
    int ch = 1, bits = 16, fmt = 1; unsigned dsz = 0;
    for (;;) { unsigned char c[8]; if (fread(c, 1, 8, in) != 8) return 0;
        const unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { unsigned char b[16]; if (fread(b, 1, 16, in) != 16) return 0; fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; bits = b[14] | b[15] << 8; fseek(in, (long)sz - 16, SEEK_CUR); }
        else if (!memcmp(c, "data", 4)) { dsz = sz; break; } else fseek(in, (long)sz + (sz & 1), SEEK_CUR); }
    const int bps = bits / 8; *n = dsz / (bps * ch); float *x = malloc(sizeof(float) * (*n));
    for (long i = 0; i < *n; i++) { double a = 0;
        for (int c = 0; c < ch; c++) {
            if (fmt == 3) { float v; if (fread(&v, 4, 1, in) != 1) v = 0; a += v; }
            else if (bits == 16) { int16_t v; if (fread(&v, 2, 1, in) != 1) v = 0; a += v / 32768.0; }
            else if (bits == 24) { unsigned char b[3]; if (fread(b, 1, 3, in) != 3) memset(b, 0, 3); int v = b[0] | b[1] << 8 | b[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF; a += v / 8388608.0; }
            else { int32_t v; if (fread(&v, 4, 1, in) != 1) v = 0; a += v / 2147483648.0; } }
        x[i] = (float)(a / ch); }
    fclose(in); return x;
}
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    long m; float *x = rd(argv[1], &m); if (!x) return 1;
    static px_engine e; px_engine_init(&e, 44100.0f);
    static px_mono mo; px_mono_init(&e, &mo, argc > 2 && argv[2][0]=='b' ? PX_MONO_BASS : PX_MONO_GUITAR); if (getenv("MINWAIT")) mo.lay.minwait = atoi(getenv("MINWAIT")); if (getenv("MWF")) mo.lay.minwait_f = (float)atof(getenv("MWF"));
    long rise = 0; int pk = 0; double lat = 0; int nl = 0;
    float dc_x1 = 0, dc_y1 = 0, last = 0; long on_i = -1; int nchg = 0, notes = 0, wrong = 0, total_chg = 0, retr = 0; float f_on = 0;
    for (long i = 0; i < m + 8000; i++) {
        float v = i < m ? x[i] : 0.0f; v += (i & 1) ? 1e-15f : -1e-15f;
        float y = v - dc_x1 + 0.9995f * dc_y1; dc_x1 = v; dc_y1 = y;
        const int k0 = mo.lay.key; px_mono_out o; px_mono_run(&e, &mo, y, &o); if (!k0 && mo.lay.key) rise = i;
        if (o.onset) {
            if (on_i >= 0 && nchg == 0) {} 
            if (rise && i - rise < 4000) { lat += (i - rise) / 44.1; nl++; } on_i = i; f_on = o.f0; nchg = 0; last = o.f0; notes++; if (o.retrigger) retr++;
            printf("%.3f onset f0=%.1f vel=%.2f%s\n", i / 44100.0, o.f0, o.vel, o.retrigger ? " RETRIG" : "");
        } else if (on_i >= 0 && o.gate && i - on_i < 6615 && last > 0 && (o.f0 / last > 1.05f || o.f0 / last < 0.95f)) {
            printf("   +%.1f ms f0 %.1f -> %.1f (x%.2f)\n", (i - on_i) / 44.1, last, o.f0, o.f0 / last); last = o.f0; nchg++; total_chg++;
            if (f_on > 0 && o.f0 < 0.6f * f_on) wrong++;
        } else if (o.gate) last = o.f0;
    }
    printf("mean rise->onset %.1f ms over %d\n", nl ? lat / nl : 0.0, nl); printf("notes %d, retriggers %d, f0 changes >5%% within 150 ms: %d, of which a drop of an octave or more from the onset pitch: %d\n", notes, retr, total_chg, wrong);
    return 0;
}
