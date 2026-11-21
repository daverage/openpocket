#!/usr/bin/env python3
"""IDMT-SMT-Guitar V2 (datasets 2,3: licks, pieces) -> FastTrack events.csv / recordings.csv (v1 format).  The original WAVs are referenced by absolute path, never copied or changed (CC BY-NC-ND 4.0).
Mapping (what the dataset's own labels support; nothing is inferred from a pitch change):
  excitation PK/FS -> PICK (confidence 0.6: the dataset's onsets are hand annotated; measured offset to the audio energy rise -22..+32 ms)
  excitation MU    -> PALM_MUTE_PICK
  expression DN    -> NOISE (dead note: a picked, muted string; no pitched note intended)  confidence 0.5
  expression SL    -> SLIDE (end pitch unknown in the labels: modulationFrequencyRange cents used when given)   BE -> BEND   VI -> VIBRATO (the note onset itself is also a PICK at the same time)
There is NO hammer-on, pull-off or hard-mute class in IDMT: those cannot be evaluated from it.  Later notes of a chord strum (within 40 ms) are kept as separate PICKs."""
import sys, os, csv, glob, json
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gt_format as G, idmt_inventory as I
OUT = os.path.join(I.ROOT, 'build', 'fasttrack', 'gt', 'idmt_licks'); os.makedirs(OUT, exist_ok=True)
recs, evs = [], []
for r in json.load(open(I.OUT + '/inventory.json')):
    if r['kind'] not in ('lick', 'piece'): continue
    name = 'idmt_' + r['name']; xml = r['wav'].replace('/audio/', '/annotation/').rsplit('.', 1)[0] + '.xml'
    recs.append(dict(recording=name, path=r['wav'], sample_rate=44100, bits=24 if r['set'] == 'dataset2' else 16, channels=1, signal='DI', guitar=r['guitar'], pickup='', player='', exercise_id=r['name'],
                     intended_tempo_bpm='', split='dev', processing='none', complete_annotation=1, licence='CC BY-NC-ND 4.0 (Fraunhofer IDMT)', notes='note onsets hand annotated; no hammer-on/pull-off/hard-mute labels'))
    for e in I.read_xml(xml)['events']:
        t = e['on'] * 1000; base = dict(recording=name, string=e['string'] or '', fret=e['fret'] if e['fret'] >= 0 else '', timing_basis='source_annotation', duration_ms=round((e['off'] - e['on']) * 1000, 1))
        if e['exp'] == 'DN': act, conf, pm, nt = 'NOISE', 0.5, '', 'dead note (muted string picked)'
        elif e['exc'] == 'MU': act, conf, pm, nt = 'PALM_MUTE_PICK', 0.6, e['midi'], ''
        else: act, conf, pm, nt = 'PICK', 0.6, e['midi'], ''
        evs.append(dict(base, t_ms=round(t, 1), action=act, pitch_midi=pm, expected_event=G.DEFAULT_EXPECTED[act], confidence=conf, end_pitch_midi='', notes=nt))
        if e['exp'] in ('SL', 'BE', 'VI'):
            a2 = {'SL': 'SLIDE', 'BE': 'BEND', 'VI': 'VIBRATO'}[e['exp']]
            endp = e['midi'] + e['mod'] / 100.0 if e['exp'] in ('SL', 'BE') and e['mod'] else ''
            evs.append(dict(base, t_ms=round(t + 1, 1), action=a2, pitch_midi=e['midi'], expected_event=G.DEFAULT_EXPECTED[a2], confidence=0.4, end_pitch_midi=endp, notes='expression label of the same note; extent from modulationFrequencyRange'))
seen, dedup, dup = set(), [], 0
for e in sorted(evs, key=lambda e: (e['recording'], float(e['t_ms']))):
    k = (e['recording'], e['t_ms'], e['action'], str(e['pitch_midi']))
    if k in seen: dup += 1; continue
    seen.add(k); dedup.append(e)
evs = dedup
if dup: print(dup, 'exact duplicate annotation(s) in the source XML dropped (the validator flagged them)')
with open(OUT + '/recordings.csv', 'w', newline='') as f: w = csv.DictWriter(f, fieldnames=G.REC_COLS); w.writeheader(); w.writerows(recs)
with open(OUT + '/events.csv', 'w', newline='') as f: w = csv.DictWriter(f, fieldnames=G.EV_COLS); w.writeheader(); w.writerows(evs)
print(len(recs), 'recordings,', len(evs), 'events ->', OUT)
