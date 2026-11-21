/* Host renderer for the ENHANCE listening/measurement study: raw float32 mono on stdin -> stdout through engine/px_enhance.h only (no engine chain).
 *   cc -O2 -std=c99 -Iengine [-DPXE_HPF_HZ=..] tests/engine/enhance_render.c -lm -o build/enhance_render ; enhance_render BETTER LOUDER SENS(0..3) VOICE(0..2) [gain_db]
 * gain_db scales the input first (e.g. -6: the pedal's ADC scaling of 0.5). The diagnostics (peak, RMS, share of the time the cell exceeded 0.5) go to stderr at the end. */
#include "px_enhance.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc < 5) return 1;
    px_enhance s; pxe_init(&s, 44100.f); pxe_set_input(&s, atoi(argv[3]), atoi(argv[4])); pxe_set(&s, (float)atof(argv[1]), (float)atof(argv[2]));
    const float g = argc > 5 ? powf(10.f, (float)atof(argv[5]) / 20.f) : 1.f; float x;
    while (fread(&x, sizeof x, 1, stdin) == 1) { const float y = pxe_tick(&s, x * g); fwrite(&y, sizeof y, 1, stdout); }
    float pk, rms, over; pxe_diag(&s, &pk, &rms, &over); fprintf(stderr, "peak %.1f dBFS rms(300ms) %.1f dBFS over0.5 %.3f\n", pk, rms, over);
    return 0;
}
