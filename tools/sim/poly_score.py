#!/usr/bin/env python3
"""Score the two-voice poly synth allocator (engine/px_pulse_poly.h) through tools/sim/pulse_poly_bench.c.

  cc -O2 -Iengine -Ifirmware tools/sim/pulse_poly_bench.c firmware/px_math.c -lm -o build/pulse_poly_bench
  PYTHONPATH=tools/sim python3 tools/sim/poly_score.py build/pulse_poly_bench --guitar   # GuitarSet takes 24..47
  PYTHONPATH=tools/sim python3 tools/sim/poly_score.py build/pulse_poly_bench            # synthetic + bass DI fixtures

GuitarSet lives in build/guitarset (mono/*_comp_mix.wav, annotation/*.jams). Bass DI notes: --bass-di DIR.
A voice counts as sounding when its gain is above 0.065 (docs/POLY-SYNTH-DESIGN.md).
"""
import argparse
import csv
from pathlib import Path
import subprocess
import tempfile
import glob
import numpy as np
from track_eval import jams_notes, dyad_score, poly_score, dyad_entry_delays, report_detector
from lowest_octave_eval import RATE, write_wav
from resonator_track_eval import read_di
from bass_di_eval import load_note, fixture
parser=argparse.ArgumentParser()
parser.add_argument('exe')
parser.add_argument('--guitar',action='store_true')
parser.add_argument('--offset',type=int,default=24,help='first GuitarSet comping take')
parser.add_argument('--takes',type=int,default=24,help='number of GuitarSet comping takes')
parser.add_argument('--transpose',type=int,default=0)
parser.add_argument('--out',default='build/poly-score')
parser.add_argument('--bass-di',default='/tmp/op-bass-di/native')
parser.add_argument('--type',default='pulse')
args=parser.parse_args()
root=Path(args.out); root.mkdir(parents=True,exist_ok=True)
def render(x):
    with tempfile.TemporaryDirectory() as temp:
        trace=Path(temp)/'trace.csv'
        y=subprocess.run([args.exe,str(trace),args.type],input=np.asarray(x,dtype='<f4').tobytes(),capture_output=True,check=True).stdout
        y=np.frombuffer(y,dtype='<f4')
        assert y.shape==x.shape and np.isfinite(y).all()
        with trace.open() as f:
            rows=[{k:float(v) for k,v in line.items()} for line in csv.DictReader(f)]
    return y,rows
def reference(rows):
    out=[]
    for r in rows:
        p=[r[f'v{k}'] for k in (1,2) if r[f'gain{k}']>.065 and r[f'v{k}']>0]
        p.sort()
        out.append(dict(t=r['t'],nn=len(p),**{f'n{i+1}':x for i,x in enumerate(p)}))
    return out
if args.guitar:
    di=Path("build/guitarset")
    if args.offset < 0 or args.takes < 1:
        parser.error('expected a non-negative offset and a positive take count')
    files=sorted((di/'mono').glob("*_comp_mix.wav"))[args.offset:args.offset + args.takes]
    if not files:
        parser.error('no GuitarSet comping takes at the requested offset')
    raw=[]; voices=[]; raw_p=[]; voice_p=[]; entries_raw=[]; entries_voice=[]
    for path in files:
        x=read_di(path)
        notes=jams_notes(di/'annotation'/(path.stem.removesuffix('_mix')+'.jams'))
        if args.transpose:
            factor=2**(args.transpose/12)
            x=np.interp(np.arange(0,len(x)-1,factor),np.arange(len(x)),x)
            notes=[(a/factor,b/factor,m+args.transpose) for a,b,m in notes]
        _,rows=render(x)
        active=reference(rows)
        raw.append(dyad_score(rows,notes)); voices.append(dyad_score(active,notes))
        raw_p.append(poly_score(rows,notes)); voice_p.append(poly_score(active,notes))
        entries_raw+=dyad_entry_delays(rows,notes); entries_voice+=dyad_entry_delays(active,notes)
    report_detector(f'Raw resonator, guitar {args.transpose:+d} semitones',raw,raw_p,entries_raw)
    report_detector(f'Pulse voices, guitar {args.transpose:+d} semitones',voices,voice_p,entries_voice)
