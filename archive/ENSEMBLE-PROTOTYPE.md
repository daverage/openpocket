# Ensemble prototype (v61, poly-test)

`PX_ENSEMBLE_TEST` adds **STRING ENSEMBLE** and **BRASS ENSEMBLE** at the end
of the SYN catalogue. The `poly-test` channel enables both. They test the
first step toward orchestral voices: one note controller driving two
independently rendered timbres.

## Signal path

```text
guitar → pxt_run + pxt_layer (one note: gate, pick, pitch, velocity, expression)
       ├─ lead: the existing sampled voice (CELLO SECTION or TRUMPET), DYNAMICS fixed at noon
       └─ support: two detuned PolyBLEP saws → two-pole low-pass opening with expression
                   → its own attack, the lead's release, optional vibrato
```

| Type | Lead | Support | Support attack | Support vibrato |
| --- | --- | --- | --- | --- |
| STRING ENSEMBLE | Cello section at the guitar's pitch | Violins one octave above, ±0.23% detune | 70 ms | 0.35%, 5.3 Hz, fades in over 0.25 s |
| BRASS ENSEMBLE | Trumpet one octave above the guitar | Horn one octave below the trumpet, ±0.09% detune | 25 ms | none |

The controls are **OCTAVE**, **TONE**, **LAYER**, **RELEASE** and **DRY**.
OCTAVE moves both parts together. TONE sets the lead's brightness. LAYER sets
the support level: at 100 it matches the lead's RMS on the lead loop plus
arpeggio. At 0 the output is the lead voice bit for bit. RELEASE and DRY
behave as on the acoustic voices.

## State and memory

The support layer adds 56 bytes to `px_acoustic`, only in the test build. That
state lives in the SYN union beside the 4,096-byte ensemble buffer: 1,512 of
4,096 bytes are used, so the engine does not grow. The lead keeps its own
sample players, filters and envelope. The support has separate phases,
filters, envelope and vibrato. Neither shares scratch state with the other.
The code runs from flash, like the rest of `px_acoustic.h`. The RAM code
window has 416 bytes free.

## Measurements

`tools/archive/legacy-synth/ensemble_bench.c` renders any SYN type of the test build and prints
RMS and peak. On the lead loop plus arpeggio, every setting gives finite
output. Peaks pass 1.0 only with DRY at 100, which adds the dry guitar, as
on the existing acoustic voices.

| BlackBox v61, idle input | Peak | SYN block |
| --- | ---: | ---: |
| CELLO ENSEMBLE (the lead alone) | 26–30% | 18–19% |
| STRING ENSEMBLE, LAYER 100 | 30–32% | 21% |
| TRUMPET (the lead alone) | 22–26% | 18% |
| BRASS ENSEMBLE, LAYER 100 | 28–29% | 21% |

Each support layer costs about 2–3% of a core. Listening and playing load
are still to be checked on the pedal. The tracking is the shared mono layer
from v60. Its scores apply here: 91.5% of unseen GuitarSet solo notes right,
6.3 ms median to pitch.

## v65: STRING DUO, an instrument per tracked note

STRING DUO is the independent duo. The two notes of the v59 poly tracker
play different instruments. The lower sounding note is a cello at its
pitch: darker, with a slow vibrato after 0.4 s. The upper note is a violin
an octave above: brighter, with vibrato after 0.25 s. Each voice has its
own oscillators, two-pole low-pass and note age. Roles follow the two
pitches at every sample. A lone note plays the cello. The controls are
**CELLO** and **VIOLIN** (levels), **TONE**, **ATTACK** and **RELEASE**. At
50/50 it plays at STRING ENSEMBLE's level. The renderer runs from flash
beside the other poly waveforms. Its tracking is the poly tracker's: 45.7%
of held-out GuitarSet dyads have both notes right.

BlackBox v65: peak 19%, or 23% with Smart Gate, with the SYN block at 16%.
There were no late blocks. Audition render: `build/phase2/string-duo.wav`.

## Next

- A second support timbre (viola, trombone) on the same note, kept light.
- A sampled or waveguide duo: two full lead states do not fit the SYN union
  beside the sample players without moving the bowed or brass delay lines.
  v65 uses light renderers instead.
