#!/usr/bin/env python3
"""Step 1 of the validation: three short UNLOADED Dempwolf runs fed from the DSP's own 88.2 kHz grid voltage (data/dsp/*.vin.f32).
Output: data/spice_hiZ/dspin_dempwolf_<test>_<drive>.npz"""
import os, sys
os.environ["VREF_SPICE"] = "spice_hiZ"
from concurrent.futures import ProcessPoolExecutor
import run_all
JOBS = [("sine_220_0.05", 0), ("sine_220_0.05", 12), ("imd_guitar_440_660_0.02", 0), ("imd_guitar_440_660_0.02", 12), ("bias_recovery", 12), ("bias_recovery", 24)]
if __name__ == "__main__":
    os.makedirs(run_all.SPICE, exist_ok=True)
    with ProcessPoolExecutor(6) as ex:
        for a, ran in ex.map(run_all.job, [("spice", n, d, "dspin_dempwolf", False) for n, d in JOBS]): print(a[1], a[2], "ran" if ran else "cached", flush=True)