else:
    t=np.arange(int(1.8*RATE))/RATE
    def pluck(f, start, stop, level, shape=(1,.27,.16)):
        env=np.clip(np.minimum((t-start)/.003,(stop-t)/.005),0,1)*np.exp(-2*np.maximum(t-start,0))
        return level*env*sum(p*np.sin(2*np.pi*f*k*(t-start)) for k,p in enumerate(shape,1))
    def bend():
        hz=110*2**(np.interp(t,[0,.3,.7,1.0,1.35,1.8],[0,0,200,200,0,0])/1200)
        env=np.clip(np.minimum((t-.1)/.003,(1.5-t)/.005),0,1)
        phi=2*np.pi*np.cumsum(hz)/RATE
        return hz, .16*env*(np.sin(phi)+.3*np.sin(2*phi)) + pluck(196,.45,1.35,.11)
    def hz(m): return 440*2**((m-69)/12)
    def synthetic(low,high):
        return pluck(hz(low),.1,.75,.15)+pluck(hz(high),.34,1.51,.13)+pluck(hz(low),.90,1.29,.11)
    def active(rows, m):
        f=hz(m)
        return np.array([any(r[f'gain{k}']>.065 and f*.97153<r[f'v{k}']<f*1.0293 for k in [1,2]) for r in rows])
    def values(name,x,lo,hi):
        y,rows=render(x)
        times=np.array([r['t'] for r in rows])
        a,b=active(rows,lo),active(rows,hi)
        first=lambda on, at, until: next((round((ti-at)*1000,1) for ti,h in zip(times,on) if at<=ti<until and h),None)
        tail=lambda at,end: next((round((ti-at)*1000,1) for ti,h in zip(times[::-1],a[::-1]) if at<=ti<end and h),0)
        peak=lambda at,end: 100*np.mean((a&b)[(times>=at)&(times<end)])
        print(name,'onsets',first(a,.1,.34),first(b,.34,.75),'dyad',round(peak(.42,.75),1),
              'repick',first(a,.9,1.29),round(peak(.98,1.29),1),'low mute',tail(.75,.9),flush=True)
        return y
    cases=[("fourth",45,50),("fifth",45,52),("octave",40,52),("semitone",40,41),("bass-fifth",23,30)]
    for tag,lo,hi in cases:
        x=synthetic(lo,hi)
        values(tag,x,lo,hi)
    # Try unequal picks and both sides of the 100 ms eviction window.
    # Slight detuning gives the upper string an independent phase trajectory.
    for upper_level in [.075, .13, .24]:
        for offset_ms in [0, 20, -20, 80, -80, 120, -120]:
            offset = offset_ms / 1000
            lo_start = .12 + max(0, -offset)
            hi_start = .12 + max(0, offset)
            x = (pluck(hz(40), lo_start, .8, .15)
                 + pluck(hz(52) * 1.005, hi_start, .8, upper_level, (1, .37, .22, .09)))
            _, rows = render(x)
            times = np.array([row['t'] for row in rows])
            together = max(lo_start, hi_start)
            overlap = (times >= together) & (times < .8)
            both = active(rows, 40) & active(rows, 52) & overlap
            entry = round((times[both][0] - together) * 1000, 1) if both.any() else None
            print(f'octave upper level {upper_level:g}, offset {offset_ms:+d} ms: '
                  f'both {100 * both.sum() / overlap.sum():.1f}%, entry {entry} ms', flush=True)
    for inst, dyn in ([('babyblue','f'),('darkblack','mf')] if Path(args.bass_di).is_dir() else []):
        for m,files in [(23, ('b1','e2')),(28,('e2','a2'))]:
            samples=[load_note(f"{args.bass_di}/{inst}_{p}_{dyn}_rr4.wav") for p in files]
            x=fixture(*samples,.16)
            y=values(f"{inst}-{m}",x,m,m+5)
            write_wav(root/f'{inst}-{m}-pulse.wav',y)
    expected, x=bend()
    y,rows=render(x)
    times=np.array([r['t'] for r in rows])
    wanted=np.interp(times,t,expected)
    region=(times>=.3)&(times<1.35)
    pitch=np.array([[r[f'v{k}'] if r[f'gain{k}']>.065 else 1 for k in [1,2]] for r in rows])
    error=abs(1200*np.log2(pitch/wanted[:,None])).min(axis=1)
    print("bend",round(100*np.mean(error[region]<50),1),"within 50 cents; median/p95",np.round(np.percentile(error[region],[50,95]),1))
    write_wav(root/'bend-pulse.wav',y)
    # A phase-continuous single string changes pitch without a fresh pick.
    # Keep the same envelope across the hammer-on and pull-off.
    legato_hz = np.select([t < .48, t < .85], [hz(40), hz(43)], default=hz(40))
    phase = 2 * np.pi * np.cumsum(legato_hz) / RATE
    envelope = .20 * np.clip(np.minimum((t - .12) / .003, (1.26 - t) / .006), 0, 1)
    envelope *= np.exp(-1.1 * np.maximum(t - .12, 0))
    legato = envelope * (np.sin(phase) + .35 * np.sin(2 * phase) + .20 * np.sin(3 * phase))
    y, rows = render(legato)
    times = np.array([row['t'] for row in rows])
    both = np.array([row['gain1'] > .065 and row['gain2'] > .065 for row in rows])
    for label, start, stop, midi in [('hammer-on', .48, .85, 43), ('pull-off', .85, 1.26, 40)]:
        region = (times >= start) & (times < stop)
        hits = active(rows, midi) & region
        delay = round(1000 * (times[hits][0] - start), 1) if hits.any() else None
        print(f'legato {label}: entry {delay} ms, correct {100 * hits[region].mean():.1f}%, '
              f'two voices {100 * both[region].mean():.1f}%', flush=True)
    write_wav(root/'legato-pulse.wav',y)
    demo=read_di('assets/loops/arpeggio.wav')*.22
    y,_=render(demo)
    write_wav(root/'guitar-pulse.wav',y)
