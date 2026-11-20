// Host benchmark (v49): Cycfi Q's bitstream-autocorrelation pitch detector (MIT, github.com/cycfi/q, read into
// build/ext/q) on a DI file, conditioned as its own test does (test/pitch/pitch_detector_ex.cpp: band-pass, peak
// envelope, compressor + make-up + hard clip, gate -28 / -60 dB). Not firmware.
//   c++ -std=c++20 -O2 -Ibuild/ext/q/q_lib/include -Ibuild/ext/q/infra/include tools/research/q_bench.cpp -o build/q_bench
//   build/q_bench IN.wav OUT.csv [onset_dB]       one row per 64 samples: t, q (Hz while the gate is open, else 0)
#include <q/support/literals.hpp>
#include <q/pitch/pitch_detector.hpp>
#include <q/fx/envelope.hpp>
#include <q/fx/lowpass.hpp>
#include <q/fx/dynamic.hpp>
#include <q/fx/clip.hpp>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>
namespace q = cycfi::q;
using namespace q::literals;

static std::vector<float> wav_read(const char *fn)
{
    std::vector<float> x; FILE *f = fopen(fn, "rb"); if (!f) return x;
    unsigned char h[12]; if (fread(h, 1, 12, f) != 12) return x;
    int ch = 1, bits = 16, fmt = 1; unsigned dsz = 0; bool ok = false;
    for (;;) { unsigned char c[8]; if (fread(c, 1, 8, f) != 8) break; unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { unsigned char b[16]; fread(b, 1, 16, f); fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; bits = b[14] | b[15] << 8; fseek(f, sz - 16, SEEK_CUR); }
        else if (!memcmp(c, "data", 4)) { dsz = sz; ok = true; break; }
        else fseek(f, (sz + 1) & ~1u, SEEK_CUR); }
    if (!ok) return x;
    int bps = bits / 8, m = dsz / (bps * ch); x.resize(m);
    for (int i = 0; i < m; i++) { double a = 0; for (int c = 0; c < ch; c++) {
        if (fmt == 3) { float v; fread(&v, 4, 1, f); a += v; } else if (bits == 16) { int16_t v; fread(&v, 2, 1, f); a += v / 32768.0; }
        else if (bits == 24) { unsigned char b[3]; fread(b, 1, 3, f); int v = b[0] | b[1] << 8 | b[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF; a += v / 8388608.0; }
        else { int32_t v; fread(&v, 4, 1, f); a += v / 2147483648.0; } }
        x[i] = (float)(a / ch); }
    fclose(f); return x;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s IN.wav OUT.csv [onset_dB]\n", argv[0]); return 1; }
    auto in = wav_read(argv[1]); if (in.empty()) return 1;
    const float sps = 44100.0f, onset_db = argc > 3 ? atof(argv[3]) : -28.0f;
    const q::frequency lowest = 60_Hz, highest = 1400_Hz;
    q::pitch_detector pd{lowest, highest, sps, -40_dB};
    q::peak_envelope_follower env{30_ms, sps};
    q::one_pole_lowpass lp{highest, sps}, lp2{lowest, sps};
    q::compressor comp{-18_dB, 1.0f / 4};
    q::hard_clip clip;
    const float onset = q::lin_float(q::decibel{onset_db, q::direct_unit}), release = q::lin_float(-60_dB);
    float threshold = onset, freq = 0; bool gate = false;
    FILE *o = fopen(argv[2], "w"); fprintf(o, "t,q\n");
    for (size_t i = 0; i < in.size(); i++) {
        float s = lp(in[i]); s -= lp2(s);
        const float e = env(std::abs(s));
        if (e > threshold) { s = clip(s * q::lin_float(comp(q::lin_to_db(e))) * 4.0f); threshold = release; gate = true; }
        else { s = 0.0f; threshold = onset; gate = false; }
        if (pd(s)) freq = pd.get_frequency();
        if (!gate) freq = 0;
        if (i % 64 == 63) fprintf(o, "%.5f,%.3f\n", (i + 1) / sps, gate ? freq : 0.0f);
    }
    fclose(o); return 0;
}
