#!/bin/sh
# ablate.sh : single-knob ablations of the production mono safeguards (each row changes ONE setting from the production default)
cd "$(dirname "$0")/../../.."
B=tests/archive/fasttrack/build.sh
$B base
$B minwait5   -DPX_MONO_MINWAIT=5
$B waitlo0    -DPX_MONO_WAIT_LO=0
$B stab2      -DPX_MONO_STAB=2
$B confirm0   -DPX_MONO_CONFIRM_MS=0
$B dead0      -DPX_MONO_DEAD=0.0f
$B slur0      -DPX_MONO_SLUR=0.0f
$B hold0      -DPX_MONO_HOLD=0.0f
$B nowait     -DPX_MONO_MINWAIT=5 -DPX_MONO_WAIT_LO=0
$B nowait_stab2 -DPX_MONO_MINWAIT=5 -DPX_MONO_WAIT_LO=0 -DPX_MONO_STAB=2
$B v73like    -DPX_MONO_MINWAIT=5 -DPX_MONO_WAIT_LO=0 -DPX_MONO_STAB=2 -DPX_MONO_CONFIRM_MS=0 -DPX_MONO_DEAD=0.0f -DPX_MONO_SLUR=0.0f -DPX_MONO_HOLD=0.0f
for v in base minwait5 waitlo0 stab2 confirm0 dead0 slur0 hold0 nowait nowait_stab2 v73like; do tests/archive/fasttrack/run_set.sh $v; done
python3 tests/archive/fasttrack/ft_score.py base minwait5 waitlo0 stab2 confirm0 dead0 slur0 hold0 nowait nowait_stab2 v73like
