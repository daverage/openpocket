"""Dallas Rangemaster as a CIRCUIT (reference simulator, offline): modified nodal analysis, trapezoidal capacitors and inductor, Ebers-Moll germanium transistor solved by Newton at every step.
Component values: ElectroSmash 'Dallas Rangemaster Treble Booster Analysis' (R1 470k, R2 68k, R3 3.9k, C1 5n, C2 10n, C3 47u, RV 10k log, 9 V, OC44).
The original is a PNP stage with the POSITIVE rail at ground; it is simulated here as the equivalent NPN stage on +9 V (every voltage mirrored), which has identical behaviour.
Source: the guitar's open-circuit signal as an EMF behind the pickup's series resistance and inductance (a pickup is a source with a frequency-dependent impedance:
the Rangemaster's 10-12 kOhm input makes that visible, as ElectroSmash says)."""
import numpy as np
VT = 0.02585
class RM:
    def __init__(s, fs=192000.0, vol=1.0, Rs=6000.0, L=2.5, Is=1.8e-9, bf=100.0, br=8.0, Icbo=0.0, Cbe=0.0, Cbc=0.0, C1=5e-9, R1=470e3, R2=68e3, R3=3.9e3, C3=47e-6, C2=10e-9, RV=10e3, Rload=1e6, Vcc=9.0):
        s.dt = 1.0 / fs; s.p = dict(Rs=Rs, L=L, Is=Is, bf=bf, br=br, Icbo=Icbo, Cbe=Cbe, Cbc=Cbc, C1=C1, R1=R1, R2=R2, R3=R3, C3=C3, C2=C2, RV=RV, Rload=Rload, Vcc=Vcc, vol=vol)
        # nodes: 0 S(source after EMF, via Rs) 1 P(after L) 2 B 3 E 4 C 5 W(wiper) 6 O(output) ; branch current: inductor iL as an extra unknown via companion; Vcc and EMF are ideal sources -> handled analytically
        s.reset()
    def conduct(s):
        p = s.p; dt = s.dt; g = np.zeros((7, 7))
        def res(a, b, R):
            gg = 1.0 / R
            if a >= 0: g[a, a] += gg
            if b >= 0: g[b, b] += gg
            if a >= 0 and b >= 0: g[a, b] -= gg; g[b, a] -= gg
        def cap(a, b, C):
            gg = 2.0 * C / dt; res_ = None
            if a >= 0: g[a, a] += gg
            if b >= 0: g[b, b] += gg
            if a >= 0 and b >= 0: g[a, b] -= gg; g[b, a] -= gg
            return gg
        # source node S: EMF through Rs -> node 0 ; inductor from node 0 to P(1): handled as companion with g = dt/(2L)
        res(0, -1, 1e12)                                    # (S is driven through Rs by the EMF: stamp as conductance to the source below)
        gRs = 1.0 / p['Rs']; g[0, 0] += gRs
        gL = dt / (2.0 * p['L']); g[0, 0] += gL; g[1, 1] += gL; g[0, 1] -= gL; g[1, 0] -= gL
        gC1 = cap(1, 2, p['C1'])
        res(2, -1, p['R2']); g[2, 2] += 0.0               # R2 to ground; R1 to Vcc (a conductance to a fixed node: stamped as g + known current)
        g[2, 2] += 1.0 / p['R1']
        res(3, -1, p['R3']); gC3 = cap(3, -1, p['C3'])
        # collector load: RV between C(4) and Vcc, wiper position x from the Vcc end: C -(1-x)RV- W -(x)RV- Vcc
        x = min(max(p['vol'], 1e-3), 1 - 1e-3); Rt = (1 - x) * p['RV']; Rb = x * p['RV']
        res(4, 5, max(Rt, 1.0)); g[5, 5] += 1.0 / max(Rb, 1.0)
        gC2 = cap(5, 6, p['C2']); res(6, -1, p['Rload'])
        # junction capacitances (companion), base-emitter and base-collector
        gCbe = cap(2, 3, p['Cbe']) if p['Cbe'] > 0 else 0.0
        gCbc = cap(2, 4, p['Cbc']) if p['Cbc'] > 0 else 0.0
        s.g = g; s.gRs = gRs; s.gL = gL; s.gC1 = gC1; s.gC3 = gC3; s.gC2 = gC2; s.gCbe = gCbe; s.gCbc = gCbc; s.x = x
    def reset(s):
        s.conduct(); p = s.p
        # DC operating point: capacitors open, inductor short, Newton on (Vb, Ve, Vc) with the pot as the collector load to Vcc through the full RV
        Vcc = p['Vcc']; vb, ve, vc = 1.1, 0.8, 6.0
        for _ in range(100):
            vbe, vbc = vb - ve, vb - vc; ic, ib, *_ = s.bjt(vbe, vbc); ie = ic + ib
            f = np.array([(Vcc - vb) / p['R1'] - vb / p['R2'] - ib, ie - ve / p['R3'], (Vcc - vc) / p['RV'] - ic])
            J = np.zeros((3, 3)); h = 1e-6
            for k, d in enumerate((np.array([h, 0, 0]), np.array([0, h, 0]), np.array([0, 0, h]))):
                vbb, vee, vcc_ = vb + d[0], ve + d[1], vc + d[2]; ic2, ib2, *_ = s.bjt(vbb - vee, vbb - vcc_); ie2 = ic2 + ib2
                f2 = np.array([(Vcc - vbb) / p['R1'] - vbb / p['R2'] - ib2, ie2 - vee / p['R3'], (Vcc - vcc_) / p['RV'] - ic2]); J[:, k] = (f2 - f) / h
            dx = np.linalg.solve(J, -f); vb, ve, vc = vb + dx[0], ve + dx[1], vc + dx[2]
            if np.abs(dx).max() < 1e-12: break
        s.dc = dict(Vb=vb, Ve=ve, Vc=vc, Ic=ic, Ie=ie, Vbe=vb - ve)
        # states: node voltages v (7), inductor current, capacitor currents, previous values
        s.v = np.array([0.0, 0.0, vb, ve, vc, vc * (0.0) , 0.0]); s.v[1] = 0.0; s.v[0] = 0.0
        # C1: P(0V) -> B(vb): P sits at 0 V DC (the EMF has no DC), cap charged to -vb ; C2: wiper -> out
        x = s.x; s.v[5] = Vcc - (Vcc - vc) * x               # wiper DC (pot divider)
        s.iL = 0.0; s.iC1 = 0.0; s.iC3 = 0.0; s.iC2 = 0.0; s.iCbe = 0.0; s.iCbc = 0.0
        s.vprev = s.v.copy(); s.e_prev = 0.0
    def bjt(s, vbe, vbc):
        p = s.p; Is, bf, br = p['Is'], p['bf'], p['br']
        eb = np.exp(min(vbe / VT, 60.0)); ec = np.exp(min(vbc / VT, 60.0))
        icc = Is * (eb - 1.0); iec = Is * (ec - 1.0)
        ic = icc - iec * (1.0 + 1.0 / br) + p['Icbo'] * 0.0
        ib = icc / bf + iec / br + p['Icbo']
        gbe = Is * eb / VT; gbc = Is * ec / VT
        # partials: d ic/d vbe = gbe ; d ic/d vbc = -gbc*(1+1/br) ; d ib/d vbe = gbe/bf ; d ib/d vbc = gbc/br
        return ic, ib, gbe, gbc
    def step(s, e):
        p = s.p; dt = s.dt; v = s.v; Vcc = p['Vcc']
        # history currents of the companions (trapezoid: i_n = g*v_n + Ihist, Ihist = -g*v_{n-1} - i_{n-1} for capacitors)
        gL, gC1, gC3, gC2 = s.gL, s.gC1, s.gC3, s.gC2
        vprev = s.vprev; Iin = np.zeros(7)
        Iin[0] += (e / p['Rs']) - (s.iL + gL * (vprev[0] - vprev[1]))        # inductor i_n = iL_prev + gL*(v_n - v_{n-1}) with v = v0-v1 : history = iL_prev + gL*(vprev0 - vprev1) folded below
        Iin[1] += (s.iL + gL * (vprev[0] - vprev[1]))
        # capacitor histories: current source from node a to b = g*vprev + iprev (flows a->b)
        def hist(a, b, g, vv, i_prev): return g * ((vv[a] if a >= 0 else 0.0) - (vv[b] if b >= 0 else 0.0)) + i_prev
        h1 = hist(1, 2, gC1, vprev, s.iC1); Iin[1] += h1; Iin[2] -= h1
        h3 = hist(3, -1, gC3, vprev, s.iC3); Iin[3] += h3
        h2 = hist(5, 6, gC2, vprev, s.iC2); Iin[5] += h2; Iin[6] -= h2
        Iin[2] += Vcc / p['R1']
        x = s.x; Iin[5] += Vcc / max(x * p['RV'], 1.0)
        if s.gCbe: hbe = hist(2, 3, s.gCbe, vprev, s.iCbe); Iin[2] += hbe; Iin[3] -= hbe
        if s.gCbc: hbc = hist(2, 4, s.gCbc, vprev, s.iCbc); Iin[2] += hbc; Iin[4] -= hbc
        G = s.g
        vn = v.copy()
        for it in range(200):
            vbe, vbc = vn[2] - vn[3], vn[2] - vn[4]; ic, ib, gbe, gbc = s.bjt(vbe, vbc)
            Gm = G.copy(); I = Iin.copy()
            # BJT currents INTO transistor terminals: ib into B, ic into C, ie=-(ib+ic) out of E ; linearised: i = i0 + gbe*dvbe + ... (Newton companion)
            dicdbe, dicdbc, dibdbe, dibdbc = gbe, -gbc * (1.0 + 1.0 / p['br']), gbe / p['bf'], gbc / p['br']
            # currents leaving each node into the transistor: B: ib ; C: ic ; E: -(ib+ic)
            for node, i0, d_be, d_bc in ((2, ib, dibdbe, dibdbc), (4, ic, dicdbe, dicdbc), (3, -(ib + ic), -(dibdbe + dicdbe), -(dibdbc + dicdbc))):
                # KCL row: G*v + i_transistor = I  ->  linear in (vb-ve),(vb-vc)
                Gm[node, 2] += d_be + d_bc; Gm[node, 3] -= d_be; Gm[node, 4] -= d_bc
                I[node] -= i0 - d_be * vbe - d_bc * vbc
            try: vnew = np.linalg.solve(Gm, I)
            except np.linalg.LinAlgError: break
            d = vnew - vn
            if np.abs(d).max() < 1e-9: vn = vnew; break
            dj = max(abs(d[2] - d[3]), abs(d[2] - d[4]))             # junction-voltage step limiting (SPICE pnjlim idea): never move a junction more than 0.1 V per iteration
            vn = vn + d * (min(1.0, 0.1 / dj) if dj > 0.1 else 1.0)
        # update states
        s.iL = s.iL + gL * ((vn[0] - vn[1]) + (vprev[0] - vprev[1]))
        s.iC1 = gC1 * (vn[1] - vn[2]) - h1
        s.iC3 = gC3 * vn[3] - h3
        s.iC2 = gC2 * (vn[5] - vn[6]) - h2
        if s.gCbe: s.iCbe = s.gCbe * (vn[2] - vn[3]) - hbe
        if s.gCbc: s.iCbc = s.gCbc * (vn[2] - vn[4]) - hbc
        s.v = vn; s.vprev = vn.copy(); return vn[6]
    def run(s, e):
        return np.array([s.step(float(x)) for x in e])
if __name__ == '__main__':
    import sys
    rm = RM(); print('DC:', {k: round(float(v), 4) for k, v in rm.dc.items()})
