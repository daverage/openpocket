# Phase 2b: what is left, in order

Written 2026-11-19, after the owner listened to v66 (BlackBox_966, poly-test channel).
It follows `build/phase2.md` (Phase 2, stages 1–7, v58–v66). It adds what the owner's
recordings in `My Audio/` show and the decisions taken since.

Each stage has the same workflow as Phase 2:
1. Prototype and score on the host.
2. Build poly-test.
3. Check that `tests/equiv.sh` leaves the standard engine bit-identical (unless the stage moves something into it).
4. Run `make test`, commit, push.
5. Dry-run, then install on the BlackBox and run `tools/poly_pedal_check.mjs`.
6. Update the progress log.

Some stages end with an owner listening gate. Work after a gate waits for the verdict.
Host work that has independent inputs, such as capturing the original HQ comparison
renders and measuring isolated octave voices, can run alongside the tracker work.

## Tracking architecture decision

The owner wants the polyphonic octave and octave fuzz, pure synth voices
that stay on one intended note when another note or overtone is present,
and ensembles that can genuinely play more than one independent note.
These need one waveform octave processor and two note-tracking roles.
Keep the waveform processor for the octave, fuzz, and waveform-based
organ, strings, and choir sounds. Use the existing lead-note layer for
pure synths and single acoustic instruments. Use one bounded note allocator
for instruments that actually render independent voices. The existing Poly
synth variants use that same allocator as STRING DUO.

STRING ENSEMBLE and BRASS ENSEMBLE currently layer two timbres around one
tracked lead pitch. Stage 5 now addresses independent notes in ensembles
and stability in the mono synths. It supersedes the plan to make SQUARE,
PWM and BASS HQ dual-tracked. The v67 build is an A/B for the existing
experiment. Promotion requires evidence on the intended instrument role
and the smallest set of rules that provides that behaviour.
[Tracking architecture review](TRACKING-ARCHITECTURE-REVIEW.md)

## Required owner recordings

Use both original DI takes as named inputs in every tracker baseline and candidate
run. Keep them alongside the GuitarSet and synthetic tests in the score report.

| DI take | What it must exercise |
|---|---|
| [scale + duo + power chords di.wav](../My%20Audio/scale%20%2B%20duo%20%2B%20power%20chords%20di.wav) | Score the single-note scale, two-note passages and power chords separately. Report overtone ghosts, missing played notes, genuine second-note entries and low-root recovery. |
| [low e open di.wav](../My%20Audio/low%20e%20open%20di.wav) | Score low-E acquisition, octave slips, semitone changes during the decay, false second voices and release. |

Record the input channel, resampling, gain, labelled time ranges, engine flags and
commit with the results. Use the same labels and preprocessing for v66 and each
candidate. Use GuitarSet development takes for tuning, and keep the held-out data
frozen for reporting. The existing wet recordings are listening references. For
pedal output latency, capture DI and wet together in one stereo take.

---

## 0. Where things stand

### Owner verdicts so far

| Item | Verdict | Consequence |
|---|---|---|
| Poly synths (Pulse, Saw, Arcade, FM) | Tracking still the biggest issue; random second notes | Stage 1 |
| STRING DUO | Worst tracking of all | Stages 1 and 2 |
| MONO SYNTH | Plays "a second note" (jumps to an overtone) | Stage 1 |
| STRING / BRASS ENSEMBLE | Tracking poor; brass jumps register | Stages 1 and 1e |
| TRUMPET | Acceptable | Same tracker as MONO SYNTH, so it benefits from stage 1 |
| POLY OCTAVE HQ, OCT −2 | Works | Moves into the standard build (stage 8) |
| POLY OCT FUZZ | Great; LEVEL 0–100 done in v66 | Done |
| LOWEST OCTAVE HQ | "Just horrible" | **Parked** (section 10) |
| SQUARE vs SQUARE HQ | Clearly different sound | Stage 4 |
| Anything not recorded | Accepted for now | — |

### Owner's rule for the HQ types

Each HQ type (ORGAN, CHURCH ORGAN, STRINGS, CHOIR, SQUARE, PWM, BASS, POLY OCTAVE)
must **sound the same as its original** and improve the response for its intended
role. The octave bank must handle chords. The pure synths must stay on one intended
note with clean transitions even when the input includes another note or overtone.

v63/v64 treated each pair as rivals and kept the one that measured better. That is
the wrong test. It retired PWM SYNTH HQ to the original, which is backwards under
this rule.

### What the recordings showed (host analysis of the two DI takes, v66 code)

- **Overtone ghosts in the poly tracker.** On the owner's scale DI, a second voice
  sounds in 39% of single-note frames. Its offsets from the played note are +12 (443 frames), +19 (285),
  +24 (117), +31 (45): the 2nd, 3rd, 4th and 6th overtones. STRING DUO plays that
  ghost as the violin, so it is the most audible there.
- **Missing played note in the poly tracker.** In 19% of the time no voice is on the
  note played. Examples: G3 read as G5, C4 as C6, D3 as A4.
- **Power chords lose the root.** E2 + B2 is read as B3, B4 and F♯4, which are the
  fifth's overtones. The low E's own pitch is weak in a DI signal, and at E2 one
  semitone is 4.9 Hz against the bank's 14 Hz bandwidth.
- **Semitone slips on fading notes.** A long E2 slips to F2 or D♯2 as it decays
  (`string duo low e.wav`, 5.2, 6.6 and 7.4 s).
- **MONO SYNTH** (the single-note tracker `pxt_layer`, which also drives TRUMPET, the
  ensembles and the HQ mono synths):
  - 84.5% of the time it is on the played note.
  - 5.0% of the time it is an octave below.
  - 3.6% of the time it is on the octave and a fifth (+19/+20). This is the
    "second note" the owner hears.
- **BRASS ENSEMBLE.** The trumpet (an octave up) and the horn (at guitar pitch) swap
  which is louder from note to note, so a scale sounds like it jumps register.
- **SQUARE HQ vs SQUARE.** HQ is about 4 dB louder and darker (more below 200 Hz,
  less above 3 kHz). HQ follows the scale more cleanly; SQUARE had wrong notes at
  note starts.

### Why the v66 overtone block misses these ghosts

`engine/px_poly_track.h`, allocator around line 365–380: a new candidate is blocked
if it is the 2nd, 3rd or 4th harmonic of a note **already held**. The block has two
gaps:
- On a pick, the overtone often confirms first, because the higher bands build up
  faster than the low fundamental. When the real note then confirms, nothing
  re-checks the voice already held on its overtone, so both keep sounding.
- The 5th and 6th overtones are not in `pxp_harmonic` at all.

### Memory left in v66 (from `firmware/build/symbols.txt`)

| Memory | Free |
|---|---|
| RAM code window | 312 B |
| General RAM | 4,536 B |
| Low RAM | 864 B |
| DSP pool | 1,392 B |
| XIP flash | about 95 KB |

