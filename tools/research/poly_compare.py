#!/usr/bin/env python3
"""OpenPocket Synth, stage 5: polyphonic note tracking compared on the same takes, scored the same way.

  python3 tools/research/poly_compare.py lookahead
  python3 tools/research/poly_compare.py run --set dev|test|gtchords|bass [--only A,B] [--limit N]
  python3 tools/research/poly_compare.py report --set ...

Candidates (each writes rows "t, f1, f2, ..." to build/stage5/poly/<set>/<cand>/<take>.csv; the pitches it reports
at time t, in Hz):
  tracker  v70 shared tracker's notes (engine/px_track.h, up to 6 a frame): the analysis alone      causal
  voices   v70 two-voice allocator (engine/px_poly_track.h via PULSE POLY): what the synth plays     causal
           (a voice counts while its gain is above 0.065, docs/POLY-SYNTH-DESIGN.md)
  bp       Spotify Basic Pitch (ICASSP 2022, ONNX 230 KB) run as published: its note events;      stamped late
           stamped late by its measured look-ahead
Sets: dev / test = GuitarSet comping takes, players 00-01 / 02-05 (test frozen: score a finished candidate once);
gtchords = Guitar-TECHS chords (electric DI); bass = IDMT bass lines.

Frame scores on a 10 ms grid against the annotated notes sounding then (a reported pitch matches a note within 50
cents, each note once): precision, recall, recall of up to two notes (the synth has two voices), F; ghosts: the
share of the unmatched reported pitches that sit 12, 19, 24 or 28 semitones (+-50 cents) above a sounding note (a
harmonic taken for a note); entry: the time from a note's onset to the first frame that reports it (median, 90th).
"""
import argparse, glob, json, os, subprocess, sys, tempfile
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'sim')); sys.path.insert(0, os.path.dirname(__file__))
from track_eval import jams_notes  # noqa: E402

OUT = os.path.join(ROOT, 'build', 'stage5', 'poly')
LOOK = os.path.join(OUT, 'lookahead.json')
CANDS = ['tracker', 'voices', 'bp', 'bp0', 'bpf']


def takes(name):
    if name in ('dev', 'test'):
        players = ('00', '01') if name == 'dev' else ('02', '03', '04', '05')
        out = []
        for w in sorted(glob.glob(os.path.join(ROOT, 'build', 'guitarset', 'mono', '*_comp_mix.wav'))):
            b = os.path.basename(w)
            if b[:2] in players:
                out.append((b[:-4], w, ('jams', os.path.join(ROOT, 'build', 'guitarset', 'annotation', b.replace('_mix.wav', '.jams')))))
        return out
    d = {'gtchords': 'gtchords', 'bass': 'bass'}[name]
    return [(os.path.basename(w)[:-4], w, ('csv', w[:-4] + '.csv'))
            for w in sorted(glob.glob(os.path.join(ROOT, 'build', 'stage5', 'sets', d, '*.wav')))]


def notes_of(ref):
    kind, path = ref
    if kind == 'jams': return jams_notes(path)
    return sorted(tuple(map(float, l.split(',')[:3])) for l in open(path) if l.strip() and not l.startswith('#'))


def read(path, sr=None):
    import soundfile as sf
    x, r = sf.read(path, dtype='float32', always_2d=True); x = x[:, 0]
    if sr and r != sr:
        import librosa
        x = librosa.resample(x, orig_sr=r, target_sr=sr); r = sr
    return x, r


def engine_trace(wav):
    x, _ = read(wav, 44100)
    with tempfile.NamedTemporaryFile(suffix='.csv', delete=False) as tf: tr = tf.name
    subprocess.run([os.path.join(ROOT, 'build', 'poly_v70'), tr, 'pulse'], input=x.astype('<f4').tobytes(),
                   stdout=subprocess.DEVNULL, check=True)
    import csv
    rows = list(csv.DictReader(open(tr))); os.unlink(tr)
    return rows


