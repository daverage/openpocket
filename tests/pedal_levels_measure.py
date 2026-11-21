#!/usr/bin/env python3
"""Loudness (BS.1770 integrated LUFS via ffmpeg) of each pedal setting minus the same loop with the block off, averaged over the loops, and the worst loop. Unity = 0.0 dB.
   python3 tests/pedal_levels_measure.py OUTDIR"""
import sys, re, glob, os, subprocess, collections
def lufs(p):
    o = subprocess.run(['ffmpeg', '-nostats', '-i', p, '-af', 'ebur128', '-f', 'null', '-'], capture_output=True, text=True).stderr
    return float(re.findall(r'I:\s+(-?[\d.]+) LUFS', o)[-1])
out = sys.argv[1]; res = collections.defaultdict(dict)
for f in sorted(glob.glob(out + '/*__*.wav')):
    loop, name = os.path.basename(f)[:-4].split('__'); res[name][loop] = lufs(f)
off = res.pop('off')
print(f'{"setting":18s} {"mean vs off":>12s} {"worst loop":>11s}   per loop')
for name in res:
    d = {l: res[name][l] - off[l] for l in off}; m = sum(d.values()) / len(d); w = min(d.values())
    print(f'{name:18s} {m:+11.1f} dB {w:+9.1f} dB   ' + ' '.join(f'{l}:{v:+.1f}' for l, v in d.items()))
