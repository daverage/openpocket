#!/usr/bin/env python3
"""Measure the renders of tests/enhance_sim.mjs: loudness (BS.1770 integrated LUFS via ffmpeg), peak, and what each ENHANCE setting changes against the 'off' render of the same loop.
   python3 tests/enhance_sim_measure.py OUTDIR [loop ...]"""
import sys, os, re, subprocess
import numpy as np, soundfile as sf
def lufs(p):
    o = subprocess.run(['ffmpeg', '-nostats', '-i', p, '-af', 'ebur128=peak=true', '-f', 'null', '-'], capture_output=True, text=True).stderr
    return float(re.findall(r'I:\s+(-?[\d.]+) LUFS', o)[-1])
def bands(s, sr):
    P = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2; fr = np.fft.rfftfreq(len(s), 1 / sr); tot = P[(fr > 60) & (fr < 16000)].sum()
    return np.array([10 * np.log10(P[(fr >= a) & (fr < b)].sum() / tot + 1e-14) for a, b in ((60, 150), (150, 400), (400, 1200), (1200, 2400), (2400, 4800), (4800, 9600))])
out = sys.argv[1]; loops = sys.argv[2:] or ['chords', 'arpeggio', 'chug', 'riff', 'lead']
order = ['warm35', 'both35', 'bright35', 'warm100', 'both100', 'bright100', 'warm100_louder3', 'both100_louder3', 'bright100_louder3']
for loop in loops:
    off, sr = sf.read(f'{out}/{loop}__off.wav', dtype='float64'); l0 = lufs(f'{out}/{loop}__off.wav'); pk0 = 20 * np.log10(np.abs(off).max()); b0 = bands(off, sr)
    e = np.sqrt(np.convolve(off ** 2, np.ones(441) / 441, 'same')); act = e > 10 ** (-50 / 20)
    print(f'\n== {loop}: off = {l0:.2f} LUFS, peak {pk0:.1f} dBFS')
    print(f'{"setting":20s} {"loudness":>9s} {"vs off":>8s} {"peak":>7s} {"added re off":>13s}   change in band share dB: 60-150 150-400 400-1.2k 1.2-2.4k 2.4-4.8k 4.8-9.6k')
    for s in order:
        y, _ = sf.read(f'{out}/{loop}__{s}.wav', dtype='float64'); l = lufs(f'{out}/{loop}__{s}.wav'); add = y - off
        ad = 20 * np.log10(np.sqrt(np.mean(add[act] ** 2)) / np.sqrt(np.mean(off[act] ** 2)) + 1e-12); bd = bands(y, sr) - b0
        print(f'{s:20s} {l:9.2f} {l - l0:+8.2f} {20 * np.log10(np.abs(y).max()):7.1f} {ad:10.1f} dB      ' + '  '.join(f'{v:+7.2f}' for v in bd))
