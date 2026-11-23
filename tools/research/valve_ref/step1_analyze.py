#!/usr/bin/env python3
"""Step 1 analysis: DSP vs UNLOADED Dempwolf SPICE (fed from the DSP's own grid voltage) vs the earlier 1 Mohm-loaded run.
Plate-current gain, plate-voltage gain, H2/H3/THD (220 Hz), IMD (440+660), cathode bias displacement + repaired recovery time."""
import os, sys, numpy as np, pandas as pd
from scipy.optimize import least_squares
from vref import *
HZ = f"{HERE}/data/spice_hiZ"
def hiz(name, d):
    o = dict(np.load(f"{HZ}/dspin_dempwolf_{name}_{d}.npz"))
    return {k: resample_poly(v, 1, 2) for k, v in o.items() if k != "t"}        # zero-phase: sample j == the DSP's oversampled sample j
rows = []
# ---- sine 220: gain, harmonics
name = "sine_220_0.05"
for d in (0, 12):
    D = dsp(name, d); H = hiz(name, d); L = spice("dempwolf", name, d)         # L: loaded (1 Mohm), band-limited input, aligned by lag
    n = min(len(D["ia"]), len(H["ia"]), len(L["ia"])); i0, i1 = int(.30 * FS2), min(int(.50 * FS2), n)
    f0 = np.array([220.0 * k for k in range(1, 11)])
    def fit(y): a, _, _ = ls_lines(y, FS2, f0, i0, i1); return a
    sets = {"DSP": (D["ia"][:n], -RP * D["ia"][:n]), "SPICE unloaded (fed from DSP grid)": (H["ia"][:n], H["vp"][:n]), "SPICE 1 Mohm loaded": (L["ia"][:n], L["vp"][:n])}
    ref = fit(sets["SPICE unloaded (fed from DSP grid)"][0])
    for lab, (ia, vp) in sets.items():
        ai, av = fit(ia), fit(vp)
        rows.append(dict(test="sine 220 Hz, 0.05", drive_db=d, system=lab, Ia_fund_uA=ai[0] * 1e3, Ia_gain_vs_unloaded_dB=db(ai[0] / ref[0]), Vp_fund_V=av[0],
                         Vp_gain_dB=db(av[0] / (0.05 * 4 * 10 ** (d / 20))), H2_dBc=db(ai[1] / ai[0]), H3_dBc=db(ai[2] / ai[0]), THD_pct=100 * np.sqrt(np.sum(ai[1:] ** 2)) / ai[0]))
# ---- IMD 440 + 660
name = "imd_guitar_440_660_0.02"
for d in (0, 12):
    D = dsp(name, d); H = hiz(name, d); L = spice("dempwolf", name, d)
    n = min(len(D["ia"]), len(H["ia"]), len(L["ia"])); i0, i1 = int(.30 * FS2), min(int(.60 * FS2), n)
    fr = 220.0 * np.arange(1, 91)
    for lab, ia in (("DSP", D["ia"][:n]), ("SPICE unloaded (fed from DSP grid)", H["ia"][:n]), ("SPICE 1 Mohm loaded", L["ia"][:n])):
        a, _, _ = ls_lines(ia, FS2, fr, i0, i1); A = dict(zip(np.round(fr).astype(int), a)); tones = np.hypot(A[440], A[660])
        im = np.sqrt(sum(v ** 2 for k, v in A.items() if k not in (440, 660, 880, 1320) and k <= 5000)) / tones
        rows.append(dict(test="IMD 440+660, 0.02", drive_db=d, system=lab, IMD_pct=100 * im, line_220_dBc=db(A[220] / tones), line_1100_dBc=db(A[1100] / tones), tone_sum_uA=tones * 1e3))
# ---- bias burst: displacement and repaired recovery time
def bias_fit(vk, t_off=0.35):
    t = np.arange(len(vk)) / FS2; per = int(round(FS2 / 220)); sm = np.convolve(vk, np.ones(per) / per, "same")
    idle = vk[int(.10 * FS2):int(.19 * FS2)].mean(); peak = sm[int(.22 * FS2):int(.34 * FS2)].max(); during = sm[int(.30 * FS2):int(.34 * FS2)].mean()
    m = (t > t_off + 0.002) & (t < t_off + 0.20); tt = t[m] - t_off; y = vk[m]
    def model(p):
        c, Dd, tau = p[:3]; out = c + Dd * np.exp(-tt / tau)
        for k in range(4): out = out + p[3 + 2 * k] * np.cos(2 * np.pi * 220 * (k + 1) * tt) + p[4 + 2 * k] * np.sin(2 * np.pi * 220 * (k + 1) * tt)
        return out
    r = least_squares(lambda p: model(p) - y, [y[-1], 0.1, 0.01] + [0] * 8, bounds=([0, -2, 1e-4] + [-1] * 8, [5, 2, 1.0] + [1] * 8))
    c, Dd, tau = r.x[:3]
    return dict(vk_idle_V=idle, vk_during_burst_V=during, vk_peak_cycle_avg_V=peak, shift_during_burst_V=during - idle, vk_end_of_burst_V=c + Dd, tau_recovery_ms=tau * 1e3,
                t50_ms=tau * np.log(2) * 1e3, t10_ms=tau * np.log(10) * 1e3, settled_V=c, shift_left_by_probe_mV=(c - idle) * 1e3, fit_rms_mV=np.sqrt(np.mean(r.fun ** 2)) * 1e3)
name = "bias_recovery"
for d in (12, 24):
    D = dsp(name, d); H = hiz(name, d); L = spice("dempwolf", name, d)
    for lab, vk in (("DSP", D["vk"]), ("SPICE unloaded (fed from DSP grid)", H["vk"]), ("SPICE 1 Mohm loaded", L["vk"])):
        rows.append(dict(test="bias burst 0.3 @220 Hz then probe", drive_db=d, system=lab, **bias_fit(vk)))
df = pd.DataFrame(rows); df.to_csv(f"{HERE}/results/step1_results.csv", index=False)
pd.set_option("display.width", 250); pd.set_option("display.max_columns", 30); pd.set_option("display.float_format", "{:.3f}".format)
for t, g in df.groupby("test"): print("\n" + t); print(g.dropna(axis=1, how="all").drop(columns="test").to_string(index=False))
