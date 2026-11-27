# POLY SHIFTER and DROP TUNE (PITCH block, v96)

Two pitch effects for chords, built on POLY OCTAVE HQ's filter bank. Neither one works out which note you play.

- **POLY SHIFTER** shifts everything by an interval: SHIFT −12..+12 semitones, FINE ±50 cents, MIX, ATTACK.
- **DROP TUNE** moves only the low E string: DROP 1..5 semitones, RANGE, MIX.

Source: `engine/px_banksyn.h` (`ps_*`, `dt_*`). Tests: `tests/engine/pshift_test.c` and `tests/engine/drop_test.c`.

Up to v95, POLY SHIFTER was a phase vocoder (Laroche and Dolson 1999, 1024-point frames). It smeared low chords, because the strings' partials sat about one FFT bin (43 Hz) apart. It also added 17 ms of latency and used about 29 KB of RAM. v96 replaced it.

## How POLY SHIFTER works

The guitar is split by the same 26-band bank that POLY OCTAVE HQ uses. It runs at 11.025 kHz, behind two half-band stages. Each band holds the guitar near one partial, as a complex signal z = |z|·u.

1. **Measure.** The band's phase step from one bank sample to the next, the angle of u·conj(u_prev), is that partial's frequency at this moment.
2. **Shift.** Each band has an output phasor w, which turns by the phase step times the RATIO (2^(semitones/12)).
3. **Output.** The band gives |z|·Re(w): the same partial at RATIO times its frequency, with its own envelope.
4. **Recombine.** The bands are summed and brought back to 44.1 kHz.

Every partial of every string moves in the same way, so a chord stays a chord. Nothing is framed, nothing is resampled and no note is tracked.

ATTACK lets a pick restart the output phasors from the input's own phase, which gives a sharper attack. It's the same pick rule as the other bank effects: the guitar's fast envelope jumping above its slow one.

## What to expect from POLY SHIFTER

* **Latency** is a few milliseconds: the bank's own band delay and the half-band filters. The dry signal isn't delayed.
* **Accuracy.** On a low chord (E2 B2 E3 G#3, six harmonics each), 56–68 % of the output lands on the shifted partials. A perfect signal scores 84 % with the same measurement, and the v75 phase vocoder about 8 %. A single note lands within 10 cents for every semitone from −12 to +12. On the pedal (BlackBox, build 996), a 220 Hz tone comes out at 330 Hz at +7 and 110 Hz at −12. On a chord DI the whole spectrum moves by exactly the interval, at the dry level.
* **Bandwidth.** The shifted voice stops near 5 kHz: bands whose shifted frequency would pass the bank's limit fade out between 4.2 and 5 kHz. POLY OCTAVE HQ has the same limit.
* **Level.** The wet voice is matched to the dry signal for each semitone (`PS_GAIN_TABLE`, measured on the fast-play DI).
* **Cost.** 39 % of an audio block on a BlackBox, against 31 % for POLY OCTAVE HQ, with no late blocks. RAM: about 0.5 KB of state beside the bank.
* **Tuning the whole guitar down.** Use SHIFT −1 (E♭ standard), −2 (D standard) or −4 (C standard) at MIX 100. The presets are in `docs/presets/tunings.json`.

## DROP TUNE

DROP TUNE gives drop tuning on a standard-tuned guitar: only the low E string's notes move down, and the other five strings stay. One pickup mixes all six strings, so the pedal can't tell which string a note is on in general. A note under about A2, though, can only be on the low string (E2 to G♯2, its frets 0 to 4).

1. **Find the low note.** The same bank finds the lowest band holding a quarter of the loudest band's level. That band's phase step gives the low note's frequency.
2. **Decide whether it's the low string.** It is when the note is under the RANGE fret, plus a quarter tone. It stops counting only well above that, because the reading wobbles by about a semitone as partials beat. Anything under 72 Hz is ignored, so mains hum isn't mistaken for a note.
3. **Move its harmonic series.** The bands holding that note's harmonics 1 to 6 are turned by the DROP ratio, exactly as POLY SHIFTER turns a band. A band's membership is judged by its own measured frequency, not its centre: the low bands are wide, and one can hold both the 1st and 2nd harmonics. Membership fades in and out over about 10 ms. Every other band is given back as it is.

**On the pedal** (build 996, DROP 2, synthetic guitar-like notes):

| Played | Result |
|---|---|
| E2 | D2, with the old pitch 29 dB down |
| G2 | F2, with the old pitch 40 dB down |
| A2 on the A string | kept |
| E3 on the D string | kept |
| Open E + A + D | D2 + A2 + D3 (the drop-D power chord), with E2 14 dB down |

**Limits:**
* A partial that another string's note shares moves with the low note. An open E chord's E3 and B3 are also the low E's 2nd and 3rd harmonics.
* In the open E–A–D chord, A2 comes out about 10 dB quieter, because it sits only a fourth above E2.
* Fretted low-string notes above RANGE are left alone.
* MIX defaults to 100. At 50 you'd hear the old and new low notes together.
* Cost: 36 % of a block on a BlackBox.

## Compared with the other pitch effects

* **WHAMMY and HARMONIZER** use two crossfading delay-line taps. They're cheaper and sound right on single notes, but add a flutter to chords.
* **POLY OCTAVE HQ** is still the choice for octaves: it uses the same bank with exact octave arithmetic (z²/|z| and the half-phase).
* **POLY SHIFTER** is the one for other intervals on more than one note.
* **DROP TUNE** is the only one that changes a single string.
