#!/bin/sh
# Synth2: the original SYN MONO (zero-crossing tracker, before Andrzej's first commit) against today's tracker on the Guitar-TECHS DI sets. Run from the repository root:
#   sh tools/research/tracker_compare/run.sh      (needs ffmpeg, python3 with numpy not required; the data sets in build/datasets/guitar-techs)
set -e
T=tools/research/tracker_compare; S=build/tracker_compare; D=build/datasets/guitar-techs; rm -rf $S; mkdir -p $S/orig $S/pm
git archive "$(git rev-parse 0bebd25^)" engine firmware/px_math.c | tar -x -C $S/orig
for p in P1 P2; do ffmpeg -loglevel error -y -i $D/${p}_techniques/audio/directinput/directinput_PalmMute.wav -ac 1 -ar 44100 -c:a pcm_s16le $S/pm/$p.wav; done
ffmpeg -loglevel error -y -i $D/P1_singlenotes/audio/directinput/directinput_allsinglenotes.wav -ac 1 -ar 44100 -c:a pcm_s16le $S/pm/SN.wav
CF="-O2 -w -fno-builtin -ffp-contract=off"
cc $CF -I$S/orig/engine $T/origmono.c $S/orig/firmware/px_math.c -lm -o $S/origmono
cc $CF -Iengine $T/ev_now.c firmware/px_math.c -lm -o $S/ev_now
S=$S GT=$D python3 $T/score_f0.py $S/origmono $S/ev_now
