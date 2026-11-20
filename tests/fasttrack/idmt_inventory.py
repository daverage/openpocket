#!/usr/bin/env python3
"""IDMT-SMT-Guitar V2 (Fraunhofer IDMT, Zenodo 7544110, CC BY-NC-ND 4.0) -> FastTrack inventory, fast-run census and ranked manifest.
Licence: no derivatives.  The audio is NEVER modified or copied: build/stage5/sets/idmt_*/ hold symlinks to the original WAVs; only note CSVs (our own
extraction of the dataset's XML annotations) are written.  Attribution: Kehling, Abesser, Dittmar, Schuller, "Automatic tablature transcription of electric
guitar recordings by estimation of score- and instrument-related parameters", DAFx 2014; dataset DOI 10.5281/zenodo.7544110.  Nothing here is committed
except this script and the text report it prints.

  python3 tests/fasttrack/idmt_inventory.py            -> build/fasttrack/idmt/{inventory.json,manifest.csv} + stdout report
Signal path: datasets 1-4 are guitars plugged into the interface Line-In (direct); dataset 4 'acoustic_mic' is a condenser microphone: labelled mic, excluded from DI sets.
Datasets 1-3 have per-note pitch/onset/offset/string/fret/excitation/expression; dataset 4 has onsets, chords and mono/poly texture only (no pitch)."""
import os, glob, json, csv, sys
import xml.etree.ElementTree as ET
import numpy as np
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
D = ROOT + '/build/datasets/idmt-guitar/x/IDMT-SMT-GUITAR_V2'
OUT = ROOT + '/build/fasttrack/idmt'; os.makedirs(OUT, exist_ok=True)
RATES = (8.0, 12.5, 20.0)

def read_xml(p):
    r = ET.parse(p).getroot(); g = r.find('globalParameter'); ev = []
    for e in r.iter('event'):
        f = lambda k, d=None: (e.find(k).text.strip() if e.find(k) is not None and e.find(k).text else d)
        ev.append(dict(on=float(f('onsetSec')), off=float(f('offsetSec')), midi=int(float(f('pitch'))), string=int(f('stringNumber', 0) or 0), fret=int(f('fretNumber', -1) or -1),
                       exc=f('excitationStyle', ''), exp=f('expressionStyle', ''), mod=float(f('modulationFrequencyRange', 0) or 0)))
    return dict(model=(g.find('instrumentModel').text if g.find('instrumentModel') is not None else ''), events=sorted(ev, key=lambda x: x['on']))

def groups(on, tol=0.04):
    """simultaneous attacks (chord strums, onsets within 40 ms) collapse to one event; returns group onset times"""
    g = []
    for t in sorted(on):
        if not g or t - g[-1] >= tol: g.append(t)
    return np.array(g)

def runs(gon, rate, min_notes=5):
    """continuous runs: >= min_notes consecutive group onsets whose every inter-onset interval <= 1/rate. returns [(start,end,n)]"""
    out, i, n = [], 0, len(gon)
    while i < n - 1:
        if gon[i + 1] - gon[i] <= 1.0 / rate:
            j = i
            while j < n - 1 and gon[j + 1] - gon[j] <= 1.0 / rate: j += 1
            if j - i + 1 >= min_notes: out.append((float(gon[i]), float(gon[j]), j - i + 1))
            i = j + 1
        else: i += 1
    return out

def stats(on):
    gon = groups(on); ioi = np.diff(gon)
    d = dict(onsets=len(on), groups=len(gon), poly_groups=int(len(on) - len(gon)))
    if len(ioi):
        d.update(ioi_med=float(np.median(ioi)), ioi_p10=float(np.percentile(ioi, 10)), ioi_min=float(ioi.min()))
        for R in RATES:
            rr = runs(gon, R); d[f'runs_{R}'] = len(rr); d[f'notes_in_runs_{R}'] = int(sum(r[2] for r in rr)); d[f'longest_{R}'] = max([r[2] for r in rr], default=0)
        d['max_rate_5'] = float(max((4.0 / (gon[i + 4] - gon[i]) for i in range(len(gon) - 4)), default=0))   # fastest 5-note window, notes/s
    return d

