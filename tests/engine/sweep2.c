/* sweeps every block x type x setting (0/50/100) with the pedal's libm; changes settings while playing
 * (like the UI); detects NaN/Inf/blow-up */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.h"
static px_engine e;
static long bad;

static int poly_oct_fuzz_level_failed(int level)
{
    const int sample_rate = 44100;
    px_preset_t preset;
    px_engine_init(&e, (float)sample_rate);
    px_preset_factory(&preset, 0);
    memset(preset.on, 0, sizeof preset.on);
    preset.on[PX_B_SYN] = 1;
    preset.type[PX_B_SYN] = PX_SYN_OCT_FUZZ;
    preset.p[PX_B_SYN][0] = preset.p[PX_B_SYN][1] = 0;
    preset.p[PX_B_SYN][2] = 50;
    preset.p[PX_B_SYN][3] = 70;
    preset.p[PX_B_SYN][4] = (uint8_t)level;
    preset.patch_volume = 50;
    px_engine_set(&e, &preset);

    /* Measure how much energy remains after projection onto the input tone. */
    float real = 0.0f, imag = 0.0f, energy = 0.0f;
    for (int i = 0; i < sample_rate * 2; i++) {
        const float phase = 6.2831853f * 110.0f * (float)(i % sample_rate) / (float)sample_rate;
        float input = 0.024f * sinf(phase), output;
        px_engine_process(&e, &input, &output, 1);
        if (!isfinite(output)) return 1;
        if (i < sample_rate) continue;
        energy += output * output;
        real += output * cosf(phase);
        imag += output * sinf(phase);
    }
    if (level == 0) return energy > 1e-12f;
    const float fundamental = 2.0f * (real * real + imag * imag) / (float)sample_rate;
    return !(energy > 0.0f && fundamental < 0.98f * energy);
}

static int hq_octave_inaccurate(const int frequencies[3], float ratio, float min_share)
{
    const int sample_rate = 44100;
    px_preset_t preset;
    px_engine_init(&e, (float)sample_rate);
    px_preset_factory(&preset, 0);
    memset(preset.on, 0, sizeof preset.on);
    preset.on[PX_B_PITCH] = 1;
    preset.type[PX_B_PITCH] = PX_PIT_POLYOCT_HQ;
    memset(preset.p[PX_B_PITCH], 0, sizeof preset.p[PX_B_PITCH]);
    preset.p[PX_B_PITCH][1] = ratio < 1.0f ? 100 : 0;
    preset.p[PX_B_PITCH][2] = ratio > 1.0f ? 100 : 0;
    preset.patch_volume = 50;
    px_engine_set(&e, &preset);

    float real[3] = {0}, imag[3] = {0}, energy = 0.0f;
    for (int i = 0; i < 2 * sample_rate; i++) {
        float input = 0.0f, output;
        const float phase = 6.2831853f * (float)(i % sample_rate) / (float)sample_rate;
        for (int n = 0; n < 3; ++n) input += 0.06f * sinf(phase * frequencies[n]);
        px_engine_process(&e, &input, &output, 1);
        if (!isfinite(output)) return 1;
        if (i < sample_rate) continue;
        energy += output * output;
        for (int n = 0; n < 3; ++n) {
            real[n] += output * cosf(phase * ratio * frequencies[n]);
            imag[n] += output * sinf(phase * ratio * frequencies[n]);
        }
    }
    float target = 0.0f;
    if (!(energy > 0.0f)) return 1;
    for (int n = 0; n < 3; ++n) {
        const float share = 2.0f * (real[n] * real[n] + imag[n] * imag[n]) / ((float)sample_rate * energy);
        if (share < 0.04f) return 1;  /* each shifted note must be present */
        target += share;
    }
    return target < min_share;
}

