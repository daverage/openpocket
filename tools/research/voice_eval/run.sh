#!/bin/sh
# Synth2 choir evaluation (VOICE): run from the repository root: sh tools/research/voice_eval/run.sh
set -e
T=tools/research/voice_eval; W=build/voice_eval; rm -rf $W; mkdir -p $W/src $W/out
cp -r engine third_party $W/src/
python3 $T/patch_scratch.py $W/src
${CC:-cc} -O2 -w -fno-builtin -ffp-contract=off -I$W/src/engine $T/voice_eval.c firmware/px_math.c -lm -o $W/voice_eval
$W/voice_eval $W/out
python3 $T/analysis.py $W/out
