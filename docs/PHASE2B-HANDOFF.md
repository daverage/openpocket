# Prompt for the next agent

Continue Phase 2b in `/Users/andrzejmarczewski/Documents/GitHub/openpocket`.
Read `AGENTS.md`, `docs/phase2b.md` and
`docs/TRACKING-ARCHITECTURE-REVIEW.md`. Inspect the current worktree and
build artifacts first. Follow the stage gates in the plan. The user wants
the existing waveform octave and fuzz, one stable lead note for pure
synths, and independently voiced ensembles using one shared allocator.
Keep the Arcade register split behind the core work.

v66 remains the mono reference. v67 is a built, host-verified A/B image in
`build/phase2b/target-v67/v67/docs/firmware/poly-test/`. It has not been
flashed. It misses the mono octave-error gate. Keep the v67 package and
recipe as a reference, and treat pedal listening, load, and latency as
pending. Ask before flashing. Use the supplied scale and low-E recordings
in `My Audio/` for all tracker checks.

Read "Reproduction and entry inspection" under the STRING DUO comparison
in Phase 2b first. Benches must use the full v67 `DSPDEF` from
`firmware/build.sh` with `PXP_SET='ACQB=0.06 SECOND=0.58'`. Pass the flag
list through bash. zsh does not split an unquoted `$F`. Working files are in
`build/phase2b/duo-release/` (ignored):

- `v67-flags.txt` holds the flag list. `w66`, `w67`, `w1`, `w2`,
  `woct2` and `woct2cap` wrap the benches with the right `PXP_SET`.
- `paired/` holds the latched release cap. `oct2/` and `oct2cap/` hold the
  two-cue octave trial (`-DPXP_OCT_TWO_CUES=1`).
- `plucks*/`, `run-real.sh` and `jitter.sh` re-extract the recorded pairs
  and sweep the clip start.
- `entry_diff.py` lists holdout entries that differ between two benches.
  `octave_trace.py` and `octave_ratios.py` trace the lower-first octave.

The latched release cap holds up on mute tails and loses no entry from
a held voice (120/270 versus 119 on v67). Its first-hit loss came mostly
from v67 crediting release tails. Two real losses remain, where the
allocator drops a note that is still sounding. It is a listening
candidate, not a recipe change.

The lower-first octave fails because `pxp_octave_evidence` requires
double-octave and sixth partials that real upper notes rarely have.
Dropping those two checks admits real octaves, but lets more harmonics in.
Holdout entries from a held voice fell from 119 to 116, and the first
low-E sustain lost its correct pitch earlier. Next, test one temporal cue
in place of the four static checks: octave-band power relative to the
root, compared with the same ratio once the held note had settled. Judge
it on the recorded pairs across clip offsets, all holdout scores, both
owner DIs and the synthetic octave matrix. Keep it out of the firmware
recipe until it passes. Favor fewer rules and preserve independent note
attacks, legato and intentional release.
