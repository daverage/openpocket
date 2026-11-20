import sys,subprocess,math,bisect,os
S=os.environ['S']; D=os.environ.get('GT','build/datasets/guitar-techs')
exec(open(os.path.dirname(os.path.abspath(__file__))+'/midi_score.py').read().split('def run(')[0])   # midi_notes
def trk(exe,wav):
    out=subprocess.run([exe,wav],capture_output=True,text=True).stdout
    T=[];G=[];Fv=[]
    for l in out.split('\n'):
        p=l.split()
        if p and p[0]=='F': T.append(float(p[1])); Fv.append(float(p[2])); G.append(int(p[3]))
    return T,Fv,G
def score(T,F,G,notes,tmax):
    hit=0;lat=[];fw=0;n=0;wrong_dur=0.0
    for (a,b,nn) in notes:
        if a>tmax: continue
        n+=1; want=440*2**((nn-69)/12)
        i0=bisect.bisect_left(T,a-0.02); i1=bisect.bisect_left(T,a+0.30)
        first=None; ok=None
        for i in range(i0,i1):
            if not G[i] or F[i]<=0: continue
            c=abs(1200*math.log2(F[i]/want))
            if first is None: first=(i,c)
            if c<50 and ok is None: ok=T[i]-a
        if ok is not None: hit+=1; lat.append(ok*1000)
        if first is not None and first[1]>=50: fw+=1
    lat.sort()
    return dict(n=n,hit=round(100*hit/n),med=round(lat[len(lat)//2]) if lat else -1,p90=round(lat[int(len(lat)*.9)]) if lat else -1,first_sounded_wrong=round(100*fw/n))
for nm,exe in (("ORIGINAL v42 MONO",sys.argv[1]),("today's tracker",sys.argv[2])):
    print("=== "+nm)
    for tag,w,m in (('P1pm',S+'/pm/P1.wav',D+'/P1_techniques/midi/midi_PalmMute.mid'),('P2pm',S+'/pm/P2.wav',D+'/P2_techniques/midi/midi_PalmMute.mid'),('SN',S+'/pm/SN.wav',D+'/P1_singlenotes/midi/midi_allsinglenotes.mid')):
        notes=midi_notes(m)
        if tag=='SN': notes=notes[:250]
        T,F,G=trk(exe,w); print("  ",tag,score(T,F,G,notes,notes[-1][0]+0.1))
print()
print("=== time spent sounding in the first 200 ms of each true note: right pitch / wrong pitch (>50 cents) / silent  (ms, mean per note)")
def times(T,F,G,notes,tmax):
    R=W=Sl=0.0;n=0
    for (a,b,nn) in notes:
        if a>tmax: continue
        n+=1; want=440*2**((nn-69)/12); i0=bisect.bisect_left(T,a); i1=bisect.bisect_left(T,min(a+0.2,b))
        for i in range(i0,i1):
            dt=0.0105
            if not G[i] or F[i]<=0: Sl+=dt
            elif abs(1200*math.log2(F[i]/want))<50: R+=dt
            else: W+=dt
    return round(1000*R/n),round(1000*W/n),round(1000*Sl/n)
for nm,exe in (("ORIGINAL v42 MONO",sys.argv[1]),("today's tracker",sys.argv[2])):
    row=[]
    for tag,w,m in (('P1pm',S+'/pm/P1.wav',D+'/P1_techniques/midi/midi_PalmMute.mid'),('P2pm',S+'/pm/P2.wav',D+'/P2_techniques/midi/midi_PalmMute.mid'),('SN',S+'/pm/SN.wav',D+'/P1_singlenotes/midi/midi_allsinglenotes.mid')):
        notes=midi_notes(m)
        if tag=='SN': notes=notes[:250]
        T,F,G=trk(exe,w); r,wr,sl=times(T,F,G,notes,notes[-1][0]+0.1); row.append("%s right %3d wrong %3d silent %3d"%(tag,r,wr,sl))
    print("  %-20s %s"%(nm," | ".join(row)))
