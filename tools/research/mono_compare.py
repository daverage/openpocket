#!/usr/bin/env python3
"""OpenPocket Synth, stage 5: single-note (mono) trackers compared on the same takes, scored the same way.

  python3 tools/research/mono_compare.py lookahead            measure each offline candidate's look-ahead (once)
  python3 tools/research/mono_compare.py run --set dev|test|owner [--only A,B] [--limit N]
  python3 tools/research/mono_compare.py report --set dev|test|owner

Candidates (each writes t, f0 rows to build/stage5/<set>/<cand>/<take>.csv; f0 0 = no note):
  engine   v70 MONO SYNTH as the pedal runs it (tools/archive/legacy-synth/mono_bench.c, build/mono_v70): causal
  q        Cycfi Q bitstream autocorrelation with its own conditioning (tools/research/q_bench.cpp): causal
  yin      librosa YIN, 46 ms frames, stamped at the frame's end: causal
  pyin     librosa pYIN with its Viterbi over the whole take: an offline reference, not a pedal candidate
  swiftf0  SwiftF0 0.3 (ONNX, 135 KB) run as published; stamped late by its measured look-ahead
  pesto    PESTO 2.0 (mir-1k_g7) run as published; stamped late by its measured look-ahead

Data sets (docs/archive/phase2b.md "Evaluation standard"): dev = GuitarSet solo takes of players 00-01; test = players 02-05,
frozen, scored once per finished candidate; gtechs = Guitar-TECHS electric DI (scales, single notes, techniques,
music); bass = IDMT bass lines; bassnotes = IDMT single bass notes (tools/research/prep_sets.py). YIN, pYIN and
SwiftF0 get a 35 Hz floor on the bass sets (60 Hz otherwise); the engine keeps its own.
GuitarSet is an acoustic guitar's pickup mix, not an electric DI.

Scores per candidate: notes right (the voice within 50 cents for most of the middle half of the note), octave errors,
median and 90th-percentile time to the first right frame (ms after the annotated onset), and mir_eval's raw pitch
accuracy, raw chroma accuracy, voicing recall and voicing false alarm on a 10 ms grid (the reference: the latest
sounding note). Rates carry a bootstrap 95 % interval over takes.
"""
import argparse, csv, glob, json, os, subprocess
import numpy as np

from track_eval import jams_notes, mono_score

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

OUT = os.path.join(ROOT, 'build', 'stage5')
FMIN = 60.0                                            # (the bass sets: 35 Hz, set in run())
LOOK = os.path.join(OUT, 'lookahead.json')
CANDS = ['engine', 'q', 'yin', 'pyin', 'swiftf0', 'pesto']
CAUSAL = {'engine', 'q', 'yin'}
EXTRA = []                                             # eng_NAME: build/mono_NAME, an engine variant (causal)


def takes(name):
    if name in ('dev', 'test'):
        players = ('00', '01') if name == 'dev' else ('02', '03', '04', '05')
        out = []
        for w in sorted(glob.glob(os.path.join(ROOT, 'build', 'guitarset', 'mono', '*_solo_mix.wav'))):
            b = os.path.basename(w)
            if b[:2] in players:
                j = os.path.join(ROOT, 'build', 'guitarset', 'annotation', b.replace('_mix.wav', '.jams'))
                out.append((b[:-4], w, ('jams', j)))
        return out
    if name in ('gtechs', 'bass', 'bassnotes'):            # tools/research/prep_sets.py
        d = os.path.join(OUT, 'sets', name)
        return [(os.path.basename(w)[:-4], w, ('csv', w[:-4] + '.csv')) for w in sorted(glob.glob(os.path.join(d, '*.wav')))]
    if name == 'owner':
        out = []
        for w in sorted(glob.glob(os.path.join(ROOT, 'build', 'mono_eval', '*-16.wav'))):
            ref = w.replace('-16.wav', '-ref.csv')
            if os.path.exists(ref): out.append((os.path.basename(w)[:-4], w, ('csv', ref)))
        return out
    raise SystemExit(f'unknown set {name}')


def notes_of(ref):
    kind, path = ref
    if kind == 'jams': return jams_notes(path)
    out = []
    for line in open(path):
        if line.startswith('#') or not line.strip(): continue
        a, b, m = map(float, line.split(',')[:3]); out.append((a, b, m))
    return sorted(out)


def read_wav(path, sr=None):
    import soundfile as sf
    x, r = sf.read(path, dtype='float32', always_2d=True)
    x = x[:, 0]
    if sr and r != sr:
        import librosa
        x = librosa.resample(x, orig_sr=r, target_sr=sr); r = sr
    return x, r


