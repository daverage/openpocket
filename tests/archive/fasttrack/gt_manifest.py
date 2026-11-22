#!/usr/bin/env python3
"""Write tests/archive/fasttrack/ground_truth_manifest.csv: every recording set we have (or know of), what its labels actually distinguish, timing accuracy and what it can test.
Counts are computed from the local copies; 'audio_available' says whether the audio is on this machine.   python3 tests/archive/fasttrack/gt_manifest.py"""
import sys, os, csv, glob, json, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, ft_score as S
ROOT = S.ROOT; rows = []
def add(**k): rows.append(k)
# --- Guitar-TECHS (CC BY 4.0) ---
cats = [('scales', lambda n: 'scales' in n, 'scale runs, one note at a time, about 0.5 s apart'), ('singlenotes', lambda n: 'singlenotes' in n, 'every single note E2..'), ('music', lambda n: 'music' in n, 'musical pieces: chords, lines, fast groups'),
        ('techniques/Bendings', lambda n: 'Bendings' in n, 'bends'), ('techniques/Vibrato', lambda n: 'Vibrato' in n, 'vibrato'), ('techniques/PalmMute', lambda n: 'PalmMute' in n, 'palm-muted notes'),
        ('techniques/Harmonics', lambda n: 'Harmonics' in n and 'Pinch' not in n, 'natural harmonics'), ('techniques/PinchHarmonics', lambda n: 'PinchHarmonics' in n, 'pinch harmonics')]
for sub, f, desc in cats:
    names = [os.path.basename(c)[:-4] for c in sorted(glob.glob(ROOT + '/build/stage5/sets/gtechs/*.csv')) if f(os.path.basename(c)[:-4])]
    notes = 0; dur = 0.0; ioi = []
    for n in names:
        tr = S.load_truth(f'{ROOT}/build/stage5/sets/gtechs/{n}.csv')
        if not tr: continue
        notes += len(tr); dur += (tr[-1][1] - tr[0][0]) / 60000; g = S.groups(tr); ioi += list(np.diff([x['on'] for x in g]))
    ioi = np.array(ioi) if ioi else np.array([1e9])
    add(dataset='Guitar-TECHS (P1-P3)', subset=sub, audio_available='yes (build/datasets/guitar-techs, build/stage5/sets/gtechs)', recordings=len(names), minutes=round(dur, 1), signal='DI (electric)', notes_or_events=notes,
        annotation_source='hexaphonic MIDI (Fishman Triple Play), per-take lag fitted to the DI', onset_basis='NOTE onset from MIDI, not a physical pluck event', pick_vs_legato='no', repeat_pick='no', hammer_pull='no',
        slide='by take name only' if 'Bend' in sub else 'no', bend_vibrato='by take name' if sub.startswith('techniques/B') or 'Vibrato' in sub else 'no', palm_mute='by take name' if 'PalmMute' in sub else 'no', hard_mute='no', noise='no (unannotated acoustic events are common: precursors, string noise)',
        timing_accuracy='per-take median offset to the audio flux -7..+22 ms; low notes later (+20 ms) than high (+5 ms); p10-p90 within a take about 17 ms', annotation_quality='good for note pitch; incomplete for techniques (palm-mute takes annotate only some chugs)',
        n_ioi_40_60=int(np.sum((ioi >= 40) & (ioi < 60))), n_ioi_60_80=int(np.sum((ioi >= 60) & (ioi < 80))), n_ioi_80_125=int(np.sum((ioi >= 80) & (ioi < 125))),
        suitable_for='pitch accuracy, note-onset recall on clean lines (scales, singlenotes), latency comparisons between variants' if sub in ('scales', 'singlenotes') else ('fast groups (music: chord rolls mixed in) with ambiguity flagged' if sub == 'music' else 'pitch following in bends/vibrato at take level; no per-event technique labels'),
        not_suitable_for='pick-vs-legato, hard mute, noise rejection, per-event technique scoring', licence='CC BY 4.0', split_note='P1-P3 are development data (used throughout)')
# --- IDMT-SMT-Guitar V2 (CC BY-NC-ND) ---
inv = json.load(open(ROOT + '/build/fasttrack/idmt/inventory.json'))
import idmt_inventory as I
def ioi_counts(rs):
    ioi = []
    for r in rs:
        if r['set'] == 'dataset4':
            cp = r['wav'].replace('/audio/', '/annotation/onsets/').rsplit('.', 1)[0] + '.csv'; on = [float(l.split(',')[0]) for l in open(cp) if l.strip() and l[0].isdigit()]
        else: on = [e['on'] for e in I.read_xml(r['wav'].replace('/audio/', '/annotation/').rsplit('.', 1)[0] + '.xml')['events']]
        ioi += list(np.diff(I.groups(on)) * 1000)
    ioi = np.array(ioi) if ioi else np.array([1e9]); return int(np.sum((ioi >= 40) & (ioi < 60))), int(np.sum((ioi >= 60) & (ioi < 80))), int(np.sum((ioi >= 80) & (ioi < 125)))
