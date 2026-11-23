# The web editor over Bluetooth: what was built, what was measured, where it stopped

Written 2026-11-23, on branch `bluetooth` (cut from `main` at `f063993`, v76). Nothing here is committed. The pedal is back on the
normal v76 firmware (`docs/firmware/BlackBox_976.fwsc`, SHA-256 `bf9526c6…`).

**Goal.** Use the web editor (`docs/editor.html`, Stage view most of all) from an Android phone in Chrome over Bluetooth Low Energy,
as it works from a computer over USB. USB must not change.

**Outcome in one paragraph.** The page side is done and tested on the host: a transport that gives `pxedit.js` the shape of a Web
MIDI access over a GATT service of our own (`docs/pxble.js`, `tests/test_pxble.mjs`). The pedal side is not: the radio runs, advertises
and hears scanners, but with it started the firmware crashes or hangs on most boots, in ways that look like memory or register
corruption, and the cause is not found. Nothing was flashed that wrote presets. The normal firmware was never changed.

## 1. Decisions taken

- **Phone:** Android with Chrome (the user's). Chrome has Web Bluetooth and Web MIDI.
- **Transport recommended: a custom GATT service through Web Bluetooth** (option B in `BLUETOOTH-EDITOR-PROMPT.md`), not standard
  BLE-MIDI with SysEx. Reasons: it does not depend on Chrome's Web MIDI listing a BLE-MIDI device (unverified), the stock M-VAVE app
  does the same (see 4), and `ed_service` stays the one protocol implementation because the SysEx travels unchanged inside the tunnel.
  Option A stays worth a five-minute check on the phone (`chrome://bluetooth-internals`, Android Bluetooth settings, then the
  editor's port list) once a radio is stable. Not done.
- **Security:** not started. Plan: radio off by default, a pairing code on the pedal's screen. Whether bonds persist is the user's
  decision (flash writes, store format).
- **Fallback that needs no firmware work:** the pedal is class-compliant USB MIDI, and Chrome on Android supports Web MIDI over USB
  with a USB-C cable and an OTG adapter. Untried.

## 2. What exists now

**Finished, host-tested, independent of the radio problem**
- `docs/pxble.js`: `splitMessage` / `Reassembler` (SysEx F0..F7 is its own framing: data is 7-bit, so no extra bytes), `makeAccess(link)`
  (a Web MIDI-shaped `{inputs, outputs}` with one port each way named "OpenPocket (Bluetooth)", which matches `PORT_RE`; lost-link and
  `reconnect()` handling), `bluetoothSupport()`, `requestBluetoothAccess()` and `connectDevice()` for the real Web Bluetooth side.
  Service `d0422e58-4744-4a69-9ddc-ce8f5e7d2a16`, RX `4d6bfa59-…` (browser writes, no response), TX `d8d0081c-…` (pedal notifies).
  Not yet wired into `editor.js` (the "Connect over Bluetooth" button, the iPhone message, lost/found for a sleeping radio).
- `tests/test_pxble.mjs` (added to `tests/run_tests.sh`, passes): the real `Editor` against `edsim` (the firmware's real editor code)
  through a fake radio, pieces of 20, 185, 512 and 7/3 bytes; noise, cut-off messages, link drop and return.
- Findings that shape the firmware side: requests are at most about 160 bytes, so a 256-byte receive buffer is enough; replies reach
  **851 bytes** (catalogue), so the transmit path must chunk. The first catalogue read is **1817 notifications at 20 bytes, 306 at
  an MTU of 185**: the negotiated MTU decides how slow the first connection is. The catalogue is cached afterwards.

**Instrumentation in the working tree (uncommitted), mostly BLE test image only**
- `firmware/openpocket.c`: DIAG lines 5 to 9 (registers `r0..r15`, ETM trace PCs, sp/psr/icfg/usp/ssp, a radio-stage breadcrumb in
  `.noinit`, the radio replay's own breadcrumb, the code bytes at the crash pc); flags `OP_BLE_NORADIO`, `OP_BLE_EARLY` (radio started before USB
  and audio; default 1); the stall capture that was last built but never read on a hang (see 6).
- `firmware/px_audio.h`: `OP_BLE_TUNER_CRAM` (default 0: tuner buffers in ordinary RAM, as in the release); icfg and nesting counters
  and the main-loop stall capture in the audio interrupt.
- `firmware/ble/ble_os.c`: `local_irq_disable/enable` now restore the previous state; `cpu_in_irq()` returns `(icfg & 0xFF) != 0`.
  `firmware/ble/ble_test.c`, `ble_status.h`: stage crumbs 20 to 23 and a hook.
- `firmware/build.sh`: `OP_BLE_RUNS` (default 1), `OP_BLE_NORADIO`, `OP_BLE_EARLY`. These do **not** reach the container through
  `node tools/op.mjs ble`; build with `docker run --rm --platform linux/amd64 --user $(id -u):$(id -g) -e HOME=/tmp -e OP_BLE=1 -e OP_BLE_NORADIO=… -v "$PWD":/src -w /src openpocket-build sh firmware/build.sh`.
- `firmware/bsp/chip/fm1_irq.h`: **touches every build**: `fm1_crash_t` gained `code[8]` and the fault handler fills it. Revert it, or
  review it, before building a release from this tree.
- `tools/log.mjs`, `docs/EDITOR-PROTOCOL.md`: DIAG now returns up to 10 lines; the log tool prints `regs`, `etm`, `radio`, `code`, `stall`.
- Not mine: untracked `docs/VALVE SIM.md`.

## 3. Memory budget (measured by the build)

| | Release v76 | BLE test image (tuner in RAM) |
| --- | --- | --- |
| RAM used / free | 53 044 / 45 260 of 98 304 | 81 400 / 16 904 |
| RAM code window free | 13 096 | 13 376 |
| Flash image | 269 KB | 507 KB (XIP limit 577 KB) |

The radio, its 24 KB heap and its task stacks fit with about 17 KB spare. "RAM is the obstacle" is not true at v76 (the old note of
under 1 KB was v57). The `.noinit` margin in the BLE image is under 400 bytes.

## 4. Other people's work read, and what it says

- **Stock M-VAVE BlackBox over BLE** (third-party reverse engineering, `github.com/jvsobrinho/mvave-blackbox-ble`; docs CC BY-SA 4.0,
  code GPL-3.0; read only, nothing copied): custom service `0000ae40-…`, `ae41` write without response, `ae42` notify, raw 8-bit packets of
  8 to 107 bytes with a checksum, a whole 92-byte preset in one 107-byte write (so the stock link carries at least 110 bytes: an MTU of
  about 110 or more), **no standard BLE-MIDI service, no pairing or encryption documented**, MTU, advertising and connection parameters
  not documented. Our `docs/STOCK-FIRMWARE.md` already covers the `AE40` service.
- **UltraRig:** only `~/Downloads/UltraRig-alpha13.1-blackbox-mac.dmg` was found: an installer (Python/PyInstaller app, with
  `jl-uboot-tool` and `wl82loader.bin`); its `Info.plist` says "UltraRig Editor uses Bluetooth to connect to your pedal". No firmware image in it.
  The user remembered a decompile on another branch; none exists in this repository, its history, stashes, dangling commits or the
  `openpocket-ref-synth` worktree. To read how UltraRig starts the radio, a firmware image or the editor app is needed.
- **FM-1 projects** with Bluetooth MIDI (ORBIT, ChoralRoot) exist for the same chip family; their code was not read.
- **Provenance risk:** the radio start-up values we replay (`firmware/bsp/chip/ble_rf_tables_v15.h`) were captured from M-VAVE's **FM-1**
  firmware in an emulator, not from a BlackBox. The VM records come from a Pocket X backup when the pedal has none. Whether FM-1
  values are right for this hardware is open.

## 4a. Measured on the BlackBox (good boots)

- `icfg` is `0x043F0700` in the main loop and `0x030B0308` in the audio interrupt: **`icfg & 0xFF` is 0 outside an interrupt and 8 inside**.
  Global enable (bit 9) stays set inside the interrupt; nesting is gated by priority.
- **0 of 12 179 audio interrupts had a radio interrupt begin inside them** (the radio ran at about 20 interrupts a second): the radio
  does not preempt audio.
- A good boot reaches "enable" at 1078 to 1310 ms (earlier, about 1122 ms, with the early start) and advertises; scan requests are
  heard (`r` climbs to 228 in about 18 s); the replay finishes at operation 3454, group 25, register `0xBBD001`.

## 5. Experiment log (BlackBox, one pedal, normal firmware restored after each series)

| # | Image | Result |
| --- | --- | --- |
| 1 | Existing BLE test image (`PX_RUNS=0`) | Crashes at 1305 ms in `dly_block`; next boot in `px_wet_run` (`EMU 1` misaligned, `DBG 0`) |
| 2 | `PX_RUNS=1` (the release engine's runs) | Same: crash at 1305 ms, `a_enc`, `r12` = 4 where `&g_eng` belongs |
| 3 | + DIAG registers | First boot after a flash clean, advertising; every cold start crashed |
| 4 | Tuner buffers in RAM, not cache RAM | Still crashes (`pxt_run` entered with `r4` = 1) |
| 5 | **Radio never started** (`OP_BLE_NORADIO=1`) | **10 of 10 cold starts clean** |
| 6 | Stage crumb | Crash 9 to 10 ms after stage RF (the replay) began, twice |
| 7 | Radio started before USB and audio | Replay completes; crash about 100 ms after "enable", in the tuner filter loop in `a_enc`: `DBG 0x10` (unmapped read), `EMU 1`, `r15` = a float |
| 8 | `local_irq_enable` restores prior state | Audio ran 56 blocks (first time); crash returning from `fm1_adc_read` to `0x07011580`, `EMU 0` |
| 9 | Real `cpu_in_irq()` | Instant crash again (`DBG 0x100000`, `EMU 1`) |
| 10 | Nesting counter | 0 of 12 179 (a good boot) |
| 11 | Crash-code capture | One clean cold boot, then a **watchdog reset after about 6 s** (2126 audio interrupts, main loop stopped) |
| 12 | + stall capture | First boot after the flash crashed at 1224 ms; **code bytes at the crash match the build exactly**; `EMU 0`, `DBG 0`; loop index 77 of a loop that ran 76 times fine |

Reading a crash: `fm1_irq.h` `fm1_fault_c`; `EMU`/`DBG` bits in `docs/HARDWARE-HANDBOOK.md` (EMU: 0 misaligned, 1 illegal instruction, 2
divide by zero, 3 stack, 8 ETM, 16 to 20 FPU; DBG: bus side, guards).

## 6. Where that leaves the cause

**Ruled out:** `PX_RUNS`; the tuner in cache RAM; our `sinf`/`cosf` (the same object in both images); the replay itself (it completes);
a wrong `cpu_in_irq`; radio interrupts preempting audio; the RAM-resident code being overwritten at the one crash checked. Starting the
radio first only moved the crash to the first audio blocks.

**Still true:** without the radio, clean; with it, most cold boots fail and the failures differ in shape: exceptions in audio code with
garbage in callee-saved registers, a main-loop return to a garbage address, a hang ending in a watchdog reset, one exception with no
cause bit set. Even the first boot after a flash can crash (it did in 12), so "warm start is fine" was luck.

**Live hypotheses, in the order I would test them**
1. **The radio's DMA writes outside its own buffers**, corrupting stack frames at random when packets arrive. Fits every shape. The
   replay's register list has no obvious DMA pointer, so the buffer addresses come from elsewhere in the library. *Test:* fill the free
   70 KB between the pool and the RAM code (`0x1c4e000..0x1c5f000`) and the margins by the stacks with a canary, run a good boot, read what
   changed and how often.
2. **The second core or the FPU:** BSP notes say "CPU1 faults only in BLE builds"; registers holding floats where pointers belong point at
   FPU or shared-state trouble. *Test:* a BLE image with `DSP_CPU1` off.
3. **The stall capture, built but never read on a hang:** it names where the main loop was stuck. *Test:* flash it, power-cycle until a
   watchdog reset, read `stall`.
4. **FM-1 replay values wrong for a BlackBox** (see 4). *Test:* diff against a BlackBox capture, or the stock firmware's own start-up order.
5. **Task stacks sit directly above the interrupt stack** (`0x1c7c000`): an overflowing task writes into the interrupt stack's top.
   *Test:* paint and check the margin after a good boot (`ble_stack_min_free` exists; the log tool does not show it).

## 7. Open questions for the user

- Check the USB route from the phone first (it needs no firmware).
- Is there an UltraRig firmware image or the editor app, so its radio start-up can be read?
- Commit the host-side work (`pxble.js`, its test) alone, and keep the radio instrumentation on `bluetooth` or drop it?
- Pairing: persistent bonds or pair each time?

## 8. To resume

`main` is the last good state. On `bluetooth`: `make test` for the host side (`node tests/test_pxble.mjs` alone is fast); build the BLE
image with the docker command in 2; always `node tools/op.mjs flash --dry-run --package build/ble/BlackBox_976.fwsc --manifest
build/ble/manifest.json` first, flash only on the user's word, and restore `docs/firmware/BlackBox_976.fwsc` afterwards. After a failed
boot the pedal starts the next one in safe mode (effects off) once; power-cycle to clear it.
