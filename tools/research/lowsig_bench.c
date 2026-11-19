/* OpenPocket Synth, stage 5: the octave bank's lowest-band selection as a signal (its bands at their own pitch,
 * 11.025 kHz float32), for tools/research/duo_bench.py.  lowsig_bench in.f32(44.1k) out.f32 SEL
 * cc -O2 -DPX_LOWSIG_HOST -Iengine tools/research/lowsig_bench.c firmware/px_math.c -lm */
#include "../../engine/px_dsp.c"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    static px_engine e; px_engine_init(&e, 44100.0f); erb_prepare(&e);
    static px_erb_octave st; st.low_sel = (float)atof(argv[3]);
    float x; unsigned n = 0;
    while (fread(&x, 4, 1, in) == 1) {
        px_lowsig_acc = 0.0f;
        const unsigned pos = st.pos; st.input[pos] = x;
        if ((pos & 1) == 0) {
            const unsigned p2 = st.pos2;
            st.in2[p2] = erb_halfband(st.input, pos, erb_hb);
            if ((p2 & 1) == 0) {
                erb_bank(&e, &st, erb_halfband(st.in2, p2, erb_hb), 0.0f, 1.0f, 0.0f, ERB_FULL);
                fwrite(&px_lowsig_acc, 4, 1, out);
            }
            st.pos2 = (p2 + 1) & 31;
        }
        st.pos = (pos + 1) & 31; ++n;
    }
    fclose(out); return 0;
}