def agg(rs): return len(rs), round(sum(r['dur'] for r in rs) / 60, 1), sum(r['onsets'] for r in rs)
for sub, rs, desc in (('dataset2 licks (3 guitars, technique variants)', [r for r in inv if r['kind'] == 'lick'], 'licks'), ('dataset2 init files (fret scans, muted5, prototypes)', [r for r in inv if r['kind'] == 'init' and r['set'] == 'dataset2'], 'init'), ('dataset3 pieces', [r for r in inv if r['kind'] == 'piece'], 'pieces')):
    n, mn, ons = agg(rs); add(dataset='IDMT-SMT-Guitar V2', subset=sub, audio_available='yes (build/datasets/idmt-guitar; symlinks build/stage5/sets/idmt_*)', recordings=n, minutes=mn, signal='DI line-in (electric), 24-bit mono' if 'dataset2' in sub else 'DI line-in 16-bit', notes_or_events=ons,
        annotation_source='hand annotation (XML): onset, offset, pitch, string, fret, excitation PK/FS/MU, expression NO/BE/SL/VI/HA/DN/ST/FL/TR', onset_basis='note onset, hand placed',
        pick_vs_legato='no hammer-on / pull-off class exists', repeat_pick='implicit (same pitch twice)', hammer_pull='no', slide='yes (SL, note level)', bend_vibrato='yes (BE, VI, note level)', palm_mute='yes (MU excitation)', hard_mute='no', noise='dead notes (DN) only',
        timing_accuracy='-22..+32 ms against the audio energy rise (median +20 ms, 999 notes); a few recordings have >25% of onsets with no energy rise nearby (e.g. AR_Lick4_*)', annotation_quality='good labels, loose timing; one exact duplicate annotation found by the validator',
        n_ioi_40_60=ioi_counts(rs)[0], n_ioi_60_80=ioi_counts(rs)[1], n_ioi_80_125=ioi_counts(rs)[2],
        suitable_for='technique-stratified recall at <=5 notes/s (palm mute, slides, bends, vibrato, dead notes), string changes, polyphony vs mono, a second player and guitars', not_suitable_for='fast runs (max 5.1 notes/s), pick-vs-legato, hard mute', licence='CC BY-NC-ND 4.0: local use, no derivatives, never commit audio', split_note='development data')
d4 = [r for r in inv if r['set'] == 'dataset4' and r['path'] == 'direct']
for tempo in ('fast', 'slow'):
    rs = [r for r in d4 if r['kind'] == 'd4_' + tempo]; n, mn, ons = agg(rs)
    add(dataset='IDMT-SMT-Guitar V2', subset=f'dataset4 {tempo} tempo (Career SG, Ibanez RG2820)', audio_available='yes', recordings=n, minutes=mn, signal='DI line-in', notes_or_events=ons, annotation_source='onset times (CSV/SV), chords per beat, mono/poly texture; NO pitch per note',
        onset_basis='hand annotated onset', pick_vs_legato='no', repeat_pick='no', hammer_pull='no', slide='no', bend_vibrato='no', palm_mute='metal pieces are palm-muted but not labelled as such', hard_mute='the recordings begin with a control signal of dead notes (not individually labelled)', noise='dead-note control signal present, unlabelled',
        timing_accuracy='unknown (not measured); some onset files are missing', annotation_quality='onsets only; 10-12.5 notes/s windows exist in metal_8_125BPM, metal_4_180BPM (see FASTTRACK-BENCHMARKS.md)', n_ioi_40_60=ioi_counts(rs)[0], n_ioi_60_80=ioi_counts(rs)[1], n_ioi_80_125=ioi_counts(rs)[2],
        suitable_for='onset-only recall and false-onset rates on full pieces; candidate fast onsets (no pitch)', not_suitable_for='pitch, technique, legato, hard mute', licence='CC BY-NC-ND 4.0', split_note='development data')
