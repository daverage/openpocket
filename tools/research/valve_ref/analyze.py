#!/usr/bin/env python3
"""Measurements -> results/*.csv.  Stage-level comparisons use the SPICE runs that were fed the DSP's own 88.2 kHz grid voltage
(dspin_*, sample-for-sample aligned).  End-to-end comparisons use the band-limited-input runs (no dspin_ prefix) and are
aligned by the measured group delay of the engine's half-band up-sampler."""
import csv, os, sys, numpy as np
from vref import *
OUT = f"{HERE}/results"; os.makedirs(OUT, exist_ok=True)
MODELS = {"dempwolf": "dempwolf", "koren": "koren", "koren_cpar": "koren_cpar"}
rows = {}
def put(table, **r): rows.setdefault(table, []).append(r)
def save():
    for t, rs in rows.items():
        keys = []; [keys.append(k) for r in rs for k in r if k not in keys]
        with open(f"{OUT}/{t}.csv", "w", newline="") as f:
            w = csv.DictWriter(f, keys); w.writeheader(); w.writerows(rs)
def have(tag, name, d): return os.path.exists(f"{HERE}/data/spice/{tag}_{name}_{d}.npz") and os.path.exists(f"{HERE}/data/dsp/{name}_{d}.final.f32")
def systems(name, d):
    """dict label -> (wet at 88.2k in engine units) : DSP and each circuit"""
    s = {"DSP": dsp(name, d)["wet"]}
    for lab, tag in MODELS.items():
        if have(tag, name, d): s[lab] = wet_ref(spice(tag, name, d), d)
    n = min(map(len, s.values())); return {k: v[:n] for k, v in s.items()}

# ---------------------------------------------------------------- A. DC operating point (idle: first sample of any test)
def dc():
    name = "impulse_0.05"; d = 0
    D = dsp(name, d)
    put("dc_operating_point", system="DSP (px_dsp.c tables)", Vg_V=0.0, Vk_V=D["vk"][0], Vp_V=B - RP * D["ia"][0], Ia_mA=D["ia"][0], Ig_mA=D["ig"][0], Ik_mA=D["ik"][0])
    for lab, tag in MODELS.items():
        tag = tag.replace("dspin_", "")                       # DC point is independent of the drive-in mode
        f = f"{HERE}/data/spice/{tag}_{name}_{d}.npz"
        if os.path.exists(f):
            o = spice(tag, name, d, dec=False)
            put("dc_operating_point", system={"dempwolf": "SPICE Dempwolf (same equations)", "koren": "SPICE Koren 12AX7", "koren_cpar": "SPICE Koren + capacitances"}[lab],
                Vg_V=o["vg"][0], Vk_V=o["vk"][0], Vp_V=o["vp"][0], Ia_mA=o["ia"][0], Ig_mA=o["ig"][0], Ik_mA=o["ik"][0])

