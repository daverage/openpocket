"""Generate aligned comparisons, per-event metrics and waveform plots."""
import argparse
import csv
from pathlib import Path
import subprocess
import wave

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

FS = 44100
CTL = 32
NOTE_ON, REPICK, PITCH_UPDATE, LEGATO, NOTE_OFF, DAMP, REJECT, PICK, STEAL, PATCH = (
    1 << i for i in range(10)
)
EVENTS = {
    NOTE_ON: "NOTE_ON", REPICK: "REPICK", PITCH_UPDATE: "PITCH_UPDATE",
    LEGATO: "LEGATO_CHANGE", NOTE_OFF: "NOTE_OFF", DAMP: "DAMP",
    REJECT: "REJECT", PICK: "PICK", STEAL: "STEAL", PATCH: "PATCH_CHANGE",
}
FRAME = np.dtype([("event", "<u4"), ("hz", "<f4"), ("vel", "<f4"), ("expr", "<f4")])
TAUS = (0.5, 1, 1.5, 2, 3)
ANALOG, ARCADE, SPLIT, INSTRUMENT, ENSEMBLE, CHOIR = (
    "analog", "arcade", "split", "instrument", "ensemble", "choir"
)


def midi(note):
    return 440 * 2 ** ((note - 69) / 12)


def samples(ms):
    return round(ms * FS / 1000)


def generate(sequence, length=1500):
    frames = np.zeros(samples(length), dtype=FRAME)
    frames["vel"] = frames["expr"] = 0.9
    for ms, kind, note in sequence:
        i = samples(ms)
        frames["event"][i] |= kind
        if note is not None:
            frames["hz"][i:] = midi(note)
    return frames


def rapid(rate):
    notes = (50, 55, 59, 62, 65, 67, 64, 60)
    return [(42 + i * 1000 / rate, NOTE_ON, notes[i % len(notes)]) for i in range(10)]


def scenarios():
    phrase = rapid(20) + [(575, NOTE_OFF, None)]
    cases = {
        "rapid_lead": (ANALOG, 0, 50, -1, phrase, 1500),
        "rapid_retrigger": (ANALOG, 0, 50, 0, phrase, 1500),
        "rapid_follow": (ANALOG, 0, 50, 1, phrase, 1500),
        "repeated": (ARCADE, 70, 50, -1,
                     [(40, NOTE_ON, 52), (80, REPICK, 52), (140, REPICK, 52),
                      (180, REPICK, 52), (250, NOTE_OFF, None)], 1500),
        "legato": (ANALOG, 0, 50, -1,
                   [(40, NOTE_ON, 47), (120, LEGATO, 52), (190, LEGATO, 55)]
                   + [(230 + i * 2, PITCH_UPDATE, 55 + 0.35 * np.sin(i / 13))
                      for i in range(60)] + [(400, NOTE_OFF, None)], 1500),
        "bass_high": (ANALOG, 0, 50, 0,
                      [(40, NOTE_ON, 28), (180, NOTE_ON, 88), (340, NOTE_ON, 31),
                       (480, NOTE_ON, 96), (570, NOTE_OFF, None)], 1500),
        "organ": (INSTRUMENT, 60, 50, -1, phrase, 1500),
        "plucked": (ARCADE, 70, 50, -1, phrase, 1500),
        "noise": (ARCADE, 80, 50, -1, phrase, 1500),
        "choir": (CHOIR, 0, 90, -1, phrase, 3200),
        "strings": (ENSEMBLE, 10, 90, -1, phrase, 3200),
        "pad": (INSTRUMENT, 0, 50, -1, phrase, 3200),
        "forced_steal": (ENSEMBLE, 10, 90, -1,
                         [(42, NOTE_ON, 48), (425, STEAL, 48), (600, STEAL, 48),
                          (850, NOTE_OFF, None)], 3200),
        "patch_change": (ANALOG, 0, 50, -1,
                         [(42, NOTE_ON, 48), (425, PATCH, None), (480, NOTE_ON, 60),
                          (650, NOTE_OFF, None)], 1500),
        "split": (SPLIT, 0, 80, -1,
                  [(42, NOTE_ON, 36), (200, NOTE_ON, 84), (400, NOTE_OFF, None)], 1500),
        "envelope_stages": (INSTRUMENT, 10, 50, -1,
                            [(42, NOTE_ON, 50), (80, NOTE_ON, 56), (280, NOTE_ON, 60),
                             (1000, NOTE_ON, 53), (1100, NOTE_OFF, None),
                             (1150, NOTE_ON, 56), (1600, NOTE_OFF, None)], 4200),
    }
    damp_time = (32 * 260 + 4) * 1000 / FS
    cases["mute_restart"] = ANALOG, 0, 50, 0, [
        (42, NOTE_ON, 43), (damp_time, DAMP | NOTE_OFF, None),
        (damp_time + 0.07, NOTE_ON, 64),
        (480, DAMP | NOTE_OFF, None), (484, NOTE_ON, 52),
        (640, DAMP | NOTE_OFF, None),
    ], 1500
    for rate in (8, 12.5, 16, 20, 24):
        sequence = rapid(rate)
        end = sequence[-1][0] + 80
        cases[f"rate_{rate}"] = ANALOG, 10, 50, 0, sequence + [(end, NOTE_OFF, None)], end + 1000
    return cases


