#!/bin/sh
# OpenPocket host tests (Linux / macOS, or in the container: tools/op.mjs test; no pedal, no JieLi toolchain needed):
#   tests/run_tests.sh [PocketX_016.fwsc]
#  - engine: every block x model x extreme knob, signal / silence / overload, with the pedal's own libm (px_math.c):
#    no NaN; long runs (3 s pluck + 8 s silence): nothing runs away (only delays at FDBK 100 hold on, by design)
#  - (not run here, it needs the git history: tests/equiv.sh REF, this tree's engine against commit REF's, bit for bit)
#  - changes without clicks (v38): knob changes glide to exactly where a full set goes, blocks on / off fade, fewer clicks
#  - Synth2: POLY OCTAVE HQ and POLY OCT FUZZ bit for bit against the last v73 source (tests/protected.sh)
#  - wet lanes (v39): the delay through its lane as inline bit for bit, the reverb's tail lag frames later, the pipeline
#  - MIDI: the musical-MIDI code (firmware/px_midi.h) on a PC: decoder, CC map, messages sent, settings byte, clock
#  - preset store: save / reload / power cut at every step of a save (firmware/px_store.h on a simulated flash)
#  - the audio callbacks' order (v42, firmware/px_aorder.h): the I2S driver calls the output's callback before the
#    input's when its interrupt enters late; every output block must still be the sound of that interrupt's guitar block
#  - USB audio (v42): the USB device code and the audio glue (bsp/chip/usb.c, firmware/px_uaudio.h) against a model of
#    the controller and a computer: with the setting off the descriptors are the earlier device's byte for byte; with
#    it on, the descriptors, the requests, 10 minutes of each stream with the clocks apart (no gap, no repeat)
#  - LED map + lights driver (led_probe.h, px_leds.h): PC7 enable after the first byte, the 595 timing, the table
#  - button lights (px_lights.h): each block lights its button, FX for its group, HOME blinks in the tuner
#  - packages: tools/pxpkg.py rebuilds the official PocketX_016 app area byte for byte (needs the official file)
#  - installer: docs/pxota.js + docs/pxpkg.js against a simulated pedal running the real update code (ota.c, ldr_core.c)
#  - flasher: tools/flash.mjs (node tools/op.mjs flash) against the same simulated pedal: update, dry run, wrong model, damaged package, rescue of a pedal that restarts in a loop
#  - rescue: tools/uboot_rescue.py (protocol, guards, resume) against a simulated UBOOT pedal, tests/uboot_sim.py
#  - hosted simulator: tools/sim.mjs driven over HTTP (controls, held buttons, screen, flash file, SysEx, the Web MIDI shim)
#  - pedal skin: its input mapping (tools/sim/pedal-input.js)
#  - sound: the WebAssembly engine on known signals, against the native build; the bundled loops against their manifest
#  - Chocolate Plus files: tools/chocolate/fcp.py against a made-up export
#  - editor: docs/pxedit.js against the simulated pedal (tests/edsim.c), PING's figures, and the editor's own logic
#    (docs/pxmidi.js: the Control Change map against docs/MIDI.md, learned controls; docs/pxlib.js: the model library)
#  - pages: docs/index.html, docs/editor.html and the pedal skin in headless Chromium (needs Playwright: npm i playwright
#    && npx playwright install chromium)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
CC="${CC:-cc}"
echo "== engine"
$CC -O2 -Iengine tests/engine/sweep2.c engine/px_dsp.c firmware/px_math.c -lm -o build/sweep && build/sweep | tail -1
$CC -O2 -Iengine tests/engine/long.c engine/px_dsp.c firmware/px_math.c -lm -o build/long && build/long | tail -1
$CC -O2 -Iengine -Ifirmware tests/engine/change_test.c firmware/px_math.c -lm -o build/change_test && build/change_test | tail -1
$CC -O2 -Iengine tests/engine/wet_test.c engine/px_dsp.c firmware/px_math.c -lm -o build/wet_test && build/wet_test | tail -1
# Synth2: the SYNTH block's ids (tombstones bypassed, POLY OCT FUZZ running)
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/syn_ids_test.c firmware/px_math.c -lm -o build/syn_ids && build/syn_ids
# The waveform-following synths (engine/px_banksyn.h, BANK ORGAN): bounds, silence, lead pitch, call-size independence.
# (ANALOG / ARCADE / INSTRUMENT / ENSEMBLE / CHOIR were retired 2026-11-27: tests/engine/synth_test.c and choir_click_test.c no longer run)
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/banksyn_test.c firmware/px_math.c -lm -o build/banksyn_test && build/banksyn_test | tail -1
# SFX ENHANCE v3: warmth (even harmonics of the low-mid band) and presence (an exciter-style high band), level independent; unity, bounds, no clicks
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/enhance_test.c firmware/px_math.c -lm -o build/enhance_test && build/enhance_test | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/pedals_test.c firmware/px_math.c -lm -o build/pedals_test && build/pedals_test | tail -1
# v75: BST (PRE / BOOST) and its VOX FIXED WAH: the catalogue, the move of the SFX pedals, the V847 tone curve, level, no clicks
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/voxwah_test.c firmware/px_math.c -lm -o build/voxwah_test && build/voxwah_test | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/fuzzface_test.c firmware/px_math.c -lm -o build/fuzzface_test && build/fuzzface_test | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/acoustic_test.c firmware/px_math.c -lm -o build/acoustic_test && build/acoustic_test | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/signature_test.c engine/px_dsp.c firmware/px_math.c -lm -o build/signature_test
build/signature_test
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/hum_test.c firmware/px_math.c -lm -o build/hum_test && build/hum_test | tail -1
$CC -O2 -w tests/tunersim.c -lm -o build/tunersim && build/tunersim | tail -1
# POLY SHIFTER on the bank (2026-11-28; v75's phase vocoder and tests/engine/pv_test.c retired): chord accuracy, semitones, bounds
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/pshift_test.c firmware/px_math.c -lm -o build/pshift_test && build/pshift_test | tail -1
# DROP TUNE (2026-11-28): the low string's notes alone move down; the other strings stay
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/drop_test.c firmware/px_math.c -lm -o build/drop_test && build/drop_test | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/global_test.c firmware/px_math.c -lm -o build/global_test && build/global_test | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/triode_test.c firmware/px_math.c -lm -o build/triode_test
build/triode_test
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/tails_test.c firmware/px_math.c -lm -o build/tails_test && build/tails_test | tail -1
for runs in 0 1; do
  $CC -O2 -w -fno-builtin -ffp-contract=off -DPX_RUNS=$runs -Iengine tests/engine/suppressor_test.c firmware/px_math.c -lm -o build/suppressor_test
  build/suppressor_test
