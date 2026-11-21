#!/bin/sh
# regress.sh : rebuild the FastTrack harness from scratch and check that the headline numbers still match regress_expected.txt (about 3 minutes).
# Needs build/stage5/sets/gtechs (see tools/research/prep_sets.py gtechs).  A mismatch means a harness, tracker or scorer change: investigate before trusting new results.
set -e; cd "$(dirname "$0")/../.."
python3 tests/fasttrack/mk_overlay.py
B=tests/fasttrack/build.sh; R=tests/fasttrack/run_set.sh; O=build/fasttrack
$B base
FT_SRC=tests/fasttrack/ft_synth.c FT_OVERLAY=1 $B synth_base -DFT_HOOKS
FT_SRC=tests/fasttrack/ft_synth.c FT_OVERLAY=1 $B synth_cl_f0 -DFT_HOOKS -DFT_FOLLOW=0
FT_SRC=tests/fasttrack/ft_ctrl_trace.c $B oA20 -DFTC_RISE=1.3f -DFTC_ADM_MIN=20
for c in B C D; do cp $O/ft_oA20 $O/ft_o${c}20; done
for v in base synth_base synth_cl_f0 oA20; do rm -rf $O/out/gtechs/$v; $R $v gtechs; done
rm -rf $O/out/gtechs/oB20 $O/out/gtechs/oC20 $O/out/gtechs/oD20
FT_ORACLE_ONSET=0 $R oB20 gtechs; FT_ORACLE_PITCH=1 $R oC20 gtechs; FT_ORACLE_ONSET=0 FT_ORACLE_PITCH=1 $R oD20 gtechs
{ python3 tests/fasttrack/ft_compare.py synth_base="legacy CLASSIC (FOLLOW on)" synth_cl_f0="legacy, FOLLOW off" oA20="controller A (rise1.3, min20)" oB20="B oracle onset" oC20="C oracle pitch" oD20="D both oracle"
  python3 tests/fasttrack/ft_score.py base; } > $O/regress_now.txt
if diff -q tests/fasttrack/regress_expected.txt $O/regress_now.txt >/dev/null; then echo "REGRESSION OK: identical to regress_expected.txt"; else echo "MISMATCH:"; diff tests/fasttrack/regress_expected.txt $O/regress_now.txt; exit 1; fi
