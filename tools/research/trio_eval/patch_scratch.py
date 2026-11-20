#!/usr/bin/env python3
"""Synth2 stage D: put Felucca TRIO into a SCRATCH copy of the engine (never the production tree), as ENSEMBLE patch 45 (the BRSLEAD slot becomes
Felucca's CHIP CHOIR, preset as upstream). usage: patch_scratch.py SCRATCH_DIR   (SCRATCH_DIR holds copies of engine/ and third_party/)"""
import re, sys, shutil
d = sys.argv[1]
shutil.copy(__file__.rsplit('/', 1)[0] + '/eng_trio.c', d + '/third_party/felucca-engines/src/eng_trio.c')
A = d + '/engine/px_synth_adapter.h'; t = open(A).read()
t = t.replace('#include "../third_party/felucca-engines/src/eng_wheel.c"\n', '#include "../third_party/felucca-engines/src/eng_wheel.c"\n#include "../third_party/felucca-engines/src/eng_trio.c"\n', 1)
old = "s->eng = arc ? &ENG_LOFI : (ens && pa->eng) ? &ENG_WHEEL : &ENG_ANALOG;"
assert old in t
t = t.replace(old, "s->eng = arc ? &ENG_LOFI : (ens && pa->eng == 2) ? &ENG_TRIO : (ens && pa->eng) ? &ENG_WHEEL : &ENG_ANALOG;")
old = "        if (pa->eng) {"
assert old in t
t = t.replace(old, "        if (pa->eng == 2) {                                                                 /* TRIO: TONE moves the band-pass centre (CUT +-40) */\n            t0->p[P_E5] = (int16_t)px_s2_clamp127((float)pa->e[5] + (m1 - 0.5f) * 80.0f);\n        } else if (pa->eng) {", 1)
open(A, 'w').write(t)
P = d + '/engine/px_synth_patches.h'; s = open(P).read()
s = re.sub(r'\{"BRSLEAD",[^\n]*', '    {"CHOIR",   {1, 0, 12, 7, 1, 62, 95, 40}, {70, 90, 110, 85}, 30, 0, 64, 0, 0, 2},   /* Felucca CHIP CHOIR */', s, count=1)
open(P, 'w').write(s)
