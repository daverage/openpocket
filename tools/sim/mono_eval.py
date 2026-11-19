#!/usr/bin/env python3
"""Score MONO SYNTH (tools/sim/mono_bench.c) on single-note playing (v60).

  PYTHONPATH=tools/sim python3 tools/sim/mono_eval.py BENCH [BENCH2 ...] [--solo N] [--offset K]

Sets: GuitarSet solo pickup mixes (build/guitarset, N takes from K), the bundled lead loop against its Guitar-TECHS
transcription, and the owner's DI recordings "My Audio/mvave.wav", "mvave2.wav" against their pYIN references.
Per note (track_eval.mono_score): right when the voice is within 50 cents for most of the middle half; octave; time to
the first right frame. Per frame while a note sounds: wrong (a pitch that is no sounding note), silent. After the last
note ends (a 60 ms grace): linger, the voice still sounding, as a share of those frames.
"""
import argparse, csv, glob, os, subprocess, sys
from statistics import median
from track_eval import ROOT, jams_notes, mono_score, cents

def notes_csv(path, vmin=0):
    out = []
    for line in open(path):
        if line.startswith('#') or not line.strip(): continue
        a, b, m, v = map(float, line.split(','))
        if v >= vmin: out.append((a, b, m))
    return sorted(out)

def pcm16(wav):
    """The bench reads 44.1 kHz mono PCM16: convert other WAVs (the owner's DI is 24-bit stereo; its left channel)."""
    import wave, numpy as np
    with wave.open(wav, 'rb') as w:
        if (w.getframerate(), w.getsampwidth(), w.getnchannels()) == (44100, 2, 1): return wav
        if w.getframerate() != 44100: raise ValueError(f'{wav}: 44.1 kHz expected')
        width, ch, raw = w.getsampwidth(), w.getnchannels(), w.readframes(w.getnframes())
    if width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 | b[:, 2].astype(np.int32) << 16)
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    else:
        x = np.frombuffer(raw, dtype={2: '<i2', 4: '<i4'}[width]) / float(1 << (8 * width - 1))
    x = x.reshape(-1, ch)[:, 0]
    out = os.path.join(ROOT, 'build', 'mono_eval', os.path.basename(wav).replace('.wav', '-16.wav'))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(wav):
        with wave.open(out, 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
            w.writeframes((np.clip(x, -1, 1 - 1 / 32768) * 32768).astype('<i2').tobytes())
    return out

def run(bench, wav, tag):
    wav = pcm16(wav)
    out = os.path.join(ROOT, 'build', 'mono_eval', os.path.basename(bench), tag + '.csv')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(bench):
        subprocess.run([bench, wav, out], check=True)
    rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(open(out))]
    return rows

def frames(rows, notes):
    wrong = silent = sounding = linger = after = 0
    import bisect
    starts = [a for a, _, _ in notes]
    for r in rows:
        t, v = r['t'], r['voice']
        act = [m for a, b, m in notes if a + 0.03 <= t < b]
        if act:
            sounding += 1
            if v <= 0: silent += 1
            elif not any(abs(cents(v, 440 * 2 ** ((m - 69) / 12))) < 50 for m in act): wrong += 1
        elif not any(a <= t < b + 0.06 for a, b, _ in notes):
            after += 1; linger += v > 0
    return dict(sounding=sounding, wrong=wrong, silent=silent, after=after, linger=linger)

def total(items):
    keys = items[0][1].keys()
    s = dict(n=0, ok=0, octave=0, lat=[])
    f = {k: 0 for k in keys}
    for res, fr in items:
        for k in ('n', 'ok', 'octave'): s[k] += res['voice'][k]
        s['lat'] += res['voice']['lat']
        for k in keys: f[k] += fr[k]
    return s, f

def report(name, s, f):
    lat = sorted(s['lat'])
    med = median(lat) if lat else float('nan'); p90 = lat[int(.9 * len(lat))] if lat else float('nan')
    print(f"  {name:22s} right {100*s['ok']/max(1,s['n']):5.1f}% of {s['n']:4d}, octave {s['octave']:3d}, "
          f"to pitch {med:5.1f}/{p90:5.1f} ms | wrong {100*f['wrong']/max(1,f['sounding']):4.1f}% "
          f"silent {100*f['silent']/max(1,f['sounding']):4.1f}% | linger {100*f['linger']/max(1,f['after']):4.1f}%")

p = argparse.ArgumentParser()
p.add_argument('bench', nargs='+'); p.add_argument('--solo', type=int, default=30); p.add_argument('--offset', type=int, default=0)
a = p.parse_args()
gs = os.path.join(ROOT, 'build', 'guitarset')
solo = sorted(glob.glob(os.path.join(gs, 'mono', '*_solo_mix.wav')))[a.offset:a.offset + a.solo]
sets = [('GuitarSet solo', [(w, jams_notes(os.path.join(gs, 'annotation', os.path.basename(w).replace('_mix.wav', '.jams'))))
                            for w in solo])]
lead = os.path.join(ROOT, 'build', 'acoustic', 'lead.truth.csv')
if os.path.exists(lead): sets.append(('lead loop', [(os.path.join(ROOT, 'assets', 'loops', 'lead.wav'), notes_csv(lead, 30))]))
for n, ref in (('mvave', 'build/mvave/ref.csv'), ('mvave2', 'build/mvave2/ref.csv')):
    wav = os.path.join(ROOT, 'My Audio', n + '.wav')
    if os.path.exists(wav) and os.path.exists(os.path.join(ROOT, ref)):
        sets.append((n + ' (owner DI)', [(wav, notes_csv(os.path.join(ROOT, ref)))]))
for bench in a.bench:
    print(bench)
    for name, items in sets:
        scored = []
        for wav, notes in items:
            rows = run(bench, wav, os.path.basename(wav).replace('.wav', ''))
            v = [{'t': r['t'], 'voice': r['voice']} for r in rows]
            scored.append((mono_score(v, notes, ['voice']), frames(v, notes)))
        report(name, *total(scored))
