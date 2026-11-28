/* Sweep extreme settings with 3 s of plucks followed by silence. Check bounds and tail decay. */
#include <stdio.h>
#include <math.h>
#include "px_dsp.h"
static px_engine e;
static float sig[44100*3];
static void mksig(void){ int pos=0; float f0s[3]={82.4f,110.0f,196.0f}; unsigned s=7;
  for(int n=0;n<3;n++){ int N=(int)(44100.0f/f0s[n]); float buf[600]; for(int i=0;i<N;i++){ s=s*1103515245u+12345u; buf[i]=((s>>8)&65535)/32768.0f-1.0f; }
    for(int i=0;i<44100;i++){ sig[pos++]=0.35f*buf[i%N]; int j=i%N; buf[j]=0.996f*0.5f*(buf[j]+buf[(j+1)%N]); } } }
int main(void){ mksig(); int bad=0, n=0; static const int V[3]={0,50,100};
  for(int blk=0;blk<PX_NBLK;blk++) for(int t=0;t<px_ntypes(blk);t++) for(int a=0;a<3;a++){
    px_preset_t p; px_preset_base(&p); for(int z=0;z<PX_NBLK;z++) p.on[z]=(z==PX_B_GATE||z==blk);
    p.type[blk]=t; for(int k=0;k<5;k++) p.p[blk][k]=(a==2)?100:V[(a+k)%3];
    px_engine_init(&e,44100.0f); px_engine_set(&e,&p); n++;
    const int max_feedback = blk == PX_B_DELAY && p.p[blk][1] == 100;
    const int self_oscillates = max_feedback &&
      (t == PX_DLY_ANALOG || t == PX_DLY_MEMORY || t == PX_DLY_TAPE || t == PX_DLY_SPACE || t == PX_DLY_LOFI);
    /* At maximum feedback the linear delays can take tens of seconds to die away.
       Analog models can self-oscillate, but their output must stay bounded. */
    const int end = max_feedback && !self_oscillates ? 60 : 11;
    float out[64], in[64] = {0};
    double pk = 0, last = 0;
    long cnt = 0;
    int nonfinite = 0;
    for (int at = 0; at + 64 <= 44100 * end; at += 64) {
      const float *src = at + 64 <= 44100 * 3 ? sig + at : in;
      px_engine_process(&e, src, out, 64);
      for (int i = 0; i < 64; i++) {
        const float v = out[i];
        if (!isfinite(v)) nonfinite = 1;
        if (fabs(v) > pk) pk = fabs(v);
        if (at >= 44100 * (end - 1)) {
          last += v * v;
          cnt++;
        }
      }
    }
    last=sqrt(last/cnt);
    int lim_ok = !(blk==PX_B_REVERB && (t==PX_REV_AMBIENT||t==PX_REV_CATHEDRAL||t==PX_REV_SHIMMER));
    if (nonfinite || pk > 50.0 || (lim_ok && !self_oscillates && last > 0.02) || last > 0.2) {
      bad++;
      printf("%-12s %-14s set%d  peak %.2f  tail(%d-%ds) %.4f %s\n",
             px_block_name(blk), px_type_name(blk,t), a, pk, end - 1, end, last,
             nonfinite ? "non-finite" : "");
    }
  }
  printf("%d runs, %d flagged\n",n,bad);
  return bad != 0;
}
