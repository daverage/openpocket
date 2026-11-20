#!/bin/sh
# build.sh NAME [-DFLAGS...]  -> build/fasttrack/ft_NAME  (bit-exact pedal numerics: -fno-builtin -ffp-contract=off)
# FT_OVERLAY=1: build against build/fasttrack/eng_root/engine (engine copy with the FastTrack experiments, tests/fasttrack/mk_overlay.py)
set -e; cd "$(dirname "$0")/../.."; mkdir -p build/fasttrack
n=$1; shift
inc=engine; src=tests/fasttrack/ft_trace.c
if [ -n "$FT_OVERLAY" ]; then inc=build/fasttrack/eng_root/engine; sed 's#../../engine/px_dsp.c#../../build/fasttrack/eng_root/engine/px_dsp.c#' $src > tests/fasttrack/.ft_trace_ov.c; src=tests/fasttrack/.ft_trace_ov.c; fi
cc -O2 -w -fno-builtin -ffp-contract=off -I$inc "$@" $src firmware/px_math.c -lm -o build/fasttrack/ft_$n
