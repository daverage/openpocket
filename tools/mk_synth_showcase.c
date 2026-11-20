#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"   /* cc -O1 -Iengine tools/mk_synth_showcase.c firmware/px_math.c -lm; run with a folder of mono 44.1 kHz loops (lead, riff, chords .wav): prints the preset file, a level report on stderr */
typedef struct { const char *name; const char *loop; struct { int blk, type; uint8_t p[5]; } b[6]; } spec;
#define E {-1,0,{0}}
static const spec S[] = {
 {"SQUARE LEAD","lead",{{PX_B_SFX,PX_SFX_SYNTH,{60,40,55,100,0}},{PX_B_DELAY,PX_DLY_ANALOG,{45,35,25,50,0}},{PX_B_REVERB,PX_REV_PLATE,{40,50,20,0,0}},E,E,E}},
 {"SQ HQ BASS","riff",{{PX_B_COMP,PX_CMP_SQUASH,{60,40,50,0,0}},{PX_B_SFX,PX_SFX_SYNTH_HQ,{35,30,70,80,0}},E,E,E,E}},
 {"POLY OCTAVES","chords",{{PX_B_PITCH,PX_PIT_POLYOCT_HQ,{55,60,45,100,40}},{PX_B_REVERB,PX_REV_HALL,{45,55,22,0,0}},E,E,E,E}},
 {"POLY FUZZ","riff",{{PX_B_SYN,PX_SYN_OCT_FUZZ,{60,50,60,50,100}},{PX_B_REVERB,PX_REV_ROOM,{30,50,15,0,0}},E,E,E,E}},
 {"MOOG BASS","riff",{{PX_B_SYN,PX_SYN_ANALOG,{60,50,55,50,50}},{PX_B_DELAY,PX_DLY_TAPE,{40,30,20,50,30}},E,E,E,E}},
 {"CHIP LEAD","lead",{{PX_B_SYN,PX_SYN_ARCADE,{10,50,50,50,100}},{PX_B_DELAY,PX_DLY_DIGITAL,{30,30,20,60,0}},E,E,E,E}},
 {"STRING PAD","chords",{{PX_B_SYN,PX_SYN_INSTRUMENT,{15,50,50,50,50}},{PX_B_MOD,PX_MOD_CHORUS,{30,50,35,0,0}},{PX_B_REVERB,PX_REV_HALL,{60,50,30,0,0}},E,E,E}},
 {"JAZZ ORGAN","chords",{{PX_B_SYN,PX_SYN_INSTRUMENT,{65,50,50,50,50}},{PX_B_MOD,PX_MOD_ROTARY,{40,60,50,0,0}},{PX_B_REVERB,PX_REV_ROOM,{30,50,15,0,0}},E,E,E}},
 {"STRING ENSEMB","chords",{{PX_B_SYN,PX_SYN_ENSEMBLE,{15,15,50,50,50}},{PX_B_REVERB,PX_REV_HALL,{55,50,30,0,0}},E,E,E,E}},
 {"CHOIR AAH","chords",{{PX_B_SYN,PX_SYN_CHOIR,{5,50,50,50,50}},{PX_B_REVERB,PX_REV_CATHEDRAL,{60,45,30,0,0}},E,E,E,E}},
 {"BRASS + SQ HQ","lead",{{PX_B_SYN,PX_SYN_ENSEMBLE,{25,45,50,50,50}},{PX_B_SFX,PX_SFX_SYNTH_HQ,{40,30,60,30,0}},{PX_B_REVERB,PX_REV_PLATE,{35,50,18,0,0}},E,E,E}},
 {"STADIUM ROAR","chords",{{PX_B_SFX,PX_SFX_APPLAUSE,{100,70,40,100,0}},{PX_B_REVERB,PX_REV_HALL,{40,50,12,0,0}},E,E,E,E}},
};
static float *rd(const char*fn,int*n){FILE*f=fopen(fn,"rb");fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,44,SEEK_SET);*n=(sz-44)/2;short*s=malloc(*n*2);fread(s,2,*n,f);fclose(f);float*x=malloc(*n*4);for(int i=0;i<*n;i++)x[i]=s[i]/32768.f;return x;}
int main(int argc,char**argv){
  printf("{\"format\":\"openpocket-presets\",\"version\":1,\"presets\":[\n");
  for(unsigned i=0;i<sizeof S/sizeof*S;i++){
    px_preset_t p; px_preset_factory(&p,0); memset(p.on,0,sizeof p.on); strncpy(p.name,S[i].name,13); p.name[13]=0;
    for(int j=0;j<6;j++) if(S[i].b[j].blk>=0){ int b=S[i].b[j].blk; p.on[b]=1; p.type[b]=S[i].b[j].type; memcpy(p.p[b],S[i].b[j].p,5); }
    px_order_default(p.order);
    char fn[300]; snprintf(fn,300,"%s/%s.wav",argv[1],S[i].loop); int n; float*x=rd(fn,&n),*y=calloc(n,4);
    px_engine e; px_engine_init(&e,44100.f); px_engine_set(&e,&p); for(int at=0;at+64<=n;at+=64) px_engine_process(&e,x+at,y+at,64);
    double a=0;int c=0;float pk=0; for(int k=0;k+2048<=n;k+=2048){double r=0;for(int j=0;j<2048;j++)r+=y[k+j]*y[k+j]; r=sqrt(r/2048); if(r>0.003){a+=r*r;c++;}} for(int k=0;k<n;k++)if(fabsf(y[k])>pk)pk=fabsf(y[k]);
    fprintf(stderr,"%-14s on %-10s gated %6.1f dBFS peak %5.1f\n",S[i].name,S[i].loop,c?10*log10(a/c):-99.0,20*log10(pk+1e-12));
    printf("{\"name\":\"%s\",\"v\":36,\"blocks\":[",p.name);
    for(int b=0;b<PX_NBLK;b++){ printf("%s{\"block\":\"%s\",\"on\":%s,\"type\":\"%s\",\"params\":[%d,%d,%d,%d,%d]}",b?",":"",px_block_label(b),p.on[b]?"true":"false",px_type_name(b,p.type[b]),p.p[b][0],p.p[b][1],p.p[b][2],p.p[b][3],p.p[b][4]); }
    printf("],\"order\":["); for(int b=0;b<PX_NBLK;b++) printf("%s\"%s\"",b?",":"",px_block_label(p.order[b])); printf("]}%s\n",i+1<sizeof S/sizeof*S?",":"");
  }
  printf("]}\n"); }
