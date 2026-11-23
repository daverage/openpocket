# Prompt: the web editor from a phone, over Bluetooth

Paste everything below the line into a new Claude Code session in this repository (branch `synth4` or a branch from it).

---

## Goal

Make the OpenPocket web editor (`docs/editor.html`, served on GitHub Pages) work from a **mobile phone over Bluetooth Low Energy**, as it works today from a computer over USB: connect, see and edit the live preset, follow the pedal's own changes, back up and restore presets. USB must keep working unchanged. Stage view (`docs/edstage.js`) is the part most wanted on a phone.

## What exists (read these first, in this order)

* `docs/EDITOR-PROTOCOL.md`: the editor protocol (SysEx `F0 7D 4F 50 ... F7`, 7-bit data, protocol 9). Requests and replies are single SysEx messages; the longest requests are about 160 bytes (`firmware/px_editor.h`, `ed_service`), a full preset reply about 110 bytes, `STATE` pushes while watched.
* `docs/pxedit.js`: the protocol client. It talks to a Web MIDI-shaped object (`access.inputs / outputs`, `send`, `onmidimessage`); `tools/midi-node.mjs` is a second implementation of that shape for Node. A new transport only has to provide the same shape.
* `docs/editor.js`, `docs/edcore.js`: connect, ping every second, re-watch, lost / found handling.
* `firmware/px_ble_midi.h`, `firmware/ble/`, `third_party/jieli-ble/`, `docs/BLE-TEST.md`, `docs/BLUETOOTH.md`, `docs/HARDWARE-HANDBOOK.md` section 11, `docs/STOCK-FIRMWARE.md` sections 10.6, 15.5, 18.5, 19: what is known about the radio on this chip (JieLi AC791N) and the stock firmware's own use of it (its second GATT service `AE40`, used by the M-VAVE app).
* `firmware/px_midi.h`, `firmware/px_midi_io.h`: how MIDI messages reach the engine today.

## Facts to start from (verify each; do not trust this list)

* The **normal firmware has no Bluetooth**. Bluetooth MIDI exists only in a separate test image (`make ble`, `OP_BLE=1`): it handles Program Change and Control Change; **SysEx is skipped**, so the editor cannot work over it yet.
* An earlier build with the radio on pushed nearly every preset into the overload bypass (150 % load) until it was rebuilt the stock firmware's way (v41); a phone sending a stream of Control Change made the sound crackle. The DSP budget, RAM (`firmware/app.ld`: measure the current RAM code window headroom from the link map, do not rely on a remembered figure, it was under 1 KB at v57; `LOWRAM` at `0x01C00000` is `0x6000` long while the comment beside it says `..0x01C08000` is free, so check which is right; the radio needs its own RAM and more) and the shedding safeguard (`firmware/px_shed.h`) all matter.
* Phones (from memory, **check against current documentation and on the actual phone**): iOS Safari and every iOS browser use WebKit, which has neither Web MIDI nor Web Bluetooth; iOS can use Web Bluetooth only through a browser such as Bluefy. Android Chrome has Web Bluetooth and Web MIDI; whether Chrome's Web MIDI lists a BLE-MIDI device that is paired in Android settings must be tested. Desktop Chrome and Edge can use a BLE-MIDI device that the OS has paired.
* The editor writes the preset store and can erase presets, so **an open radio is a security question**: anyone in range could edit the pedal.

## Work, in order. Stop and report after each step.

