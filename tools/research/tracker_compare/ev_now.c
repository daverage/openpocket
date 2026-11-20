#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "px_dsp.c"
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,44,SEEK_SET); int n=(sz-44)/2; short*s=malloc(n*2); fread(s,2,n,f); fclose(f);
  px_engine e; px_engine_init(&e,44100.f); px_mono m; px_mono_init(&e,&m,PX_MONO_GUITAR);
  for(int i=0;i<n;i++){ px_mono_out o; px_mono_run(&e,&m,s[i]/32768.f,&o);
    if(o.onset) printf("O %.4f %.2f %d\n",i/44100.0,o.f0,o.retrigger);
    if(o.release) printf("R %.4f\n",i/44100.0);
    if(i%441==0) printf("F %.4f %.2f %d\n",i/44100.0,o.f0,o.gate); }
}
