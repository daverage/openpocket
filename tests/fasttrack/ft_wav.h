/* shared WAV reader (PCM16/24/32, float; mono or stereo mixed down) and a PCM16 mono writer */
#ifndef FT_WAV_H
#define FT_WAV_H
static float *ft_rd(const char *p, long *n)
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
static void ft_wr16(FILE *f, const short *x, long n)
{
    unsigned dsz = (unsigned)(2 * n), rate = 44100, br = 88200;
    fwrite("RIFF", 1, 4, f); unsigned v = 36 + dsz; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    short s2 = 1; fwrite(&s2, 2, 1, f); fwrite(&s2, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); s2 = 2; fwrite(&s2, 2, 1, f); s2 = 16; fwrite(&s2, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dsz, 4, 1, f); fwrite(x, 2, n, f);
}
#endif
