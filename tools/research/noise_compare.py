#!/usr/bin/env python3
"""Reproducible DI + independently generated noise comparison, no extra packages.
Uses HEAD's Smart Gate as the baseline; accepts an explicit commit as argument.
Artifacts are written to build/noise-comparison/. Does not modify presets or hardware.
"""
from pathlib import Path
import array, json, math, random, subprocess, sys, tempfile, wave
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/noise-comparison'
OUT.mkdir(parents=True, exist_ok=True)
revision = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
run = lambda args: subprocess.check_output([str(a) for a in args], cwd=ROOT, text=True)
with tempfile.TemporaryDirectory(prefix='openpocket-noise-') as td:
    old = Path(td); (old/'engine').mkdir()
    for f in run(['git','ls-tree','-r','--name-only',revision,'engine']).splitlines():
        if f.endswith(('.h','.c')):
            (old/f).write_bytes(subprocess.check_output(['git','show',f'{revision}:{f}'],cwd=ROOT))
    (old/'third_party').symlink_to(ROOT/'third_party',target_is_directory=True)
    for name, eng, flag in [('before',old/'engine',['-DNOISE_BASELINE']),('after',ROOT/'engine',[])]:
        run(['cc','-O2','-w',*flag,'-I'+str(eng),ROOT/'tools/research/noise_compare.c',eng/'px_dsp.c',ROOT/'firmware/px_math.c','-lm','-o',OUT/name])
    with wave.open(str(ROOT/'docs/nam_di.wav')) as wav:
        assert wav.getnchannels()==1 and wav.getsampwidth()==2 and wav.getframerate()==44100
        pcm=array.array('h',wav.readframes(wav.getnframes()))
    rate=44100; rng=random.Random(721)
    clean=[0.0]*rate+[v/32768*0.15 for v in pcm]+[0.0]*(rate*6)
    noisy=[v + 0.00015*rng.uniform(-1,1) + 0.0002*math.sin(2*math.pi*50*i/rate) for i,v in enumerate(clean)]
    for name,data in [('clean',clean),('noisy',noisy)]:
        (OUT/(name+'.f32')).write_bytes(array.array('f',data).tobytes())
    def render(exe, input_name, label, mode, threshold):
        timing=run([OUT/exe,OUT/(input_name+'.f32'),OUT/(label+'.f32'),mode,threshold]).strip()
        a=array.array('f');a.frombytes((OUT/(label+'.f32')).read_bytes());return a,timing
    dry,_=render('after','clean','reference',-1,0)
    rows=[]
    for exe,label,model,thr in [('before','Noise Gate',0,35),('before','Previous Smart Gate',1,8),('after','Noise Suppressor',1,40)]:
        y,timing=render(exe,'noisy',label.replace(' ','_'),model,thr)
        n=rate*2
        rms=lambda a: math.sqrt(sum(v*v for v in a)/len(a))
        idle=20*math.log10(max(1e-15,rms(y[-n:]))/rms(noisy[-n:]))
        a,b=rate,rate+len(pcm)
        projection=sum(y[i]*dry[i] for i in range(a,b))/sum(dry[i]**2 for i in range(a,b))
        # 10 ms windows count wanted energy loss; do not label windows as individual notes.
        lost=active=0; lowest=1.0
        for at in range(a,b-441,441):
            power=sum(dry[i]**2 for i in range(at,at+441))
            if power/441 < 1e-6: continue
            g=sum(dry[i]*y[i] for i in range(at,at+441))/power
            active+=1;lost+=g<0.5;lowest=min(lowest,g)
        transition_end=rate+len(pcm)+rate*3
        transition=20*math.log10(max(1e-15,rms(y[transition_end-n:transition_end]))/rms(noisy[transition_end-n:transition_end]))
        rows.append(dict(model=label,post_note_1_to_3s_db=round(transition,2),idle_db=round(idle,2),wanted_projection_db=round(20*math.log10(max(projection,1e-12)),3),windows_below_half=lost,active_windows=active,host=timing))
    report={'baseline':run(['git','rev-parse',revision]).strip(),'input':'docs/nam_di.wav at 0.15; 1 s idle + 6 s DI + 6 s idle; seeded hiss and 50 Hz hum','thresholds':'Normal 35 / Smart 8 / Suppressor 40: approximately -59 dBFS; release approximately 253 ms; HUM/HUSH off','results':rows}
    (OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
