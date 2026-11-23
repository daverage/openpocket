# OpenPocket editor protocol (SysEx over USB-MIDI)

The web editor (`docs/editor.html`, `docs/pxedit.js`) talks to the pedal with this protocol. The firmware side is
`firmware/px_editor.h`; `tests/edsim.c` runs the same code on a PC for the tests. Protocol version 5 (OpenPocket v48: `FOOTSWITCH`, command 29; version 4: v42, `USB_AUDIO`, command 28; version 3: v38, the cabinet IR commands 24..27; version 2: v37, without them;
version 1: v32 to v36, also without the NAM commands 19..22). A later version only adds commands: an editor written for
an earlier one works unchanged.

## Framing

A request is `F0 7D 4F 50 <cmd> <args...> F7` (`7D` = the non-commercial SysEx ID, `4F 50` = "OP"). Every request
gets exactly one reply with the same header and the same `<cmd>`; a request the pedal does not understand gets no
reply. Every data byte is 7 bit. Send the next request only after the reply to the previous one (the pedal keeps
one incoming frame at a time). While the editor watches (`WATCH`), the pedal also sends `STATE` frames by itself.

| Item | Encoding |
| --- | --- |
| byte | one 7-bit byte |
| string | ASCII 32..126, ended by a 0 byte |
| color | an RGB565 color in 3 bytes: bits 0-6, 7-13, 14-15 |
| preset `P` | 96 bytes in protocol 10, 94 in protocols 7–9, 86 with the 9 blocks of protocol 6. The layout is `name` 14 (ASCII, 0-padded, at most 13 characters), `on` 10 (0/1), `type` 10, `p` 10 x 5 (0..100), `order` 10 (a permutation of 0..9), followed in protocol 10 by `valve_on` (0/1) and `valve_drive` (0..100, 71 for +12 dB). Protocol 10 accepts 94-byte presets and migrates the old COMP type 4 into the Valve fields. Other old presets get Valve off at +12 dB. |

The 10 blocks (protocol 7), by index: 0 GATE, 1 COMP, 2 OCTAVE, 3 SYNTH, 4 SFX, 5 EQ, 6 MOD, 7 DELAY,
8 REVERB, 9 BST, PRE / BOOST (`CAT_BLOCK` gives their labels and names; `INFO` gives the count, so read it: protocol 6 has 9). BST is last in the index and sits right after the gate in the default order. A preset sent with a RANGEMASTER, EP BOOST or MAXIMIZER in SFX (types 10..12, which are tombstones now) has it moved to BST. `order` is the processing and display order.

## Commands