def main():
    rows = []
    # datasets 1-3: pitched annotations
    for ds in ('dataset1', 'dataset2', 'dataset3'):
        for xp in sorted(glob.glob(f'{D}/{ds}/**/*.xml', recursive=True)):
            a = read_xml(xp); name = os.path.basename(xp)[:-4]
            wav = xp.replace('/annotation/', '/audio/')[:-4] + '.wav'
            if not os.path.exists(wav): continue
            ev = a['events']
            if not ev: continue
            kind = ('lick' if '_Lick' in name else 'init') if ds == 'dataset2' else ('piece' if ds == 'dataset3' else 'single')
            if ds == 'dataset2' and name.split('_')[0] not in ('AR', 'FS', 'LP'): kind = 'init'
            on = [e['on'] for e in ev]; s = stats(on)
            s.update(set=ds, name=name, kind=kind, wav=wav, guitar=a['model'] or name.split('_')[0], path='direct', pitched=True,
                     techniques=sorted({e['exc'] for e in ev if e['exc']} | {e['exp'] for e in ev if e['exp'] and e['exp'] != 'NO'}),
                     n_bend=sum(e['exp'] == 'BE' for e in ev), n_slide=sum(e['exp'] == 'SL' for e in ev), n_vib=sum(e['exp'] == 'VI' for e in ev), n_dead=sum(e['exp'] == 'DN' for e in ev),
                     n_muted=sum(e['exc'] == 'MU' for e in ev), n_harm=sum(e['exp'] == 'HA' for e in ev),
                     strings_changes=int(sum(1 for i in range(1, len(ev)) if ev[i]['string'] != ev[i - 1]['string'] and ev[i]['on'] - ev[i - 1]['on'] >= 0.04)),
                     repeats=int(sum(1 for i in range(1, len(ev)) if ev[i]['midi'] == ev[i - 1]['midi'] and ev[i]['on'] - ev[i - 1]['on'] >= 0.04)), dur=float(max(e['off'] for e in ev)))
            rows.append(s)
            if ds in ('dataset2', 'dataset3') and kind in ('lick', 'piece'):   # scorer inputs: symlink to the ORIGINAL wav + our note CSV
                sd = f"{ROOT}/build/stage5/sets/idmt_{kind}s"; os.makedirs(sd, exist_ok=True)
                tag = ('idmt_' + name)
                link = f'{sd}/{tag}.wav'
                if os.path.lexists(link): os.remove(link)
                os.symlink(wav, link)
                with open(f'{sd}/{tag}.csv', 'w') as f:
                    for e in ev: f.write(f"{e['on']:.4f},{max(e['off'], e['on'] + 0.02):.4f},{e['midi']}\n")
    # dataset 4: onsets only
    for cp in sorted(glob.glob(f'{D}/dataset4/*/*/*/annotation/onsets/*.csv')):
        parts = cp.split('/'); gtr, tempo, genre = parts[-6], parts[-5], parts[-4]; name = os.path.basename(cp)[:-4]
        on = [float(l.split(',')[0]) for l in open(cp) if l.strip() and l[0].isdigit()]
        wav = cp.replace('/annotation/onsets/', '/audio/')[:-4] + '.wav'
        if len(on) < 2 or not os.path.exists(wav): continue
        tx = cp.replace('/onsets/', '/texture/')[:-4] + '.txt'
        s = stats(on); s.update(set='dataset4', name=f'{gtr}/{tempo}/{name}', kind='d4_' + tempo, wav=wav, guitar=gtr, path='mic' if gtr == 'acoustic_mic' else ('pickup' if gtr == 'acoustic_pickup' else 'direct'), pitched=False,
                               genre=genre, dur=on[-1])
        rows.append(s)
    json.dump(rows, open(OUT + '/inventory.json', 'w'), indent=1, default=str)
    return rows

if __name__ == '__main__':
    rows = main()
    def tot(rs, k): return sum(r.get(k, 0) for r in rs)
    print(f'{len(rows)} annotated recordings')
    for ds in ('dataset1', 'dataset2', 'dataset3', 'dataset4'):
        rs = [r for r in rows if r['set'] == ds]
        print(f"{ds}: {len(rs)} recordings, {tot(rs,'onsets')} onsets, {sum(r['dur'] for r in rs)/60:.0f} min, paths={sorted({r['path'] for r in rs})}, kinds={sorted({r['kind'] for r in rs})}")
    print('\nFast-run census (runs = >=5 consecutive groups, every inter-onset interval under 1/rate; chord attacks collapsed):')
    for label, rs in (('licks (d2, direct)', [r for r in rows if r['kind'] == 'lick']), ('pieces (d3)', [r for r in rows if r['kind'] == 'piece']), ('d2 init files', [r for r in rows if r['kind'] == 'init']),
                      ('d4 direct, fast tempo', [r for r in rows if r['kind'] == 'd4_fast' and r['path'] == 'direct']), ('d4 direct, slow tempo', [r for r in rows if r['kind'] == 'd4_slow' and r['path'] == 'direct'])):
        print(f"  {label:24s} files={len(rs):3d} groups={tot(rs,'groups'):6d} |" + ''.join(f" >={R}/s: runs={tot(rs, f'runs_{R}'):3d} notes={tot(rs, f'notes_in_runs_{R}'):4d} longest={max([r.get(f'longest_{R}',0) for r in rs], default=0):2d} |" for R in RATES) + f" max5={max([r.get('max_rate_5',0) for r in rs], default=0):.1f}/s")
