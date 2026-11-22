#!/bin/sh
set -eu
cd "$(dirname "$0")/../../../.."
src=tests/archive/fasttrack/declick
out=build/fasttrack/declick
mkdir -p "$out"
cc=${CC:-cc}
"$cc" -std=c11 -O2 -Wall -Wextra -Werror "$src/declick.c" "$src/test_declick.c" -lm -o "$out/unit"
"$out/unit"
python3 "$src/overlay.py" "$out/overlay"
"$cc" -O2 -fno-builtin -ffp-contract=off -Iengine -Itests/archive/fasttrack "$src/render.c" firmware/px_math.c \
    -DFTC_RISE=1.3f -DFTC_ADM_MIN=20 -lm -o "$out/native"
"$cc" -O2 -fno-builtin -ffp-contract=off -I"$out/overlay/engine" -I"$src" -Itests/archive/fasttrack \
    -DFTC_RISE=1.3f -DFTC_ADM_MIN=20 -DFT_DECLICK_OVERLAY "$src/render.c" "$src/declick.c" firmware/px_math.c -lm -o "$out/overlay-render"
"$out/native" test-order
MPLCONFIGDIR="$PWD/$out/matplotlib" python3 "$src/benchmark.py" "$out" "$@"
