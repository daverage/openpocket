#!/bin/sh
# OpenPocket Synth2: are POLY OCTAVE HQ and POLY OCT FUZZ bit-identical to commit REF's? (tests/engine/protected.c)
#   tests/protected.sh REF       e.g. tests/protected.sh 2f3519b5   (the Synth2 baseline, the last `synth` commit)
# Built as the pedal builds the engine (-fno-builtin -ffp-contract=off, firmware/px_math.c). Writes to build/protected. Exit 0: identical.
set -e
cd "$(dirname "$0")/.."
REF=${1:?usage: tests/protected.sh REF}
CC="${CC:-cc}"
B=build/protected
rm -rf "$B/ref"; mkdir -p "$B/ref"
git archive "$REF" engine firmware/px_math.c | tar -x -C "$B/ref"
$CC -O2 -w -fno-builtin -ffp-contract=off -I"$B/ref/engine" -o "$B/ref-run" tests/engine/protected.c "$B/ref/firmware/px_math.c" -lm
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine -o "$B/new-run" tests/engine/protected.c firmware/px_math.c -lm
"$B/ref-run" assets/loops > "$B/ref.txt"
"$B/new-run" assets/loops > "$B/new.txt"
rc=0
if cmp -s "$B/ref.txt" "$B/new.txt"; then echo "protected: identical to $REF, $(tail -1 "$B/new.txt")"
else echo "protected: DIFFERS from $REF"; diff "$B/ref.txt" "$B/new.txt" | head -8; rc=1; fi
# every other block, alone, every type x 8 settings (tests/engine/equiv.c section A): the SYNTH block's retired types are left out, and
# PITCH type 5 (LOWEST OCTAVE HQ: its gain was raised on purpose in Synth2 fix pass 1; tests/engine/lowest_gain_test.c measures that change)
$CC -O2 -w -fno-builtin -ffp-contract=off -I"$B/ref/engine" -o "$B/eq-ref" tests/engine/equiv.c "$B/ref/firmware/px_math.c" -lm
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine -o "$B/eq-new" tests/engine/equiv.c firmware/px_math.c -lm
"$B/eq-ref" A | grep -v '^A blk 3 type \([0-9]\|1[0-5]\|1[7-9]\|2[0-9]\) \|^A blk 2 type 5 \|runs$' > "$B/eq-ref.txt"
"$B/eq-new" A | grep -v '^A blk 3 type \([0-9]\|1[0-5]\|1[7-9]\|2[0-9]\) \|^A blk 2 type 5 \|runs$' > "$B/eq-new.txt"
if cmp -s "$B/eq-ref.txt" "$B/eq-new.txt"; then echo "protected: the other blocks (alone) identical, $(wc -l < "$B/eq-new.txt") runs"
else echo "protected: the other blocks DIFFER"; diff "$B/eq-ref.txt" "$B/eq-new.txt" | head -8; rc=1; fi
exit $rc
