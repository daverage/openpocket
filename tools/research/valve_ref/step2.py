#!/usr/bin/env python3
"""Step 2: what does the surrounding circuit add to the (validated) Dempwolf triode?  Same nonlinear equations in every variant.
  A  baseline        the validated, unloaded stage (33k stopper, 100k plate, 1.5k||2.2u cathode), ideal voltage source
  B  + parasitics    grid-plate 1.7 pF, grid-cathode 1.6 pF, plate-cathode 0.46 pF (12AX7 data-sheet values)
  C  + input network 1 Mohm grid leak and a source impedance: 0 ohm | 10 kohm | passive single-coil pickup + cable
  D  + coupling      22 nF input coupling cap, 22 nF output cap into a 1 Mohm external load
Usage: python3 step2.py run   (nonlinear transient runs -> data/step2/*.npz)      python3 step2.py ac   (small-signal AC -> data/step2/ac_*.npz)"""
import os, subprocess, sys, tempfile, numpy as np
from concurrent.futures import ProcessPoolExecutor
from scipy.signal import resample_poly
HERE = os.path.dirname(os.path.abspath(__file__)); OUT = f"{HERE}/data/step2"; FS, UP = 44100, 4
PICKUP = dict(Rdc=6e3, L=2.5, Cp=500e-12)       # single coil: 6 kohm, 2.5 H, coil + 3 m of cable ~500 pF  (resonance ~4.5 kHz, damped by the grid leak)
CIRCUITS = {   # name: (parasitics, input network, coupling, source)
    "A": (0, 0, 0, "0"), "B": (1, 0, 0, "0"),
    "C0": (1, 1, 0, "0"), "C10k": (1, 1, 0, "10k"), "Cpu": (1, 1, 0, "pickup"),
    "D0": (1, 1, 1, "0"), "Cpu_nopar": (0, 1, 0, "pickup"), "D10k": (1, 1, 1, "10k"), "Dpu": (1, 1, 1, "pickup")}

def circuit(name, drive):
    par, inp, cpl, src = CIRCUITS[name]; L = []
    L += ["Vbb b 0 250", "Rp b p 100k", "Vsa p pt 0", "Rk k 0 1.5k", "Ck k 0 2.2u", "Vsk kt k 0", "XT pt gt kt tube_dempwolf", "Rs a g 33k", "Vsg g gt 0"]
    if par: L += ["Cgk gt kt 1.6p", "Cgp gt pt 1.7p", "Cpk pt kt 0.46p"]
    # source network: EMF Vin -> n
    if src == "0": L += ["Rsrc vin n 1m"]
    elif src == "10k": L += ["Rsrc vin n 10k"]
    else: L += [f"Rdc vin m1 {PICKUP['Rdc']}", f"Lp m1 n {PICKUP['L']}", f"Cpick n 0 {PICKUP['Cp']}"]
    if cpl: L += ["Cin n a 22n"]
    else: L += ["Rlink n a 1m"]
    if inp: L += ["Rleak a 0 1Meg"]
    L += (["Cc p out 22n", "Rl out 0 1Meg"] if cpl else ["Cc p out 22p", "Rl out 0 1G"])
    return "\n".join(L)

def netlist(name, mode, x=None, drive=0, tag=""):
    head = f"* step2 circuit {name}\n.include {HERE}/models/dempwolf.lib\n"
    if mode == "ac":
        return head + circuit(name, drive) + f"\nVin vin 0 DC 0 AC 1\n.options reltol=1e-4 abstol=1e-12 vntol=1e-9\n.ac dec 60 5 200k\n.control\nset num_threads=1\nrun\nset wr_singlescale\nwrdata {tag}.dat v(vin) v(n) v(a) v(g) v(pt) v(out)\nquit\n.endc\n.end\n"
    g = 4.0 * 10 ** (drive / 20); xu = resample_poly(x.astype(np.float64), UP, 1); n = len(xu); dt = 1.0 / (FS * UP)
    pts = [f"{i*dt:.9e} {g*xu[i]:.9e}" for i in range(n)]; pwl = "\n".join("+ " + " ".join(pts[i:i+4]) for i in range(0, n, 4))
    return (head + circuit(name, drive) + f"\nVin vin 0 PWL(\n{pwl}\n+ )\n.options reltol=1e-4 abstol=1e-12 vntol=1e-9 method=trap itl4=100\n.tran {dt:.9e} {(n-1)*dt:.9e} 0 {dt:.9e}\n"
            f".control\nset num_threads=1\nrun\nlinearize v(vin) v(n) v(g) v(k) v(pt) v(out) i(vsa) i(vsg)\nset wr_singlescale\nwrdata {tag}.dat v(vin) v(n) v(g) v(k) v(pt) v(out) i(vsa) i(vsg)\nquit\n.endc\n.end\n")

def run_one(job):
    mode, name, test, drive = job
    f = f"{OUT}/{'ac_' if mode == 'ac' else ''}{name}_{test}_{drive}.npz"
    if os.path.exists(f): return job, 0
    d = tempfile.mkdtemp(prefix="s2_"); x = None if mode == "ac" else np.fromfile(f"{HERE}/data/in/{test}.f32", "<f4")
    open(f"{d}/c.cir", "w").write(netlist(name, mode, x, drive, f"{d}/o"))
    r = subprocess.run(["ngspice", "-b", f"{d}/c.cir"], capture_output=True, text=True, env={**os.environ, "OMP_NUM_THREADS": "1"}, timeout=3600)
    if not os.path.exists(f"{d}/o.dat"): raise RuntimeError(f"{job}: {r.stdout[-1500:]}")
    a = np.loadtxt(f"{d}/o.dat")
    if mode == "ac":
        c = a[:, 1:].reshape(len(a), -1, 2); np.savez_compressed(f, f=a[:, 0], **{k: c[:, i, 0] + 1j * c[:, i, 1] for i, k in enumerate(["vin", "n", "a", "g", "p", "out"])})
    else:
        c = a[:, 1:]; np.savez_compressed(f, t=a[:, 0], **{k: c[:, i] for i, k in enumerate(["vin", "n", "g", "k", "p", "out"])}, ia=c[:, 6] * 1e3, ig=c[:, 7] * 1e3)
    return job, 1

TESTS = [("sine_220_0.05", d) for d in (0, 6, 12)] + [("imd_guitar_440_660_0.02", d) for d in (0, 6, 12)] + [("bias_recovery", 12), ("transient_pick", 12)]
if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    jobs = [("ac", c, "ac", 0) for c in CIRCUITS] if sys.argv[1] == "ac" else [("tran", c, t, d) for t, d in TESTS for c in CIRCUITS]
    if len(sys.argv) > 2: jobs = [j for j in jobs if any(s in f"{j[1]}_{j[2]}" for s in sys.argv[2:])]
    n = 0
    with ProcessPoolExecutor(14) as ex:
        for j, ran in ex.map(run_one, jobs): n += 1; print(n, len(jobs), j[1], j[2], j[3], flush=True)
