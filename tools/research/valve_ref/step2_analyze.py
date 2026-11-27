#!/usr/bin/env python3
"""Step 2 analysis: incremental effect of parasitics, input network and coupling on the validated Dempwolf stage."""
import os, numpy as np, pandas as pd
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from scipy.optimize import least_squares
from vref import ls_lines, db, HERE
from step2 import CIRCUITS as ALL
CIRCUITS = {k: v for k, v in ALL.items() if os.path.exists(f"{HERE}/data/step2/{k}_sine_220_0.05_0.npz")}   # circuits with transient runs
D2 = f"{HERE}/data/step2"; FS = 44100.0 * 4
ld = lambda c, t, d: dict(np.load(f"{D2}/{c}_{t}_{d}.npz"))
ACF = [20, 50, 100, 220, 1000, 3000, 5000, 8000, 10000, 15000, 20000]
# ---------------- AC: frequency response + phase, referred to the EMF; plate and grid
ac = {c: dict(np.load(f"{D2}/ac_{c}_ac_0.npz")) for c in ALL}; rows = []
for c, o in ac.items():
    f = o["f"]; at = lambda k, hz: o[k][np.argmin(abs(f - hz))]; A = lambda k, hz: ac["A"][k][np.argmin(abs(ac["A"]["f"] - hz))]
    r = dict(circuit=c)
    for hz in ACF:
        r[f"plate_dB@{hz}"] = db(at("p", hz)); r[f"plate_vs_A_dB@{hz}"] = db(at("p", hz) / A("p", hz)); r[f"phase_vs_A_deg@{hz}"] = np.degrees(np.angle(at("p", hz) / A("p", hz)))
        r[f"grid_vs_EMF_dB@{hz}"] = db(at("g", hz))
    m = (f > 1000) & (f < 150000); p = db(o["p"][m]); ref = db(o["p"][np.argmin(abs(f - 1000))])
    r["peak_dB_re_1k"] = p.max() - ref; r["peak_Hz"] = f[m][p.argmax()]; below = np.where(p < ref - 3)[0]; r["f_minus3dB_Hz"] = f[m][below[0]] if len(below) else np.nan
    rows.append(r)
pd.DataFrame(rows).to_csv(f"{HERE}/results/step2_ac.csv", index=False)
fig, (a, b) = plt.subplots(1, 2, figsize=(12, 4))
for c, o in ac.items():
    ls = "-" if c[0] in "AB" else ("--" if "10k" in c or "0" == c[1:] else ":"); a.semilogx(o["f"], db(o["p"]), ls, label=c); b.semilogx(o["f"], db(o["g"]), ls, label=c)
a.set_xlim(10, 100e3); b.set_xlim(10, 100e3); a.set_ylim(0, 45); b.set_ylim(-35, 8); a.set_xlabel("Hz"); b.set_xlabel("Hz"); a.set_ylabel("EMF to plate (dB)"); b.set_ylabel("EMF to grid, after the source network (dB)"); a.grid(alpha=.3, which="both"); b.grid(alpha=.3, which="both"); a.legend(ncol=2, fontsize=8)
a.set_title("Plate response, small signal (AC analysis)"); b.set_title("Grid response"); fig.tight_layout(); fig.savefig(f"{HERE}/figs/step2_freqresp.png", dpi=110); plt.close(fig)

# ---------------- transient tests
def fit_lines(y, freqs, t0, t1):
    a, ph, _ = ls_lines(y, FS, freqs, int(t0 * FS), int(t1 * FS)); return a
res = []
for d in (0, 6, 12):
    base = {}
    for c in CIRCUITS:
        o = ld(c, "sine_220_0.05", d); f0 = 220.0 * np.arange(1, 11)
        a = fit_lines(o["ia"], f0, .3, .5); ag = fit_lines(o["g"], f0[:1], .3, .5)
        r = dict(test="sine 220 Hz, 0.05", drive_db=d, circuit=c, Ia_fund_uA=a[0] * 1e3, grid_fund_V=ag[0], H2_dBc=db(a[1] / a[0]), H3_dBc=db(a[2] / a[0]), THD_pct=100 * np.sqrt(np.sum(a[1:] ** 2)) / a[0])
        base.setdefault("A", r) if c == "A" else None; res.append(r)
    for r in res:
        if r["test"] == "sine 220 Hz, 0.05" and r["drive_db"] == d: r["gain_vs_A_dB"] = db(r["Ia_fund_uA"] / base["A"]["Ia_fund_uA"]); r["THD_vs_A_pct"] = 100 * (r["THD_pct"] / base["A"]["THD_pct"] - 1)
    base = {}
    for c in CIRCUITS:
        o = ld(c, "imd_guitar_440_660_0.02", d); fr = 220.0 * np.arange(1, 91); a = fit_lines(o["ia"], fr, .3, .6); A = dict(zip(np.round(fr).astype(int), a)); tones = np.hypot(A[440], A[660])
        im = 100 * np.sqrt(sum(v ** 2 for k, v in A.items() if k not in (440, 660, 880, 1320) and k <= 5000)) / tones
        r = dict(test="IMD 440+660, 0.02", drive_db=d, circuit=c, IMD_pct=im, tone_sum_uA=tones * 1e3); res.append(r)
        if c == "A": base = r
    for r in res:
        if r["test"].startswith("IMD") and r["drive_db"] == d: r["IMD_vs_A_pct"] = 100 * (r["IMD_pct"] / base["IMD_pct"] - 1)
