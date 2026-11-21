# v51 resonator test on AC791N

The `resonator-test` channel replaces the candidate detector in BASS SYNTH HQ with a fixed-size C
resonator bank. It reuses the existing quarter-rate decimator and the existing mono voice and note layer.
The 66-note bank covers B0 through E6. It reports every 128 input samples and requires three consecutive
reports for secondary candidates. BASS SYNTH HQ auditions the strongest candidate. This build is an opt-in
target measurement. It has no two-voice oscillator, lowest-note mask, or continuous bend refinement.
The diagnostic detector has no spectral-flux repick signal, so BASS SYNTH HQ's repick behavior in this
build is not representative of a finished effect.

The C bank reproduced the Rust reference's held-out guitar result with the OpenPocket decimator. On the
next 24 GuitarSet takes, 18,904 of 44,878 settled dyad frames had both candidates right (42.1%), the
lowest candidate was right in 69.3%, and extra candidates appeared in 37.4% of one-note frames.
Synthetic B0 and E1 dyads scored 66/252 and 83/252 correct frames. Those tests establish candidate
behavior on known signals. They do not measure real bass, audio output latency or pedal overhead.

## Build and memory

```sh
OP_CHANNEL=resonator-test OP_TEST_VERSION=51 sh firmware/build.sh
```

The linker reported 23,344 of 24,576 bytes of low RAM and 3,672 bytes free in the high RAM code window.
Against the v50 target build the bank adds 1,392 bytes of low RAM and 688 bytes of RAM code. The
application image grew from 471,884 to 472,968 bytes. Both packaged images remain 675,968 bytes.
The release build excludes the resonator candidate code. The shared decimator refactor produced
identical baseline bench CSV output on the tested recorded clip.

## Same-preset probe

```sh
SOLO_SMART_GATE=1 TYPES='BASS SYNTH HQ@50/50/50/50/0' node tools/core2_probe.mjs 20
```

The probe saves the current sound in memory, enables only BASS SYNTH HQ and SMART GATE in the
temporary preset, collects PING telemetry, and restores the previous sound. It does not save a preset.
Run on v50 and v51 with the same dry input at the same level. Repeat with a quiet low note, a loud
six-string strum, and a quiet second note under a held note. Check peak callback load, late blocks,
safety bypass and whether CPU1 stays enabled. Normal PING reports a combined block peak for this
preset. Separate core peaks are available in the layer-split NAM path only. A longer repeatable DI
and loopback recording is required to measure the worst-case load and audible latency.

## Connected BlackBox smoke test

The v50 and v51 runs both used the two-second temporary preset above. The BlackBox rebooted into
`BlackBox_951` after verifying and installing the v51 package. Both runs reported a 26% combined
peak, zero late blocks, no safety bypass, and an active second core. PING's block breakdown
reported SYN at 18% and GT at 4% in both runs. The probe restored the previous sound. Input was not
controlled or recorded, so this establishes a basic hardware smoke result rather than equivalent
worst-case load under playing.

[Host resonator benchmark](../research/POLY-TRACKER-RESONATOR-BENCH.md)