# ---- the candidates: wav -> (t, f0) arrays, t = when a pedal would know it ----
def run_bench(exe, wav, col):
    tmp = os.path.join(OUT, 'tmp.csv')
    subprocess.run([exe, wav, tmp], check=True)
    rows = list(csv.DictReader(open(tmp)))
    return np.array([float(r['t']) for r in rows]), np.array([float(r[col]) for r in rows])


def cand_engine(wav, _la): return run_bench(os.path.join(ROOT, 'build', 'mono_v70'), wav, 'voice')


def cand_q(wav, _la):
    tmp = os.path.join(OUT, 'tmp.csv')
    subprocess.run([os.path.join(ROOT, 'build', 'q_bench'), wav, tmp], check=True)
    rows = list(csv.reader(open(tmp)))
    if rows and not rows[0][0].replace('.', '').isdigit(): rows = rows[1:]
    return np.array([float(r[0]) for r in rows]), np.array([float(r[1]) for r in rows])


def cand_yin(wav, _la):
    import librosa
    x, sr = read_wav(wav, 22050)
    n, hop = 1024, 128
    f = librosa.yin(x, fmin=FMIN, fmax=1400, sr=sr, frame_length=n, hop_length=hop, center=False, trough_threshold=0.1)
    rms = librosa.feature.rms(y=x, frame_length=n, hop_length=hop, center=False)[0][:len(f)]
    f = np.where(rms > 10 ** (-50 / 20), f, 0.0)
    return (np.arange(len(f)) * hop + n) / sr, f


def cand_pyin(wav, _la):
    import librosa
    x, sr = read_wav(wav, 22050)
    n, hop = 2048, 256
    f, voiced, _ = librosa.pyin(x, fmin=FMIN, fmax=1400, sr=sr, frame_length=n, hop_length=hop, center=False)
    f = np.where(voiced, np.nan_to_num(f), 0.0)
    return (np.arange(len(f)) * hop + n) / sr, f


def swift_raw(x, sr):
    import swift_f0
    r = swift_f0.SwiftF0().detect(x, sr, fmin=max(46.875, FMIN), fmax=1400)
    return np.asarray(r.timestamps), np.where(np.asarray(r.confidence) >= 0.5, np.asarray(r.pitch_hz), 0.0)


def cand_swiftf0(wav, la):
    x, sr = read_wav(wav)
    t, f = swift_raw(x, sr)
    return t + la['swiftf0'], f


def pesto_raw(x, sr):
    import torch, pesto
    t, f, conf, _ = pesto.predict(torch.from_numpy(x), sr, step_size=10.0)
    t, f, conf = t.numpy() / 1000.0 if t.max() > 1000 else t.numpy(), f.numpy(), conf.numpy()
    return t, np.where(conf > 0.5, f, 0.0)


def cand_pesto(wav, la):
    x, sr = read_wav(wav)
    t, f = pesto_raw(x, sr)
    return t + la['pesto'], f


def runner(cand):
    if cand.startswith('eng_'): return lambda wav, la: run_bench(os.path.join(ROOT, 'build', 'mono_' + cand[4:]), wav, 'voice')
    return RUN[cand]


RUN = {'engine': cand_engine, 'q': cand_q, 'yin': cand_yin, 'pyin': cand_pyin, 'swiftf0': cand_swiftf0,
       'pesto': cand_pesto}


# ---- look-ahead: how far before the end of the audio a frame still changes when the audio is cut there ----
def lookahead():
    _, w, _ = takes('dev')[0]
    x, sr = read_wav(w)
    out = {}
    for name, raw in (('swiftf0', swift_raw), ('pesto', pesto_raw)):
        t_full, f_full = raw(x, sr)
        worst = 0.0
        for cut in np.linspace(5.0, min(25.0, len(x) / sr - 1), 12):
            t, f = raw(x[: int(cut * sr)], sr)
            n = min(len(f), len(f_full))
            diff = np.nonzero(np.abs(f[:n] - f_full[:n]) > 1e-3)[0]
            if len(diff): worst = max(worst, cut - t[diff[0]])
        out[name] = round(float(worst), 3)
        print(f'{name}: frames up to {worst * 1000:.0f} ms before the end of the audio change when it is cut there')
    os.makedirs(OUT, exist_ok=True)
    json.dump(out, open(LOOK, 'w'))