def cand_tracker(wav, _la):
    rows = engine_trace(wav)
    return [(float(r['t']), [float(r[f'n{k}']) for k in range(1, int(r['nn']) + 1)]) for r in rows]


def cand_voices(wav, _la):
    rows = engine_trace(wav)
    return [(float(r['t']), [float(r[v]) for v, g in (('v1', 'gain1'), ('v2', 'gain2')) if float(r[g]) > 0.065 and float(r[v]) > 0])
            for r in rows]


def bp_events(x, sr):
    from basic_pitch.inference import predict
    import soundfile as sf
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tf: p = tf.name
    sf.write(p, x, sr)
    _, _, ev = predict(p)
    os.unlink(p)
    return [(float(a), float(b), int(m)) for a, b, m, *_ in ev]


def frames_of_events(ev, end, shift=0.0, hop=0.01):
    grid = np.arange(0, end, hop)
    return [(t, [440 * 2 ** ((m - 69) / 12) for a, b, m in ev if a + shift <= t < b + shift]) for t in grid]


def cand_bp(wav, la):
    x, sr = read(wav, 22050)
    return frames_of_events(bp_events(x, sr), len(x) / sr + la['bp'], la['bp'])


def cand_bp0(wav, _la):                                # (offline: no look-ahead charged; an accuracy ceiling only)
    x, sr = read(wav, 22050)
    return frames_of_events(bp_events(x, sr), len(x) / sr)


BPF_LOOK = 0.091                                        # (measured: the frame output's look-ahead)


def cand_bpf(wav, _la):                                 # Basic Pitch's frame output (note > 0.5), stamped 91 ms late
    import tempfile, soundfile as sf
    from basic_pitch.inference import run_inference, Model
    import basic_pitch
    global _BPM
    if '_BPM' not in globals(): _BPM = Model(os.path.join(os.path.dirname(basic_pitch.__file__), 'saved_models', 'icassp_2022', 'nmp.onnx'))
    x, sr = read(wav, 22050)
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tf: p = tf.name
    sf.write(p, x, sr); note = run_inference(p, _BPM)['note']; os.unlink(p)
    fps = 22050 / 256
    return [(i / fps + BPF_LOOK, [440 * 2 ** ((k + 21 - 69) / 12) for k in np.nonzero(row > 0.5)[0]]) for i, row in enumerate(note)]


RUN = {'bpf': cand_bpf, 'tracker': cand_tracker, 'voices': cand_voices, 'bp': cand_bp, 'bp0': cand_bp0}


def lookahead():
    _, w, _ = takes('dev')[0]
    x, sr = read(w, 22050)
    full = bp_events(x, sr)
    worst = 0.0
    for cut in np.linspace(6.0, min(20.0, len(x) / sr - 1), 8):
        ev = bp_events(x[: int(cut * sr)], sr)
        # the earliest time at which the cut changes anything: an event that starts, ends or differs before the cut
        a = {(round(s, 2), m) for s, e, m in full if s < cut}; b = {(round(s, 2), m) for s, e, m in ev}
        diff = sorted(t for t, m in a ^ b)
        ends = [min(e, cut) for s, e, m in ev if e >= cut - 0.02]
        changed = min(diff + [cut - 0.0] if diff else [cut])
        if diff: worst = max(worst, cut - changed)
    os.makedirs(OUT, exist_ok=True)
    json.dump({'bp': round(float(worst), 3)}, open(LOOK, 'w'))
    print(f'Basic Pitch: notes up to {worst * 1000:.0f} ms before the end of the audio change when it is cut there')


def cache(sname, cand, take): return os.path.join(OUT, sname, cand, take + '.csv')


