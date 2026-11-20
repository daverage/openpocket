#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
/* the ORIGINAL SYN MONO (v42: zero-crossing tracker, median of 3 periods, 12 ms pitch glide, level-gated, no re-attack): events in the same format as ev2 */
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); int hdr=(strlen(argv[1])>4&&!strcmp(argv[1]+strlen(argv[1])-4,".wav"))?44:0; fseek(f,hdr,SEEK_SET); int n=(sz-hdr)/2; short*s=malloc(n*2); fread(s,2,n,f); fclose(f);
  float*x=malloc(n*4),*y=calloc(n,4); for(int i=0;i<n;i++)x[i]=s[i]/32768.f;
  px_preset_t p; px_preset_factory(&p,7); for(int z=0;z<PX_NBLK;z++)p.on[z]=0; p.on[PX_B_SYN]=1; p.type[PX_B_SYN]=PX_SYN_MONO; { uint8_t q[5]={30,50,60,30,60}; memcpy(p.p[PX_B_SYN],q,5); }
  px_engine e; px_engine_init(&e,44100.f); px_engine_set(&e,&p);
  int sounding=0;
  for(int at=0;at+64<=n;at+=64){ px_engine_process(&e,x+at,y+at,64);
    double t=(at+64)/44100.0; int on=e.syn_kg>0.3f&&e.syn_f0>0.0f;
    if(on&&!sounding){ printf("O %.4f %.2f 0\n",t,e.syn_f0); sounding=1; } else if(sounding&&e.syn_kg<0.1f){ printf("R %.4f\n",t); sounding=0; }
    if((at/64)%7==0) printf("F %.4f %.2f %d\n",t,e.syn_f0,sounding); }
}