add(dataset='IDMT-SMT-Guitar V2', subset='dataset4 acoustic_mic / acoustic_pickup', audio_available='yes', recordings=256, minutes=0, signal='MICROPHONE (Rode NT1-A) and acoustic pickup: NOT DI', notes_or_events=0, annotation_source='none (no onset files for these)', onset_basis='n/a', pick_vs_legato='no', repeat_pick='no', hammer_pull='no', slide='no', bend_vibrato='no', palm_mute='no', hard_mute='no', noise='no', timing_accuracy='n/a', annotation_quality='unannotated', n_ioi_40_60=0, n_ioi_60_80=0, n_ioi_80_125=0, suitable_for='nothing yet', not_suitable_for='everything electric/DI', licence='CC BY-NC-ND 4.0', split_note='excluded')
# --- AG-PT-set (CC BY 4.0), labels read remotely; audio (6.7 GB) not downloaded ---
names = {int(r['expressive_technique_id']): r['name'] for r in csv.DictReader(open(ROOT + '/build/datasets/agpt/expressive_techniques.csv'))}
lab = collections.Counter(names[int(x['expressive_technique_id'])] for x in csv.DictReader(open(ROOT + '/build/datasets/agpt/note_labels.csv')))
add(dataset='AG-PT-set (Stefani & Turchet, Audio Mostly 2024)', subset='labelled techniques: ' + ', '.join(f'{k} {v}' for k, v in lab.most_common()), audio_available='NO (6.7 GB, acoustic; only README, metadata and label files read: build/datasets/agpt)', recordings=809, minutes=604, signal='acoustic guitar, built-in pickups (NOT electric DI), 48 kHz 24-bit',
    notes_or_events=sum(lab.values()), annotation_source='onset labels at millisecond level (Audacity labels + note_labels.csv), technique id, pitch for pitched', onset_basis='physical onset (pluck or percussive hit), labelled per event', pick_vs_legato='no (hammer-on is NOT among the labelled techniques)', repeat_pick='yes (repeated notes)', hammer_pull='UNLABELLED (technique 10 recorded, no onsets)',
    slide='no', bend_vibrato='UNLABELLED (bending, vibrato recorded, no onsets)', palm_mute='yes (7588 labelled)', hard_mute='no', noise='percussive body hits and muted-string hits are labelled (kick, snare, tom: 8028 events): useful as non-note events',
    timing_accuracy='millisecond-level by construction (the paper is about temporally precise onset labels)', annotation_quality='best onset timing of all sets here; acoustic domain', n_ioi_40_60=0, n_ioi_60_80=0, n_ioi_80_125=0,
    suitable_for='onset-detector timing and false-onset behaviour on pluck vs percussive vs palm-mute events (acoustic: supplementary), 3 dynamic levels', not_suitable_for='electric DI pickup behaviour, hammer-on/bend/vibrato labels, fast runs', licence='CC BY 4.0', split_note='not downloaded; decide before use')
# --- the owner's own recordings (My Audio) ---
my = [('bleep3 di.wav', 'palm mutes, chugs, tremolo, hard mutes, fast notes'), ('fast play di.wav', 'fast playing'), ('bleep.wav', 'palm mutes, chugs, tremolo, hard mutes (21 s)'), ('low e open di.wav', 'low E open'), ('scale + duo + power chords di.wav', 'scale, duo, power chords')]
for f, desc in my:
    add(dataset='Owner recordings (My Audio, not committed)', subset=f + ': ' + desc, audio_available='yes (local, git-ignored)', recordings=1, minutes='', signal='DI (24-bit stereo, left channel used)', notes_or_events=0, annotation_source='NONE (offline pYIN/librosa pseudo-reference only, not ground truth)', onset_basis='n/a',
        pick_vs_legato='no', repeat_pick='no', hammer_pull='no', slide='no', bend_vibrato='no', palm_mute='audible, unlabelled', hard_mute='audible, unlabelled', noise='audible, unlabelled', timing_accuracy='n/a', annotation_quality='unlabelled', n_ioi_40_60=0, n_ioi_60_80=0, n_ioi_80_125=0,
        suitable_for='LABEL THESE FIRST: they are the user\'s own playing and sound; annotating bleep3 di and fast play di with the v1 format gives the most relevant labelled fast data at no recording cost', not_suitable_for='scoring until labelled', licence='owner', split_note='proposed: label as dev; record fresh holdout separately')
cols = ['dataset', 'subset', 'audio_available', 'recordings', 'minutes', 'signal', 'notes_or_events', 'annotation_source', 'onset_basis', 'pick_vs_legato', 'repeat_pick', 'hammer_pull', 'slide', 'bend_vibrato', 'palm_mute', 'hard_mute', 'noise', 'timing_accuracy',
        'annotation_quality', 'n_ioi_40_60', 'n_ioi_60_80', 'n_ioi_80_125', 'suitable_for', 'not_suitable_for', 'licence', 'split_note']
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ground_truth_manifest.csv'), 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=cols); w.writeheader(); w.writerows(rows)
print(len(rows), 'manifest rows')
