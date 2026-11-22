#!/usr/bin/env python3
"""Validate a FastTrack ground-truth set.   python3 tests/archive/fasttrack/validate_annotations.py DIR   (DIR holds recordings.csv and events.csv; paths in recordings.csv are relative to DIR or absolute)
Errors (exit 1): missing columns, unknown enum, non-numeric fields, unsorted/duplicate times, event after the end of the file, confidence outside 0..1, 'intended' timing with confidence > 0.3,
identical (time, action, pitch) twice, expected_event not the default for the action without a note, DI set not 44.1 kHz mono / processing not 'none', recording without events.
Warnings (reported, do not fail): an annotated onset with no energy rise within +-40 ms in the audio (annotation timing suspect), HAMMER_ON/PULL_OFF without a ringing note before it,
REPICK at a different pitch than the previous pick, pitch outside 28..96, IOI statistics, events with confidence < 0.5 (excluded from headline scores)."""
import sys, os, csv, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gt_format as G

def energy_rise_near(x, sr, t_ms, win=40):
    h = max(1, int(0.002 * sr)); i0 = int((t_ms - win) / 1000 * sr); i1 = int((t_ms + win) / 1000 * sr)
    if i0 < 0 or i1 > len(x): return None
    seg = np.abs(x[max(0, i0 - int(0.03 * sr)):i1]); k = np.ones(h) / h; e = np.convolve(seg, k, 'same'); pre = np.percentile(e[:max(1, int(0.03 * sr))], 50) + 1e-6
    return float(e.max() / pre)