# bias burst, 12 dB
def bias_fit(o):
    vk, t = o["k"], o["t"]; fs = 1 / (t[1] - t[0]); t_off = 0.35; per = int(round(fs / 220)); sm = np.convolve(vk, np.ones(per) / per, "same")
    idle = vk[int(.10 * fs):int(.19 * fs)].mean(); during = sm[int(.30 * fs):int(.34 * fs)].mean(); m = (t > t_off + .002) & (t < t_off + .20); tt = t[m] - t_off; y = vk[m]
    def model(p):
        out = p[0] + p[1] * np.exp(-tt / p[2])
        for k in range(4): out = out + p[3 + 2 * k] * np.cos(2 * np.pi * 220 * (k + 1) * tt) + p[4 + 2 * k] * np.sin(2 * np.pi * 220 * (k + 1) * tt)
        return out
    r = least_squares(lambda p: model(p) - y, [y[-1], .1, .01] + [0] * 8, bounds=([0, -2, 1e-4] + [-1] * 8, [5, 2, 1] + [1] * 8))
    return dict(shift_during_burst_V=during - idle, tau_recovery_ms=r.x[2] * 1e3, t50_ms=r.x[2] * np.log(2) * 1e3, fit_rms_mV=np.sqrt(np.mean(r.fun ** 2)) * 1e3)
for c in CIRCUITS: res.append(dict(test="bias burst 0.3 @220 Hz", drive_db=12, circuit=c, **bias_fit(ld(c, "bias_recovery", 12))))
# transient pick: peak amplitude, timing, shape vs A
oA = ld("A", "transient_pick", 12); iA = oA["ia"] - oA["ia"][:100].mean()
def lag(x, y, n=60):
    c = np.correlate(x - x.mean(), y - y.mean(), "full"); k = np.argmax(c[len(x) - 1 - n:len(x) + n]) - n; i = len(x) - 1 + k; a, b, cc = c[i - 1], c[i], c[i + 1]; return k + 0.5 * (a - cc) / (a - 2 * b + cc)
for c in CIRCUITS:
    o = ld(c, "transient_pick", 12); i = o["ia"] - o["ia"][:100].mean(); s = slice(int(.045 * FS), int(.08 * FS))
    L = lag(i[s], iA[s]); e = i[s] - iA[s]
    res.append(dict(test="hard pick burst (1.5 kHz, 10 ms), +12 dB", drive_db=12, circuit=c, peak_pos_vs_A_dB=db(i[s].max() / iA[s].max()), peak_neg_vs_A_dB=db(abs(i[s].min()) / abs(iA[s].min())),
                    lag_vs_A_us=L / FS * 1e6, resid_rms_dB_re_A=db(np.sqrt(np.mean(e ** 2)) / np.sqrt(np.mean(iA[s] ** 2))), grid_peak_V=np.abs(o["g"][s]).max()))
df = pd.DataFrame(res); df.to_csv(f"{HERE}/results/step2_results.csv", index=False)
pd.set_option("display.width", 250); pd.set_option("display.max_columns", 40); pd.set_option("display.float_format", "{:.2f}".format)
print(pd.DataFrame(rows)[["circuit", "plate_dB@1000", "plate_vs_A_dB@10000", "plate_vs_A_dB@20000", "phase_vs_A_deg@10000", "grid_vs_EMF_dB@5000", "peak_dB_re_1k", "peak_Hz", "f_minus3dB_Hz"]].to_string(index=False))
for t, g in df.groupby("test"): print("\n" + t); print(g.dropna(axis=1, how="all").drop(columns="test").to_string(index=False))
