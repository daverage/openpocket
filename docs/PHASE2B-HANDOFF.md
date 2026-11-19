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

The latest work tested faster release for one STRING DUO voice when the
other still has a matching candidate. See the STRING DUO comparison in
Phase 2b. Prototype source, commands reflected in benchmark logs, and WAVs
are under `build/phase2b/duo-release/`. The v67 baseline, unconditional
cap and cap latched at note-off are respectively `check-0.log`,
`check-1.log` and `check-2.log`, with `owner-N.log` and
`check-real-N.log` alongside. These are ignored local files. The
committed benchmark now also reports entries with both voices held.

Reproduce the v67 and narrower release comparison, then inspect the
specific lost or late entries and the lower-first recorded octave. Use
the audible first-hit, held-only and five-report results together,
plus isolated mutes and both owner DIs. A shorter tail that changes a
score without improving the played transition is weak evidence. Keep
experimental logic out of the firmware recipe while it misses the gate.
Work from the existing tracker and renderer. Favor fewer rules and
preserve independent note attacks, legato and intentional release.
