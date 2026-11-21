#!/usr/bin/env python3
"""Reaper review session for FastTrack annotations (no Reaper install needed to generate or read it).
  python3 tests/fasttrack/reaper_session.py make AUDIO.wav RECORDING EVENTS.csv OUT.RPP     build the session from a draft
  python3 tests/fasttrack/reaper_session.py import SESSION.RPP RECORDING OUT_EVENTS.csv      read your corrected markers back into events.csv (v1 format)
Session contents (the original audio is referenced by absolute path, never copied or changed):
  track 1  the original audio.
  PROJECT MARKERS  one per proposed event, named 'ACTION [pitch] [conf]'; colour by class (green pick, lime repick, olive palm mute, magenta hammer-on, purple pull-off, red hard mute, orange noise);
           a leading '?' marks an UNCERTAIN proposal (confidence < 0.5, or a legato/hard-mute/noise class, which are the least certain);
           grey 'CAND?' markers: onset candidates from a MORE SENSITIVE detector that the draft did not keep: possible missed notes.
  track 2  'UNCERTAIN: listen' - an empty item at every uncertain proposal (text = the proposal and the evidence), so they can be stepped through with 'next item'.
  track 3  'GAPS: any missed pick?' - an item over every stretch where sound continues for >= 120 ms between proposals, to listen for notes that no marker covers.
Review: step through track 2, then track 3; for each marker move it to the first waveform rise, rename it to the right ACTION (and pitch), or delete it; add markers (M) for missed events.
Naming: 'PICK 45', 'REPICK 45', 'HAMMER_ON 47', 'PULL_OFF 45', 'PALM_MUTE_PICK 40', 'HARD_MUTE', 'NOISE', 'LIFT', 'SLIDE', 'BEND', 'VIBRATO'.  Optional trailing confidence 0..1.
On import: a marker that is unchanged from the proposal (same time and name) or still starts with '?' stays UNREVIEWED (its draft confidence, basis spectrogram) unless you pass --accept-unchanged (then it counts as reviewed: confidence 0.9 unless a number is in the name, basis 'listening'); a marker you moved or renamed counts as reviewed; 'CAND?' and 'GAP' markers are ignored."""
import sys, os, csv, re, uuid, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gt_format as G
COL = {'PICK': (0, 160, 0), 'REPICK': (120, 220, 0), 'PALM_MUTE_PICK': (128, 128, 0), 'HAMMER_ON': (220, 0, 220), 'PULL_OFF': (130, 0, 190), 'HARD_MUTE': (230, 0, 0), 'NOISE': (255, 140, 0), 'LIFT': (0, 150, 220), 'CAND': (130, 130, 130), 'SLIDE': (0, 190, 190), 'BEND': (0, 190, 190), 'VIBRATO': (0, 190, 190)}
def rc(c): r, g, b = c; return (r | g << 8 | b << 16) | 0x1000000
def guid(): return '{' + str(uuid.uuid4()).upper() + '}'
def gaps_and_cands(audio, ev_times):
    import soundfile as sf
    x, sr = sf.read(audio, dtype='float32'); x = x if x.ndim == 1 else x[:, 0]; h = int(0.010 * sr); e = np.sqrt(np.convolve(x ** 2, np.ones(h) / h, 'same')); db = 20 * np.log10(e + 1e-7); step = int(0.001 * sr)
    cands = []; last = -1e9
    for i in range(int(0.04 * sr), len(db) - int(0.02 * sr), step):                                   # more sensitive than the draft: 5 dB, gate -52 dB, 30 ms apart
        if db[i] - np.min(db[i - int(0.04 * sr):i]) > 5 and db[i] > -52 and (i - last) > 0.030 * sr and db[i] >= db[i - step]: cands.append(i / sr); last = i
    cands = [c for c in cands if not any(abs(c - t) < 0.035 for t in ev_times)]
    ts = sorted(ev_times); gaps = []; edges = [0.0] + ts + [len(x) / sr]
    for a, b in zip(edges[:-1], edges[1:]):
        if b - a < 0.12: continue
        seg = e[int(a * sr):int(b * sr)]; on = seg > 0.004
        if on.mean() > 0.0 and np.sum(on) / sr >= 0.12: gaps.append((a, b))
    return cands, gaps