# ---------------------------------------------------------------- B. steady sines: harmonics, THD, THD+N, non-harmonic (alias) floor
def sines():
    for name, m in MAN.items():
        if m["kind"] != "sine": continue
        f0 = m["f"]
        for d in m["drives"]:
            S = systems(name, d)
            for lab, w in S.items():
                fs = FS2; i0, i1 = int(0.30 * fs), int(0.50 * fs)
                nh = int(min(20000, 0.49 * fs) // f0)
                amps, ph, res = ls_lines(w, fs, f0 * np.arange(1, nh + 1), i0, i1)
                a1 = amps[0]; harm = amps[1:]; band = [k + 2 for k in range(len(harm)) if f0 * (k + 2) <= 20000]
                thd = np.sqrt(np.sum(amps[np.array(band) - 1] ** 2)) / a1 if band else 0
                # THD+N in the 20 Hz-20 kHz band: everything but the fundamental
                a_1, _, res1 = ls_lines(w, fs, np.array([f0]), i0, i1)
                thdn = band_rms(res1, fs) * np.sqrt(2) / a_1[0]
                nonharm = band_rms(res, fs) * np.sqrt(2) / a1                   # residual after ALL harmonics below Nyquist: noise + aliasing
                row = dict(test=name, f0=f0, amp=m["amp"], drive_db=d, system=lab, fund_amp=a1, gain_vs_input=a1 / m["amp"], THD_pct=100 * thd, THDN_pct=100 * thdn,
                           nonharm_dBc=db(nonharm))
                for k in range(2, 9): row[f"H{k}_dBc"] = db(amps[k - 1] / a1)
                put("sines_stage", **row)
            # end-to-end: processed output vs the same output stage fed the reference wet
            if have("dempwolf", name, d):
                pass
    # processed (44.1 kHz) alias / harmonic comparison: final DSP vs post(wet_ref)
    for name, m in MAN.items():
        if m["kind"] != "sine": continue
        f0 = m["f"]
        for d in m["drives"]:
            D = dsp(name, d)
            outs = {"DSP": D["final"]}
            for lab, tag in MODELS.items():
                if have(tag, name, d): outs[lab] = post(D["x"], wet_ref(spice(tag, name, d), d), d, f"{HERE}/data/tmp_{name}_{d}")
            for lab, y in outs.items():
                n = min(len(y), len(D["final"])); y = y[:n]; fs = FS; i0, i1 = int(0.30 * fs), int(0.50 * fs)
                nh = int(min(20000, 0.49 * fs) // f0)
                amps, _, res = ls_lines(y, fs, f0 * np.arange(1, nh + 1), i0, i1)
                put("sines_processed", test=name, f0=f0, amp=m["amp"], drive_db=d, system=lab, fund_amp=amps[0], THD_pct=100 * np.sqrt(np.sum(amps[1:] ** 2)) / amps[0],
                    nonharm_dBc=db(band_rms(res, fs) * np.sqrt(2) / amps[0]), gain_vs_input=amps[0] / m["amp"])
            # sample-aligned residuals
            S = systems(name, d)
            i0, i1 = int(0.30 * FS2), int(0.50 * FS2)
            for lab in S:
                if lab == "DSP": continue
                e = S["DSP"][i0:i1] - S[lab][i0:i1]; r = S[lab][i0:i1]
                put("residual_stage", test=name, drive_db=d, circuit=lab, resid_rms_dB_rel_ref=db(np.sqrt(np.mean(e ** 2)) / np.sqrt(np.mean(r ** 2))), resid_peak_rel=float(np.abs(e).max() / np.abs(r).max()),
                    gain_err_dB=db(np.sqrt(np.mean(S["DSP"][i0:i1] ** 2)) / np.sqrt(np.mean(r ** 2))))
                if lab in outs:
                    y = outs[lab]; n = min(len(y), len(D["final"])); j0, j1 = int(0.30 * FS), int(0.50 * FS)
                    e = D["final"][j0:j1] - y[j0:j1]
                    put("residual_processed", test=name, drive_db=d, circuit=lab, resid_rms_dB_rel_ref=db(np.sqrt(np.mean(e ** 2)) / np.sqrt(np.mean(y[j0:j1] ** 2))))

# ---------------------------------------------------------------- C. two-tone IMD
def imd():
    for name, m in MAN.items():
        if m["kind"] != "imd": continue
        f1, f2 = m["f1"], m["f2"]
        for d in m["drives"]:
            S = systems(name, d)
            for lab, w in S.items():
                fs = FS2; i0, i1 = int(0.30 * fs), int(0.60 * fs)
                if f2 == 660:
                    base = 220.0; freqs = base * np.arange(1, int(20000 // base) + 1)
                    amps, _, _ = ls_lines(w, fs, freqs, i0, i1); a = dict(zip(np.round(freqs).astype(int), amps))
                    tones = np.sqrt(a[440] ** 2 + a[660] ** 2); im = [k for k in a if k not in (440, 660) and k <= 5000]
                    imd = np.sqrt(sum(a[k] ** 2 for k in im if k not in (880, 1320))) / tones      # excl. 2nd harmonics 2f1, 2f2
                    put("imd", test=name, amp=m["amp"], drive_db=d, system=lab, IMD_pct=100 * imd, line_220_dBc=db(a[220] / tones), line_880_dBc=db(a[880] / tones),
                        line_1100_dBc=db(a[1100] / tones), tone_f1=a[440], tone_f2=a[660], gain_f1=a[440] / (m["amp"] / 2))
                else:
                    f1v = np.arange(1, int(20000 // f1) + 1) * f1
                    sb = np.array([f2 + k * f1 for k in range(-6, 7) if k != 0] + [2 * f2 + k * f1 for k in range(-4, 5)])
                    amps, _, _ = ls_lines(w, fs, np.concatenate([f1v, [f2], sb]), i0, i1)
                    a2 = amps[len(f1v)]; sbamps = amps[len(f1v) + 1: len(f1v) + 1 + 12]
                    imd = np.sqrt(np.sum(sbamps ** 2)) / a2
                    put("imd", test=name, amp=m["amp"], drive_db=d, system=lab, IMD_pct=100 * imd, f2_amp=a2, f1_amp=amps[0], gain_f2=a2 / (m["amp"] * 0.2),
                        sideband_f2pm_f1_dBc=db(np.sqrt(amps[len(f1v) + 1 + 4] ** 2 + amps[len(f1v) + 1 + 6] ** 2) / a2 if False else np.sqrt(sbamps[5] ** 2 + sbamps[6] ** 2) / a2))

# ---------------------------------------------------------------- D. gain compression vs input amplitude (log amplitude sweep)
def ampsweep(figdata):
    for name, m in MAN.items():
        if m["kind"] != "ampsweep": continue
        f = m["f"]; dur = m["dur"]
        for d in m["drives"]:
            name_ = name; pname = name
            S = systems(name, d); cyc = 4; L = int(cyc * FS2 / f)
            ts = np.arange(0.05, dur - 0.05, 0.02); curves = {}
            for lab, w in S.items():
                A = []; 
                for t0 in ts:
                    i0 = int(t0 * FS2); a, _, _ = ls_lines(w, FS2, f * np.arange(1, 6), i0, i0 + L); A.append(a[0])
                curves[lab] = np.array(A)
            ain = m["a0"] * 10 ** (3 * (ts + 0.5 * cyc / f) / dur)
            for lab, A in curves.items():
                g = A / ain; g0 = np.median(g[:5]); gdb = db(g / g0)
                # 1 dB compression point (input amplitude in units, and in volts at the grid), first crossing of -1 dB
                idx = np.where(gdb < -1)[0]; a1 = ain[idx[0]] if len(idx) else np.nan
                put("compression", test=name, drive_db=d, system=lab, small_signal_gain=g0, comp_1dB_in_units=a1, comp_1dB_in_volts=a1 * drive_lin(d) if a1 == a1 else np.nan,
                    gain_dB_at_0p1V=float(np.interp(0.1 / drive_lin(d), ain, gdb)), gain_dB_at_1V=float(np.interp(1.0 / drive_lin(d), ain, gdb)), gain_dB_at_3V=float(np.interp(3.0 / drive_lin(d), ain, gdb)))
            figdata[(name, d)] = (ain, {k: db(v / ain / np.median((v / ain)[:5])) for k, v in curves.items()})

# ---------------------------------------------------------------- E. cathode bias displacement and recovery
def bias(figdata):
    name = "bias_recovery"; m = MAN[name]
    for d in m["drives"]:
        D = dsp(name, d); k = {"DSP": D["vk"]}
        for lab, tag in MODELS.items():
            if have(tag, name, d): k[lab] = spice(tag, name, d)["vk"]
        n = min(map(len, k.values())); k = {a: b[:n] for a, b in k.items()}; t = np.arange(n) / FS2
        per = int(FS2 / 220); 
        for lab, v in k.items():
            sm = np.convolve(v, np.ones(per) / per, mode="same")             # cycle-average: the bias component
            v0 = sm[int(0.15 * FS2)]; pk = sm[int(0.2 * FS2):int(0.36 * FS2)].max(); end = sm[int(0.35 * FS2)]
            disp = pk - v0
            after = sm[int(0.36 * FS2):]; ta = t[int(0.36 * FS2):] - m["t_off"]
            def t_to(frac):
                idx = np.where(after - v0 < frac * (end - v0))[0]; return ta[idx[0]] * 1e3 if len(idx) else np.nan
            # probe tone gain (fundamental, 20 ms windows) vs the settled value
            w = k[lab]; pg = []
            for t0 in (0.37, 0.40, 0.45, 0.55, 0.7, 0.9):
                i0 = int(t0 * FS2); a, _, _ = ls_lines(S := (wet_ref(spice(MODELS[lab], name, d), d)[:n] if lab != "DSP" else D["wet"][:n]), FS2, np.array([220.0]), i0, i0 + int(0.03 * FS2)); pg.append(a[0] / m["probe"])
            put("bias_recovery", drive_db=d, system=lab, vk_idle_V=v0, vk_peak_V=pk, vk_displacement_V=disp, vk_displacement_end_of_burst_V=end - v0,
                t_to_50pct_recovery_ms=t_to(0.5), t_to_10pct_ms=t_to(0.1), **{f"probe_gain_t{int(t*1000)}ms": g for t, g in zip((0.37, 0.40, 0.45, 0.55, 0.7, 0.9), pg)})
        figdata[(name, d)] = (t, k)

# ---------------------------------------------------------------- F. frequency response (small-signal chirp)
def freqresp(figdata):
    name = "chirp_small"
    for d in MAN[name]["drives"]:
        D = dsp(name, d); x_in = D["vin"]
        sets = {"DSP": -RP * (D["ia"] - D["ia"][:200].mean())}                      # plate-voltage swing implied by the DSP's plate current
        for lab, tag in {**MODELS}.items():
            if have(tag, name, d): o = spice(tag, name, d); sets[lab] = o["vp"] - o["vp"][:200].mean()
        n = min(len(x_in), *map(len, sets.values()))
        f = np.fft.rfftfreq(n, 1 / FS2); X = np.fft.rfft(x_in[:n]); mask = (f > 20) & (f < 20000)
        for lab, y in sets.items():
            H = np.fft.rfft(y[:n]) / np.where(np.abs(X) > 1e-9 * np.abs(X).max(), X, np.nan)
            Hs = np.convolve(np.abs(H), np.ones(15) / 15, "same")                        # light smoothing across bins
            figdata[("fr", d, lab)] = (f[mask], Hs[mask], np.unwrap(np.angle(H[mask])))
            g = lambda hz: float(np.interp(hz, f[mask], np.nan_to_num(Hs[mask])))
            put("frequency_response", drive_db=d, system=lab, **{f"gain_{hz}Hz_dB": db(g(hz)) for hz in (30, 50, 82, 150, 330, 1000, 3000, 6000, 10000, 16000, 20000)})
        # the processed output (44.1 kHz): final / x
        F = np.fft.rfft(D["final"]); Xin = np.fft.rfft(D["x"][: len(D["final"])]); ff = np.fft.rfftfreq(len(D["final"]), 1 / FS)
        Hf = np.convolve(np.abs(F / np.where(np.abs(Xin) > 1e-9 * np.abs(Xin).max(), Xin, np.nan)), np.ones(9) / 9, "same"); mk = (ff > 20) & (ff < 20000)
        figdata[("fr", d, "DSP_processed")] = (ff[mk], Hf[mk], None)
        put("frequency_response", drive_db=d, system="DSP processed output / input (unity = 0 dB)", **{f"gain_{hz}Hz_dB": db(float(np.interp(hz, ff[mk], np.nan_to_num(Hf[mk])))) for hz in (30, 50, 82, 150, 330, 1000, 3000, 6000, 10000, 16000, 20000)})

# ---------------------------------------------------------------- G. impulses and transients, guitar: residuals + internal state
def waveform_tests(figdata):
    for name, m in MAN.items():
        if m["kind"] not in ("impulse", "transient", "guitar"): continue
        for d in m["drives"]:
            S = systems(name, d); D = dsp(name, d)
            n = len(S["DSP"]); skip = int(0.02 * FS2) if m["kind"] != "guitar" else int(0.1 * FS2)
            figdata[(name, d)] = S
            for lab, w in S.items():
                if lab == "DSP": continue
                e = S["DSP"][skip:] - w[skip:]; r = w[skip:]
                put("waveform_residual", test=name, kind=m["kind"], drive_db=d, circuit=lab, ref_rms=float(np.sqrt(np.mean(r ** 2))), resid_rms_dB_rel_ref=db(np.sqrt(np.mean(e ** 2)) / np.sqrt(np.mean(r ** 2))),
                    peak_dsp=float(np.abs(S["DSP"][skip:]).max()), peak_ref=float(np.abs(r).max()), peak_err_dB=db(np.abs(S["DSP"][skip:]).max() / np.abs(r).max()))
            if m["kind"] == "guitar":
                for lab, tag in MODELS.items():
                    if have(tag, name, d) and lab != "koren_cpar":
                        y = post(D["x"], wet_ref(spice(tag, name, d), d), d, f"{HERE}/data/tmp_{name}_{d}")
                        e = D["final"][4410:] - y[4410:len(D["final"])]
                        put("guitar_processed", test=name, drive_db=d, circuit=lab, resid_rms_dB_rel_ref=db(np.sqrt(np.mean(e ** 2)) / np.sqrt(np.mean(y[4410:len(D['final'])] ** 2))),
                            dsp_rms_dBFS=db(np.sqrt(np.mean(D["final"] ** 2))), ref_rms_dBFS=db(np.sqrt(np.mean(y ** 2))))

if __name__ == "__main__":
    fig = {}
    dc(); sines(); imd(); ampsweep(fig); bias(fig); freqresp(fig); waveform_tests(fig)
    save()
    import pickle; pickle.dump(fig, open(f"{HERE}/data/figdata.pkl", "wb"))
    print({k: len(v) for k, v in rows.items()})
