#!/bin/sh
# level_sweep.sh : input-level sensitivity. Runs the legacy chain (FOLLOW off) and controller A at -12, -6, 0, +6, +12 dB gain on the whole Guitar-TECHS set
# and prints the headline table. The trackers use ABSOLUTE thresholds (gate 0.003 / 0.0045, wrapper 0.01), so a level change is not neutral.   (about 2 minutes)
set -e; cd "$(dirname "$0")/../../.."; O=build/fasttrack; R=tests/archive/fasttrack/run_set.sh
[ -x $O/ft_synth_cl_f0 ] && [ -x $O/ft_oA20 ] || { echo "run tests/archive/fasttrack/regress.sh first"; exit 1; }
args=""
for g in m12 m6 p6 p12; do
  case $g in m12) db=-12;; m6) db=-6;; p6) db=6;; p12) db=12;; esac
  cp $O/ft_synth_cl_f0 $O/ft_lvlL_$g; cp $O/ft_oA20 $O/ft_lvlC_$g; rm -rf $O/out/gtechs/lvlL_$g $O/out/gtechs/lvlC_$g
  FT_GAIN_DB=$db $R lvlL_$g gtechs; FT_GAIN_DB=$db $R lvlC_$g gtechs
  args="$args lvlL_$g=legacy_FOLLOWoff_${db}dB lvlC_$g=controllerA_${db}dB"
done
python3 tests/archive/fasttrack/ft_compare.py synth_cl_f0=legacy_FOLLOWoff_0dB oA20=controllerA_0dB $args