The tracker and renderers run from flash.
The saved [v66 build log](../build/phase2/build-v66.log#L7) records the section
budgets. The saved [pedal check](../build/phase2/pedal-v66.txt) reports zero late
blocks in its measurements. The [host test log](../build/phase2/test-v66.log)
records that the browser page tests were skipped because Playwright was unavailable.

---

## Stage 1: v67, remove overtone ghosts from both trackers

**Need:** the biggest fault the owner hears. It affects every tracked voice: the
Poly synths, STRING DUO, MONO SYNTH, TRUMPET, the ensembles, and the HQ mono synths.

**Reason this comes first:** everything later depends on a clean tracker.
- The independently voiced ensembles in stage 5 and the STRING DUO redesign in
  stage 2 inherit the same allocator's ghosts.
- Retiring originals to HQ (stage 7) would lock them in.

Prototype within the current memory budget and record the target map deltas.

### 1a. Poly tracker: evict an overtone when its fundamental arrives
- **Rule.** When a voice confirms a note F, check the other held voice. If its note
  is within tolerance of 2F, 3F, 4F, 5F or 6F, and it started within a short window
  of F (start at about 100 ms), release it or retune it to F.
- **Choosing between release and retune.**
  - Retune if F has no voice of its own yet: the fundamental then takes the voice
    with no new attack.
  - Release if both voices are on.
- **Protect real octave dyads.** The window helps with a note added later. Real
  notes picked together or close in time still need independent onset or harmonic
  evidence. Test simultaneous attacks and staggered attacks in both orders,
  including both sides of the proposed window and unequal pick levels.
- **Extend `pxp_harmonic`** to 5× and 6× with tolerance growing as now. +31 is the
  4th most common ghost. The helper is also used in candidate filtering, so measure
  the effect of changing it there separately from the allocator change.
- **Tunables, for the host sweep:** window length, harmonic list, and an
  energy-ratio escape. An "overtone" much louder than a single string's overtone
  would be is kept as a real note.

### 1b. Single-note tracker (`pxt_layer`, `engine/px_track.h`): no overtone jumps without a pick
- **Rule.** A pitch change to about 2×, 3× or 4× the current note, or to an octave
  below it, is only accepted at a detected pick or after a long confirmation. A slur
  or legato note on a real interval is unaffected.
- Preserve the early correction where a real lower fundamental takes over from an
  onset overtone. Treat that separately from a jump away from a settled note.
- **Reason.** The +19/+20 blips (3.6%) and the −12 (5.0%) are what the owner hears
  as a second note.
- **Check.** Re-run `tools/sim/mono_eval.py`. Unseen GuitarSet solos are at 91.5%
  right, with 120 octave errors in 6,187. Also check the owner's DI takes.

### 1c. Lock pitch on fading notes
- **Rule.** While a held note decays with no new pick, its semitone stays fixed. The
  voice follows the band's fine frequency, so bends and vibrato still glide, but it
  cannot hop to the neighbouring semitone bin.
- **Reason.** The F2/D♯2 slips on long low notes. At E2 one semitone is 4.9 Hz, well
  inside the bank's 14 Hz bandwidth, so a decaying tail drifts between bins.

### 1d. Scoring before the build
- `tools/sim/poly_score.py` comping holdout (24–47), reproduced from the current
  v66 source: 40.9% both-note accuracy, 28.9% extra voices on single notes, and
  129 of 270 second-note entries found within 100 ms, with a 25.3 ms median among
  hits. Report hit rate with median and tail latency so missed notes remain visible.
- Separate [v66 solo baseline](POLY-SYNTH-DESIGN.md#L359): 118 false starts per
  minute and 11.8 ms median / 45.2 ms p90 to the right pitch. Record this separately
  from second-note acquisition and from audible pedal latency.
- Add overtone-ghost frame rate and duration to `tools/sim/poly_diag.py`. Re-score
  the named scale DI and low-E DI above against v66 and each candidate, along with
  GuitarSet solos. Keep a separate count for all second voices so the reported
  39% second-voice baseline on the scale can be distinguished from overtone-only
  frames.
- Score the poly eviction, mono jump protection and fade lock separately before
  scoring the combined build. Report dyad and root recall by interval and register.
- Compare a causal even/odd partial-envelope check on the current resonator
  data before tuning more fixed harmonic thresholds. The
  [tracking research update](NOTE-TRACKING-RESEARCH.md#phase-2b-evidence-and-next-benchmark)
  records the published method, upstream references and earlier tests.
- **Acceptance:**
  - overtone-ghost time at least halved against the measured v66 overtone-only baseline
  - extra voices lower
  - dyads within 2 percentage points of v66 for this test build. Production also
    requires improved tracking on real recordings and acceptable pedal listening.
  - solo and second-note latency and entry success at least as good as v66
  - simultaneous and staggered octave dyads still enter, and bends, legato,
    repicks and independent mutes still work
- Single-note tracker: `mono_eval.py` right ≥ 91.5% and fewer octave errors.

### 1e. BRASS ENSEMBLE balance (separate check for v67)
- Keep the trumpet-to-horn level ratio constant across notes. Today the sampled
  trumpet's level changes with register; the horn layer's does not.
- **Reason.** The scale sounds like it jumps octaves although the pitch is right.
- **Check:** `tools/sim/ensemble_bench.c` on a slow scale. The level difference
  between the two layers should vary by less than 3 dB across the guitar's range.
  Score the tracker independently of this voicing change.

**Owner listening gate after v67:**
- Start with STRING DUO, MONO SYNTH and a poly synth on the same scale, low-E
  sustain and dyads. Check TRUMPET and both ensembles on the scale as well.
- For latency, record **DI on the left channel and pedal output on the right in one
  take**. Phase 2 stage 1 still lacks a true audible-latency figure, and separate
  takes cannot give one.

---

## Stage 2: STRING DUO, make "one note" the safe default

**Need:** STRING DUO is the voice where a tracking error is most audible. A wrong
second note becomes a second instrument playing.

### 2a. SPLIT control (one of the five knobs)
- Decide after the v67 listening gate whether this control is useful. If tracking
  still loses low roots, run the stage 3 experiment before redesigning the Duo.
- All five knobs have existing meanings: CELLO, VIOLIN, TONE, ATTACK and RELEASE.
  Agree which control changes and specify the default and saved-preset mapping
  before adding SPLIT.
- Three settings:
  - **single:** one instrument, cello or violin by register.
  - **auto:** today's behaviour, after stage 1.
  - **two:** always split.
- **Reason.** "Single" prevents an extra instrument from sounding during solo
  playing. It still depends on the tracker for the primary note's pitch.

### 2b. Lead plus second (redesign if the listening gate calls for it)

- Give one allocator both voice slots. Keep the established played pitch
  while it remains credible. Admit the second only when there is evidence
  for an independent note.
- A separate mono policy running beside the allocator would duplicate
  listening, could disagree about the lead, and adds state and compute.
  Use the good single-note behaviour of TRUMPET as the comparison for Duo
  playing one note.
- Evaluate routing the two confirmed pitches by register when it improves
  the voicing. The parked lowest-of-chord effect serves a different job.
- Redesign only if the owner still hears wrong second notes in "auto".

### STRING DUO entry and release comparison

The host Poly bench now renders STRING DUO with `--type duo` in
`poly_score.py` and `poly_diag.py`. The Pulse results cannot stand in for
Duo because the effects use different gain envelopes. The v66 and v67
Duo holdout reached 123 and 125 second-note entries out of 270, with
median/p90 of 27.1/62.7 ms and 28.2/62.1 ms. The five-report counts
rose from 115 to 119. Scale ghosts on the owner DI fell from 36.67%
to 12.52%. On the first and second low-E sustains they went from
29.20% / 11.92% to 24.36% / 11.92%.

Use repeated `--guitar-pair MIDI WAV MIDI WAV` options with
`poly_score.py` to assemble independently muted dyads from isolated
recorded notes. The two orderings place the notes at 120 and 300 ms,
mute the first at 660 ms, and let the second finish at 980 ms. The
report gives sustained paired entry, the share of the mute tail with
the old pitch still sounding and still held, and the other pitch after
50 ms. It saves wet WAVs for listening. The three checked pairs were
51.09/63.22, 69.25/66.05 and 46.12/51.09. The new source plucks were
63.22 at 17.622 s in `03_SS3-84-Bb` and 46.12 at 3.871 s in
`02_SS2-88-F`. The other plucks were also used in the mono interference
bench.

The lower-first recorded octave never reached sustained entry in either
build. The reverse octave sounded both at 45.4 ms on v66 and 51.2 ms on
v67. After the mute, the surviving pitch was present for 89.2% of the
post-50 ms tail in one direction and 100% in the other. On two non-octave
orders, v67 still sounded the muted pitch for the full 320 ms tail while
the allocator held it for only 7.3% or 12.7% of that interval. The
renderer release is therefore part of the audible Duo problem. These
were layered recordings with known mute timing. Played strings and
on-pedal output still need the listening gate.


A host-only 25 ms individual release cap shortened the two problematic
320 ms tails. Applying it whenever a partner was held reduced first-hit
entries from 125 to 118. Applying it only when the partner still had a
matching candidate at the instant the first voice released reached
123/270, with median 31.4 ms versus 28.2 ms on v67. It cut the two mute
tails from 100% to 28.2% and 22.7%, kept scale ghosts at 12.52%, and
reduced extra comping voices from 54.5% to 45.4%. The new both-voices-held
diagnostic reached 80/270 versus 78/270 on v67. That diagnostic complements
the audible score. The slower first-hit median and missing lower-first
octave keep both release variants host-only.

#### Reproduction and entry inspection (2026-11-19, second pass)

The holdout, owner DI and synthetic results above reproduce exactly when
the bench is built with the full v67 `DSPDEF` list from
`firmware/build.sh` and run with `PXP_SET='ACQB=0.06 SECOND=0.58'`. Pass
the flags through a shell that splits words. zsh passes an unquoted `$F`
as one argument, which defines only the first macro and silently scores
something between v66 and v67.

The recorded-pair clips were temporary files. Re-extracting them with
`ffmpeg -ss ONSET -t DURATION` from the GuitarSet solo annotations gave
different pair results. A sweep of clip start from −30 to +3 ms moved
the lower-first octave between no entry and 71–205 ms. In the same
v67 build, the muted pitch remained for 100% of the tail in two to five
of the six orders, depending on the offset. Across all 60 offset and order
combinations, the latched cap gave identical entry times and reduced
each of those full tails to 17–37%. The exception was one combination
that never admitted the second voice.
The cap does not apply there, and the lone release still rang through the
tail. The tail result holds up. Single pair entry times are anecdotal.

A per-entry diff of the holdout explains most of the first-hit loss.
v67 credits a release tail as an entry when the new note repeats a pitch
that was just played. The unheld, decaying voice matches the new pitch
within 0–3 ms, as at 5.987 s in `01_Jazz1-130-D` (1.3 → 42.0 ms) and
7.604 s in `01_Jazz3-150-C` (0.2 → 20.5 ms). In 20.316 s of `00_SS3-84-Bb`
and 19.723 s of `01_Funk2-108-Eb`, neither build re-attacks the voice.
v67 counts a hit and the cap does not. Two losses are real. At 30.776 s
in `00_SS3-84-Bb` and 31.150 s in `00_SS2-88-F`, the allocator releases a
note that is still sounding. The long v67 tail hides that release.
`poly_score.py` now also reports entries whose new pitch comes from a
held voice. In this score, the entering pitch comes from a held voice
and the other pitch is sounding.

| Duo build | First hit | From a held voice | Five reports | Both held |
|---|---|---|---|---|
| v66 | 123, 27.1 ms | 117, 32.4 ms | 115 | 75 |
| v67 | 125, 28.2 ms | 119, 32.8 ms | 119 | 78 |
| cap whenever partner held | 118, 30.4 ms | 116, 33.0 ms | 109 | 80 |
| cap latched at note-off | 123, 31.4 ms | 120, 33.9 ms | 116 | 80 |

On the owner DIs, the latched cap kept scale ghosts at 12.52% and the
correct-note-absent rate within 0.05 points of v67. Two-voice time fell
from 50.13% to 34.99%. Low-E ghosts fell from 24.36% / 11.92% to
19.96% / 9.21%. Its remaining cost is the two allocator dropouts. The
listening question is whether the shorter mute tail is worth hearing
those dropouts.

The lower-first octave fails in the allocator, not the picker. The
picker lists 63.0 about 28 ms after the upper note starts.
`pxp_octave_evidence` never passes during the overlap at either clip
offset. Octave/root power rises from about 0.45 before the entry to
1.3–1.9 after it, and the fifth stays below 0.01 of the octave. The
double octave (needs > 0.04 × octave, measured 0.001–0.03) and sixth
(needs > 0.025 × octave, mostly 0.006–0.02) checks fail. Any admission
comes from the level guard. It occurred at 74 ms at one offset and
not at all at the other.

A host trial kept only the octave/root and fifth checks
(`PXP_OCT_TWO_CUES`, under `build/phase2b/duo-release/oct2`). The recorded
lower-first octave entered at 30.9 ms with 91.9% overlap. Its reverse
held 83.9% of the overlap instead of 16.1%. At the quietest synthetic
level, the octave entered at two more offsets. At the 0.13 level, the
−120 ms overlap rose from 8.8% to 87.0%. Holdout octaves entered at 9.926 s in
`00_SS2-88-F`, 21.879 s in `01_BN1-129-Eb` and 24.765 s in `00_SS1-100-C#`.
Entries from a held voice fell from 119 to 116, and five-report entries
fell from 119 to 113. The losses included five fourths or thirds and
two lower octaves. An admitted octave occupies the second slot when the
next real note arrives. The first low-E sustain also lost its correct
pitch at 6.124 s instead of 7.015 s.
The static spectral shape cannot separate a held note's own second
harmonic from an added octave. The rise in octave/root power after the
held note settles is the stronger cue. Test it next as a replacement for
the four static checks.

---

## Stage 3: check the octave on the waveform

**Need:**
- The 19% of time with no voice on the played note, much of it an octave up (G3 read
  as G5).
- Power chords losing the root.

**Rule.** Before a voice starts, compare the waveform's periodicity at the
candidate pitch and at plausible lower pitches. Start with low single notes.
Score power chords separately and require corroborating evidence, such as root-band
energy or harmonic support. A fifth can produce apparent periodicity below the
played root. If evidence is ambiguous, leave the candidate unchanged.

**Memory.**
- Start with the existing 256-float, 1/16-rate waveform history in `px_track`
  and the mono period check in `engine/px_track.h`. The poly path already passes
  its tracker state through the shared decimator. Populate the history from that
  stream and verify that each path updates it once.
- If the existing history lacks enough resolution, size a separate buffer:
  - at full rate, two periods of E2 are about 24 ms, which is 1,070 floats, about 4.3 KB;
  - at quarter rate, about 1.1 KB.
- Check the actual target RAM section and CPU cost before committing to a larger
  buffer. Consider a pool borrow only if the smaller options fail and a larger
  buffer has a measured benefit (section 9).

**Acceptance:**
- The played-note-missing share drops from 19% to under 10% on the owner's scale DI.
- The GuitarSet octave error rate drops.
- Power-chord roots (E2+B2, A2+E3 and similar) are found in most of the chord's
  duration.
- No new low ghost notes on single notes or chords, and no slower audible attack.

**Reason it follows v67:**
- It touches the same allocator.
- It may solve the remaining Duo errors before the redesign needs a second tracker.

---

## Stage 4: make every HQ type sound like its original

**Need:** the owner's rule. SQUARE HQ is about 4 dB louder and darker. CHURCH ORGAN
HQ's 2' comes from the band bank, unlike the original. The other pairs were never
checked for sound match, only for "on pitch".

**What to do:**
- Share controls, voicing, filters and envelope code where practical. Keep the
  original renders as references. Waveform-driven comparators, dividers and
  tracked oscillators can need distinct source generation to reproduce the same sound.
- The pitch source is either:
  - the band bank, for the organs, strings and choir, or
  - the tracker, for SQUARE, PWM and BASS.

**Check:** a new sound-match test.
- Render both types on clean single notes held long enough that both track
  correctly: GuitarSet solos and synthetic plucks across the neck.
- Compare level (within 0.5 dB) and spectral shape: centroid within 5%, and the
  band energies below 200 Hz and above 3 kHz within 1 dB.
- Compare wet-only tone and the mixed output at matching settings. Include
  attacks and releases so the sustained-note metrics cannot hide voicing differences.
- Add it to `tools/sim/syn_compare.py`, alongside today's pitch and wobble scores.

**Pairs:**
- SQUARE / SQUARE HQ (SFX)
- PWM / PWM HQ
- BASS / BASS HQ, already retired. Re-check the sound match anyway.
- ORGAN / ORGAN HQ
- CHURCH ORGAN / CHURCH ORGAN HQ
- STRINGS / STRINGS HQ
- CHOIR / CHOIR HQ

**Owner listening gate:** a quick A/B per pair at the same settings. They should be
indistinguishable on single notes.

---

## Stage 5: stable mono synths and independently voiced ensembles

- **Pure synths.** Run MONO and the tracked SQUARE, PWM and BASS variants on
  the existing lead-note policy. While the current note has credible
  support, keep following it through a second string or an overtone. Let
  a credible new pick or a persistent legato interval take over. Use the
  originals as sound references, including the comparator and divider
  voicing where it matters. Score intended-note retention, unwanted pitch
  jumps, recovery after a mute, and attack on solos, bends, legato and
  passages with a second note. BASS's sub-octaves derive from one lead.
- **Independently voiced ensembles.** Use the existing two-note allocator
  for STRING DUO and prototype independently played pitches for STRING
  ENSEMBLE and BRASS ENSEMBLE with the same allocator. Share the renderer
  where practical and keep each played note's onset, pitch, and release
  independent. Preserve the current layered mono sound as the A/B
  reference. Measure both notes and individual mutes on real dyads. The
  organ, strings, and choir waveform effects already handle chord input.
- **Acceptance.** Check matched wet output, audible arrival and peak block
  load on the pedal with Smart Gate and a representative chain. Verify
  per-voice memory for the acoustic ensembles before proposing additional
  voices. Use the v67 A/B to determine which tracker rules are worth carrying.

### Mono lead interference benchmark

Run `python3 tools/sim/mono_interference.py BENCH66 BENCH67` with the
`mono_bench.c` executables. The synthetic cases establish a lead at 100 ms,
introduce a second note or strengthen a partial at 340 ms, and mute the
lead at 760 ms in the two-note cases. Score lead pitch in the overlap and
the first five consecutive reports of the remaining note after the mute.
The report identifies a tracker that had switched before the mute.

The 16-case comparison used four synthetic intervals and three real
GuitarSet pairs, each at second-to-lead input gains of 0.55 and 1.20,
plus two rising-partial cases. The real clips came from solo annotations
at 4.694 s in `02_Rock3-148-C` (48.18), 21.521 s in `00_BN2-166-Ab`
(51.09), 2.812 s in `01_BN3-119-G` (69.25), 4.709 s in `03_BN1-147-Gb`
(66.05), and 1.660 s in `03_Jazz1-130-D` (72.04). The three pairs
were 48.18/51.09, 69.25/66.05 and 48.18/72.04. The scorer accepts these
isolated clips with repeated `--pair LEAD_MIDI LEAD.wav SECOND_MIDI SECOND.wav`
arguments. These constructed overlaps provide known note identities and
mute timing. Played dyads still need listening and verification.

v66 and v67 retained the lead for the same 45.1% of overlap frames on
average, with all 16 leads correctly established. In v66, the upper-octave
cases retained 95.8–100%, but adding a lower octave retained only
6.2–10.7%. On the synthetic H2 rise the lead was absent from the candidate
list during every wrong-pitch frame. It was absent during 94.6–95.4% of
the wrong-pitch frames in the lower-octave cases. The stronger fourth
case also had no listed lead during 98.9% of its wrong frames.

A host-only test let a settled lead remain when it was listed within
50 cents at one fifth or more of the top salience. Mean retention reached
46.3%. On the scale DI it reduced octave-below frames from 5.16% to
1.18%, and both low-E sustains matched v66. The longer owner recordings
`mvave` and `mvave2` fell from 74.2% and 84.4% of notes correct to
71.4% and 79.2%, while GuitarSet solo median arrival slowed from
6.3 to 7.6 ms. Keep v66 as the reference. Measure the evidence for
the held pitch before the candidate shortlist discards it. Retaining
only pitches already on that list cannot cover most of these misses.

### Support for a held pitch before ranking

An annotated-pitch probe compared the existing local maxima with all 15
active ERB band frequency estimates and their bandwidth-normalised
amplitudes. It asked whether the lead still had at least 20% of its own
median pre-interference amplitude within 50 cents. Across the 16 cases,
the maxima retained that evidence in 32.5% of wrong-pitch overlap
reports. All bands retained it in 66.4%. From 65 ms after the lead mute,
the all-band test still fired in 2.6% of reports. These scores use the
known lead pitch. They do not account for a tracker latching onto a wrong
pitch or changing its reference level.

The causal prototype compared band power near its own held pitch with a
slowly decaying reference learned while the main estimate agreed. Holding
on any disagreement raised mean lead retention from 45.1% to 57.9%, but
correct scale frames fell from 86.41% to 73.47%, and held-out solo accuracy
fell from 91.5% to 90.1%. Limiting the decision to jumps up by a factor
of two or three, with the existing detected-pick path intact, kept the
lead throughout both synthetic partial rises. Mean retention was 51.9%.
The scale and both low-E sustains matched v66, but solo accuracy was
91.4% and octave errors rose from 120 to 122. The lower-second-note
cases also remained weak. Keep this host-only. The evidence justifies
testing the held pitch before ranking, with a separate check that the
pitch was correctly acquired and should survive a legato transition.

### Short-period check before choosing an octave below

Per-note comparison of the raw-band hold found five correct solo notes
that became octave-down errors, two octave-up errors that became correct,
and one octave error that moved to a different wrong pitch. In the new
octave-down cases, the held low pitch persisted after the main estimator
returned to the correct note. Requiring the held pitch to appear in the
spectral shortlist before learning its reference level left the same
122 octave errors. Requiring distinct spectral partials before accepting
a longer period also finished at 122, reduced mean lead retention to
39.3%, and reduced accuracy on the longer `mvave` DI.

The simpler trial, `PXT_LOCAL_PERIOD_FIRST`, runs the existing local
period refinement when the exact spectral lag gives a poor match, before
the existing check of longer lags. With the hold rule absent, it reached
5,664/6,187 correct and 119 octave errors versus v66's 5,660 and 120, at
the same 6.3/40.0 ms median/p90. On solo takes 144–167 it matched v66 at
1,658/1,720 correct and 26 octave errors. The scale DI, both low-E sustains
and the synthetic minor-third and octave legato checks matched v66.
Average interference retention was 45.5% versus v66's 45.1%. Combining
the period check with the band hold again stopped both synthetic overtone
jumps, but reached 121 octave errors. The period experiment is available
for host comparison. Meaningful retention through a second string and
stronger natural overtones remains unproven.

To reproduce the local-period comparison on the poly-test mono path,
build `tools/sim/mono_bench.c` with `-DPX_MONO_LAYER=1 -DPX_ERB_LAYOUT2=1`
and `-DPXT_LOCAL_PERIOD_FIRST=1`. Run `mono_eval.py --offset 30 --solo 60`
with the v66 and candidate benches, `mono_interference.py`, and
`poly_diag.py --mono --owner-di 'My Audio'`.

---

## Stage 6: show where the band-bank HQ types actually win

**Need:**
- ORGAN, CHURCH ORGAN, STRINGS and CHOIR HQ measured equal or slightly worse on
  pitch than their delay-line originals.
- They cost 30–60% more host time (`docs/CATALOGUE-CONSOLIDATION.md`).
- Under the owner's rule, they need to track better, or they have no reason to exist.

**Likely reason the scores show no win:** in these voicings the 8' voice is the
guitar itself, so both versions share most of the output, and the overall on-pitch
score cannot show the octave voices' difference.

**What to measure, on the octave voices alone** (8' muted, or via a debug output):
- arrival time on low notes. The 26-band bank cut this from 16.9 to 11.1 ms in v62.
- wobble on chords
- chord accuracy at OCT −1 and +1
- clicks on picks (the v64 click score)

Start these host measurements alongside v67. Repeat the relevant comparisons after
any voicing changes before making the retirement decision.

**Result:**
- If HQ wins: retire the original in stage 7.
- If HQ doesn't win: say so to the owner. Either improve the bank for these voicings
  or drop the HQ variant. The owner decides.

---

## Stage 7: retire originals to HQ, where HQ meets the rule

**Need:** finish the catalogue consolidation the right way round.

**What to do:**
- For each pair that passes stage 4 (sound) and stage 5 or 6 (tracking), map the
  original's type to the HQ type at load. The same five controls keep their meaning,
  following the `px_type_canonical` / `px_block_canonical` pattern (v63/v64).
- **Undo v64's PWM retirement.** PWM SYNTH HQ presets currently load as PWM SYNTH.
  Once PWM HQ passes, reverse it so PWM SYNTH loads as PWM SYNTH HQ.
- Requalify BASS SYNTH HQ against the original before carrying the poly-test mapping
  into standard firmware.
- Check that:
  - factory presets and the owner's saved slots load correctly (slot 24 is the known
    migrated one);
  - the editor shows the right names;
  - the flash is not rewritten.
- Update `docs/SOUNDS-AND-CONTROLS.md` and `docs/CATALOGUE-CONSOLIDATION.md`.

**Owner listening gate:** the retired types play their HQ replacements and sound
like the old ones.

---

## Stage 8: move the agreed changes into the standard firmware and delete old code

**Need:** everything so far lives only in the poly-test channel. The owner's normal
firmware is still v57-era for these voices.

Promote in small batches. POLY OCTAVE HQ with OCT −2 has its own acceptance path
and can be verified independently of the tracked synths. Move each tracked effect
after its production gate and delete the superseded code after its migration passes.

**Move into the standard build** (flags into the default build in
`firmware/build.sh`):
- the stage 1 trackers
- MONO SYNTH on the single-note tracker (`PX_MONO_LAYER`)
- the separate 26-band octave bank (`PX_ERB_LAYOUT2`)
- OCT −2 (`PX_SUB2_TEST`)
- the retirements and migrations (`PX_CONSOLIDATE_TEST`)
- the Poly synths, ensembles and duo the owner accepts (`PX_POLY_TEST`,
  `PX_ENSEMBLE_TEST`)
- POLY OCT FUZZ LEVEL, already there

**Delete:**
- `syn_track`, MONO SYNTH's old period detector
- BASS SYNTH's flip-flop divider
- the delay-line POLY OCTAVE effect

  Keep the delay-line shifter itself: WHAMMY, HARMONIZER, DETUNE and BARITONE use it.
- each original retired in stage 7, and its now-unused code

**Check:**
- `make test` passes.
- The equivalence test against the previous standard build differs only where
  intended. Record the expected differences.
- Pedal CPU check on every changed SYN and PITCH type, plus representative chains
  with Smart Gate on and off, with 0 late blocks.
- The memory table is updated.

**Reason for the production gates:** the migrations affect saved presets and the
catalogue for every user. The tracking and sound criteria apply to each batch.

---

## Stage 9: regroup the catalogue and evaluate the firmware split

### 9a. Effect reorganisation (both firmwares)

**Need:** the current grouping is unintuitive.
- SQUARE SYNTH is in SFX.
- POLY OCT FUZZ is in SYN, though it is a fuzz.
- Octave effects are split between PITCH and SYN.
- 12-STRING, ACOUSTIC, SITAR and BARITONE are guitar emulations filed as synths.
- SYN holds about 30 unrelated types.

**Proposal:** keep the 12 internal block numbers. Group the pedal and editor menus
by how players look for effects, independent of their stored block or DSP position:
- **OCTAVE / PITCH:**
  - OCTAVER
  - POLY OCTAVE HQ (with OCT −2)
  - WHAMMY
  - HARMONIZER
  - DETUNE
  - BARITONE
  - 12-STRING
- **SYNTH, guitar-shaped** (they shift the guitar itself, chords work):
  - ORGAN
  - CHURCH ORGAN
  - STRINGS
  - CHOIR
  - STRING MACHINE
  - POLY BRASS
- **SYNTH, tracked** (they follow played notes):
  - MONO SYNTH
  - SQUARE
  - PWM
  - BASS
  - the Poly synths
  - STRING DUO
- **INSTRUMENTS:**
  - TRUMPET
  - BOWED CELLO
  - CELLO ENSEMBLE
  - STRING ENSEMBLE
  - BRASS ENSEMBLE
  - ACOUSTIC
  - SITAR
- **DRIVE:** add POLY OCT FUZZ as an octave fuzz.
- **SFX:** wahs, ring mod, bitcrusher, slicer, lo-fi.

**Optional moves between DSP blocks** need a separate preset design:
- SQUARE to SYN
- BARITONE and 12-STRING to PITCH
- POLY OCT FUZZ to DRIVE

The v64 canonicalisation maps a type and parameters within a block. It cannot
move an effect or resolve a destination already in use. Before moving any effect,
define how to preserve both active effects, their parameters, the chain order and
footswitch or MIDI assignments. Verify saved presets and editor round trips before
shipping a move.

**Reason it follows stage 8:** fewer types after the retirements means a shorter,
clearer list. Reorganise once, not twice.

### 9b. Update `docs/spacesaver.md`

Update the spec now, alongside v67, so it can inform later memory decisions. The
spec predates Phase 2. Add:
- the effects built since: Saw, Arcade and FM Poly, the ensembles, STRING DUO,
  OCT −2, MONO SYNTH on the note tracker
- the current test-channel mappings and the per-pair HQ retirement criteria
- the 9a grouping, for both the Full and the Synth firmware
- measured v66 memory per family, replacing estimates:

  | Family | DSP pool | General RAM | RAM code | Low RAM | Flash |
  |---|---|---|---|---|---|
  | NAM | 86 KB | 17.7 KB | 5.7 KB | — | about 108 KB code, plus the factory models |
  | AMP / CAB / IR | — | 10.3 KB | about 2.2 KB | 9 KB | — |

- the tracker and renderers run from flash
- the Phase 2 evidence on current tracking errors, and the experiments needed to
  show which improvements benefit from more memory
- the NAM-pool borrow option (9c)
- installer and editor variant handling

### 9c. Decide the split

**Decision gate:** keep one firmware while the tracker work fits. Measure CPU and
each target memory section before estimating the benefit of a split.
- v67 can start within the current budget. Measure stage 3 with the existing
  history first. The dual-tracker Duo, extra voices and a faster onset bank may
  need more state or code space.
- If a proven candidate needs more data space, test borrowing NAM's pool. Model
  loading and scratch already use the region. Define exclusive ownership and
  fallback behaviour, and test preset changes, NAM enable and disable, model loads
  and bench access. The pool cannot expand the RAM-code window.
- Split only after a host-proven design sounds or tracks better on the pedal and
  a measured memory region prevents it from fitting in Full. Measure the headroom
  a stripped profile actually recovers before building the variant UI.
- Two firmwares mean two packages, variant-aware installer and editor, and rules for
  presets across them.

---

## Section 10: parked or closed

- **LOWEST OCTAVE HQ** (v54 lowest-note mode): parked at the owner's verdict. It
  stays in poly-test only, out of the standard build, with no further work. Two
  ideas would come back if it is reopened:
  - the harmonic-fit second opinion (about 2.5 KB)
  - candidates below 60 Hz

  Revisit the lowest-of-chord feature only if a distinct musical need remains.
- **HQ octave phase coherence and transient handling** (Phase 2 stage 6): tested on
  the host in v64, neither a clear win. Closed unless the owner hears warble or
  clicks on POLY OCTAVE HQ.
- **Pulse Poly low notes:** reported weak on v65. Expected to improve with stage 1
  (fewer ghosts stealing the second voice) and stage 3 (octave check). Re-check at
  the stage 1 gate, then decide whether it needs its own work.
- **Two-speed tracker** (a fast bank for note starts, the slow bank to confirm the
  note): the most promising latency idea. Prototype it on the host after stage 3,
  when latency becomes the owner's main complaint again. It needs about 2 KB of RAM
  and about 1 KB of RAM code, which is the split or borrow question.
- **ARCADE POLY register split:** after Phase 2b, let the player choose one Arcade
  tone for the low register and another for the high register. Start with a split
  at G3 so open low E, A and D use the low tone and open G, B and high E use the
  high tone in standard tuning. Fretted notes follow their detected pitch. Keep
  the tone selected at note-on for its bends and release, and audition dyads with
  one note on each side of the split.

---

## Order and gates at a glance

| # | Stage | Build | Memory | Owner gate |
|---|---|---|---|---|
| 1 | Score both owner DIs and GuitarSet, fix overtone ghosts and fade slips, check brass balance separately | v67 | measure map deltas | Listen with one stereo DI + wet take |
| 3 | Recover missed notes and low roots with a waveform check | next prototype | existing history first | Listen for low notes, chords and attacks |
| 2 | Reassess Duo after v67. Add SPLIT if useful, or try lead plus second if still needed after the root work | as needed | measure tracker state | Listen on solos and genuine dyads |
| 4, 6 | Capture original HQ references and isolated octave-voice scores alongside v67. Match HQ voicings | host and test build | measure per pair | Level-matched A/B |
| 5, 7 | Stable mono synths and independently voiced ensembles, then qualify each pair | after tracker gate | measure per pair | Listen on solos, interference and dyads |
| 8 | Promote the accepted octave and tracked changes in batches, then delete superseded code | release | update section budgets | Presets, audio and pedal checks |
| 9a | Group pedal/editor menus, with a separate decision on any cross-block move | after catalogue decisions | — | Preset and chain-order checks |
| 9b, 9c | Refresh the space-saver spec now. Decide on a profile split after measuring a proven candidate | spec in parallel, split conditional | region-specific budget | Decide from pedal evidence |

Build numbers after v67 depend on which experiments pass and which features the
listening checks justify.

## Stage 1 progress

The owner DI check now runs both recordings through the actual host benches via
`tools/sim/poly_diag.py BENCH --owner-di 'My Audio'`. Add `--mono` for
`mono_bench`. The scorer uses the left channel at 44.1 kHz and the original gain,
with PCM16 conversion for the mono bench. It uses fixed note ranges derived from
the DI pitches and partials. The v66 reference is engine revision `f12462e`,
with the bench's Poly flags and `PX_MONO_LAYER` for the mono run.

| v66 measurement | Result |
|---|---:|
| Scale frames with two sounding voices | 39.24% |
| Scale frames with two voices including an overtone ghost | 35.15% |
| Scale frames with the played note absent | 17.13% |
| First / second open E sustain with an overtone ghost | 25.07% / 9.80% |
| First / second open E sustain with a neighbouring pitch | 0.18% / 3.08% |
| Mono scale frames on the correct pitch / upper harmonic / octave below | 86.41% / 2.02% / 5.16% |

The fixed note ranges differ from the exploratory recording analysis above.
Use the fixed-range v66 numbers when evaluating candidate runs. The v67
half-ghost limit for this benchmark is 17.575%.

- Eviction within 100 ms cut scale ghosts to 19.17%. Adding the fifth- and
  sixth-harmonic allocator guard reached 16.82%, but true octave dyad recall
  fell from 12.7% to 10.4% on the GuitarSet development set. A simple power-ratio
  limit or narrower frequency tolerance gave back too much of the ghost reduction.
- The added octave tests found that v66 already misses simultaneous E2/E3 and the
  upper string starting 20 ms later, even with the upper string slightly detuned.
  The current vetoes can reject a true second note before allocation. Continue
  measuring candidate evidence and voice decisions separately.
- Delaying every mono third- or fourth-harmonic jump reduced GuitarSet held-out
  accuracy from 91.5% to 91.3% and increased octave errors from 120 to 121. Requiring
  a strong incumbent pitch candidate made no difference on either owner DI. The
  scale trace showed that several wrong notes were selected during acquisition
  after a pick. Checking candidate periods up to 900 Hz with stricter evidence
  for a lower pitch improved held-out accuracy to 92.0% and cut octave errors to
  113. Scale upper harmonics fell from 2.02% to 1.70% and the octave-below rate
  fell from 5.16% to 4.95%. Neither low-E sustain changed.
- Debouncing nominal bins alone left the open-E slips unchanged and worsened the
  synthetic bend result. A slew limit on the continuous phase estimate reduced
  second-sustain slips from 3.08% to 0.37% with the bend result steady at 88.1%
  within 50 cents.
- Comparing even and odd partials recovered the five tested synthetic octave
  entries. Combining that evidence with a short fade after eviction and the
  low-note slew limit reduced scale ghosts to 17.18%. It improved GuitarSet
  development octave recall from 12.7% to 17.2%. On the frozen comping holdout,
  both-note accuracy rose from 40.9% to 41.3% and median second entry improved
  from 25.3 ms to 23.9 ms. The entry count fell from 129 to 128 out of 270 and
  extra voices rose from 28.9% to 29.3%, so that candidate still misses the
  Stage 1 gate. A tighter octave-evidence check with seven-report confirmation
  cut scale ghosts to 9.22% and comping extras to 27.7%. It retained 39.5%
  both-note accuracy and a 25.3 ms second-entry median. It had 127 of 270 entries,
  two fewer than v66. Continue with the same evaluation criteria.
- Keeping the six-report confirmation and giving an active second voice a
  5% entry power floor retained 129 of 270 entries at 25.3 ms, lowered comping
  extras to 28.6%, and scored 40.6% on both-note accuracy. Scale ghosts were
  13.15%. Solo arrival was 11.8 ms median / 45.0 ms p90, with 117 false starts
  per minute. Solo reach within 150 ms was 95.6% against v66's 95.8%, so the
  candidate is still for host testing. Shortening first voice confirmation
  increased false starts on the development set.
- Delaying the low-note slew limit until the pitch has settled preserved the
  improvement on both sustains in the low-E take. With the higher acquisition floor applied
  only above the note already held, the candidate recovered all 2,669 baseline
  solo entries and all 129 second-note entries. Solo median/p90 were
  11.8/45.2 ms and second-note median/p90 improved from 25.3/65.6 to 25.3/62.4 ms
  with a 6% upper-voice floor and a 58% held-voice threshold. Extra comping
  voices rose to 29.2% versus 28.9% on v66. The host gate remains open. A
  stronger octave-to-root power check slowed some genuine octave pairs.
  On the owner DI, the frozen candidate reached 16.08% scale ghosts
  and 21.40% / 9.80% on the two low-E sustains. Both v66 and the frozen
  candidate had zero frames with both notes correct in the four fixed
  duo and power-chord interiors.
- The comping trace breakdown attributes the remaining increase mainly to
  upper octaves that remain held during annotated single notes. Dropping them
  on loss of static evidence reduced their duration and increased false starts.
  An extra report of confirmation reduced extras to 28.7%, missed one baseline
  second note and raised second-note p90 from 65.6 to 67.8 ms. Compare temporal
  evidence from both partial groups before further threshold sweeps.
- A separate GuitarSet slice (takes 144–167 for each input set) compared the
  locked 6% upper-floor candidate with v66. The candidate reached 1702/1726
  solos versus 1703/1726, and found 96/322 dyad entries versus 95/322. Extra
  comping voices rounded to 22.6% on both. This audit leaves the original
  Stage 1 acceptance gate open.
- A four-report average of even-minus-odd partial power in dB, motivated by
  Zhou et al., passed 33.24% of genuine upper-octave entry frames on GuitarSet
  takes 24–47, up from 19.55% for the static check. The rate on difficult
  single-note frames fell from 2.57% to 1.64%. The audit on takes 144–167
  found 35.25% vs 19.78% for upper entry and 0.45% vs 1.30% for single
  notes. On the scale DI, single-note admissions fell from 0.12% to 0%;
  on the low-E DI they rose from 0.04% to 0.34%. The benchmark uses the
  annotated root and measures candidate evidence. The voice bench measures
  the ghosts that actually sound.
- In the allocator, replacing the static check with that average reduced
  simultaneous synthetic octave recall from 58.1% to 15.0%. Using the average
  only to add a confirmation report on weak upper-octave candidates retained
  58.1% recall and brought scale ghosts to 11.52% and comping extras to 28.7%.
  It reached 2,668 solo entries and 128 second-note entries, one below v66
  in each case, and second-note p90 reached 67.8 ms versus 65.6 ms on v66.
  The upstream pitch trace already showed the played octave in both lost
  cases. In `01_BN1-129-Eb_comp` at 21.879 s, E♭4 sounded after 34.9 ms
  with the frozen candidate. In `01_Jazz3-137-Eb_solo` at 7.864 s, E♭5
  sounded after 97.8 ms. The added report left both silent throughout
  their scoring windows. These two cases provide direct checks for any
  subsequent change to octave confirmation.
- The synthetic E2/E3 check now covers 21 combinations of relative pick level
  and upper-note offset (0, ±20, ±80 and ±120 ms). Within 150 ms, v66 found
  both voices in 13 cases and the frozen candidate in 17. All four remaining
  misses have the upper string at half the lower string's amplitude and
  arriving at the same time or later. When the softer upper string starts
  first, the frozen build briefly reaches both pitches but keeps both for
  less than 1% of the overlap in each case. In the four misses the picker
  never listed E3. The octave rescue offered it for at most 1, 1, 3 and 0
  consecutive reports at offsets 0, +20, +80 and +120 ms respectively.
  Confirmation alone cannot recover a candidate absent at +120 ms.
- A host trial used harmonic history only after both voices had held for
  100 ms. Four weak reports attenuated the upper voice by 20 dB while
  preserving its pitch and allocator state. It kept all 2,669 solo entries,
  129 comping entries and the frozen candidate's 21-case octave results.
  Scale ghosts were 13.67% and low-E ghosts 21.40% / 2.63%. Comping extra
  voices still measured 29.0% versus 28.9% on v66, and false starts increased
  to 121/min. Adding hysteresis brought comping extras to 28.9% but lost one
  comping entry and cut simultaneous octave overlap from 58.1% to 35.9%.
  Protecting pairs that had shown the static octave evidence recovered that
  overlap, but still lost the comping entry and reached 29.1% extras.
  The weak-upper misses and the reappearance of attenuated ghosts remain
  separate checks for the next allocator trial.
- Tracing voice acquisitions on GuitarSet takes 24–47 found 130 additional
  held frames where the tracker had started an octave that was absent at
  the acquisition timestamp. Correctly played octave tails accounted for
  46 additional frames, while several non-octave categories improved. The
  net change was 109 more two-voice frames during annotated single notes.
  A strong upper harmonic could escape the allocator guard as the lower
  voice's fine pitch wandered, even though its nominal note stayed fixed.
- `PXP_NOMINAL_OCTAVE_TEST` also checks the octave against the nominal lower
  note after it has held about 100 ms and is below 110 Hz. Existing harmonic
  evidence can still admit an independent octave. The original holdout
  reached 28.8% extra voices (v66 28.9%), 40.9% both-note accuracy, all
  2,669 solo entries at 11.8/45.2 ms median/p90, and all 129 comping entries
  at 25.3/62.4 ms. Scale ghosts were 16.08% and the low-E sustains measured
  17.71% / 9.80%, versus v66's 25.07% / 9.80%. The synthetic matrix still
  reached both notes within 150 ms in 17/21 cases. The medium-pick +120 ms
  case entered at 79.3 ms (previously 67.7 ms), and the simultaneous
  case remained at 97.7 ms.
- The separate audits still show entry regressions. On takes 144–167 the
  candidate reached 1,702/1,726 solos against v66's 1,703, and 96/322
  comping entries against 95. The missing solo entry is E4 at 16.836 s in
  `05_Jazz3-150-C_solo`. It is a true major third above a held C4, and its
  power repeatedly falls below the 6% second-voice floor during confirmation.
  Letting a pending non-harmonic dip below that floor during confirmation
  restored the E4 but
  raised original-holdout extras to at least 29.1% and comping p90 to
  67.8 ms. A fresh 72–95 slice reached 1,839/1,868 solo entries in both
  builds and reduced extras from 21.2% to 20.9%, with 104/327 comping
  entries versus v66's 106. The change remains opt-in pending those
  entry regressions and a pedal check.
- A phase check compared each harmonic's report-to-report phase advance
  with the root's advance and required greater disagreement in two even
  partials than in the odd partials. With a 1 Hz excess referred to the root,
  it admitted 5.0% of scale control reports, 13.2% of low-E control reports
  and 6.9–16.2% across the four weak-upper synthetic offsets. This simple
  phase residual overlaps the low-E control too much to add missing
  candidates. This was a host trace with the root supplied by the fixture.
- A second phase experiment measured changes over several reports. It removed
  the root's phase from each partial and compared the average of four recent
  complex amplitudes with the preceding twelve. A candidate needed a residual
  at H2 and either H4 or H6 above the residuals in the odd partials. At the
  same development control rate as the static check, it admitted 19.55% and
  18.35% of upper-entry frames on the original and audit holdouts, versus
  33.24% and 35.25% for the four-report power contrast. It admitted 0% of
  scale controls and 0.04% of low-E controls. Normalising each residual by
  its previous variability reduced held-out recall further. Only one weak
  synthetic offset produced any qualifying reports with these variants
  (+80 ms, two reports in the 150 ms entry window). The phase experiment
  stays host-only.
- The synthetic Poly bench now checks a phase-continuous E2→G2→E2 legato
  phrase with a shared decay envelope. v66 and the frozen candidate reached
  the hammer-on in 51.2 ms and the pull-off in 52.7 ms, with zero two-voice
  frames. With the same fixture and a second E2→E3→E2 run, v66 MONO SYNTH
  and the 900 Hz period-check candidate had identical first-hit timings
  (38.1/29.5 ms for the minor third, 23.6/38.2 ms for the octave).
  Played legato is still part of the pedal listening check.
- A host comparison with Cycfi Q's BACF detector covered both owner DIs and
  32 native-pitch bass plucks. With its low bound set to 27 Hz for bass, Q
  detected all eight B0, E1 and A1 plucks at each pitch where v66 MONO SYNTH
  detected zero. Q took 109–126 ms median across those pitches. At guitar E2
  it took 126 ms versus 55 ms on v66, and the stock Q setup followed fewer
  notes in the owner scale while following more of the low-E sustains.
  Retain Q as a bass reference and keep the current mono guitar path for v67.
  [Bass comparison and fixture setup](NOTE-TRACKING-RESEARCH.md#cycfi-q-on-isolated-bass-notes)
  [Method and reproduction](NOTE-TRACKING-RESEARCH.md#octave-evidence-results)
- The brass benchmark isolated trumpet (LAYER 0) and horn (LAYER 100 minus
  LAYER 0) from matched synthetic notes between E2 and C5. At TONE 50, the first
  register correction reduced the span of their balance from 3.97 dB to 1.63 dB.
  The separate dark-tone trim brought the span to 1.91, 1.59, 1.63, 1.42 and
  1.42 dB at TONE 0, 25, 50, 75 and 100 respectively. Pedal listening and CPU
  verification are pending.

To reproduce the opt-in nominal-octave host build and score both owner DIs:

```sh
cc -O2 -Iengine -Ifirmware \
  -DPX_POLY_EVICT_TEST=1 -DPXP_EXTEND_GUARD=1 -DPXP_EVICT_RATIO=6 \
  -DPX_POLY_OCTAVE_EVIDENCE_TEST=1 -DPXP_EVICT_FAST_RELEASE=1 \
  -DPXP_OCT_ODD_LIMIT=0.10f -DPXP_OCT_SIXTH_FLOOR=0.025f \
  -DPX_POLY_FADE_TEST=1 -DPXP_FADE_SETTLE=16 -DPXP_LATE_ROOT_TEST=9 \
  -DPXP_NOMINAL_OCTAVE_TEST=1 tools/sim/pulse_poly_bench.c firmware/px_math.c \
  -lm -o build/phase2b/poly-nominal-low
PXP_SET='ACQB=0.06 SECOND=0.58' python3 tools/sim/poly_diag.py \
  build/phase2b/poly-nominal-low --owner-di 'My Audio'
```

### v67 test image

Build `OP_CHANNEL=poly-test OP_TEST_VERSION=67 sh firmware/build.sh` in
the Linux JieLi toolchain container. The recipe enables the Stage 1 tracker
and brass experiments for this test version. It adds the confirmed-bin
start for a low second voice below an existing held note. The original
holdout still reached 2,669 solos and 129 second-note entries, with 28.8%
extra comping voices. On takes 72–95 the latter change restored one entry,
for 105/327 against v66's 106. Across the three slices (24, 72 and 144),
the counts for five consecutive correct reports were 102/270, 81/327 and
76/322 for v67 against 101/270, 80/327 and 77/322 for v66. Report that
measure alongside the first-hit counts. One nominal-guard miss on the
fresh slice was scored as a v66 first hit from a voice already fading before
the annotated attack. Another involved pitch settling just over the 100 ms
limit. A third lost the held low note while its partner entered.

The channel-equivalent mono retest did not reproduce the earlier
stricter-period trial. With the actual v67 flags and 6,187 held-out notes,
v66 and v67 both scored 91.5%. Octave errors went from 120 to 121, so v67
misses the mono acceptance gate. On the owner scale, correct frames rose
from 86.41% to 87.06% and upper harmonics fell from 2.02% to 1.65%.
The mono low-E results matched v66. Use the v66 mono layer as the baseline
for the simpler continuity design.

The isolated target build wrote BlackBox_967 and PocketX_967, with both
package hashes verified against the manifest. Relative to v66, the app grew
1,484 bytes, RAM use grew 16 bytes, and the RAM code window grew 28 bytes
with 280 bytes free. The RAM/flash call check passed and standard DSP
matched HEAD in 1,022 equivalence runs. The test image is a listening
reference. The pedal's real-time load, audible latency and owner verdict
are pending. Channel-equivalent host checks across both owner DIs and two
bundled loops gave bit-identical v66/v67 output for POLY OCTAVE HQ,
POLY OCT FUZZ and CHURCH ORGAN HQ. The general sweep's existing
minimum-level octave-fuzz flag also reproduced on v66. `make test` exited
successfully with the existing engine sweep and long-run flags. The optional
NumPy NAM comparison, factory rollback, and Playwright checks were skipped.
[Architecture review and feature removals](TRACKING-ARCHITECTURE-REVIEW.md)

## Parallel HQ and space-saver work

The first `syn_compare.py --match` run saved dry inputs and paired wet audition
WAVs under `build/phase2b/hq-reference`. The sustained-note results confirm
the HQ voicing differences, including median level changes of +4.19 dB for
SQUARE, +6.95 dB for BASS and +8.72 dB for PWM at the bench settings. This
run covers eight synthetic notes. The updated bench also renders mixed
output at DRY or MIX 50, keeps the dry DI, and writes note locations and
per-note scores alongside the aligned audition WAVs.

```sh
cc -O2 -Iengine -Ifirmware tools/sim/ensemble_bench.c firmware/px_math.c -lm -o build/ensemble_bench
python3 tools/sim/syn_compare.py --match build/phase2b/hq-real-reference --match-real --takes 48
python3 tools/sim/syn_compare.py --match build/phase2b/hq-shared-trim --match-real --takes 48 --share-trim
```

Both runs selected 30 clean notes from 48 evenly spaced GuitarSet solo
takes. Each clip includes 60 ms of pre-roll, at least 380 ms of playing and
180 ms of unmodified tail. The sound comparison measures the middle half
of each note and screens for energy on the expected harmonic grid.
That screen can also pass octave errors. The wet attack uses the dry
energy onset and half the settled wet RMS. The release uses the last report
above −20 dB relative to the wet level just before the annotated note end.
The report gives paired counts because some attacks begin in the pre-roll
and some releases exceed the available tail.

At the standard trims, zero of the 30 played notes met all four limits
for any Stage 4 pair, wet or mixed. Most of the level difference came
from the block trim table. The original types have measured trims and
the HQ types use unity gain. A bench-only test applied each original's
trim to HQ, including its dry contribution.

| Pair | Median wet level difference before / with shared trim | Notes within all limits wet / mixed with shared trim |
|---|---:|---:|
| ORGAN | +10.72 / −2.70 dB | 0 / 0 of 30 |
| CHURCH ORGAN | +9.05 / −3.13 dB | 0 / 0 of 30 |
| STRINGS | +12.96 / −0.45 dB | 1 / 7 of 30 |
| CHOIR | +5.41 / −0.40 dB | 0 / 4 of 30 |
| PWM SYNTH | +8.54 / −0.18 dB | 2 / 0 of 30 |
| BASS SYNTH | +6.97 / +0.01 dB | 27 / 27 of 30 |
| SQUARE SYNTH | +4.20 / +0.00 dB | 1 / 0 of 30 |

For BASS, cutting just the HQ wet level left zero mixed notes within all
limits. Sharing the original's whole-block trim restored 27/30 in both
mixes. Two low-note misses came from HQ's 35 Hz floor, which raises its
lowest generated octave. With the floor disabled in a host prototype,
29/30 played and 8/8 synthetic notes met the sustained limits, wet and
mixed. A release difference remained on the synthetic notes (median
16.7 ms original, 143.9 ms HQ). BASS voicing and the sub-floor choice still
need the listening gate. No production trim or octave-floor changes were
made for this comparison.

`syn_compare.py --octave-voices up|down` now isolates the requested voice in the
ensemble bench. In the first 12 GuitarSet comping takes with the v66 octave bank,
isolated +1 on-pitch energy improved from 62.1% to 70.9% for ORGAN, 61.5% to 69.0%
for CHURCH ORGAN, 56.3% to 65.7% for STRINGS, and 52.8% to 61.9% for CHOIR.
Isolated −1 improved by 1.7 to 2.7 points for the three types that have it.
Mean added wobble also fell in each measured comparison. Wet arrival and
pick-click results from the synthetic D2–E3 steps show the strongest arrival
improvement on CHURCH ORGAN, from 17.9 to 12.1 ms up and 32.9 to 9.9 ms down.
ORGAN and STRINGS arrival was effectively unchanged. CHOIR improved on the
up octave from 140.4 to 132.5 ms with its standard swell. High-band pick
energy relative to steady output rose for most HQ voices, so compare their
transients on real guitar at matching wet levels before deciding retirement.

The [space-saver spec](spacesaver.md#phase-2b-update) now reflects the v66
catalogue and memory map, the HQ retirement gates and the NAM-pool borrowing
conditions. A product split remains contingent on a measured DSP benefit.
