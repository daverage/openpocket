#!/usr/bin/env python3
"""Synth2 choir evaluation: put the original FM-1 synth's VOICE engine (eng_formant.c, GPL-3.0-only) into a SCRATCH copy of the engine as INSTRUMENT patch 45 (the BRSLEAD slot
becomes Felucca's CHOIR AAH, as upstream), with the ENV_LIN table its TALK morph needs (generated from Felucca's tools/gen_tables.py formula). usage: patch_scratch.py SCRATCH_DIR"""
import re, sys, os
d = sys.argv[1]; here = os.path.dirname(os.path.abspath(__file__)); FS, CTL = 44100, 32
os.makedirs(d + '/third_party/felucca-host', exist_ok=True)
open(d + '/third_party/felucca-host/eng_formant.c', 'w').write(open(here + '/eng_formant.c').read())
ms = [10000 ** (v / 127) for v in range(128)]
open(d + '/third_party/felucca-host/fe_env_tables.h', 'w').write('static const uint32_t ENV_LIN[128] = {' + ','.join(str(max(1, int((1 << 24) * CTL / (m * FS / 1000)))) for m in ms) + '};\n')
A = d + '/engine/px_synth_adapter.h'; t = open(A).read()
t = t.replace('#include "../third_party/felucca-engines/src/eng_wheel.c"\n', '#include "../third_party/felucca-engines/src/eng_wheel.c"\n#include "../third_party/felucca-host/fe_env_tables.h"\n#define FELUCCA_CPU2 0\n#include "../third_party/felucca-host/eng_formant.c"\n', 1)
old = "s->eng = arc ? &ENG_LOFI : (ens && pa->eng) ? &ENG_WHEEL : &ENG_ANALOG;"; assert old in t
t = t.replace(old, "s->eng = arc ? &ENG_LOFI : (ens && pa->eng == 3) ? &ENG_FORMANT : (ens && pa->eng) ? &ENG_WHEEL : &ENG_ANALOG;")
m = re.search(r"        if \(pa->eng\) \{([^\n]*)\n", t); assert m
t = t[:m.start()] + "        if (pa->eng == 3) {                                                                 /* VOICE: TONE shifts the formants (the singer's size), +-5 semitones */\n            t0->p[P_E3] = (int16_t)((int)pa->e[3] + (int)((mt - 0.5f) * 10.0f + (mt < 0.5f ? -0.5f : 0.5f)));\n        } else if (pa->eng) {" + m.group(1) + "\n" + t[m.end():]
open(A, 'w').write(t)
P = d + '/engine/px_synth_patches.h'; s = open(P).read()
s = re.sub(r'\{"BRSLEAD",[^\n]*', '    {"CHOIR",   {0, 0, 0, 0, 40, 22, 60, 0}, {85, 90, 115, 95}, 0, 0, 64, 0, 0, 3, 0, 0, 0},   /* Felucca VOICE: CHOIR AAH */', s, count=1)
open(P, 'w').write(s)
