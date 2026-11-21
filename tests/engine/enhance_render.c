/* Host renderer for the ENHANCE listening/measurement study: raw float32 mono on stdin -> stdout through engine/px_enhance.h only (no engine chain).
 *   cc -O2 -std=c99 -Iengine tests/engine/enhance_render.c -lm -o build/enhance_render ; enhance_render BETTER LOUDER TONE [gain_db] */
#include "px_enhance.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc < 4) return 1;
    px_enhance s; pxe_init(&s, 44100.f); pxe_set(&s, (float)atof(argv[1]), (float)atof(argv[2]), (float)atof(argv[3]));
    const float g = argc > 4 ? powf(10.f, (float)atof(argv[4]) / 20.f) : 1.f; float x;
    while (fread(&x, sizeof x, 1, stdin) == 1) { const float y = pxe_tick(&s, x * g); fwrite(&y, sizeof y, 1, stdout); }
    return 0;
}
