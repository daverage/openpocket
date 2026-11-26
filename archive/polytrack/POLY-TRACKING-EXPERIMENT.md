This should be a **research and feasibility project**, not an instruction to implement another polyphonic tracker immediately.

The goal is to discover whether we can produce a genuinely better solution for standard guitar pickups, benchmark it against established algorithms, and establish whether it is practical on OpenPocket hardware.

# PolyTrack X: Advanced Polyphonic Guitar Tracking Research

## Mission

Research, design and prototype a new state-of-the-art polyphonic guitar pitch-tracking algorithm for standard monophonic guitar and bass pickups.

The ultimate goal is to enable:

- Polyphonic guitar synthesizers with independently controlled oscillator voices.
- Guitar-to-MIDI-style note detection.
- Intelligent harmonisers.
- Lowest-note bass synthesis.
- Chord-aware effects.
- Pitch-aware orchestral and ensemble synthesizers.

Also investigate how polyphonic pitch tracking can assist virtual capo and polyphonic pitch shifting, while recognising that tracking individual notes is not necessarily the best method for transforming complete guitar audio.

**This is a research project. Do not modify production firmware until a demonstrably superior algorithm has been developed and validated.**

## 1. Project context

Target repository:

[https://github.com/daverage/openpocket](https://github.com/daverage/openpocket)

Research branch: `polytrack-x`, created from the latest appropriate `synth2` commit.

Target hardware: M-VAVE ANN BlackBox / Donner Pocket X, using the JieLi AC791N dual-core processor.

Audio specifications:

- Standard mono instrument input.
- 44.1 kHz sample rate.
- 128-frame hardware processing blocks.
- 64-frame engine processing calls.
- Approximately 2.9 ms per hardware block.
- Limited RAM and real-time DSP headroom.

The current Synth2 firmware has:

- A strong monophonic pitch tracker.
- Working Felucca ANALOG and ARCADE synthesis.
- Poly Octave HQ using ERB-based audio transformations.
- Lowest Octave HQ.
- Poly Oct Fuzz.

These are not to be replaced during this research.

### Previous experiment: Duo

The earlier two-note tracker used a resonator bank and harmonic heuristics.

It was abandoned because:

- It frequently identified harmonics as independent notes.
- It produced false pitches during note attacks.
- It was unstable during chords.
- Host measurements did not predict satisfactory hardware performance.
- Limiting detection to two notes was not sufficient for proper polyphonic synthesis.

The new research must use a materially different approach.

Do not rebuild Duo with another set of thresholds or voice-stealing rules.

---

## 2. Primary research question

**Can a guitar-specific, low-latency, multi-fundamental estimator reliably detect and continuously track three to six simultaneous notes from a standard mono guitar pickup, with sufficient accuracy and processing efficiency for live synthesis on the AC791N?**

Secondary questions:

1. Can simultaneous notes be separated from their harmonics reliably?
2. Can note attacks be detected before the fundamental fully stabilises?
3. Can independent note identities remain stable during sustained chords?
4. Can the system identify notes added to and removed from an existing chord?
5. Can bends and vibrato be tracked without false retriggering?
6. Can guitar tuning and string constraints improve accuracy?
7. What is the minimum realistic latency?
8. Can the algorithm operate within the available CPU and memory budget?
9. Would a hybrid classical DSP and compact machine-learning approach outperform either alone?
10. Which downstream effects benefit from explicit note detection, and which should use full-signal audio processing?

---

## 3. Literature and implementation review

Before designing the algorithm, conduct a systematic review of established methods and available source code.

Prioritise research involving:

- Electric-guitar polyphonic transcription.
- Real-time multi-pitch estimation.
- Low-latency audio analysis.
- Embedded music DSP.
- Polyphonic guitar synthesizers.
- Guitar-to-MIDI systems.
- Chord-aware effects.
- Existing open-source guitar pedals and VST implementations.

### Research areas

**A. Harmonic summation and harmonic product spectrum**

Investigate how harmonic relationships can identify fundamentals and where they fail with simultaneous guitar notes.

**B. Iterative estimation and spectral subtraction**

Review Klapuri-style multi-F0 estimation, iterative note extraction and methods for avoiding incorrect removal of shared harmonic energy.

**C. Sparse harmonic decomposition**

Investigate sparse non-negative reconstruction, harmonic dictionaries, non-negative matrix factorisation and constrained optimisation.

**D. Time-domain periodicity**

Review autocorrelation, MPM, YIN, BACF, multi-periodicity analysis and their applicability to overlapping signals.

**E. Constant-Q and multiresolution analysis**

Explore FFT, STFT, CQT, resonator banks, wavelets and hybrid time-frequency systems.

**F. Temporal note tracking**

Review hidden Markov models, probabilistic tracking, multi-target assignment, temporal continuity, note-onset estimation and state-space tracking.

**G. Neural multi-pitch estimation**

Review lightweight convolutional, recurrent and transformer-based transcription systems.

Evaluate whether an extremely small neural correction or candidate-scoring network could run on the target processor.

Do not assume neural inference is appropriate simply because it is modern.

**H. Guitar-specific physical modelling**

Investigate priors for:

- String tuning.
- Harmonic series.
- String inharmonicity.
- Pickup position and spectral response.
- Pick transients.
- String decay.
- Fretting constraints.
- Open strings.
- Bends.
- Alternate tunings.

Avoid constraints that prevent legitimate guitar techniques.

### Relevant starting sources

- [OpenPocket](https://github.com/daverage/openpocket)
- [Cycfi Q](https://github.com/cycfi/q)
- [Essentia](https://github.com/MTG/essentia)
- [librosa](https://github.com/librosa/librosa)
- [Basic Pitch](https://github.com/spotify/basic-pitch)
- [CREPE](https://github.com/marl/crepe)
- [pYIN](https://code.soundsoftware.ac.uk/projects/pyin)
- [GuitarSet](https://guitarset.weebly.com/)
- [IDMT-SMT datasets](https://www.idmt.fraunhofer.de/)

Also locate and inspect open-source guitar-specific multi-pitch detectors, including the previously identified Polyphonic Pitch Detector for Guitars project, and verify its current repository, licence, published latency and accuracy claims.

Search beyond these references.

### Research requirements

For each serious candidate, document:

- Algorithm.
- Published evidence.
- Source implementation.
- Licence.
- Sample-rate assumptions.
- Analysis window and algorithmic delay.
- CPU and memory requirements.
- Polyphonic note capacity.
- Test conditions and datasets.
- Known failure modes.
- Suitability for embedded C/C++.
- Whether it is a plausible production implementation or only a reference model.

Distinguish published measurements, author claims, independent replication and our own tests.

---

## 4. Proposed architecture to investigate

Use this as a starting hypothesis, not a predetermined solution.

### Layer A: Multi-resolution signal analysis

Combine:

**Fast path**

- Transient detection.
- Instantaneous frequency.
- Harmonic phase information.
- Short-window spectral analysis.
- Existing ERB/resonator infrastructure where useful.

**Slow path**

- Longer-window spectral resolution.
- Fundamental confirmation.
- Harmonic structure.
- Sustained-note analysis.

The fast path should provide early note candidates.

The slow path should confirm fundamentals and suppress harmonic errors.

Investigate whether multiresolution analysis provides a measurable advantage over a single optimised analysis window.

### Layer B: Harmonic candidate generation

For each plausible fundamental frequency, calculate harmonic support.

A basic candidate score could be:

[\
H(f)=\sum\_{k=1}^{K}w_kS(kf)\
]

where (S) is the measured spectral evidence and (w_k) is the weight of harmonic (k).

This is only an initial candidate-generation mechanism.

It must not directly determine the output notes.

### Layer C: Joint multi-fundamental estimation

Instead of selecting peaks independently, find the combination of fundamentals that best explains the observed signal.

Investigate a sparse objective of the form:

[\
\min\_{\mathbf a\geq0}\
|S-D\mathbf a|^2_W+\
\lambda|\mathbf a|\_1\
]

where:

- (S) is the observed spectrum.
- (D) is a dictionary of plausible guitar-note harmonic templates.
- (\mathbf a) represents note strengths.
- (W) weights frequency bins.
- (\lambda) controls sparsity.

The exact loss and optimisation method should be determined experimentally.

Investigate:

- Shared-harmonic accounting.
- Residual-spectrum validation.
- Missing fundamental detection.
- Harmonic overlap.
- Spectral-template adaptation.
- Octave confusion.
- Upper-harmonic false positives.
- Confidence calibration.

Do not use independent greedy note selection as the sole decision mechanism unless benchmarking proves it competitive.

### Layer D: Guitar-specific constraints

Use guitar knowledge as soft priors.

Initial configuration:

- Six-string guitar.
- Standard EADGBE tuning.
- Up to six simultaneous notes.
- Configurable alternate tuning.
- Optional bass configuration.

Investigate whether explicit string assignment improves stability.

For example, a stable estimate should consider the possibility that an open string continues ringing while another fretted note begins.

Do not assume every note must be assigned to a unique string unless the evidence supports that model.

### Layer E: Independent temporal tracking

Maintain stable note objects rather than generating a new unordered pitch list every frame.

Each tracked note should expose:

- Continuous frequency.
- Estimated note number.
- Confidence.
- Amplitude.
- Onset.
- Release.
- State.
- Stable identity.
- Optional string hypothesis.
- Timestamp.

Implement explicit transitions between:

- Candidate.
- Confirmed.
- Sustained.
- Releasing.
- Inactive.

Evaluate onset confidence separately from pitch confidence.

A weak fundamental should not automatically generate a second note.

A pitch correction should not automatically retrigger a synth voice.

### Layer F: Output interface

Design a small, reusable event interface.

Example conceptual output:

```
typedef struct {
    float frequency_hz;
    float amplitude;
    float confidence;
    uint32_t sample_time;
    uint8_t note_id;
    uint8_t event;
    int8_t string_hint;
} polytrack_note_event;
```

This is a proposed interface, not required final code.

Keep the tracker independent of the chosen synthesizer.

A downstream voice allocator should be able to consume these events without accessing the tracker's internal DSP state.

---

## 5. Benchmark methodology

Create a reproducible host benchmark before optimising for firmware.

### Required musical material

Include:

- Individual notes.
- Two-note intervals.
- Power chords.
- Three-note chords.
- Four-note chords.
- Five- and six-string chords.
- Open chords.
- Barre chords.
- Arpeggios.
- Notes added to sustained chords.
- Notes released from sustained chords.
- Bends while another string rings.
- Hammer-ons and pull-offs.
- Palm muting.
- Quiet notes.
- Different pickups and guitars.
- Bass guitar.

Test clean electric-guitar DI first.

Then evaluate gain, compression and distortion as separate conditions.

The production detector will normally receive clean input, but its behaviour under difficult signals is still useful to characterise.

### Ground truth

Use datasets with reliable polyphonic note labels, including onset and offset timestamps.

Where ground truth is absent, annotate a controlled evaluation set or use isolated string recordings with known notes to construct reproducible mixtures.

Do not treat artificially mixed notes as equivalent to real guitar chords.

Include recordings made through the actual BlackBox input path.

Divide datasets by performance and recording session to prevent leakage between training, tuning and testing.

### Required metrics

- Framewise precision.
- Framewise recall.
- Framewise F1.
- Note-event precision and recall.
- Octave error rate.
- Harmonic false-positive rate.
- Note-on latency.
- Note-off latency.
- Pitch tracking error in cents.
- Voice identity stability.
- False retrigger count.
- Maximum reliable polyphony.
- CPU time per audio block.
- Peak RAM.
- End-to-end system latency.

Report results separately for one, two, three, four, five and six notes.

Include worst-case results rather than only favourable averages.

---

## 6. Proposed success thresholds

These are engineering targets to test, not asserted capabilities.

### Musical accuracy

For clean electric guitar:

- Strong monophonic performance, without regression against OpenPocket's existing tracker.
- Reliable two-note tracking.
- Three- and four-note chords as the primary acceptance target.
- Five- and six-note chords as a stretch target.
- Substantially fewer false harmonic notes than Duo.

Set quantitative event-level precision, recall and stability thresholds after establishing the baseline and before evaluating a new candidate on the held-out test set.

Do not select thresholds after examining final results.

### Latency

Initial research targets:

| Condition                      | Target                           |
| ------------------------------ | -------------------------------- |
| Monophonic guitar, median      | Under 20 ms                      |
| Common chords, median          | Under 30 ms                      |
| Common chords, 90th percentile | Under 50 ms                      |
| Low bass fundamentals          | Report separately                |
| Voice release                  | Fast enough to avoid stuck notes |

Report additional buffering independently.

A low-latency but inaccurate initial estimate is not automatically better than a slightly later correct note.

Evaluate audible synth output, not just detection timestamps.

### Embedded performance

Target hardware must remain within its real-time processing deadline.

Aim for worst-case CPU usage below approximately 70% of the relevant core's audio budget, including necessary scheduling overhead.

Investigate whether tracking can execute on CPU0 while synthesis runs on CPU1.

Do not assume that CPU0 is idle: it already handles input/output and can process delay/reverb wet lanes.

Any multicore design must measure:

- Both cores.
- Shared-memory contention.
- Buffer exchange.
- Synchronisation.
- Latency overhead.
- CPU fallback behaviour.
- Existing wet-effect regressions.

---

## 7. Baseline comparisons

Compare against:

1. The old OpenPocket Duo tracker.
2. The current OpenPocket monophonic tracker on single notes.
3. Relevant published open-source multi-pitch implementations.
4. A simple harmonic-summation baseline.
5. A conventional spectral multi-F0 baseline.
6. An offline neural model, where useful.

Where possible, evaluate every method on the same recordings and metrics.

Do not compare published figures from different datasets as if they were directly equivalent.

A new algorithm must provide a meaningful improvement in musical results, not merely have a more sophisticated implementation.

---

## 8. Downstream application research

### A. Polyphonic synthesizers

Primary use case.

Drive multiple Felucca oscillators from independent note events.

Experiment first with a simple, unfiltered reference oscillator so pitch-tracking errors are clearly audible.

Then evaluate:

- ANALOG.
- Organ.
- Strings.
- Brass.
- Choir-style patches.

Track polyphonic note quality independently from synthesizer sound quality.

### B. Intelligent harmonisers

Investigate two approaches:

**Symbolic harmonisation**

Use detected notes, scale and chord context to generate new synthesizer voices at musically appropriate intervals.

**Audio harmonisation**

Use detected notes to guide transformations of the actual guitar sound.

The second requires separate audio-processing research.

Do not assume accurate polyphonic transcription automatically provides separated guitar-string audio.

### C. Virtual capo

Treat full-chord pitch shifting as its own DSP problem.

Research:

- Pitch-synchronous overlap-add.
- Polyphonic period synchronisation.
- ERB-based transformations.
- Multiresolution pitch shifting.
- Low-latency STFT approaches.

Compare established full-signal algorithms against the current OpenPocket processing.

Do not make the capo dependent on six-note transcription unless testing demonstrates a benefit.

### D. Lowest-note synthesis

Evaluate whether PolyTrack X can reliably identify the lowest active fundamental and drive a bass synth.

Compare against:

- The current monophonic tracker.
- Lowest Octave HQ.
- Existing lowest-note selection behaviour.

A new lowest-note algorithm must outperform the working implementation, not replace it by assumption.

---

## 9. Implementation strategy

### Phase 1: Research

Deliver:

- Literature review.
- Open-source implementation comparison.
- Algorithm shortlist.
- Licence review.
- Proposed benchmark methodology.
- Preliminary CPU/memory feasibility.

No firmware changes.

### Phase 2: Reference prototype

Implement a host-only multipitch tracker.

Prioritise correctness and diagnostic visibility over embedded optimisation.

Provide visualisations of:

- Spectral evidence.
- Candidate fundamentals.
- Shared harmonic energy.
- Note confidence.
- Voice trajectories.
- False-positive events.

Include a simple audio synthesizer driven by the detected note events.

### Phase 3: Comparative evaluation

Benchmark competing algorithms.

Perform ablation tests to determine which components actually improve results.

Investigate failures and revise the model.

Freeze the evaluation protocol before final testing.

### Phase 4: Embedded feasibility

Only if Phase 3 succeeds:

- Convert the selected algorithm to processor-compatible C.
- Remove unnecessary dynamic allocation.
- Optimise memory.
- Profile processing hotspots.
- Evaluate fixed-point versus floating-point operation.
- Test alternative core scheduling.
- Preserve deterministic outputs where practical.

Create a distinct experimental firmware feature behind a compile-time flag.

Do not replace Synth2's production tracker.

### Phase 5: Pedal-in-the-loop evaluation

Prepare a supervised test build.

Evaluate actual guitar input, processor load and audible synthesis.

Include real playing tests.

No automatic flashing.

### Phase 6: Production decision

Recommend one of:

- Integrate as a production polyphonic tracker.
- Restrict to specific applications, such as chord recognition or lowest-note detection.
- Continue host research.
- Reject because it does not outperform the existing implementations sufficiently.

A negative result is acceptable.

---

## 10. Research deliverables

Produce the following files:

- `docs/POLYTRACK-X-RESEARCH.md`: research findings and citations.
- `docs/POLYTRACK-X-ALGORITHMS.md`: algorithm comparison.
- `docs/archive/polytrack/POLYTRACK-X-BENCHMARKS.md`: methodology and results.
- `docs/POLYTRACK-X-ARCHITECTURE.md`: proposed design.
- `docs/POLYTRACK-X-FEASIBILITY.md`: CPU/RAM/latency assessment.
- `tests/archive/polytrack/`: reproducible host tests and evaluation scripts.
- A separate reference prototype with clear build instructions.

Use real links to original papers, repositories and licences.

Make all measurements reproducible.

Document which conclusions are experimentally supported and which remain hypotheses.

## 11. Constraints

- No hexaphonic pickup.
- No external MIDI requirement.
- No dependence on cloud processing.
- No automatic firmware flashing.
- No changes to working Synth2 effects during initial research.
- No return to the old Duo allocator.
- No fabricated benchmark results.
- No assumption that six notes are always recoverable.
- No proprietary or incompatible code copied into the firmware.
- No performance claims based only on a simulator.
- No uncontrolled expansion into a complete audio-to-MIDI product before core tracking is demonstrated.

## 12. Final research question

The project should ultimately answer:

**What is the most accurate, lowest-latency polyphonic guitar note tracker that can realistically run on OpenPocket hardware, and is it sufficiently better than the existing approaches to justify deploying it?**

The answer must be supported by reproducible measurements and audible examples.

### Starting instruction

Begin with **Phase 1 only**.

Research established algorithms, examine actual source implementations, document their trade-offs and propose the three most promising candidates for a host prototype.

Do not start writing production DSP until the research review is complete.

My suggested emphasis is **three- and four-note accuracy first, six-note capability second**. That provides a credible path to useful guitar-controlled synth chords without repeating the fundamental mistake of Duo: generating extra voices before we're sufficiently confident those notes actually exist.

For this project, the most useful early deliverable would be an interactive diagnostic tool that shows the detected fundamentals against their harmonic evidence while playing recorded guitar DI. It would make it much easier to understand why the algorithm invents notes, rather than repeatedly tuning thresholds without seeing the underlying failure.