# ---- scoring ----
def frame_metrics(t, f, notes):
    import mir_eval
    grid = np.arange(0, max(t[-1] if len(t) else 0, notes[-1][1] if notes else 0), 0.01)
    ref = np.zeros_like(grid)
    for a, b, m in notes:                                  # the latest-starting sounding note wins
        ref[(grid >= a) & (grid < b)] = 440 * 2 ** ((m - 69) / 12)
    est = np.interp(grid, t, f, left=0, right=0) if len(t) else np.zeros_like(grid)
    i = np.searchsorted(t, grid, side='right') - 1           # (hold the last value: no interpolation across 0)
    est = np.where(i >= 0, f[np.clip(i, 0, len(f) - 1)], 0.0)
    s = mir_eval.melody.evaluate(grid, ref, grid, est)
    return {k: s[k] for k in ('Raw Pitch Accuracy', 'Raw Chroma Accuracy', 'Voicing Recall', 'Voicing False Alarm')}


def cache_path(sname, cand, take): return os.path.join(OUT, sname, cand, take + '.csv')


def run(args):
    global FMIN
    FMIN = 35.0 if args.set.startswith('bass') else 60.0
    la = json.load(open(LOOK)) if os.path.exists(LOOK) else {}
    only = args.only.split(',') if args.only else CANDS
    tk = takes(args.set)[: args.limit or None]
    for cand in only:
        if cand in ('swiftf0', 'pesto') and cand not in la: raise SystemExit('run "lookahead" first')
        os.makedirs(os.path.join(OUT, args.set, cand), exist_ok=True)
        for name, wav, _ in tk:
            p = cache_path(args.set, cand, name)
            if os.path.exists(p): continue
            t, f = runner(cand)(wav, la)
            np.savetxt(p, np.column_stack([t, f]), fmt='%.5f', delimiter=',')
        print(f'{cand}: {len(tk)} takes')


def boot(vals, n=2000, seed=1):
    v = np.asarray(vals, float)
    if len(v) == 0: return (float('nan'),) * 2
    r = np.random.default_rng(seed)
    m = [v[r.integers(0, len(v), len(v))].mean() for _ in range(n)]
    return float(np.percentile(m, 2.5)), float(np.percentile(m, 97.5))


def report(args):
    tk = takes(args.set)
    rows = []
    for cand in CANDS + (args.extra.split(',') if args.extra else []):
        per, lat, fm = [], [], {}
        for name, wav, ref in tk:
            p = cache_path(args.set, cand, name)
            if not os.path.exists(p): continue
            a = np.loadtxt(p, delimiter=',', ndmin=2)
            t, f = (a[:, 0], a[:, 1]) if len(a) else (np.array([]), np.array([]))
            notes = notes_of(ref)
            if not notes: continue
            s = mono_score([{'t': ti, 'v': fi} for ti, fi in zip(t, f)], notes, ['v'])['v']
            per.append((s['n'], s['ok'], s['octave'])); lat += s['lat']
            for k, v in frame_metrics(t, f, notes).items(): fm.setdefault(k, []).append(v)
        if not per: continue
        n = sum(x[0] for x in per); ok = sum(x[1] for x in per); octv = sum(x[2] for x in per)
        rate = [x[1] / x[0] for x in per if x[0]]
        lo, hi = boot(rate)
        rows.append((cand, len(per), n, 100 * ok / n, 100 * lo, 100 * hi, 100 * octv / n,
                     np.median(lat) if lat else float('nan'), np.percentile(lat, 90) if lat else float('nan'),
                     *(100 * np.mean(fm[k]) for k in ('Raw Pitch Accuracy', 'Raw Chroma Accuracy', 'Voicing Recall', 'Voicing False Alarm'))))
    print(f'set {args.set}: notes right = within 50 cents for most of the note\'s middle half; latency = to the first right frame')
    print(f'{"candidate":9} {"takes":>5} {"notes":>6} {"right %":>8} {"95% CI":>13} {"octave %":>8} {"lat50":>6} {"lat90":>6} {"RPA":>5} {"RCA":>5} {"VR":>5} {"VFA":>5}  causal')
    for r in rows:
        print(f'{r[0]:9} {r[1]:5d} {r[2]:6d} {r[3]:8.1f} {r[4]:6.1f}-{r[5]:5.1f} {r[6]:8.1f} {r[7]:6.0f} {r[8]:6.0f} {r[9]:5.1f} {r[10]:5.1f} {r[11]:5.1f} {r[12]:5.1f}  {"yes" if r[0] in CAUSAL or r[0].startswith("eng_") else "no (stamped late)" if r[0] != "pyin" else "no (offline reference)"}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['lookahead', 'run', 'report'])
    ap.add_argument('--set', default='dev')
    ap.add_argument('--only'); ap.add_argument('--limit', type=int); ap.add_argument('--extra')
    a = ap.parse_args()
    {'lookahead': lambda: lookahead(), 'run': lambda: run(a), 'report': lambda: report(a)}[a.cmd]()