def validate(d, audio=True):
    errs, warns = [], []
    rp, ep = os.path.join(d, 'recordings.csv'), os.path.join(d, 'events.csv')
    if not (os.path.exists(rp) and os.path.exists(ep)): return ['recordings.csv or events.csv missing'], [], {}
    recs = list(csv.DictReader(open(rp))); evs = list(csv.DictReader(open(ep)))
    if recs and set(G.REC_COLS) - set(recs[0]): errs.append('recordings.csv missing columns: %s' % sorted(set(G.REC_COLS) - set(recs[0])))
    if evs and set(G.EV_COLS) - set(evs[0]): errs.append('events.csv missing columns: %s' % sorted(set(G.EV_COLS) - set(evs[0])))
    if errs: return errs, warns, {}
    byrec = collections.defaultdict(list)
    for k, e in enumerate(evs, 2): e['_line'] = k; byrec[e['recording']].append(e)
    stats = collections.Counter(); sig = {}
    for r in recs:
        name = r['recording']
        if r['signal'] not in G.SIGNALS: errs.append(f'{name}: signal {r["signal"]!r} not in {G.SIGNALS}')
        if r['split'] not in G.SPLITS: errs.append(f'{name}: split {r["split"]!r} not in {G.SPLITS}')
        if r['signal'] == 'DI':
            if r['processing'] != 'none': errs.append(f'{name}: DI recording must have processing=none (is {r["processing"]!r})')
            if str(r['sample_rate']) != '44100' or str(r['channels']) != '1': errs.append(f'{name}: DI recordings must be 44.1 kHz mono (is {r["sample_rate"]} Hz, {r["channels"]} ch)')
        path = r['path'] if os.path.isabs(r['path']) else os.path.join(d, r['path']); dur = None; x = None; sr = None
        if audio:
            try:
                import soundfile as sf
                info = sf.info(path); dur = info.duration * 1000
                if str(info.samplerate) != str(r['sample_rate']): errs.append(f'{name}: file is {info.samplerate} Hz, recordings.csv says {r["sample_rate"]}')
                x, sr = sf.read(path, dtype='float32'); x = x if x.ndim == 1 else x[:, 0]
            except Exception as ex: errs.append(f'{name}: cannot read audio {path}: {ex}')
        es = byrec.get(name, [])
        if not es: errs.append(f'{name}: no events'); continue
        seen = set(); last = -1; prev_pick = None; ringing = False; no_rise = 0; checked = 0
        for e in es:
            ln = e['_line']
            try: t = float(e['t_ms']); c = float(e['confidence'])
            except ValueError: errs.append(f'events.csv:{ln}: t_ms/confidence not numeric'); continue
            a = e['action']
            if a not in G.ACTIONS: errs.append(f'events.csv:{ln}: action {a!r} not in {G.ACTIONS}'); continue
            if e['expected_event'] not in G.EXPECTED: errs.append(f'events.csv:{ln}: expected_event {e["expected_event"]!r} invalid')
            elif e['expected_event'] != G.DEFAULT_EXPECTED[a] and not e['notes']: errs.append(f'events.csv:{ln}: {a} expects {G.DEFAULT_EXPECTED[a]}, has {e["expected_event"]} and no note explains it')
            if e['timing_basis'] not in G.TIMING: errs.append(f'events.csv:{ln}: timing_basis {e["timing_basis"]!r} invalid')
            if e['timing_basis'] == 'intended' and c > 0.3: errs.append(f'events.csv:{ln}: timing_basis=intended needs confidence <= 0.3 (intended timing is not acoustic timing)')
            if not 0 <= c <= 1: errs.append(f'events.csv:{ln}: confidence {c} outside 0..1')
            if t < last: errs.append(f'events.csv:{ln}: times not sorted ({t} after {last})')
            key = (t, a, e['pitch_midi'])
            if key in seen: errs.append(f'events.csv:{ln}: duplicate event (same time, action and pitch) {key}')
            seen.add(key)
            last = max(last, t)
            if dur is not None and not (0 <= t <= dur): errs.append(f'events.csv:{ln}: t_ms {t} outside the file (0..{dur:.0f})')
            if e['pitch_midi'] not in ('', 'None'):
                try:
                    pm = float(e['pitch_midi'])
                    if not 28 <= pm <= 96: warns.append(f'events.csv:{ln}: pitch {pm} outside 28..96')
                except ValueError: errs.append(f'events.csv:{ln}: pitch_midi not numeric')
            else: pm = None
            if a in ('SLIDE', 'BEND') and e['end_pitch_midi'] in ('', 'None'): warns.append(f'events.csv:{ln}: {a} without end_pitch_midi')
            if a in ('HAMMER_ON', 'PULL_OFF') and not ringing: warns.append(f'events.csv:{ln}: {a} but no ringing note annotated before it')
            if a == 'REPICK' and prev_pick is not None and pm is not None and abs(pm - prev_pick) > 0.5: warns.append(f'events.csv:{ln}: REPICK at a different pitch than the previous pick')
            if a in G.PICK_LIKE: prev_pick = pm; ringing = True
            if a in ('HARD_MUTE', 'LIFT'): ringing = False
            if a in ('HAMMER_ON', 'PULL_OFF', 'SLIDE', 'BEND') and pm is not None: prev_pick = pm
            stats[a] += 1
            if c < 0.5: stats['low_confidence'] += 1
            if x is not None and a in G.PICK_LIKE + ('HAMMER_ON', 'PULL_OFF', 'HARD_MUTE'):
                rr = energy_rise_near(x, sr, t); checked += 1
                if rr is not None and rr < 2.0: no_rise += 1
        if checked and no_rise / checked > 0.25: warns.append(f'{name}: {no_rise}/{checked} annotated onsets show no energy rise within +-40 ms: annotation timing suspect')
        iois = np.diff([float(e['t_ms']) for e in es if e['action'] in G.PICK_LIKE])
        if len(iois): stats['pick_iois'] += len(iois); sig[name] = (float(np.median(iois)), float(iois.min()))
    for nm in set(byrec) - {r['recording'] for r in recs}: errs.append(f'events for unknown recording {nm}')
    return errs, warns, dict(stats)

if __name__ == '__main__':
    d = sys.argv[1]; errs, warns, st = validate(d, audio='--no-audio' not in sys.argv)
    print(f'{d}: {len(errs)} errors, {len(warns)} warnings'); print('events by action:', {k: v for k, v in sorted(st.items()) if k.isupper() or k in ('low_confidence', 'pick_iois')})
    for e in errs[:30]: print('ERROR', e)
    for w in warns[:15]: print('warn ', w)
    if len(errs) > 30: print(f'... {len(errs)-30} more errors')
    sys.exit(1 if errs else 0)
