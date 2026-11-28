# POLY SHIFTER

POLY SHIFTER moves chords by −12 to +12 semitones with fine tuning of ±50 cents.
Exact octaves use the same octave voices and wet level as POLY OCTAVE HQ.

## How POLY SHIFTER works

The guitar is split by the same 26-band bank that POLY OCTAVE HQ uses. It runs at 11.025 kHz, behind two half-band stages. Each band holds the guitar near one partial, as a complex signal z = |z|·u.

1. **Measure.** The band's phase step from one bank sample to the next, the angle of u·conj(u_prev), is that partial's frequency at this moment.
2. **Shift.** Each band has a relative phasor w, which turns by the phase step times (RATIO − 1). RATIO is 2^(semitones/12).
3. **Output.** The band gives Re(z·w), which preserves the live envelope and shifts the total phase by RATIO.
4. **Recombine.** The bands are summed and brought back to 44.1 kHz.

Every partial of every string moves in the same way, so a chord stays a chord. Nothing is framed, nothing is resampled and no note is tracked.

ATTACK lets a pick restart the output phasors from the input's own phase, which gives a sharper attack. It's the same pick rule as the other bank effects: the guitar's fast envelope jumping above its slow one.

## What to expect from POLY SHIFTER

* **Latency** is a few milliseconds: the bank's own band delay and the half-band filters. The dry signal isn't delayed.
* **Accuracy.** On a low chord (E2 B2 E3 G#3, six harmonics each), 56–68 % of the output lands on the shifted partials. A perfect signal scores 84 % with the same measurement, and the v75 phase vocoder about 8 %. A single note lands within 10 cents for every semitone from −12 to +12. On the pedal (BlackBox, build 996), a 220 Hz tone comes out at 330 Hz at +7 and 110 Hz at −12. On a chord DI the whole spectrum moves by exactly the interval, at the dry level.
* **Bandwidth.** The shifted voice stops near 5 kHz: bands whose shifted frequency would pass the bank's limit fade out between 4.2 and 5 kHz. POLY OCTAVE HQ has the same limit.
* **Level.** The wet voice is matched to the dry signal for each semitone (`PS_GAIN_TABLE`, measured on the fast-play DI).
* **Tuning the whole guitar down.** Use SHIFT −1 (E♭ standard), −2 (D standard) or −4 (C standard) at MIX 100. The presets are in `docs/presets/tunings.json`.


Older presets that use Drop Tune open with the pitch block bypassed.
