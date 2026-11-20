#!/bin/sh
# build.sh NAME [-DFLAGS...]  -> build/fasttrack/ft_NAME  (bit-exact pedal numerics: -fno-builtin -ffp-contract=off)
set -e; cd "$(dirname "$0")/../.."; mkdir -p build/fasttrack
n=$1; shift
cc -O2 -w -fno-builtin -ffp-contract=off -Iengine "$@" tests/fasttrack/ft_trace.c firmware/px_math.c -lm -o build/fasttrack/ft_$n
