/* Rangemaster circuit core (engine/px_pedals.h) against the Python reduced reference (tools/research/pedals/rm_reduced.py, itself checked against the full nodal simulator).
 * Prints the sine response at the circuit rate 96 kHz with no oversampling; the expected numbers are asserted in the engine test (pedals_test.c). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "px_pedals.h"
int main(int argc, char **argv)
{
    const float f = argc > 1 ? (float)atof(argv[1]) : 1000.0f, mv = argc > 2 ? (float)atof(argv[2]) : 100.0f, pk = argc > 3 ? (float)atof(argv[3]) : 0.0f;
    px_rm r; pxp_rm_init(&r, 96000.0f / (float)PXP_OS); pxp_rm_set(&r, 100.0f, pk);
    r.x = r.x_t = 1.0f;
    const int n = 96000 * 6 / 10; double re[3] = {0}, im[3] = {0}; float mx = -1e9f, mn = 1e9f; int cnt = 0;
    for (int i = 0; i < n; i++) {
        const float y = pxp_rm_core(&r, mv * 1e-3f * sinf(6.2831853f * f * (float)i / 96000.0f));
        if (i >= n / 2) { for (int k = 0; k < 3; k++) { const double w = 6.283185307179586 * f * (k + 1) * i / 96000.0; re[k] += y * cos(w); im[k] += y * sin(w); } if (y > mx) mx = y; if (y < mn) mn = y; cnt++; }
    }
    double a[3]; for (int k = 0; k < 3; k++) a[k] = 2.0 * sqrt(re[k] * re[k] + im[k] * im[k]) / cnt;
    printf("%g Hz %g mV pickup %g: gain %.2f dB H2 %.1f H3 %.1f peak %.4f / %.4f\n", f, mv, pk, 20 * log10(a[0] / (mv * 1e-3)), 20 * log10(a[1] / a[0] + 1e-12), 20 * log10(a[2] / a[0] + 1e-12), mx, mn);
    return 0;
}