def write_wav(path, y):
    # Same full-scale conversion for both renders; raw float is also retained.
    pcm = np.rint(np.clip(y, -1, 32767 / 32768) * 32768).astype("<i2")
    with wave.open(str(path), "wb") as f:
        f.setparams((1, 2, FS, 0, "NONE", "not compressed"))
        f.writeframes(pcm.tobytes())


def render(out, prefix, frames, config, tau, native=False, require_silence=False):
    program = out / ("native" if native else "overlay-render")
    raw = prefix.parent / (prefix.name + ".f32")
    trace = prefix.parent / (prefix.name + ".csv")
    type_id, patch, stack, policy = config
    with raw.open("wb") as f:
        subprocess.run([program, "render", frames, trace, str(type_id), str(patch),
                        str(stack), str(tau), str(policy), "1", str(int(require_silence))],
                       stdout=f, check=True)
    return np.fromfile(raw, dtype="<f4"), np.genfromtxt(trace, delimiter=",", names=True)


def rms(x):
    return float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0


def spectral_energy(x):
    n = len(x)
    if n < 2:
        return 0.0
    window = np.hanning(n)
    bins = np.fft.rfftfreq(n, 1 / FS)
    spectrum = np.fft.rfft(x * window)
    return float(2 * np.sum(abs(spectrum[(bins >= 2000) & (bins <= 8000)]) ** 2)
                 / (n * np.sum(window ** 2)))


def metrics(y, i):
    lo = max(0, i - samples(10))
    before = y[lo:i].astype(np.float64)
    around = y[max(0, i - 1):i + samples(4)].astype(np.float64)
    if len(around) < 2:
        return {}
    d = abs(np.diff(around))
    neighbor = abs(np.diff(before))
    p95 = float(np.quantile(neighbor, 0.95)) if len(neighbor) else 0.0
    attack = y[i:i + samples(10)]
    # A fixed -60 dBFS onset proxy, applicable to starts from silence.
    # A ringing voice can already exceed the threshold at t=0.
    above = np.flatnonzero(abs(attack) >= 0.001)
    return dict(step=d[0], peak_difference=max(d), nearby_difference_p95=p95,
                difference_outlier=max(d) / max(p95, 1e-6), nearby_rms=rms(before),
                band_energy=spectral_energy(y[max(0, i - samples(2)):i + samples(2)]),
                peak=float(max(abs(around))), clipping=int(np.count_nonzero(abs(around) >= 1)),
                rms_2ms=rms(attack[:samples(2)]), rms_5ms=rms(attack[:samples(5)]),
                rms_10ms=rms(attack), audible_sample=i + int(above[0]) if len(above) else -1)


def plot(path, a, b, transitions, name, n_transitions=6):
    selected = transitions[:n_transitions]
    if not selected:
        return
    fig, axes = plt.subplots(len(selected), 1, squeeze=False, figsize=(9, 1.65 * len(selected)))
    for ax, (detected, rendered, kind) in zip(axes[:, 0], selected):
        idx = np.arange(max(0, rendered - samples(2)), min(len(a), rendered + samples(5)))
        x = (idx - rendered) * 1000 / FS
        ax.plot(x, a[idx], label="Existing", linewidth=0.9)
        ax.plot(x, b[idx], label="1.5 ms correction", linewidth=0.9, alpha=0.8)
        ax.axvline(0, color="black", linewidth=0.6)
        ax.axvline((detected - rendered) * 1000 / FS, color="gray", linestyle=":", linewidth=0.8)
        ax.set_title(f"{kind} at {detected / FS:.3f} s", fontsize=9)
        ax.set_ylabel("FS")
    axes[0, 0].legend(loc="upper right", fontsize=8)
    axes[-1, 0].set_xlabel("ms relative to first rendered output")
    fig.suptitle(name.replace("_", " "), fontsize=11)
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)


