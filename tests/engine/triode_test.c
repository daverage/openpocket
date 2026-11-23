/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include "px_dsp.c"

int main(void)
{
    /* The stopper and the grid-current law must agree across the table join and tail. */
    for (int i = 0; i <= 6400; ++i) {
        const float volts = (float)i * 0.01f;
        const double grid = triode_grid_current(volts);
        const double grid_to_cathode = volts - PXT_STOPPER * grid;
        const double expected = 0.6177 * pow(log1p(exp(9.901 * grid_to_cathode)) / 9.901, 1.314) + 0.00008025;
        if (fabs(grid - expected) > 0.001) {
            fprintf(stderr, "VALVE: grid load-line error at %.2f V: %.6f mA\n", volts, grid - expected);
            return 1;
        }
    }

    /* The load line must keep the plate above the cathode on boosted inputs. */
    for (int bias = 0; bias <= 4; ++bias)
        for (int input = -640; input <= 960; ++input) {
            const float vk = (float)bias;
            const float vin = (float)input * 0.1f;
            const triode_currents current = triode_load_line(vin, vk);
            const float plate_to_cathode = PXT_SUPPLY - PXT_PLATE_KOHM * current.plate - vk;
            if (!(plate_to_cathode >= 0.0f && current.cathode >= 0.0f)) {
                fprintf(stderr, "VALVE: invalid load line at input %.1f V, cathode %.1f V\n", vin, vk);
                return 1;
            }
        }
    puts("VALVE: grid load line and plate voltage limits OK");
    return 0;
}