done
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/hush_test.c firmware/px_math.c -lm -o build/hush_test && build/hush_test | tail -1
$CC -O2 -w -Iengine tests/engine/factory_test.c engine/px_dsp.c firmware/px_math.c -lm -o build/factory_test && build/factory_test | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/shed_test.c firmware/px_math.c -lm -o build/shed_test && build/shed_test | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine -Ifirmware tests/shedsim.c firmware/px_math.c -lm -o build/shedsim && build/shedsim | tail -1
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/poly_range_test.c firmware/px_math.c -lm -o build/poly_range_test && build/poly_range_test | tail -1
# Felucca's own golden renders for the vendored engines
CC="$CC" sh third_party/felucca-engines/tests/felucca_ref.sh
# Synth2: the mono tracker's interface (engine/px_mono.h)
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/mono_test.c firmware/px_math.c -lm -o build/mono_test && build/mono_test | tail -1
# Synth2 fix pass 1: LOWEST OCTAVE HQ's own gain (x1.4 (+2.9 dB) against the v73 level), the other pitch types untouched
$CC -O2 -w -fno-builtin -ffp-contract=off -Iengine -DPX_LOWEST_HQ_GAIN=1.0f tests/engine/lowest_gain_test.c firmware/px_math.c -lm -o build/lgt_ref && $CC -O2 -w -fno-builtin -ffp-contract=off -Iengine tests/engine/lowest_gain_test.c firmware/px_math.c -lm -o build/lgt \
  && python3 -c "
