#!/bin/sh
# one.sh VARIANT OUTDIR WAV
b=$(basename "$3" .wav)
FT_TRACE=$2/$b.tr build/fasttrack/ft_$1 "$3" > $2/$b.ev
