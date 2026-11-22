#!/bin/sh
# run_set.sh VARIANT [SET=gtechs]  : run build/fasttrack/ft_VARIANT over every take of build/stage5/sets/SET (8 parallel), outputs in build/fasttrack/out/SET/VARIANT/
cd "$(dirname "$0")/../../.." || exit 1
v=$1; s=${2:-gtechs}; o=build/fasttrack/out/$s/$v; mkdir -p $o
for w in build/stage5/sets/$s/*.wav; do echo "$w"; done | xargs -P 8 -n 1 tests/archive/fasttrack/one.sh $v $o
