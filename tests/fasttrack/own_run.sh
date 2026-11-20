#!/bin/sh
# own_run.sh NAME [flags]  : build overlay variant NAME and run it on the owner's unlabelled DI clips (build/fasttrack/own/)
cd "$(dirname "$0")/../.." || exit 1
n=$1; shift

for f in build/fasttrack/own/*.wav; do b=$(basename $f .wav); FT_TRACE=build/fasttrack/own/$b.$n.tr build/fasttrack/ft_$n $f > build/fasttrack/own/$b.$n.ev; done