def write_csv(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


def compare(out, name, frames, config, stats, summaries, *, synthetic, reference=None):
    folder = out / name
    folder.mkdir(exist_ok=True)
    stream = folder / "events.frames"
    frames.tofile(stream)
    base, native = render(out, folder / "a", stream, config, 0, native=True,
                          require_silence=synthetic)
    control, _ = render(out, folder / "overlay_off", stream, config, 0)
    assert np.array_equal(base, control), f"overlay bypass differs on {name}"
    if reference is not None:
        assert base.tobytes() == reference, f"legacy event replay differs from direct adapter on {name}"
    write_wav(folder / "a.wav", base)
    traces = []
    plotted = []
    for i in np.flatnonzero(frames["event"]):
        flags = int(frames["event"][i])
        label = "|".join(name for mask, name in EVENTS.items() if flags & mask)
        block = i // CTL
        landed = block < len(native)
        traces.append(dict(event=label, detected=i, adapter_received=i,
                           render=int(native["render"][block]) if landed else -1,
                           output=int(native["output"][block]) if landed else -1,
                           hz=frames["hz"][i], velocity=frames["vel"][i],
                           expression=frames["expr"][i]))
        if landed and flags & (NOTE_ON | REPICK | LEGATO | DAMP | STEAL | PATCH):
            plotted.append((i, int(native["output"][block]), label))
    write_csv(folder / "event_trace.csv", traces)
    for tau in TAUS:
        candidate, changed = render(out, folder / f"b_{tau:g}ms", stream, config, tau,
                                    require_silence=synthetic)
        assert len(base) == len(candidate)
        for key in ("render", "output", "starts", "release", "voices", "damped", "env",
                    "ordered_starts", "ordered_releases", "ordered_damps", "ordered_gate", "raw_mix_hash", "stage_before", "env_before"):
            assert np.array_equal(native[key], changed[key]), (name, tau, key)
        assert np.isfinite(candidate).all()
        raw_diff = abs(base - candidate)
        active = int(np.count_nonzero(changed["corrected_slots"]))
        for variant, signal, trace in (("a", base, native), ("b", candidate, changed)):
            summaries.append(dict(case=name, tau_ms=tau, variant=variant,
                                  peak=float(max(abs(signal))), clipping=int(np.count_nonzero(abs(signal) >= 1)),
                                  changed_samples=int(np.count_nonzero(raw_diff)), corrected_blocks=active,
                                  host_median_ns=float(np.median(trace["ns"])),
                                  host_p99_ns=float(np.quantile(trace["ns"], 0.99)),
                                  host_max_ns=float(max(trace["ns"])),
                                  measured_over_slot=int(np.count_nonzero(trace["ns"] >= CTL / FS * 1e9))))
        for tr in traces:
            i = tr["output"]
            if i <= 0 or tr["event"] in ("PITCH_UPDATE", "PICK", "REJECT",
                                         "PICK|PITCH_UPDATE", "PITCH_UPDATE|PICK"):
                continue
            idx = tr["detected"] // CTL
            for variant, signal, trace in (("a", base, native), ("b", candidate, changed)):
                measurements = metrics(signal, i)
                if measurements:
                    audible = measurements["audible_sample"]
                    stats.append(dict(case=name, tau_ms=tau, variant=variant, **tr,
                                      starts=int(trace["starts"][idx]), release=int(trace["release"][idx]),
                                      corrected_slots=int(trace["corrected_slots"][idx]),
                                      replacement_slots=int(trace["replacement_slots"][idx]),
                                      stage_before=int(trace["stage_before"][idx]),
                                      env_before=float(trace["env_before"][idx]),
                                      first_corrected_output=i if trace["replacement_slots"][idx] else -1,
                                      event_to_threshold=audible - tr["detected"] if audible >= 0 else -1,
                                      **measurements))
        if name == "legato":
            assert np.array_equal(base, candidate), "legato or continuous pitch armed correction"
        if tau == 1.5:
            write_wav(folder / "b.wav", candidate)
            plot(folder / "transitions.png", base, candidate, plotted, name)
    print(f"{name}: {len(traces)} trace rows, {len(base)} frames", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--di-dir", type=Path)
    parser.add_argument("--case", action="append", help="select synthetic cases")
    parser.add_argument("--controller", action="store_true",
                        help="run separately after controlled comparison passes")
    args = parser.parse_args()
    stats, summaries = [], []
    for name, (type_id, patch, stack, policy, sequence, length) in scenarios().items():
        if not args.case or name in args.case:
            compare(args.out, name, generate(sequence, length), (type_id, patch, stack, policy),
                    stats, summaries, synthetic=True)
    if args.di_dir:
        for stem in ("fast_play_di", "bleep3_di"):
            wav = args.di_dir / f"{stem}.mono24.wav"
            for source in ("legacy", "controller") if args.controller else ("legacy",):
                raw = subprocess.check_output([args.out / "native", "capture", source, wav])
                frames = np.frombuffer(raw, dtype=FRAME)
                reference = (subprocess.check_output([args.out / "native", "raw-legacy", wav])
                             if source == "legacy" else None)
                compare(args.out, f"{source}_{stem}", frames, (ANALOG, 0, 50, -1),
                        stats, summaries, synthetic=False, reference=reference)
                if source == "legacy":
                    compare(args.out, f"{source}_{stem}_retrigger", frames, (ANALOG, 0, 50, 0),
                            stats, summaries, synthetic=False)
    write_csv(args.out / "transition_metrics.csv", stats)
    write_csv(args.out / "summary.csv", summaries)


if __name__ == "__main__":
    main()
