#!/bin/sh
# Synth2 stage D: the TRIO / CHIP CHOIR evaluation in a scratch copy. Run from the repository root: sh tools/research/trio_eval/run.sh
set -e
T=tools/research/trio_eval; W=build/trio_eval; rm -rf $W; mkdir -p $W/src $W/out
cp -r engine third_party $W/src/
python3 $T/patch_scratch.py $W/src
${CC:-cc} -O2 -w -fno-builtin -ffp-contract=off -I$W/src/engine $T/trio_eval.c firmware/px_math.c -lm -o $W/trio_eval
$W/trio_eval $W/out
python3 $T/analysis.py $W/out
