#!/usr/bin/env python3
"""OpenPocket Synth, stage 5: electric guitar and bass test sets for tools/research/mono_compare.py.

  python3 tools/research/prep_sets.py gtechs        Guitar-TECHS (CC BY 4.0), DI takes and their MIDI
  python3 tools/research/prep_sets.py bass          IDMT-SMT-Bass-Single-Track (CC BY-NC-ND 4.0): 17 bass lines
  python3 tools/research/prep_sets.py bassnotes     IDMT-SMT-Bass (CC BY-NC-ND 4.0): single notes, E1 .. G3

Reads build/datasets/ (downloaded from Zenodo 14963133, 7544099, 7188892) and writes build/stage5/sets/<set>/:
NAME.wav (44.1 kHz mono PCM16: what the benches read) and NAME.csv (onset, offset, MIDI note per line). Nothing here is
committed: the IDMT licence allows no derivatives, so its audio and its notes stay on this machine.

Guitar-TECHS: the notes come from a Fishman Triple Play hexaphonic pickup, whose MIDI runs late. Each take's lag is
found by sliding its note onsets over the DI's onset strength (-200 .. +200 ms) and taking the best fit; it is reported
and taken out. Pitch bends are not in the reference (the note number only).
IDMT single notes: the pitch is the file name's string (1 = E1, MIDI 28) and fret; the note runs from where the level
first comes within 30 dB of its peak to where it last falls 40 dB under it.
"""
import glob, os, sys
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
DATA = os.path.join(ROOT, 'build', 'datasets')
OUT = os.path.join(ROOT, 'build', 'stage5', 'sets')


def write(set_name, name, x, sr, notes):
    import soundfile as sf, librosa
    d = os.path.join(OUT, set_name); os.makedirs(d, exist_ok=True)
    if sr != 44100: x = librosa.resample(x, orig_sr=sr, target_sr=44100)
    x = x / max(1e-9, np.max(np.abs(x))) * min(1.0, np.max(np.abs(x)) * 1.0)   # (as recorded; clipped only if over)
    sf.write(os.path.join(d, name + '.wav'), np.clip(x, -1, 1), 44100, subtype='PCM_16')
    with open(os.path.join(d, name + '.csv'), 'w') as f:
        for a, b, m in sorted(notes): f.write(f'{a:.4f},{b:.4f},{m:.0f}\n')


def midi_notes(path):
    import mido
    mid = mido.MidiFile(path)
    t, on, out = 0.0, {}, []
    for msg in mid:                                   # (merged tracks, times in seconds)
        t += msg.time
        if msg.type == 'note_on' and msg.velocity > 0: on[(msg.channel, msg.note)] = t
        elif msg.type in ('note_off', 'note_on'):
            a = on.pop((msg.channel, msg.note), None)
            if a is not None and t - a > 0.02: out.append((a, t, msg.note))
    return sorted(out)


def lag_of(x, sr, notes):
    import librosa
    hop = 256
    env = librosa.onset.onset_strength(y=x, sr=sr, hop_length=hop)
    env = env / (env.max() + 1e-9)
    on = np.array([a for a, _, _ in notes])
    best, lag = -1, 0.0
    for d in np.arange(-0.2, 0.2001, 0.002):
        idx = np.round((on - d) * sr / hop).astype(int)
        idx = idx[(idx >= 0) & (idx < len(env))]
        s = env[idx].sum() + sum(env[np.clip(idx + k, 0, len(env) - 1)].sum() * 0.5 for k in (-1, 1))
        if s > best: best, lag = s, d
    return lag


def gtechs():
    import soundfile as sf
    rows = []
    for di in sorted(glob.glob(os.path.join(DATA, 'guitar-techs', 'P*', 'audio', 'directinput', '*.wav'))):
        part = di.split(os.sep)[-4]                        # P1_scales ...
        key = os.path.basename(di)[len('directinput_'):-4]
        mid = os.path.join(DATA, 'guitar-techs', part, 'midi', f'midi_{key}.mid')
        if not os.path.exists(mid): print('no MIDI for', di); continue
        x, sr = sf.read(di, dtype='float32', always_2d=True); x = x[:, 0]
        notes = midi_notes(mid)
        if not notes: continue
        lag = lag_of(x, sr, notes)
        notes = [(a - lag, b - lag, m) for a, b, m in notes if a - lag >= 0]
        name = f'{part}_{key}'
        write('gtechs', name, x, sr, notes)
        rows.append((name, len(notes), lag))
        print(f'{name:28} {len(notes):5d} notes, MIDI {lag * 1000:+.0f} ms against the DI, '
              f'MIDI {min(m for *_, m in notes):.0f}..{max(m for *_, m in notes):.0f}')


def bass():
    import soundfile as sf
    for w in sorted(glob.glob(os.path.join(DATA, 'idmt-bass', 'audio', '*.wav'))):
        n = os.path.basename(w)[:-4]
        c = os.path.join(DATA, 'idmt-bass', 'misc', 'notes_csv', n + '_note_parameters.csv')
        if not os.path.exists(c): continue
        notes = []
        for line in open(c):
            p = line.strip().split(',')
            if len(p) >= 3: notes.append((float(p[0]), float(p[1]), float(p[2])))
        x, sr = sf.read(w, dtype='float32', always_2d=True)
        write('bass', 'line_' + n, x[:, 0], sr, notes)
        print(f'line_{n}: {len(notes)} notes, MIDI {min(m for *_, m in notes):.0f}..{max(m for *_, m in notes):.0f}')


def bassnotes(per_dir=60, seed=1):
    import soundfile as sf
    rng = np.random.default_rng(seed)
    base = [28, 33, 38, 43]                               # E1 A1 D2 G2
    total = 0
    for d in sorted(glob.glob(os.path.join(DATA, 'idmt-bass', 'PS', '*')) + glob.glob(os.path.join(DATA, 'idmt-bass', 'ES', '*'))):
        files = sorted(glob.glob(os.path.join(d, '*.wav')))
        if not files: continue
        pick = [files[i] for i in rng.choice(len(files), min(per_dir, len(files)), replace=False)]
        for w in pick:
            p = os.path.basename(w)[:-4].split('_')
            try: s, fret = int(p[-2]), int(p[-1])
            except ValueError: continue
            if not 1 <= s <= 4: continue
            x, sr = sf.read(w, dtype='float32', always_2d=True); x = x[:, 0]
            hop = 256
            e = np.array([np.sqrt(np.mean(x[i:i + 1024] ** 2) + 1e-12) for i in range(0, max(1, len(x) - 1024), hop)])
            db = 20 * np.log10(e / e.max())
            on = np.nonzero(db > -30)[0]; live = np.nonzero(db > -40)[0]
            if not len(on): continue
            a, b = on[0] * hop / sr, (live[-1] * hop + 1024) / sr
            write('bassnotes', f'{os.path.basename(d)}_{os.path.basename(w)[:-4]}', x, sr, [(a, b, base[s - 1] + fret)])
            total += 1
    print(f'{total} single bass notes (up to {per_dir} from each playing / expression style)')


if __name__ == '__main__':
    {'gtechs': gtechs, 'bass': bass, 'bassnotes': bassnotes}[sys.argv[1]]()
