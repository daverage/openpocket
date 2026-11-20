#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
/* how LOUD are the short / wrong notes? run the synth over a DI file, take the tracker's notes (onset .. release), the synth output's peak in each note; report the notes shorter than 80 ms:
 * their count and how many peak within 12 dB of the file's loud notes (the 90th percentile of the notes' peaks): the audible blips */
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); int hdr=argc>3?44:0; fseek(f,hdr,SEEK_SET); int n=(sz-hdr)/2; short*s=malloc(n*2); fread(s,2,n,f); fclose(f);
  float*x=malloc(n*4),*y=calloc(n,4); for(int i=0;i<n;i++)x[i]=s[i]/32768.f;
  px_preset_t p; px_preset_factory(&p,7); for(int z=0;z<PX_NBLK;z++)p.on[z]=0; p.on[PX_B_SYN]=1; p.type[PX_B_SYN]=PX_SYN_ANALOG; { uint8_t q[5]={5,50,50,50,50}; memcpy(p.p[PX_B_SYN],q,5); }
  px_engine e; px_engine_init(&e,44100.f); px_engine_set(&e,&p); for(int at=0;at+64<=n;at+=64) px_engine_process(&e,x+at,y+at,64);
  px_engine e2; px_engine_init(&e2,44100.f); px_mono m; px_mono_init(&e2,&m,PX_MONO_GUITAR);
  int cap=n/200+16; int *on=malloc(cap*4),*off=malloc(cap*4); int nn=0,start=-1;
  for(int i=0;i<n;i++){ px_mono_out o; px_mono_run(&e2,&m,x[i],&o); if(o.release&&start>=0&&nn<cap){ on[nn]=start; off[nn]=i; nn++; start=-1; } if(o.onset&&!o.release) start=i; else if(o.onset) start=i; }
  float *pk=malloc(nn*4); float *srt=malloc(nn*4); for(int k=0;k<nn;k++){ float m2=0; int a=on[k],b=off[k]+2205; if(b>n)b=n; for(int i=a;i<b;i++) if(fabsf(y[i])>m2)m2=fabsf(y[i]); pk[k]=m2; srt[k]=m2; }
  for(int i=0;i<nn;i++) for(int j=i+1;j<nn;j++) if(srt[j]<srt[i]){float t=srt[i];srt[i]=srt[j];srt[j]=t;}
  float loud=nn?srt[(int)(nn*0.9)]:1; int shortn=0,aud=0; for(int k=0;k<nn;k++){ if(off[k]-on[k]<0.08*44100){ shortn++; if(pk[k]>loud*0.25f) aud++; } }
  printf("notes %d, short (<80 ms) %d, of them within 12 dB of the loud notes (audible blips) %d\n",nn,shortn,aud);
}