def run(args):
    la = json.load(open(LOOK)) if os.path.exists(LOOK) else {}
    for cand in (args.only.split(',') if args.only else CANDS):
        if cand == 'bp' and 'bp' not in la: raise SystemExit('run "lookahead" first')
        os.makedirs(os.path.join(OUT, args.set, cand), exist_ok=True)
        tk = takes(args.set)[: args.limit or None]
        for name, wav, _ in tk:
            p = cache(args.set, cand, name)
            if os.path.exists(p): continue
            rows = RUN[cand](wav, la)
            with open(p, 'w') as f:
                for t, fs in rows: f.write(','.join([f'{t:.4f}'] + [f'{v:.2f}' for v in fs]) + '\n')
        print(f'{cand}: {len(tk)} takes')


def load(p):
    t, fs = [], []
    for line in open(p):
        v = line.strip().split(',')
        t.append(float(v[0])); fs.append([float(a) for a in v[1:] if float(a) > 0])
    return np.array(t), fs


def score(t, fs, notes, hop=0.01):
    end = max(t[-1] if len(t) else 0, notes[-1][1] if notes else 0)
    grid = np.arange(0, end, hop)
    idx = np.searchsorted(t, grid, side='right') - 1
    tp = fp = fn = ghost = 0; r2n = r2d = 0
    for g, i in zip(grid, idx):
        est = list(fs[i]) if i >= 0 else []
        ref = [440 * 2 ** ((m - 69) / 12) for a, b, m in notes if a <= g < b]
        used = [False] * len(ref); hit = 0
        for e in est:
            j = next((k for k, r in enumerate(ref) if not used[k] and abs(1200 * np.log2(e / r)) < 50), None)
            if j is None:
                fp += 1
                if any(abs(1200 * np.log2(e / r) - 100 * s) < 50 for r in ref for s in (12, 19, 24, 28)): ghost += 1
            else: used[j] = True; hit += 1
        tp += hit; fn += len(ref) - hit
        if ref: r2n += min(hit, 2); r2d += min(len(ref), 2)
    lat = []
    for a, b, m in notes:
        f = 440 * 2 ** ((m - 69) / 12)
        i0, i1 = np.searchsorted(t, a), np.searchsorted(t, min(b, a + 0.5))
        for i in range(i0, i1):
            if any(abs(1200 * np.log2(e / f)) < 50 for e in fs[i]): lat.append(1000 * (t[i] - a)); break
    return tp, fp, fn, ghost, r2n, r2d, lat, len(notes)


def report(args):
    print(f'set {args.set}: frames of 10 ms; P precision, R recall, R2 recall of up to two notes, ghosts = harmonics among the wrong notes;'
          ' entry = onset to first report (ms), found = notes reported within 0.5 s')
    print(f'{"candidate":9} {"takes":>5} {"P %":>6} {"R %":>6} {"R2 %":>6} {"F %":>6} {"ghost %":>8} {"entry50":>8} {"entry90":>8} {"found %":>8}')
    for cand in CANDS:
        S = np.zeros(6); lat = []; nn = 0; k = 0
        for name, wav, ref in takes(args.set):
            p = cache(args.set, cand, name)
            if not os.path.exists(p): continue
            t, fs = load(p); notes = notes_of(ref)
            if not notes: continue
            *s, l, n = score(t, fs, notes); S += s; lat += l; nn += n; k += 1
        if not k: continue
        tp, fp, fn, gh, r2n, r2d = S
        P, R = tp / max(1, tp + fp), tp / max(1, tp + fn)
        print(f'{cand:9} {k:5d} {100 * P:6.1f} {100 * R:6.1f} {100 * r2n / max(1, r2d):6.1f} {200 * P * R / max(1e-9, P + R):6.1f} '
              f'{100 * gh / max(1, fp):8.1f} {np.median(lat):8.0f} {np.percentile(lat, 90):8.0f} {100 * len(lat) / max(1, nn):8.1f}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['lookahead', 'run', 'report'])
    ap.add_argument('--set', default='dev'); ap.add_argument('--only'); ap.add_argument('--limit', type=int)
    a = ap.parse_args()
    {'lookahead': lookahead, 'run': lambda: run(a), 'report': lambda: report(a)}[a.cmd]()