import subprocess
a=[float(v) for v in subprocess.check_output(['build/lgt_ref']).split()]; b=[float(v) for v in subprocess.check_output(['build/lgt']).split()]
r=[b[i]/a[i] for i in range(3)]
ok=all(1.25<x<1.45 for x in r) and a[3]==b[3] and b[5]<0.5*b[4]
print('lowest octave hq gain: wet x%.2f x%.2f x%.2f, poly octave hq unchanged %s, RANGE cuts a note above it %.3f -> %.3f: %s'%(r[0],r[1],r[2],a[3]==b[3],b[4],b[5],'ok' if ok else 'FAILED'))
raise SystemExit(0 if ok else 1)"
# Synth2: the protected effects against the v73 baseline (needs that commit in the clone's history)
# (the reference is the stored hashes since 2026-11-27: tests/protected.sh --update after a deliberate change)
CC="$CC" tests/protected.sh --golden
echo "== preset store"
$CC -O1 -Iengine tests/test_store.c engine/px_dsp.c firmware/px_math.c -lm -o build/test_store && build/test_store | tail -2
echo "== audio callbacks' order (the output block is its interrupt's, whichever callback the driver calls first)"
$CC -O1 -Wall -Ifirmware tests/test_aorder.c -o build/test_aorder && build/test_aorder | tail -1
echo "== USB audio (the device code and the audio glue against a model of the controller and a computer)"
UAF="-O1 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Iengine -Ifirmware -Ifirmware/bsp/chip"
$CC $UAF tests/test_usb_audio.c -lm -o build/test_usb_audio
$CC $UAF -DT_REF tests/test_usb_audio.c -lm -o build/test_usb_audio_ref      # the USB device as it was before v42
build/test_usb_audio desc > build/usb_desc_off.txt
build/test_usb_audio_ref > build/usb_desc_ref.txt
if cmp -s build/usb_desc_off.txt build/usb_desc_ref.txt; then echo "USB AUDIO off: the descriptors are the earlier device's, byte for byte"
else echo "USB AUDIO off: the descriptors differ from the earlier device's"; diff build/usb_desc_ref.txt build/usb_desc_off.txt; exit 1; fi
build/test_usb_audio > build/usb_audio.log || { cat build/usb_audio.log; exit 1; }
tail -1 build/usb_audio.log
echo "== screen (every page fully drawn)"
mkdir -p build/ui && $CC -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -o build/uisim tests/uisim.c engine/px_dsp.c firmware/px_math.c -lm
build/uisim build/ui > build/ui/log.txt
if grep -q "never drawn" build/ui/log.txt; then grep "never drawn" build/ui/log.txt; echo "screen: FAIL"; exit 1; else echo "screen: $(wc -l < build/ui/log.txt) pages ok (build/ui/*.ppm)"; fi
echo "== MIDI (firmware/px_midi.h: decoder, Control Change map, what the pedal sends, settings, clock)"
for t in midisim midiio; do
  $CC -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -o build/$t tests/$t.c engine/px_dsp.c firmware/px_math.c -lm
  build/$t > build/$t.log || { grep FAIL build/$t.log; exit 1; }
  tail -1 build/$t.log
done
echo "== LED map and the lights driver (LED_PROBE build, issue #8)"
mkdir -p build/lp && $CC -O1 -Wall -Wno-unused-function -Ifirmware -o build/lpsim tests/lpsim.c && build/lpsim build/lp
echo "== button lights in the normal firmware (px_lights.h, issue #8)"
$CC -O1 -Wall -Wno-unused-function -Iengine -Ifirmware -o build/lightsim tests/lightsim.c
build/lightsim > build/lightsim.log || { cat build/lightsim.log; exit 1; }; tail -1 build/lightsim.log
echo "== simulated pedal"
$CC -O1 -Wall -Wno-unused-function -o build/devsim tests/devsim.c
if [ -n "$1" ]; then echo "== packages"; python3 tests/test_pxpkg.py "$1"; fi
echo "== installer"
node tests/test_installer.mjs "$1"
echo "== flasher (tools/flash.mjs)"
node tests/test_flash.mjs
echo "== build entry point (tools/op.mjs)"
node tests/test_op.mjs
echo "== rescue tool (tools/uboot_rescue.py on a simulated UBOOT pedal)"
python3 tests/test_uboot_rescue.py $1
echo "== hosted simulator (tools/sim.mjs)"
node tests/test_sim.mjs
echo "== Bluetooth test image sources (third_party/jieli-ble)"
node tests/test_ble_sources.mjs
echo "== Bluetooth test image status codes"
$CC -O1 -Wall -Wno-unused-function -o build/ble_status tests/test_ble_status.c && build/ble_status
$CC -O1 -Wall -Wno-unused-function -o build/ble_vm tests/test_ble_vm.c && build/ble_vm
echo "== Chocolate Plus files (tools/chocolate/fcp.py)"
python3 tests/test_fcp.py
echo "== pedal skin input"
node tests/test_pedal_input.mjs
echo "== simulated sound"
node tests/test_audio.mjs
echo "== editor"
$CC -O1 -Wall -Wno-unused-function -o build/edsim tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm
node tests/test_editor.mjs
node tests/test_ping.mjs
node tests/test_editor_logic.mjs
echo "== log channel (tools/op.mjs log)"
$CC -O1 -Wall -Wno-unused-function -DOP_BLE_TEST=1 -o build/edsim_diag tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm
node tests/test_log.mjs
if node -e "require('playwright')" 2>/dev/null; then echo "== page"; node tests/browser/test_page.mjs build/page; echo "== editor page"; node tests/browser/test_editor_page.mjs build/editor; echo "== pedal skin"; node tests/browser/test_pedal_page.mjs build/pedal; echo "== pedal sound"; node tests/browser/test_audio_page.mjs build/audio; else echo "(Playwright not installed: page tests skipped)"; fi
