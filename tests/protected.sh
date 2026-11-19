#!/bin/sh
# OpenPocket Synth2: are POLY OCTAVE HQ and POLY OCT FUZZ bit-identical to commit REF's? (tests/engine/protected.c)
#   tests/protected.sh REF       e.g. tests/protected.sh 06358805   (the Synth2 baseline, the last `synth` commit)
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
if cmp -s "$B/ref.txt" "$B/new.txt"; then echo "protected: identical to $REF, $(tail -1 "$B/new.txt")"; exit 0; fi
echo "protected: DIFFERS from $REF"
diff "$B/ref.txt" "$B/new.txt" | head -8
exit 1
