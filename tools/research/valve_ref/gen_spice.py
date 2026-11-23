#!/usr/bin/env python3
"""Build + run the ngspice reference for one input file.  python3 gen_spice.py MODEL in.f32 out.npz drive_db [--cpar]
MODEL: dempwolf | koren.   in.f32 = mono float32 at 44.1 kHz in engine units; 4 V per unit at 0 dB drive.
Output npz (all at 4x the engine rate = 176.4 kHz, uniform grid, SPICE time 0 == input sample 0):
    t, vin, vg, vk, vp, vout, ia, ig, ik"""
import os, subprocess, sys, tempfile, numpy as np
from scipy.signal import resample_poly
HERE = os.path.dirname(os.path.abspath(__file__))
FS, UP = 44100, 4
B, RP, RK, CK, RS = 250.0, 100e3, 1.5e3, 2.2e-6, 33e3
RL, CC = 1e9, 22e-12   # output coupling into a practically infinite load, as the DSP assumes: tau = Cc*Rl = 22 ms exactly (first runs used 1 Mohm / 21 nF, see REPORT.md)

def netlist(model, x, drive_db, cpar, tag, dspin=False):
    """dspin=False: x = the 44.1 kHz input samples, reconstructed band-limited (x4) -> volts = 4*10^(dB/20)*x.
       dspin=True : x = the DSP's own internal oversampled grid voltage (88.2 kHz, volts, valve_host *.vin.f32), x2 -> the SPICE
                    stage sees exactly the samples the engine's triode stage sees (isolates the valve model from the up-sampler)."""
    if dspin:
        g = 1.0; xu = resample_poly(x.astype(np.float64), 2, 1); n = len(xu); dt = 1.0 / (FS * 4)
    else:
        g = 4.0 * 10 ** (drive_db / 20)
        xu = resample_poly(x.astype(np.float64), UP, 1) if UP > 1 else x   # band-limited reconstruction of the same samples
        n = len(xu); dt = 1.0 / (FS * UP)
    pts = []
    for i in range(n):
        pts.append(f"{i*dt:.9e} {g*xu[i]:.9e}")
    pwl = "\n".join("+ " + " ".join(pts[i:i+4]) for i in range(0, len(pts), 4))
    lib = "dempwolf.lib" if model == "dempwolf" else "koren.lib"
    xs = "XT pt gt kt tube_dempwolf" if model == "dempwolf" else f"XT pt gt kt tube_koren params: cpar={1 if cpar else 0}"
    net = f"""* OpenPocket 12AX7 common-cathode stage, {model}, drive {drive_db} dB, cpar={cpar}
.include {HERE}/models/{lib}
Vbb b 0 {B}
Rp b p {RP}
Vsa p pt 0
Vin in 0 PWL(
{pwl}
+ )
Rs in g {RS}
Vsg g gt 0
Rk k 0 {RK}
Ck k 0 {CK}
Vsk kt k 0
Cc p out {CC}
Rl out 0 {RL}
{xs}
.options reltol=1e-3 abstol=1e-12 vntol=1e-9 method=trap itl4=100
.tran {dt:.9e} {(n-1)*dt:.9e} 0 {dt:.9e}
.control
set noaskquit
set num_threads=1
run
linearize v(in) v(g) v(k) v(p) v(out) i(vsa) i(vsg) i(vsk)
set wr_singlescale
wrdata {tag}.dat v(in) v(g) v(k) v(p) v(out) i(vsa) i(vsg) i(vsk)
quit
.endc
.end
"""
    return net, n, dt

def run(model, x, drive_db, cpar=False, keep=None, dspin=False):
    d = tempfile.mkdtemp(prefix="vref_")
    net, n, dt = netlist(model, x, drive_db, cpar, os.path.join(d, "o"), dspin)
    cir = os.path.join(d, "c.cir"); open(cir, "w").write(net)
    if keep: open(keep, "w").write(net.split("Vin in 0 PWL(")[0] + "... PWL omitted ...\n" + net.split("+ )")[1])
    r = subprocess.run(["ngspice", "-b", cir], capture_output=True, text=True, timeout=7200, env={**os.environ, "OMP_NUM_THREADS": "1"})
    dat = os.path.join(d, "o.dat")
    if not os.path.exists(dat):
        raise RuntimeError("ngspice failed:\n" + r.stdout[-3000:] + r.stderr[-1000:])
    a = np.loadtxt(dat)                # wr_singlescale: t, then (v) per vec; columns: t, v1, t?, ... handle both
    # wrdata with single scale writes: time v1 v2 ... (8 vectors) ; without, pairs.  Detect:
    if a.shape[1] == 9: cols = a[:, 1:]
    else: cols = a[:, 1::2]
    t = a[:, 0]
    out = dict(t=t, vin=cols[:, 0], vg=cols[:, 1], vk=cols[:, 2], vp=cols[:, 3], vout=cols[:, 4],
               ia=cols[:, 5] * 1e3, ig=cols[:, 6] * 1e3, ik=cols[:, 7] * 1e3)   # currents in mA, tube sign (into plate / grid / cathode)
    return out, r.stdout

if __name__ == "__main__":
    model, inp, outp, db = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
    x = np.fromfile(inp, "<f4")
    o, log = run(model, x, db, cpar="--cpar" in sys.argv)
    np.savez_compressed(outp, **o)
