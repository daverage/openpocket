#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Sets each factory preset's LEVEL (the gate block's 3rd setting, 50 = 0 dB, 0.3 dB a step) in engine/px_dsp.c so that every preset plays as loud as the guitar alone
# (mean over the five bundled loops, BS.1770: tools/research/preset_levels.c). Two passes; INIT is left at 50.
#   python3 tools/research/preset_levels.py [passes=2]
import csv, io, os, re, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "engine", "px_dsp.c")
def measure():
    exe = os.path.join(ROOT, "build", "preset_levels"); os.makedirs(os.path.dirname(exe), exist_ok=True)
    subprocess.run(["cc", "-O2", "-w", "-Iengine", "-Itools/research", "tools/research/preset_levels.c", "engine/px_dsp.c", "firmware/px_math.c", "-lm", "-o", exe], cwd=ROOT, check=True)
    return list(csv.DictReader(io.StringIO(subprocess.run([exe], cwd=ROOT, check=True, capture_output=True, text=True).stdout)))
src = open(SRC).read(); orig = src
try:
    for p in range(int(sys.argv[1]) if len(sys.argv) > 1 else 2):
        rows = measure(); worst = 0
        for r in rows:
            if r["name"] == "INIT" or r["name"].startswith("INIT "): continue
            lvl, mean = int(r["level"]), float(r["mean_db"]); worst = max(worst, abs(mean))
            new = max(0, min(100, lvl - round(mean / 0.3)))
            pat = re.compile(r'(\{"%s",\s+)(\d+)(,)' % re.escape(r["name"]))
            a = src.index("static const fact_spec FACT[] = {")                      # only the FACT table: effect names repeat in the catalogue tables above it
            b = src.index("};", a)
            if not pat.search(src[a:b]): sys.exit("preset_levels: %s not found in FACT" % r["name"])
            src = src[:a] + pat.sub(lambda m: m.group(1) + str(new).rjust(2) + m.group(3), src[a:b], count=1) + src[b:]
        open(SRC, "w").write(src)
        print("pass %d: the largest mean error %.2f dB" % (p + 1, worst))
    rows = measure()
    print("final: the largest mean error %.2f dB" % max(abs(float(r["mean_db"])) for r in rows if not r["name"].startswith("INIT")))
except BaseException:
    open(SRC, "w").write(orig); print("preset_levels: failed, engine/px_dsp.c restored", file=sys.stderr); raise