def make(audio, rec, evp, out):
    import soundfile as sf
    info = sf.info(audio); dur = info.duration; evs = [r for r in csv.DictReader(open(evp)) if r['recording'] == rec]
    ev_times = [float(r['t_ms']) / 1000 for r in evs]; cands, gaps = gaps_and_cands(audio, ev_times)
    L = ['<REAPER_PROJECT 0.1 "7.0/macOS-arm64" 1700000000 0', '  <NOTES 0 2', '  >', '  TEMPO 120 4 4 0', f'  SAMPLERATE {info.samplerate} 0 0']
    mk = 1; unc = []
    def marker(t, name, color):
        nonlocal mk; L.append(f'  MARKER {mk} {t:.6f} "{name}" 0 {rc(color)} 1 R {guid()}'); mk += 1
    props = []
    for r in evs:
        t = float(r['t_ms']) / 1000; a = r['action']; c = float(r['confidence']); pm = r['pitch_midi']
        uncertain = c < 0.5 or a in ('HAMMER_ON', 'PULL_OFF', 'HARD_MUTE', 'NOISE')
        name = ('? ' if uncertain else '') + a + (f' {pm}' if pm not in ('', 'None') else '') + f' {c:.2f}'
        marker(t, name, COL.get(a, (0, 0, 255))); props.append([round(t, 6), name])
        if uncertain: unc.append((t, name + '  | ' + r['notes'].replace('DRAFT (machine-assisted, not listened to): ', '').replace('"', "'")))
    for t in cands: marker(t, 'CAND? missed note?', COL['CAND'])
    def track(name, items):
        L.append(f'  <TRACK {guid()}'); L.append(f'    NAME "{name}"'); L.append('    NCHAN 2')
        for it in items: L.extend(it)
        L.append('  >')
    wav = ['    <ITEM', '      POSITION 0', '      SNAPOFFS 0', f'      LENGTH {dur:.9f}', '      LOOP 0', '      SEL 0', f'      IGUID {guid()}', '      NAME "original audio"', f'      GUID {guid()}', '      <SOURCE WAVE', f'        FILE "{os.path.abspath(audio)}"', '      >', '    >']
    track('audio (original)', [wav])
    def empty(t, ln, text, color=None):
        return ['    <ITEM', f'      POSITION {t:.6f}', f'      LENGTH {ln:.6f}', '      LOOP 0', f'      IGUID {guid()}', f'      NAME "{text[:60]}"', f'      GUID {guid()}', '      <NOTES', '        |' + text, '      >', '    >']
    track('UNCERTAIN: listen', [empty(max(0, t - 0.02), 0.06, txt) for t, txt in unc])
    track('GAPS: any missed pick?', [empty(a, b - a, f'gap {a:.2f}-{b:.2f} s: sound continues with no marker; any missed pick?') for a, b in gaps])
    L.append('>')
    json.dump(props, open(out + '.proposals.json', 'w')); open(out, 'w').write('\n'.join(L) + '\n'); print(f'{out}: {len(evs)} proposed markers ({len(unc)} uncertain), {len(cands)} CAND? markers, {len(gaps)} gap items')
def imp(rpp, rec, out, accept_unchanged=False):
    rows = []; skipped = 0
    prop = json.load(open(rpp + '.proposals.json')) if os.path.exists(rpp + '.proposals.json') else []
    for ln in open(rpp):
        m = re.match(r'\s*MARKER\s+\d+\s+([\-0-9.eE]+)\s+"([^"]*)"\s+(\d+)', ln)
        if not m or m.group(3) != '0': continue
        t = float(m.group(1)); name = m.group(2).strip(); unrev = name.startswith('?'); name = name.lstrip('? ').strip(); tok = name.split()
        if not tok or tok[0].upper() not in G.ACTIONS: skipped += 1; continue
        act = tok[0].upper(); nums = [x for x in tok[1:] if re.fullmatch(r'\d+(\.\d+)?', x)]
        pitch = nums[0] if nums and float(nums[0]) > 1.0 else ''; conf = nums[-1] if nums and float(nums[-1]) <= 1.0 and (len(nums) > 1 or pitch == '') else ''
        raw = m.group(2).strip(); unchanged = any(abs(t - pt) < 0.0015 and raw == pn for pt, pn in prop)          # still exactly as proposed
        reviewed = (not unrev) and (not unchanged or accept_unchanged)
        if reviewed: c = float(conf) if conf and not unchanged else 0.9
        else: c = float(conf) if conf and unchanged else 0.4
        rows.append(dict(recording=rec, t_ms=round(t * 1000, 1), action=act, pitch_midi=pitch, string='', fret='', expected_event=G.DEFAULT_EXPECTED[act], confidence=c, timing_basis='listening' if reviewed else 'spectrogram', end_pitch_midi='', duration_ms='', notes='reviewed in Reaper' if reviewed else 'unreviewed proposal (unchanged or still marked ?)'))
    rows.sort(key=lambda r: r['t_ms'])
    with open(out, 'w', newline='') as f: w = csv.DictWriter(f, fieldnames=G.EV_COLS); w.writeheader(); w.writerows(rows)
    print(f'{len(rows)} events -> {out} ({skipped} markers ignored: CAND?/GAP/unknown); unreviewed: {sum(1 for r in rows if r["notes"].startswith("unreviewed"))}')
if __name__ == '__main__':
    if sys.argv[1] == 'make': make(*sys.argv[2:6])
    else: imp(*sys.argv[2:5], accept_unchanged='--accept-unchanged' in sys.argv)
