# OpenPocket v96 (build 996)

In build 996 the synths are rebuilt so that none of them guesses which note you play. Each one is built straight from the guitar's own waveform, as SQUARE SYNTH and POLY OCTAVE HQ always were. They follow picks, bends, slides and mutes with no tracking delay, and the chord types play chords. POLY SHIFTER now works the same way, and a new DROP TUNE gives drop tuning on a standard-tuned guitar. The rest of v95 is kept.

## SYNTH block

| Type | What it is | Controls |
|---|---|---|
| BANK ORGAN | Drawbar organ (16′ 8′ 4′ 2⅔′ 2′) from POLY OCTAVE HQ's 26 bands. PERC restarts on each pick. Pair it with MOD ROTARY. | REGISTER (JAZZ, FULL, GOSPEL, CHURCH), PERC, SUSTAIN, TONE, LEVEL |
| STRINGS | Bowed pad with an ensemble chorus | TONE, ATTACK, RELEASE, CHORUS, LEVEL |
| CHOIR PAD | The string pad through three vowel formants | VOWEL (AH OH OO EH EE), ATTACK, RELEASE, CHORUS, LEVEL |
| BRASS | Brass section: the filter opens on the pick, then settles | TONE, BITE, RELEASE, WIDTH, LEVEL |
| PIPE ORGAN | Stops from 16′ to 1′ | STOPS (FLUTES, DIAPASON, FULL, MIXTURE), SWELL, RELEASE, TONE, LEVEL |
| SQ BRASS, SQ BASS, SQ FLUTE | SQUARE SYNTH's own square and sub, shaped into a brass lead, a bass or a flute. SQ FLUTE's breath is on ENVELOPE, so ENVELOPE 0 gives a clean note. | CUTOFF, RESONANCE, ENVELOPE, SUB, LEVEL |
| POLY OCT FUZZ | Unchanged | DOWN, UP, FUZZ, TONE, LEVEL |

The chord types process every harmonic of every string, so a chord sounds rich rather than like a clean synth. On a BlackBox one chord pad takes about half of the DSP, BANK ORGAN about a third, and the SQ voices under a tenth.

**Retired:**
- **SYNTH:** ANALOG, ARCADE, ARCADE SPLIT, INSTRUMENT, ENSEMBLE and CHOIR (the note-tracking synths, which missed notes and jumped octaves on a normal pickup).
- **SFX:** CROSS OSC and SQUARE SYNTH HQ.

A preset that uses a retired type loads with that block off, and its settings are kept. The factory synth presets 19–30 now use the new types: SQUARE LEAD, JAZZ ORGAN, STRING PAD, CHOIR AAH, BRASS SECTION, PIPE ORGAN, FULL ORGAN, CHOIR OOH, SQ FLUTE, SQ BASS, SQ BRASS and POLY FUZZ.

## OCTAVE block

- **POLY SHIFTER** now runs on POLY OCTAVE HQ's filter bank, replacing the phase vocoder. Every partial of every string moves by the interval, so chords stay clean. On a low E chord about 60 % of the output lands on the right notes, against about 8 % before.
  - Latency is a few milliseconds instead of 17.
  - Same controls as before: SHIFT −12 to +12 semitones, FINE, MIX and ATTACK.
  - The shifted voice stops near 5 kHz.
  - To tune the whole guitar down, use SHIFT −1 (E♭ standard) or −2 (D standard) at MIX 100.
- **DROP TUNE** (new) moves only the low E string's notes down, so you can play drop-D riffs on a standard-tuned guitar with the same fingering.
  - DROP sets 1–5 semitones (2 = drop D).
  - RANGE sets the highest fret counted as the low string (default 3).
  - MIX defaults to 100.
  - Limits: one pickup mixes the strings, so only notes under about A2 can be recognised as the low string. Harmonics shared with other strings move with the low note.

Preset files to load in the editor: `docs/presets/synth-showcase.json` (the synths) and `docs/presets/tunings.json` (E♭, D and C standard, drop D and drop C♯).

## Memory

Retiring the phase vocoder and the old note tracker frees about 29 KB of DSP memory and 3 KB of low RAM. The firmware is about 10 KB smaller.

## Preserved features

Build 996 keeps v95's noise suppressor, patch volume, SIGNATURE, the Fuzz Face and Rangemaster optimisations, Valve and the global settings.

Install from the [web installer](https://daverage.github.io/openpocket/) and select **v96 · build 996**. The packages are `PocketX_996.fwsc` and `BlackBox_996.fwsc`.
