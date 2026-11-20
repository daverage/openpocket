import sys,struct,math,bisect
def midi_notes(path):
    d=open(path,'rb').read(); fmt,nt,div=struct.unpack('>HHH',d[8:14]); pos=14; ev=[]; tempos=[(0,500000)]
    for _ in range(nt):
        assert d[pos:pos+4]==b'MTrk'; ln=struct.unpack('>I',d[pos+4:pos+8])[0]; p=pos+8; end=p+ln; t=0; run=0
        while p<end:
            v=0
            while True:
                b=d[p];p+=1;v=(v<<7)|(b&0x7f)
                if not b&0x80:break
            t+=v; b=d[p]
            if b==0xff:
                typ=d[p+1];p+=2;l=0
                while True:
                    c=d[p];p+=1;l=(l<<7)|(c&0x7f)
                    if not c&0x80:break
                if typ==0x51: tempos.append((t,int.from_bytes(d[p:p+3],'big')))
                p+=l
            elif b in(0xf0,0xf7):
                p+=1;l=0
                while True:
                    c=d[p];p+=1;l=(l<<7)|(c&0x7f)
                    if not c&0x80:break
                p+=l
            else:
                if b&0x80: run=b;p+=1
                st=run;
                if st>>4 in(0xc,0xd): p+=1
                else:
                    a=d[p];c=d[p+1];p+=2
                    if st>>4==9 and c>0: ev.append((t,'on',a))
                    elif st>>4==8 or (st>>4==9 and c==0): ev.append((t,'off',a))
        pos=end
    tempos.sort()
    def sec(tk):
        s=0;lt=0;lq=tempos[0][1]
        for tt,q in tempos:
            if tt>tk:break
            s+=(tt-lt)*lq/div/1e6;lt=tt;lq=q
        return s+(tk-lt)*lq/div/1e6
    ev.sort(); on={}; notes=[]
    for t,k,n in ev:
        if k=='on': on[n]=sec(t)
        elif n in on: notes.append((on.pop(n),sec(t),n))
    return sorted(notes)
def run(txt,notes):
    O=[];R=[];F=[]
    for l in open(txt):
        p=l.split()
        if p[0]=='O':O.append((float(p[1]),float(p[2]),int(p[3])))
        elif p[0]=='R':R.append(float(p[1]))
        else:F.append((float(p[1]),float(p[2]),int(p[3])))
    ot=[o[0] for o in O]; hit=0;late=[];wrong=0;wrongn=[]
    used=set()
    for (a,b,n) in notes:
        i=bisect.bisect_left(ot,a-0.02)
        if i<len(ot) and ot[i]<a+0.15:
            hit+=1;used.add(i);late.append(ot[i]-a)
    extra=[O[i] for i in range(len(O)) if i not in used]
    # pitch at the end of each GT note's first 100 ms vs midi
    ft=[f[0] for f in F]; bad=0; tot=0; jumps=0
    for (a,b,n) in notes:
        want=440*2**((n-69)/12)
        i0=bisect.bisect_left(ft,a+0.06); i1=bisect.bisect_left(ft,min(b,a+0.5))
        seg=[F[i] for i in range(i0,i1) if F[i][2]]
        if len(seg)<3: continue
        tot+=1
        cents=[1200*math.log2(f[1]/want) for f in seg if f[1]>0]
        if any(abs(c)>60 for c in cents): bad+=1
        # f0 stepping inside the note
        for k in range(1,len(seg)):
            if seg[k][1]>0 and seg[k-1][1]>0 and abs(1200*math.log2(seg[k][1]/seg[k-1][1]))>60: jumps+=1;break
    return dict(gt=len(notes),hit=hit,extra=len(extra),rel=len(R),notes_with_pitch_off_60c=bad,of=tot,notes_with_f0_jump=jumps)
for tag,mid in (('P1','P1_techniques'),('P2','P2_techniques')):
    notes=midi_notes(sys.argv[1]+'/'+mid+'/midi/midi_PalmMute.mid')
    for v in ('907','908'):
        print(tag,v,run(sys.argv[2]+'/pm/%s.%s.txt'%(tag,v),notes))
print("---- f0 steps >60 cents inside a note (908), with the nearest events")
for tag,mid in (('P1','P1_techniques'),('P2','P2_techniques')):
    notes=midi_notes(sys.argv[1]+'/'+mid+'/midi/midi_PalmMute.mid')
    F=[];O=[]
    for l in open(sys.argv[2]+'/pm/%s.908.txt'%tag):
        p=l.split()
        if p[0]=='F':F.append((float(p[1]),float(p[2]),int(p[3])))
        elif p[0]=='O':O.append(float(p[1]))
    shown=0
    for (a,b,n) in notes:
        want=440*2**((n-69)/12)
        seg=[f for f in F if a+0.06<=f[0]<=min(b,a+0.5) and f[2]]
        for k in range(1,len(seg)):
            if seg[k][1]>0 and seg[k-1][1]>0:
                c=1200*math.log2(seg[k][1]/seg[k-1][1])
                if abs(c)>60 and shown<8:
                    near=[round(o-a,3) for o in O if abs(o-seg[k][0])<0.1]
                    print(tag,"note %.1fs midi %d (%.1f Hz): f0 %.1f -> %.1f Hz (%+.0f cents) at +%.3fs; onsets within 100 ms: %s"%(a,n,want,seg[k-1][1],seg[k][1],c,seg[k][0]-a,near)); shown+=1; break