static int run(int blocks, float amp){
  float in[64], out[64]; int nan = 0; static unsigned seed = 1;
  for (int b = 0; b < blocks; b++) {
    for (int i = 0; i < 64; i++) { seed = seed*1103515245u+12345u; float nz = ((seed>>9)&1023)/1024.0f - 0.5f;
      in[i] = amp * (((b*64+i) % 180 < 90) ? 0.5f : -0.5f) + amp*0.2f*nz; }
    px_engine_process(&e, in, out, 64);
    for (int i = 0; i < 64; i++) if (out[i] != out[i] || out[i] > 50 || out[i] < -50) nan = 1;
  }
  return nan;
}
int main(void){
  px_engine_init(&e, 44100.0f);
  static const int V[3] = {0, 50, 100}; long n = 0;
  for (int blk = 0; blk < PX_NBLK; blk++)
   for (int t = 0; t < px_ntypes(blk); t++)
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) {
      px_preset_t p; px_preset_factory(&p, (blk*7+t) % px_preset_count());
      for (int z = 0; z < PX_NBLK; z++) p.on[z] = (z == PX_B_GATE || z == blk);
      p.type[blk] = (uint8_t)t;
      p.p[blk][0] = V[a]; p.p[blk][1] = V[b]; p.p[blk][2] = V[c]; p.p[blk][3] = V[(a+c)%3]; p.p[blk][4] = V[(b+c)%3];
      px_engine_set(&e, &p); n++;
      if (run(30, 1.0f) | run(10, 0.0f) | run(10, 3.0f)) {   /* signal, silence, very loud */
        bad++; if (bad < 15) printf("PROBLEM: %s / type %d %s  p=%d %d %d\n", px_block_name(blk), t, px_type_name(blk,t), V[a],V[b],V[c]);
        px_engine_init(&e, 44100.0f);
      }
    }
  /* all blocks together, every preset, extreme settings */
  for (int pr = 0; pr < px_preset_count(); pr++) for (int x = 0; x < 3; x++) {
      px_preset_t p; px_preset_factory(&p, pr);
      for (int z = 0; z < PX_NBLK; z++) { p.on[z] = 1; for (int k = 0; k < 5; k++) p.p[z][k] = V[(x+k+z)%3]; }
      px_engine_set(&e, &p); n++;
      if (run(40, 1.0f) | run(20, 0.0f)) { bad++; printf("PROBLEM all ON preset %d x%d\n", pr, x); px_engine_init(&e,44100.0f); }
  }
  /* block order (v29): random orders, all blocks on, every preset; invalid orders fall back to the default */
  { unsigned r = 12345u;
    for (int pr = 0; pr < px_preset_count(); pr++) for (int x = 0; x < 4; x++) {
      px_preset_t p; px_preset_factory(&p, pr);
      for (int z = 0; z < PX_NBLK; z++) p.on[z] = 1;
      for (int i = PX_NBLK - 1; i > 0; i--) { r = r * 1103515245u + 12345u; int j = (int)((r >> 8) % (unsigned)(i + 1));
        uint8_t t = p.order[i]; p.order[i] = p.order[j]; p.order[j] = t; }
      if (x == 3) p.order[0] = p.order[1];        /* not a permutation */
      px_engine_set(&e, &p); n++;
      if (run(40, 1.0f) | run(20, 0.0f) | run(10, 3.0f)) { bad++; printf("PROBLEM order preset %d x%d\n", pr, x); px_engine_init(&e,44100.0f); }
    }
  }
  static const int close_chord[] = {82, 110, 147}, open_chord[] = {82, 147, 247};
  /* Phase doubling mixes close partials within a filter band. Still require all three
     doubled notes there, and a clear majority of shifted energy on the open chord. */
  if (hq_octave_inaccurate(close_chord, 2.0f, 0.35f) || hq_octave_inaccurate(open_chord, 2.0f, 0.7f)) {
      bad++; puts("PROBLEM POLY OCTAVE HQ missed the doubled chord frequencies");
  }
  if (hq_octave_inaccurate(close_chord, 0.5f, 0.7f)) { bad++; puts("PROBLEM POLY OCTAVE HQ missed the halved chord frequencies"); }
  if (poly_oct_fuzz_level_failed(0)) { bad++; puts("PROBLEM POLY OCT FUZZ did not mute at LEVEL 0"); }
  if (poly_oct_fuzz_level_failed(50)) { bad++; puts("PROBLEM POLY OCT FUZZ lost its distortion at LEVEL 50"); }
  printf("%ld configurations tested, %ld problems\n", n, bad);
  return bad != 0;
}
