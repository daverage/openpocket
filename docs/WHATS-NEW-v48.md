# What's new in OpenPocket v48: STOMP mode, trumpet and cello played from the guitar

## STOMP mode: footswitches that switch effects

**Hold CAB** (0.8 s) to change between PATCH mode (the footswitches change presets, as before) and STOMP mode. In STOMP
mode each footswitch switches what the preset assigns it: a block on or off, or **BYPASS** (the dry guitar with the DSP
off; the footer turns red). Both footswitches together still open the tuner. Presets do not change in STOMP mode: hold
CAB again to go back to PATCH.

To assign a footswitch: in STOMP mode, hold it and press a block button (MOD, AMP, CAB, DLY, REV), or press FX to step
through DRV, SFX, SYN, PIT, CMP, GT, EQ, BYPASS and nothing. The footer on PLAY shows both (`STOMP   L: DRV   R: BYP`).
The assignment belongs to the preset: hold PUSH to save it with the preset. STOMP mode itself is kept in the flash with
the next save (switching modes never writes the flash, so it never mutes the sound). The web editor's protocol can read
and set both (`FOOTSWITCH`, protocol 5: [EDITOR-PROTOCOL.md](EDITOR-PROTOCOL.md)); the editor page has no control for it
yet.

A short CAB press still opens the CAB page, now at its release (as PUSH, HOME and FX do), since it can be held.

## Trumpet and cello played from the guitar

Four new SYN types play an instrument from your guitar instead of reshaping the guitar's sound. The pedal follows the
note you play (one note at a time), with its bends, slides and slurs, and turns your pick strength and the string's
level into the instrument's dynamics. The research, the host prototype and its measurements are in
[REALISTIC-INSTRUMENT-SYNTHESIS.md](research/REALISTIC-INSTRUMENT-SYNTHESIS.md) and
[REALISTIC-INSTRUMENT-PROTOTYPE.md](research/REALISTIC-INSTRUMENT-PROTOTYPE.md).

| Type | What it is | Register at OCTAVE 0 |
| --- | --- | --- |
| TRUMPET | recorded trumpet (VSCO 2 CE, CC0), soft and loud, 3 roots | an octave above the guitar |
| TRUMPET MODEL | physical model of lips and a brass bore (after STK Brass) | an octave above the guitar |
| CELLO | physical model of a bowed string and a cello body (after STK Bowed) | the guitar's pitch |
| CELLO SECTION | recorded cello section (VSCO 2 CE, CC0), soft and loud, 3 roots | the guitar's pitch |

Settings: OCTAVE (−1 / 0 / +1), then TONE or BRIGHT or BOW, DYNAMICS or VIBRATO, RELEASE, and DRY (clean guitar mixed in).
The full list is in [SOUNDS-AND-CONTROLS.md](SOUNDS-AND-CONTROLS.md).

## How it follows the guitar

- **The note** comes from the HQ tracker that BASS SYNTH HQ uses, run twice: on its own low band, and on 200 Hz to
  1 kHz for notes above about 300 Hz, where the low band's filters bury the note. On the host, the bundled lead loop
  came out 26 of 27 notes right against its transcription (the miss is a note that overlaps the one before).
- **A new pick** is heard in the attack's click, so a softer repick of a ringing string still starts a new note.
- **Latency:** the instrument starts once the pitch is locked, about 30 to 60 ms after the pick on the host (longer on
  the low E). Use DRY to keep the guitar's own attack under it.
- **Chords** are not followed: play single notes.

## What it costs

- Flash: 152 KB of samples (8-bit mu-law; trumpet at 22.05 kHz, cello section at 11.025 kHz).
- RAM: none added. The voices share the SYN block's existing buffers.
- DSP time has not yet been measured on the pedal. PING and the DSP load page show it. If a preset with one of these
  types and a heavy amp runs out of time, turn other blocks off.
- The Bluetooth test image has no room for the samples: there, TRUMPET and CELLO SECTION play the models.

Presets, other sounds and their indices are unchanged. The new types come after POLY OCT FUZZ.