1. **Find out the user's phone and browser** (ask: Android or iPhone, which browser). Everything below depends on it. If it is an iPhone, the realistic options are Bluefy (Web Bluetooth) or a small native wrapper; say so and ask before building.
2. **Feasibility test with the existing test image**, no firmware changes: flash the BLE test image only on the user's explicit word (`openpocket-flash` skill: dry run first), connect from the phone (expect flaky first connections: `docs/BLE-TEST.md` records disconnection reason `3E` on a Mac's first attempts, so retry before concluding a path fails; bonds are RAM-only, so re-pair after every power cycle), and record what each candidate path can do: (a) Web MIDI on the phone sees the pedal as a BLE-MIDI device; (b) Web Bluetooth can connect to the BLE-MIDI GATT service (`03B80E5A-EDE8-4B33-A751-6CE34EC4C700`); (c) what MTU is negotiated (the editor's messages are far longer than the 20-byte default); (d) throughput: the central picks the connection interval and the pedal does not ask for another, so time a full preset read, a bank dump, and a minute of the editor's 1 s ping / re-watch / `STATE` pushes. Report a table.
3. **Choose the transport** with the user, from these two (give a recommendation and the cost of each):
   * **A. Standard BLE-MIDI with SysEx** (the editor page unchanged where the phone's Web MIDI sees the device): the firmware reassembles SysEx that arrives split over several BLE-MIDI packets (start and end timestamp bytes, running status, several messages in one write: `px_ble_midi.h` already parses the rest) and splits its own replies and `STATE` pushes the same way, honouring the MTU.
   * **B. A custom GATT service used through Web Bluetooth** (a `docs/pxble.js` transport with the same shape `pxedit.js` expects, plus a service on the pedal with its own framing, chunking and flow control): works where Web Bluetooth exists, independent of MIDI support, more work on both sides.
4. **Firmware**: SysEx in and out over the chosen link into the existing `ed_service` path (the same protocol code, no second implementation); Bluetooth stays **off by default**; pairing / authorisation so that a stranger cannot connect and write (a pairing step on the pedal's MIDI page, or a code shown on the pedal's screen; it must work on both the Pocket X and the BlackBox screens; whether bonds are persisted is a decision for the user, because it means flash writes and touches the store format, so bring it to them instead of settling it); the radio must not change the DSP: measure the DSP load and peak with `tools/research/pedal_load.mjs` and the editor PING, on a preset that already uses 60 to 70 %, with the radio off, on and connected, and with the editor polling and pushing; check `ed_w` timing (the editor re-watches every second, the pedal stops pushing 5 s after the last request) and the BLE connection interval.
5. **Web page**: the transport and a "Connect over Bluetooth" path that is shown only where it can work; the Stage view and the editor layout checked at phone width; the lost / found handling of `editor.js` for a link that drops and comes back (phones sleep the radio); touch-sized targets in Stage view; Web Bluetooth needs a user gesture and a secure origin (GitHub Pages is fine, plain `make sim` over http on a LAN address is not); a clear message on iPhone Safari that explains why it cannot connect and what to use instead.
6. **Tests that need no pedal**: extend the simulator (`tools/sim.mjs`, `tests/edsim.c`) or add a BLE-MIDI framing simulator so that SysEx split over 20-byte packets and over a larger MTU, a dropped link and a long reply are tested on the host; keep `make test` green; add the framing to `tests/`.
7. **Docs**: `docs/BLUETOOTH.md` (what works, how to pair, which phones and browsers), `docs/EDITOR-PROTOCOL.md` (the transports), `README.md`.

## Definition of done

Name the phone and browser, and list what was seen working on it: connect, edit a knob and see the pedal change, change a preset on the pedal and see the editor follow, drop and restore the link, back up and restore. Anything not tried on the phone is reported as untried.

## Rules

* **Never flash or write to the pedal without the user's explicit instruction** for that pedal and package; dry-run first. Never save presets to it: a save converts the store to format 7.
* Keep USB behaviour, the editor protocol and the existing tests unchanged. Do not copy code from other firmware: write from the specifications (Bluetooth SIG, Apple's BLE-MIDI specification, the Web Bluetooth and Web MIDI specifications) and from this repository.
* Report what is measured on the pedal and what is only measured on the host. Do not say it works on a phone until it has been tried on that phone.
* Everything stays uncommitted until the user asks for a commit.
