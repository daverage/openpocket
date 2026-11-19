#!/bin/sh
# The vendored Felucca engines against upstream's golden renders (tests/golden.txt, 122 lines; the engines present here: their lines).
# Run from the repository root: sh third_party/felucca-engines/tests/felucca_ref.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p ../../build/felucca
B=../../build/felucca
${CC:-cc} -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -I. -o $B/fe_host_ref tests/fe_host_ref.c
$B/fe_host_ref > $B/renders.txt
grep '^analog/' tests/golden.txt > $B/golden_present.txt
if cmp -s $B/golden_present.txt $B/renders.txt; then echo "felucca: $(wc -l < $B/renders.txt | tr -d ' ') ANALOG renders identical to upstream's golden hashes"; else echo "felucca: FAIL"; diff $B/golden_present.txt $B/renders.txt | head; exit 1; fi