Protocol 6 (v70, OpenPocket Synth) retires the NAM model and cabinet IR commands (19 to 22 and 24 to 27): the pedal gives
them no reply. The other numbers are unchanged.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | protocol (1), firmware string ("v32"), identity string ("PocketX_932"), blocks (12), parameters per block (5), slots (52), name length (13) |
| 2 CAT_BLOCK | block | block, label string ("AMP"), name string ("AMPLIFIER"), types, color |
| 3 CAT_TYPE | block, type | block, type, name string, parameters n, text mask (bit k: parameter k has its own value texts, see `TEXTS`), then n parameter-name strings |
| 4 TEXTS | block, type, k | block, type, k, then 101 strings: the text the pedal shows for values 0..100 ("" = the number itself) |
| 5 STATE | — | slot (0..51), flags (bit 0: the sound differs from the slot's, bit 1: the slot holds a saved preset), focused block, `P` (the current sound) |
| 6 SET | block, what, k, v | block, what, k, v (after clamping). what 0: parameter k = v (0..100); 1: on = v (0/1); 2: type = v. Protocol 10 uses index 10 for Valve with parameter k = 0 (Drive) and on/off. |
| 7 ORDER | 12 bytes | rc (0 ok, 1 not a permutation) |
| 8 NAME | string | the name as stored (13 characters at most, other characters become "?") |
| 9 PUT | `P` | rc (0 ok, 1 invalid). Replaces the current sound (not saved) |
| 10 SELECT | slot | slot, rc. Loads the slot as the current sound (unsaved changes are dropped, as on the pedal) |
| 11 FOCUS | block | block. The pedal shows that block's page |
| 12 LIST | start, count (1..26) | start, count, then per slot: saved (0/1), name string |
| 13 SLOT_GET | slot | slot, saved, `P` (the factory preset when not saved) |
| 14 SLOT_PUT | slot, flags, [`P`] | slot, rc (0 ok, 1 invalid, 2 flash). flags bit 0: `P` follows and is stored as a saved preset (else the slot goes back to its factory preset); bit 1: write the flash now (else only in memory until `COMMIT`) |
| 15 COMMIT | — | rc (0 ok, 2 flash). Writes every slot to the flash (one write, about 0.3 s) |
| 16 SAVE | slot | slot, rc. Saves the current sound into the slot, writes the flash, the slot becomes the current one (as the pedal's SAVE) |
| 17 WATCH | on (0/1) | on. While on, the pedal pushes `STATE` (cmd 5) when the sound, the slot or the focused block changes on the pedal; it stops by itself 5 s after the last request |
| 18 PING | — | DSP load % (0..127), flags (bit 0: bypass, bit 1: second core, bit 2: a block is switched off for the load, protocol 8); since v40.1 also the peak load % since the preset was set and since boot (0..127 each), the late blocks and the blocks at the soft clipper since boot (2 bytes each: bits 0-6, 7-13, wrapping), the bytes of the main stack and of the interrupt stack never used since boot (2 bytes each, in units of 4 bytes); the Bluetooth test image adds the lowest free bytes of its heap and of its deepest task's stack (the same coding). Since v40.2 then a count n and n figures of 2 bytes each (bits 0-6, 7-13): v70 sends one per block, its share of an audio block's time over the last second in %, in the engine's block order (the NAM, layer-split and LOADING figures of v40.2 to v67 are gone). `node tools/op.mjs dsp` reads them per preset |
| 23 DIAG | — | n (5), then n strings: this boot, the last boot, the status line, the link, the trace. Read only. Only the Bluetooth test image answers: other firmware gives no reply. `docs/BLE-TEST.md`, "Reading the pedal from a computer" |
| 28 USB_AUDIO | nothing, or on (0/1) | on (0/1), rc (0 ok, 1 this firmware has no USB audio, 2 flash), state, n (20), then n counts of 4 bytes each (bits 0-6, 7-13, 14-20, 21-27). Without an argument it only reads. With one it sets the USB AUDIO setting and, if it changed, writes it to the flash; see "USB audio" below |
| 29 FOOTSWITCH | nothing, or left, right, stomp (each 127: leave it) | left, right, stomp (0/1), bypass (0/1). In STOMP mode a press can toggle a block (0..9), Valve (10, protocol 10), or BYPASS (12). Unassigned is 15. Protocol 9 adds four **gestures** to set and read in this order: left double tap, left hold, right double tap, right hold. Gestures support the same toggle actions as well as 16 tuner, 17 next preset, 18 previous preset and 19 STOMP/PATCH. Use 127 to leave an action unchanged. The four gestures occupy slot bytes 95..98 in flash. `SAVE` stores the assignments with the preset. STOMP mode (hold CAB on the pedal) is written to flash with the next save. |
| 30 SHED | nothing, or 1 (bring the blocks back) | block mask low 7 bits, block mask bits 7..13, count low 7 bits, count bits 7..13. The blocks the load safeguard switched off (bit b = block b; protocol 8, v75: it switches the heaviest block off for 4 blocks over 85 % in a row, up to 3 per preset, before it bypasses everything), and how many times it has since power-on. Request 1 sets them all to come back at the next audio block (they fade in). A change on the pedal that switches such a block or changes its type, or another preset, clears it too |
| 31 GLOBAL | nothing to read, or 11 values to set | tuner A (0: 440, else 414 + n Hz), input high-pass Hz / 5, five output EQ gains (half-dB steps, 24 flat), limiter (0 off, else -n / 2 dB), tails (0/1), smart gate hush (0..100), lights (bits 0..1: the tuner's lights, 0 blink 1 needle 2 strobe; bit 2: lights off). The global settings (protocol 9, v75), outside the presets. Set in the pedal's memory (the engine takes them at its next block); `COMMIT` writes them with the store (sector 0's spare 16 bytes) |
| 32 TUNER | nothing, or 1 (open the pedal's tuner page: the output is muted) / 0 (close it) | frequency in 0.01 Hz (3 x 7 bits, 0: no note), 1 if the pedal's tuner page is open, the reference A (0: 440, else 414 + n). What the tuner hears now, steadied (the editor's stage view) |

### USB audio (protocol 4)

`USB_AUDIO` reads and sets the pedal's USB AUDIO setting ([USB-AUDIO.md](USB-AUDIO.md)), the same one as the fourth
row of the pedal's MIDI page. After a change the pedal sends this reply, and 150 ms later it leaves USB for one second
and comes back as another USB device, with or without the audio streams. The MIDI port goes away with it: the editor
must connect again (after about 3 s).

`state`: bit 0, the computer has the record stream open; bit 1, it is reading it; bit 2, it has the playback stream
open; bit 3, its playback is sounding.

The 20 counts, since the pedal started (they wrap at 2^28), in this order (a client reads `n` of them):

| # | Count |
| --- | --- |
| 1..3 | record stream: times started, packets sent, frames sent |
| 4 | record ring empty: a packet repeated its last frame. It rises when a change of type, order or model or a save holds the audio for a moment (the sound is silent then) |
| 5 | record ring full: a block was dropped because the computer was not reading |
| 6 | the computer asked for a packet and none was ready |
| 7 | the computer stopped reading for more than 20 ms with the stream still open |
| 8, 9 | packets one frame longer and one frame shorter than the pattern (how the pedal's clock is followed) |
| 10, 11 | the record ring's lowest and highest fill at a block's start, in frames, since the stream started |
| 12..14 | playback stream: times started, packets received, frames received |
| 15, 16 | playback ring empty (the playback starts over) and full (a packet dropped) |
| 17 | the time the last audio block spent on USB audio, in timer ticks (a block has 69,648) |
| 18 | record packets the computer took late: it left a USB frame or more without asking for one. Nothing the pedal can mend; macOS does it now and then in the first second of a stream |
| 19 | playback packets that came late: a USB frame or more without one |
| 20 | audio blocks whose output was asked for before their input (`firmware/px_aorder.h`). They are sent in the right order since v42; before, each was a block sent twice and one lost. It rises about twice a second while a stream is open, and with MIDI traffic, with or without USB audio |

The Bluetooth test image has no USB audio: it answers `0, 1, 0, 0`.

## Preset files

The editor saves presets as JSON: `{"format": "openpocket-presets", "version": 1, "presets": [...]}`, each preset
`{"slot": 1..52, "name", "blocks": [{"block": "AMP", "on", "type": "type name", "params": [5 numbers]}...],
"order": ["GATE", ...]}`. Blocks and types are written by name, so a file stays readable and survives a firmware
that adds types; a single preset is the same object with one preset in `presets`.

Each preset written by OpenPocket v36 carries `"v": 36` (v35: `35`). Older presets are converted on import: without
`"v": 36`, the preset LEVEL (the GT block's 3rd setting, `PRESET LVL`, unused before) is set to 50 = 0 dB; without `"v": 35`,
its delay TIME is moved to v35's scale (the same delay time; REVERSE excepted); amp / drive / cab types from before
v34 (`"RECTIFIED"`...) become the nearest current type. Each change of the delay or the types is listed in the import
notes.
