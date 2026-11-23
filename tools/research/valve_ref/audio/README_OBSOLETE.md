# OBSOLETE: do not use these files for listening comparisons

Everything in `audio/named/` and `audio/blind/` was rendered with the SPICE reference that had a **1 MOhm load** on the plate
(the netlist then used 21 nF / 1 MOhm). That load raises the plate-current gain by about 0.47 dB and adds distortion
(see `REPORT.md`, Step 1). Against the unloaded circuit the DSP agrees to about 0.01 dB, so these "SPICE" files are not a fair reference.

The WAV files are not tracked in Git. Regenerate them from an unloaded netlist (`RL, CC = 1e9, 22e-12` in `gen_spice.py`) before any blind test.
