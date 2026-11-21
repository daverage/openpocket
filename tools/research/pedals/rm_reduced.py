"""The Rangemaster reduced to its two transistor-port equations (what the C engine runs). Checked against the full nodal simulator rm_circuit.py.
base: series chain EMF - Rs - L - C1 - base with the bias network R1||R2 (Thevenin Vth) at the base; emitter: R3 || C3; collector: RV to Vcc.
Trapezoidal L and C1 and C3; Ebers-Moll Ge transistor; Newton on (Vbe, Vbc); the volume is an attenuator on the collector (wiper)."""
import numpy as np
VT = 0.02585
class RMR:
    def __init__(s, fs, vol=1.0, Rs=6000.0, L=2.5, Is=1.8e-9, bf=100.0, br=8.0, C1=5e-9, R1=470e3, R2=68e3, R3=3.9e3, C3=47e-6, RV=10e3, Vcc=9.0):
        s.dt = 1 / fs; s.Is, s.bf, s.br, s.R3, s.RV, s.Vcc, s.x = Is, bf, br, R3, RV, Vcc, vol
        s.gL = s.dt / (2 * L); s.gC1 = 2 * C1 / s.dt; s.gC3 = 2 * C3 / s.dt
        s.Rtot = Rs + 1 / s.gL + 1 / s.gC1; s.Rbb = R1 * R2 / (R1 + R2); s.Vth = Vcc * R2 / (R1 + R2)
        s.Rpar = s.Rtot * s.Rbb / (s.Rtot + s.Rbb); s.Ge = 1 / R3 + s.gC3
        s.dc()
    def bjt(s, vbe, vbc):
        eb = np.exp(min(vbe / VT, 40.0)); ec = np.exp(min(vbc / VT, 40.0)); Is = s.Is
        ic = Is * (eb - ec) - Is * (ec - 1) / s.br; ib = Is * (eb - 1) / s.bf + Is * (ec - 1) / s.br
        gbe, gbc = Is * eb / VT, Is * ec / VT
        return ic, ib, gbe, gbc
    def newton(s, a_b, r_bb, r_be_c, a_c, r_cb, r_cc, vbe, vbc):
        # vbe = a_b - r_bb*ib - r_be_c*ic ; vbc = a_c - r_cb*ib - r_cc*ic
        for _ in range(40):
            ic, ib, gbe, gbc = s.bjt(vbe, vbc)
            F1 = vbe - (a_b - r_bb * ib - r_be_c * ic); F2 = vbc - (a_c - r_cb * ib - r_cc * ic)
            dic_dbe, dic_dbc, dib_dbe, dib_dbc = gbe, -gbc * (1 + 1 / s.br), gbe / s.bf, gbc / s.br
            J11 = 1 + r_bb * dib_dbe + r_be_c * dic_dbe; J12 = r_bb * dib_dbc + r_be_c * dic_dbc
            J21 = r_cb * dib_dbe + r_cc * dic_dbe; J22 = 1 + r_cb * dib_dbc + r_cc * dic_dbc
            det = J11 * J22 - J12 * J21; d1 = (-F1 * J22 + F2 * J12) / det; d2 = (-F2 * J11 + F1 * J21) / det
            dj = max(abs(d1), abs(d2)); k = 0.1 / dj if dj > 0.1 else 1.0
            vbe += k * d1; vbc += k * d2
            if dj < 1e-9: break
        ic, ib, *_ = s.bjt(vbe, vbc); return vbe, vbc, ib, ic
    def dc(s):
        # capacitors open, no series current: base Thevenin (Vth, Rbb), emitter R3, collector RV
        vbe, vbc, ib, ic = s.newton(s.Vth, s.Rbb + s.R3, s.R3, s.Vth - s.Vcc, s.Rbb, -s.RV, 0.25, -5.0)    # vbc = vb - vc = Vth - Rbb ib - (Vcc - RV ic)
        vb = s.Vth - s.Rbb * ib; ve = s.R3 * (ib + ic); vc = s.Vcc - s.RV * ic
        s.vbe, s.vbc = vbe, vbc; s.i = 0.0; s.vL = 0.0; s.vC = -vb; s.ve = ve; s.iC3 = 0.0; s.vc_dc = vc; s.dcv = (vb, ve, vc, ic)
        s.hp = 0.0
    def step(s, e):
        hL = s.i + s.gL * s.vL; hC = s.gC1 * s.vC + s.i; hE = s.gC3 * s.ve + s.iC3
        Ah = hL / s.gL - hC / s.gC1
        Vb0 = s.Rpar * ((e + Ah) / s.Rtot + s.Vth / s.Rbb); Ve0 = hE / s.Ge; Ge_inv = 1 / s.Ge
        # vbe = Vb0 - Ve0 - (Rpar + 1/Ge) ib - (1/Ge) ic ; vbc = Vb0 - Vcc - Rpar ib + RV ic   (vc = Vcc - RV ic)
        s.vbe, s.vbc, ib, ic = s.newton(Vb0 - Ve0, s.Rpar + Ge_inv, Ge_inv, Vb0 - s.Vcc, s.Rpar, -s.RV, s.vbe, s.vbc)
        vb = Vb0 - s.Rpar * ib; ve = Ve0 + (ib + ic) * Ge_inv; vc = s.Vcc - s.RV * ic
        i = (e - vb + Ah) / s.Rtot; s.vL = (i - hL) / s.gL; s.vC = (i + hC) / s.gC1; s.i = i
        s.iC3 = s.gC3 * ve - hE; s.ve = ve
        return s.x * (vc - s.vc_dc)                                         # AC part of the wiper voltage (C2 / Rload is a 16 Hz DC block)
    def run(s, e): return np.array([s.step(float(v)) for v in e])
if __name__ == '__main__':
    import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from rm_circuit import RM
    FS = 96000.0
    for lab, kw in (('single coil', {}), ('humbucker', dict(Rs=8000.0, L=5.0)), ('ideal', dict(Rs=1.0, L=1e-6))):
        for f, mv in ((1000, 10), (1000, 100), (3000, 100), (400, 300)):
            n = int(FS * 0.04); t = np.arange(n) / FS; e = mv * 1e-3 * np.sin(2 * np.pi * f * t)
            ya = RM(fs=FS, **kw).run(e); yb = RMR(FS, **kw).run(e)
            seg = slice(int(FS * 0.01), None); err = np.abs(ya[seg] - yb[seg]).max(); pk = np.abs(ya[seg]).max()
            print(f'{lab:12s} {f:5d} Hz {mv:4d} mV: full-MNA peak {pk:.4f} V, reduced-vs-full max diff {err:.5f} V ({100 * err / pk:.2f} %)')
