#!/bin/sh
# Host listening prototype: guitar-driven trumpet and cello (docs/research/REALISTIC-INSTRUMENT-PROTOTYPE.md). Not firmware.
#   tools/legacy-synth/acoustic_voice.sh [OUTDIR]        default OUTDIR: build/acoustic
# Needs a C compiler and Python 3 with numpy and scipy. The first run downloads the VSCO 2 CE samples (CC0, about
# 30 MB) and the Guitar-TECHS P3_music archive (CC BY 4.0, 130 MB, for the lead loop's transcription) into OUTDIR.
# Writes OUTDIR/out/raw/*.wav, OUTDIR/out/matched/*.wav (each at -20 dBFS RMS) and OUTDIR/out/report.md.
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-build/acoustic}
python3 tools/legacy-synth/acoustic_prep.py "$OUT"
python3 tools/legacy-synth/acoustic_truth.py "$OUT"
mkdir -p build
${CC:-cc} -O2 -Wall -Wno-unused-function -Iengine -Ifirmware tools/legacy-synth/acoustic_voice.c firmware/px_math.c -lm -o build/acoustic_voice
rm -rf "$OUT/out"
build/acoustic_voice "$OUT/bank.bin" "$OUT/out" assets/loops/lead.wav="$OUT/lead.truth.csv" assets/loops/arpeggio.wav
python3 tools/legacy-synth/acoustic_compare.py "$OUT" > /dev/null
echo "listen: $OUT/out/matched/   measurements: $OUT/out/report.md"
