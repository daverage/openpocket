# What's new in OpenPocket v49: a clean HQ octave, a new note tracker

The details and every measurement are in [NOTE-TRACKING.md](archive/NOTE-TRACKING.md).

## The HQ octave without the gargle, at half the DSP

POLY OCTAVE HQ, STRINGS HQ, CHOIR HQ, ORGAN HQ, CHURCH ORGAN HQ and POLY OCT FUZZ share a new band bank: 29 bands from 60 Hz to 2 kHz, running at a quarter of the rate.

- **What was wrong:** v48's bands were wider than a guitar note's harmonic spacing above a few hundred Hz. Doubling several partials together made the octave gargle, warble and drop out.
- **Single notes:** now come out much cleaner. On the host, 91% of the octave's energy lands where it should, against 85%; at 440 Hz it's 99.7% against 93%.
- **Octave down:** chords and repicks are more even.
- **DSP:** the cost roughly halves. On the pedal, STRINGS HQ dropped from 59% to 34% of a core and POLY OCT FUZZ from 70% to 35%.
- **Levels:** the octave voices stay within about 1 dB of v48.

## Trumpet and cello follow your playing

TRUMPET, TRUMPET MODEL, CELLO and CELLO SECTION now find notes with a new tracker built on the same band bank. It pairs harmonic analysis with a check on the waveform itself for the low strings.

| Notes right | v48 | v49 |
| --- | ---: | ---: |
| GuitarSet solo takes (real guitar pickup, annotated) | 68% | 85% |
| Your M-VAVE recording | 54% | 78% |
| Your low-string riffs | 44% | 84% |

Half the notes reach the right pitch within about 8 ms of the pick, and 90% within about 41 ms. v48 needed 16 ms and 79 ms. One note at a time, as before. On the pedal the voices use 27–40% of a core.

## The cello answers sooner

CELLO's bow now starts faster, and the string is already moving when the bow catches it. CELLO SECTION skips most of each recording's slow bow-in. The first sound now comes 10–54 ms after the pick for CELLO, against 43–227 ms before, and 25–39 ms for CELLO SECTION, against 37–61 ms. The bowed note still swells to full level over a few hundred ms.

## Louder HQ octaves, a brighter trumpet

- **POLY OCTAVE HQ:** its octave voices were about 10 dB (up) and 3 dB (down) under the dry guitar, as they had been in v48. They now match the standard POLY OCTAVE.
- **TRUMPET:** now plays its loud (fortissimo) recordings for everything but the gentlest picks. The soft recordings are close to pure tones, which made it sound like a flute. TONE is fully open from the middle up.

## HQ synths that track, a steady 2'

- **BASS SYNTH HQ, PWM SYNTH HQ and SQUARE SYNTH HQ** now follow the new tracker. On the owner's low-string riffs, 84% of notes are right, against 61% with their old tracker. They use about 21% of a core, against about 10%. The standard BASS SYNTH, PWM SYNTH and SQUARE SYNTH stay the light options.
- **CHURCH ORGAN HQ's 2'** no longer warbles: it comes from the band bank, as the 4' and 16' do.

## Steadier under chords

- **Accidental chords:** a second note under TRUMPET, CELLO SECTION or the HQ synths makes them jump about 40% less (measured on GuitarSet's comping takes).
- **BASS SYNTH HQ:** its lower sub no longer drops below 35 Hz, where a low E's would be 20.6 Hz.

## No squeak on the cello's attack

A low note could speak for a moment on one of its high partials before its fundamental settled, which the bowed CELLO played as a squeak. The tracker now waits 7 ms before a high candidate may start a note, and lets the fundamental take over at once when it arrives.

## The octave bank rests when its voices are off

STRINGS HQ, CHOIR HQ, the HQ organs, POLY OCT FUZZ and the HQ PITCH octaves ran their band bank even with every octave level at zero. It now stops 64 samples after the last voice reaches zero and clears its state. A voice turned back up starts from silence and builds up with the bands over 10–17 ms, so it fades in instead of stepping in on stale filter state. On the pedal (BlackBox, AMP and CAB off), STRINGS HQ with OCT at 0 uses 14% of a core instead of 34%.

## A new TRUMPET

TRUMPET sounded more like a flute. Its VSCO recordings had about half a real trumpet's brightness. v49 rebuilds it from the University of Iowa's non-vibrato trumpet recordings (mf and ff, sounding E3, C4 and C5):

- **The attack is the recording.** Each note keeps its recorded attack, up to 20 ms past where the attack ends.
- **The sustain is synthesised.** Up to 64 harmonics are fitted to the recording just before the splice, so the 20 ms crossfade meets the same waveform. Their levels come from the recording's steady part. The result is one loop of a whole number of cycles, within 1 cent of the recording's pitch.

The existing sample player plays it, so TRUMPET still uses 20% of a core. The held tone's brightness is within 4% of the recordings' (1,290–1,490 Hz). The old trumpet's was 830–920 Hz. The trumpet data shrank from 95,057 to 24,628 bytes, so the firmware image is 70 KB smaller. Picks below a quarter strength use the mf take. That is 14–31% of the picks in the owner's recordings, mostly light or legato ones; normal picks use ff. Pick to trigger is 10–19 ms and trigger to sound 4–5 ms (host, plucked DI).

## TRUMPET MODEL plays above the 9th fret

The owner found that TRUMPET MODEL did not play past the 9th fret of the G string. The tracker had the right pitch on every fret; the brass model itself stopped oscillating. Above sounding C5 its lip needed more blowing pressure than expression gave, and above G♯5 it fell silent even at full expression (plucked level −19 dB at fret 8, −45 dB at fret 12, −62 dB at fret 15). As for a player, the blowing pressure now rises 1.5 times an octave above sounding E4 (`PXA_BR_P0`, `PXA_BR_PUP`), with the extra level taken back out. The pitch table was re-solved (`acoustic_test --calibrate`). Plucked notes from the open G string to fret 15 now come out at −19.5 to −20.5 dB and in tune. The voice's level against BRASS was matched again.

## Also in v49

STOMP mode and the four acoustic types from v48 are included (see [WHATS-NEW-v48.md](WHATS-NEW-v48.md)).
