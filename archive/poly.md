# OpenPocket polyphonic octave and tracking architecture

## Executive conclusion

The strongest practical architecture for OpenPocket is **not one universal pitch engine**. The evidence points to three related but distinct DSP paths:

| Function | Recommended engine | Status | Why |
|---|---|---|---|
| **Full-chord −1 octave** | OpenPocket HQ ERB phase-halving | **Keep** | Already has unusually low measured response time and good dyad pitch concentration on your material; no reviewed alternative has demonstrated a better latency/quality/CPU combination |
| **Full-chord +1 octave** | OpenPocket ERB phase-doubling with the compact 27-band geometry | **Promote to pedal A/B candidate** | Best current OpenPocket trade-off between low-note response, wobble and target energy; faster in the host layout comparison; target CPU and RAM deltas are unmeasured |
| **Full-chord −2 octave** | Cascade or directly extend the HQ ERB half-phase path, but benchmark separately | **Experimental** | Architecturally coherent with the successful −1 path; error and phase-sign mistakes accumulate, so −2 needs independent listening and spectral tests |
| **OC-3-style range octave** | Existing HQ bank with frequency-range weighting | **Keep/improve** | OpenPocket already has a per-band down-octave range mask in `erb_bank()` |
| **OC-5-style lowest-note only** | Resonator multi-F0 detector driving a **harmonic/band mask into the HQ octave engine**, not oscillator resynthesis | **Best next prototype** | Gives note identity while preserving the low-latency spectral octave engine |
| **Polyphonic synth note identities** | Quarter-rate resonator bank, augmented rather than copied verbatim | **Best candidate found so far** | It is the first tested causal detector that clearly improves your held-out guitar result |
| **Per-voice continuous pitch** | Candidate-centred complex demodulation / instantaneous-frequency refinement plus temporal voice state | **Build on top of resonator identities** | The upstream resonator is semitone-quantised and cannot itself follow bends or vibrato continuously |
| **Desktop octave reference** | Signalsmith Stretch, plus a good identity-phase-locked STFT implementation | **Reference only** | Valuable quality ceiling; latency, buffers and workload make it a poor first AC791N deployment target |
| **Desktop tracking reference** | Essentia/Klapuri, possibly Basic Pitch offline | **Reference only** | Your causal Essentia result is already worse than OpenPocket; neural transcription is the wrong computational class for the pedal |

That separation follows the architecture already documented in the repository: full-chord octave processing transforms the composite waveform, whereas a poly synth needs explicit note identities and persistent voices. `V49-PITCH-REFERENCE-REVIEW.md` makes exactly that distinction. citeturn170613view6

The most important research finding is therefore not that OpenPocket needs a radically different octave algorithm. **Your current ERB-PS2-derived octave path is already in one of the strongest published algorithm families for this problem.** Thuillier's 2016 thesis compared several real-time polyphonic guitar octave approaches against three commercial pedals and found ERB-PS2, phase scaling of a constant-ERB complex filter bank, to have the best overall performance among its candidates, with substantially reduced latency and comparable or sometimes better sound quality than the pedal baselines. citeturn784565search7turn784565search0

The larger opportunity is in **multi-pitch tracking and selective processing**. Your resonator result is sufficiently better than the existing tracker to justify changing that subsystem, but the upstream detector should be treated as a source of candidate identities rather than as the finished poly-synth tracker.

## What is actually in OpenPocket now

I verified checkpoint `7dab840`. It is a real commit titled `Benchmark polyphonic tracker candidates`; it changed five files, including `engine/px_track.h`, `docs/research/POLY-SYNTH-DESIGN.md`, `docs/research/POLY-TRACKER-OPTIONS.md` and the host evaluator. Its committed candidate sweep showed that simply adding more ERB bands did essentially nothing for two-note detection, while preserving shared harmonic residue increased dyad recall only by accepting a large false-candidate penalty. citeturn170613view0turn170613view1

The repository has moved on since that checkpoint. On the current `main` visible today, `docs/research/V49-PITCH-REFERENCE-REVIEW.md` and `docs/research/POLY-TRACKER-RESONATOR-BENCH.md` are committed files. The latter explicitly records the upstream resonator commit used, `325114842cfc93c515a55fa776b4e72b11a8cc13`, and says the firmware does not yet bundle that upstream implementation. citeturn170613view2 The joint-fit source and results were also pushed in [checkpoint `6dfcf70`](https://github.com/daverage/openpocket/commit/6dfcf70).

### The current octave engine

The core implementation is in `engine/px_dsp.c`. The HQ bank coefficients are shared statics and are prepared at one quarter of the 44.1 kHz engine rate. Current `main` starts the ERB grid at 60 Hz, uses spacing `37.5 + 0.04f`, and width `0.8` times that spacing. citeturn170427view3

The actual octave operation is unusually compact. `erb_bank()`:

- updates each complex second-order band;
- computes +1 octave as `Re(z²/|z|)`;
- computes +2 from the fourth power;
- tracks branch sign for phase halving;
- computes −1 octave from the complex half-angle;
- applies a per-band `range` taper before summing the down voice. citeturn170427view2

That is exactly why an OC-3/OC-5-style selective mode can be added without replacing the actual octave generator. The selection mechanism can decide **which subbands contribute**, while `erb_bank()` remains the audio transformation.

The bank runs at 11.025 kHz after two half-band stages and is interpolated back to 44.1 kHz in `erb_octaves2()`. citeturn170427view2 This is central to its suitability for the AC791N: the expensive complex bank is not running at full audio rate.

Your current benchmark document records a useful boundary. The compact 27-band layout reduced the average D2–E3 wet response from 16.9 to 13.4 ms and worst case from 17.6 to 14.2 ms, with near-target chord energy moving from 77.5% to 75.7%; the 80-band Terrarium geometry reached 97.3% near-target energy in your host geometry experiment but had a 65.8 ms low-note group response. The document correctly notes that the latter was not a native Terrarium timing measurement. citeturn314930view0

That is a classic filter-bank trade-off rather than evidence of a hidden superior algorithm: narrow low-frequency filters separate harmonics better but settle more slowly.

### The current tracker

`engine/px_track.h` is more ambitious than a normal tuner. It derives instantaneous frequency from phase advance in the ERB bank, extracts local spectral peaks, generates fundamental candidates from subharmonics, scores them using Klapuri-style harmonic salience and greedily removes explained harmonics. It can emit up to six notes. citeturn314930view2

Its current limits are explicit in the source:

`PXT_FMIN` is 60 Hz, so bass low E at 41.2 Hz and five-string low B at 30.9 Hz are outside its candidate range. Guitar low E is 82.4 Hz. `PXT_NOTES` is already six, so **voice count is not the fundamental problem**. The issue is candidate evidence and ambiguity. citeturn314930view3

The current implementation reads only 15 analysis bands, those up to roughly 790 Hz, because the repository's earlier tests found 29 bands no more accurate for its present candidate algorithm. citeturn314930view3 The frame routine estimates a frequency per band from complex phase advance, then treats local band-power maxima as partials. citeturn314930view4 That explains why adding overlapping upper bands did not fix close-note ambiguity: the problem is not merely insufficient band count. Several fundamentals can explain the same upper harmonic evidence.

The repository also documents the cost: the current v49 tracker accounts for about 20% of one core in the existing instrument paths, whereas the simpler zero-crossing synth tracking path contributes to synth effects costing roughly 9–12% overall. citeturn170613view5

## Full-chord polyphonic octave effects

### Ranked shortlist

**First: OpenPocket's current HQ ERB phase engine, with different bank geometries for up and down if necessary.**

This is the deployable winner. Thuillier's published result gives the algorithm family credible external support, while your own controlled measurements give it project-specific evidence. It has no FFT frame latency, runs its bank at quarter rate, uses little state, and the target code is already optimised around the AC791N.

For **−1**, I would freeze the current HQ geometry unless a new candidate beats it audibly and on hardware. Your down-octave evidence gives no reason to destabilise it.

For **+1**, the compact 27-band experiment is worth turning into an optional build. Its slight loss in target-energy concentration is small enough that listening, attack character and pedal CPU should decide. The result should not simply replace the HQ bank globally, because the optimal band shape for phase doubling is not necessarily the optimal one for phase halving.

**Second: a compact identity-phase-locked STFT shifter using the AC791N hardware FFT.**

This is the most serious alternative worth implementing, but I rank it below the ERB path for production until measured.

Identity phase locking exists specifically to preserve vertical spectral phase relationships around spectral peaks and reduces the phasiness and transient smearing of a basic phase vocoder. citeturn784565search8turn784565search9 `stftPitchShift` is a useful MIT-licensed implementation reference and supports simultaneous spectral pitch shifts. citeturn647084search1

OpenPocket has a meaningful advantage over a generic small MCU here: the AC791N contains an asynchronous fixed-point FFT unit. Your hardware handbook documents both its command interface OpenPocket currently uses software FFT routines for the 1024-tap cabinet convolution. The peripheral requires separate integration. citeturn788044view3

That makes a **256- or possibly 512-point STFT prototype** more credible than I would otherwise expect on this device. At 44.1 kHz, however, a causal 512-sample analysis window already spans 11.6 ms before peak tracking, synthesis overlap and any lookahead. A 1024-point implementation spans 23.2 ms. A short FFT helps attacks but compromises low-E separation; a long FFT helps low fundamentals but delays attacks. A multiresolution STFT can mitigate that, but complexity and state rise sharply.

I would therefore implement this as a **host plus hardware research branch**, not the new default.

**Third: Terrarium-style dense ERB processing.**

Terrarium Poly Octave is a real MIT-licensed embedded polyphonic octave pedal implementation. citeturn784565search2turn784565search3 It is useful evidence that this class of algorithm works in an embedded pedal and useful code for checking equations, filtering and gain structure.

It is not the answer to OpenPocket's latency problem. Your own matched geometry experiment already found the expected consequence of its much narrower low bands: better chord separation but far slower low-frequency establishment. citeturn314930view0

**Fourth: Signalsmith Stretch, Rubber Band, pbshift and similar general-purpose shifters, as references rather than firmware.**

Signalsmith Stretch is MIT licensed and supports arbitrary polyphonic pitch maps, but its own API explicitly exposes non-trivial input/output latency and describes substantial numerical work; OpenPocket's review records default analysis scales around 100–120 ms. citeturn647084search8turn314930view0 It remains an excellent desktop quality comparison.

Rubber Band's current `LiveShifter` is explicitly documented by its author as still having **50 ms or more** input/output delay depending on configuration. citeturn647084search3 That rules it out for the feel you are targeting even before CPU/porting cost.

`pbshift` is technically interesting because it implements multiresolution and transient-aware processing, but its own documented live tier is around 64 ms and its more sophisticated multiresolution engine is an offline path. citeturn647084search2 It is therefore a source of ideas and test material, not an AC791N candidate.

### Why WSOLA/SOLA/PSOLA should not replace the HQ octave

OpenPocket's older/general chord-capable shifter uses moving delay taps plus synchronised overlap-add searching. The repository documents a 64-sample correlation search and long delay ranges for the poly octave voices. citeturn347240view3

These waveform/time-domain techniques remain useful for arbitrary monophonic-ish shifts and can preserve attacks well, but octave-down of a polyphonic chord forces long waveform periods and ambiguous correlation peaks. That is precisely where your BYOD and existing host timing comparisons become irregular.

PSOLA is even less attractive because its natural unit is a pitch-synchronous period. With several simultaneous fundamentals, there is no single correct glottal/guitar period on which to splice the whole mixture.

So for full-chord ±12/−24, I would **not return to a time-domain period-shifting architecture**.

## Lowest-note and OC-3/OC-5-style selective octave

The commercial-pedal research gives less algorithmic information than internet folklore suggests.

I found genuine hardware teardowns and firmware evidence for EHX pedals, but **not a verified public reconstruction of the POG algorithm**. The original large POG teardown identifies a Motorola DSP56364 plus an 8051-family P87C52 and reports dumping the programmable device; the author explicitly says the binary should contain firmware, but does not present an algorithmic reverse engineering of the pitch shifter. citeturn550178search4 The Micro POG teardown identifies DSP56364/PIC hardware in one revision and a later Blackfin BF592 revision; again, this is hardware provenance, not an extracted octave algorithm. citeturn550178search5 The POG2 teardown identifies an ADSP-BF531. citeturn550178search6

For Boss, publicly available OC-3 service notes demonstrate that the product has a DSP/CPU signal path and test mode, but the material I could verify does not expose the DSP algorithm. citeturn550178search8 Boss's public OC-5 documentation describes polyphonic operation and lowest-note/range behaviour, not implementation details. citeturn550178search0

So there is no sound basis for saying that Boss or EHX use an FFT, ERB bank, resonator bank, sinusoidal modelling or any other particular algorithm solely from those teardowns.

### The practical OpenPocket solution

For an **OC-3-style Range control**, you already have most of the architecture. `erb_bank()` weights the down-octave contribution according to band index via `range`, while preserving phase history in all bands. citeturn170427view2 This is cheap, deterministic and musically useful.

For an **OC-5-style lowest-note-only mode**, use the polyphonic detector as a control signal but keep the octave audio spectral.

A good dataflow is:

```text
guitar
  │
  ├──► quarter-rate analysis
  │       │
  │       └──► resonator candidate detector
  │               │
  │               └──► lowest credible f0 + confidence
  │
  └──► existing HQ ERB octave bank
          │
          ├── harmonic-support mask centred on lowest f0
          ├── fade in a narrow mask after candidate confirmation
          └── −1 / −2 phase-halved output
```

Do **not** simply low-pass below, say, 150 Hz. The fundamental of a low guitar note can be weak, and most of its audible identity is in harmonics above that cutoff. Conversely, a low note's harmonics overlap upper notes, so perfect separation from a single mono pickup is mathematically unavailable when components coincide at the same frequencies.

Instead, generate a soft mask from the detected lowest fundamental:

\[
M_i=\max_h w_h\,K(f_i-hf_0)
\]

where `f_i` is the centre or instantaneous frequency of ERB band `i`, `K` is a smooth distance kernel and harmonic weights decay with `h`. Start with just a few low bands and downweight shared harmonics. Apply the mask to the down-octave accumulation in `erb_bank()`. citeturn170427view2

That gives you something meaningfully closer to lowest-note selective processing than the present monotonic `range` mask, without trying to resynthesise the lowest note.

When confidence drops in Lowest mode, hold the last valid mask briefly, then fade the wet output. Keep fixed Range as a separate mode.

For −2, derive the second octave from the already selected lowest-note content rather than letting broad-band −2 processing create spurious low energy from every chord partial. That will be much closer to the intended Boss-type behaviour.

## Polyphonic tracker for synth voices

### Ranked shortlist

**First: a modified fast-resonator bank plus a purpose-built temporal voice layer.**

The paper behind the resonator approach is a serious signal-processing method, not merely a GitHub trick. Zhou, Reiss, Mattavelli and Zoia's 2009 method uses a fast resonator time-frequency representation, harmonic grouping and post-processing for computationally efficient polyphonic pitch estimation. citeturn784565search5

More importantly, the specific open-source implementation has now beaten OpenPocket on your own holdout. Current committed results show, on the matched 128-sample holdout grid, 42.1% both-note accuracy versus 20.1%, 69.3% versus 56.8% lowest-note accuracy, and a slightly lower single-note extra-candidate rate, with essentially unchanged median successful entry time. citeturn314930view1

The current benchmark also gives an excellent embedded starting point: 53 quarter-rate resonators require about 584,000 resonator updates per second; a four-float state/coefficient layout plus one energy value projects to about 1,060 bytes for 53 guitar notes and 1,320 bytes for 66 guitar-plus-bass notes. Those are estimates, not measured target footprint. citeturn314930view1

This is small relative to the device's memory map, although RAM is already tight: the hardware handbook documents 96 KB ordinary RAM, a 248 KB pool and an 88 KB RAM-code region, and explicitly warns that SRAM accesses have no data cache. citeturn788044view4turn788044view5

The upstream reference has important defects that you should **not reproduce unchanged**:

- semitone-centred resonators quantise candidate identity;
- adjacent semitones cannot both survive its simple local-maximum rule;
- octave suppression can remove a legitimate octave dyad;
- long resonator decay produces release tails;
- fixed confirmation can miss short bass notes;
- its current outputs do not represent independent continuous bend trajectories. These limitations are documented in your committed benchmark. citeturn170613view2turn314930view1

So the best engine is a **resonator-derived OpenPocket tracker**, not simply the upstream detector pasted into firmware.

### Continuous frequency refinement

This is the critical addition for synths.

Each accepted semitone candidate should become a small local tracking object:

```text
candidate
    nominal note
    continuous frequency / cents
    confidence
    harmonic support
    amplitude
    onset evidence
    release evidence
    age
    missing-frame count
```

I would refine frequency with **complex demodulation around the candidate**, because it is naturally compatible with the resonator architecture and much cheaper than running a full independent YIN detector for every voice.

For a candidate \(f_c\), maintain complex phase measurements from one or more strong partials near \(h f_c\). The phase increment from report to report gives the frequency error:

\[
\Delta f_h \approx \frac{\Delta\phi_h}{2\pi\Delta t\,h}
\]

Subtract the expected carrier advance before measuring the residual phase. At a 2.9 ms report cadence the wrapped residual alone limits the unambiguous correction to roughly ±172/h Hz. Use shorter internal phase steps or unwrap against a prediction. Reject updates when overlapping partials make the phase ambiguous. Then combine the partial estimates with confidence weights and a bounded tracking loop. This gives continuous cents while the coarse resonator grid prevents the estimator from attaching itself to the wrong harmonic.

Another viable implementation is **three neighbouring resonators per active candidate only**, centred below/on/above the coarse pitch. Parabolic energy interpolation gives sub-bin pitch and the complex phase slope provides another frequency estimate. Because only 2–6 active candidates need refinement, you do not need three fine resonators for every MIDI note.

This is a much better match to guitar bends than the current global `pxt_refine()` waveform-period search, which operates on the mixture and cannot intrinsically say which of two simultaneous voices a waveform period belongs to. The present source shows the low-frequency period refinement immediately before `pxt_frame()` and then performs global partial extraction. citeturn314930view4

### Voice assignment

Do not couple candidate detection directly to oscillator creation. Insert a persistent association layer:

```text
resonator evidence
      ↓
candidate peaks
      ↓
harmonic verification
      ↓
continuous-f0 refinement
      ↓
temporal association
      ↓
voice state
      ↓
oscillator / envelope / filter
```

Assignment cost can remain tiny with only six voices. Construct a 6×N cost matrix from:

- pitch distance in cents;
- predicted pitch velocity, for bends;
- amplitude continuity;
- shared harmonic support;
- onset evidence;
- whether a candidate disappeared only momentarily.

For six voices, exhaustive or Hungarian-style assignment is computationally irrelevant compared with the spectral analysis. The important work is defining the cost and release hysteresis.

A voice should not die merely because its candidate is absent for one frame. Conversely, the synthetic bass test already shows tails of roughly 20–32 ms after a mute in the tested resonator configuration. citeturn314930view1 Release therefore needs a faster independent energy cue. Use energy in the candidate's low harmonics and negative spectral flux, not just resonator decay.

### Why the alternatives rank lower

**Existing ERB harmonic-salience tracker:** retain as fallback and perhaps reuse its onset/level features, but it is no longer the best candidate source. The repository's own 7dab840 sweep shows that band count and simple greedy-removal tweaks are giving diminishing returns. citeturn170613view0turn170613view1

**Essentia/Klapuri:** useful scientific reference, not the next embedded engine. OpenPocket's review notes that it uses peak extraction plus harmonic/optimisation stages and normally works on sizeable frames; your matched causal experiment already failed to improve the project baseline. citeturn170613view7

**pYIN/YIN/MPM/BACF/Cycfi Q:** good **monophonic** refiners, not multi-F0 extractors. Public pYIN implementations explicitly describe one pitch per frame and warn against polyphonic audio. citeturn354383search5 Cycfi Q is interesting for cheap continuous mono tracking, but it cannot replace the multi-note candidate stage.

**NNLS/template fitting:** intellectually attractive because it can explicitly explain a mixture as several harmonic templates, but your joint one/two-note fit has already shown the failure mode likely to dominate a lightweight implementation: shared harmonics make multiple hypotheses nearly equivalent. A serious NNLS system needs a richer high-resolution spectrum, iterative optimisation and temporal priors, raising CPU and latency.

**Neural transcription such as Basic Pitch:** potentially excellent as an offline upper-bound experiment, especially because modern systems can return pitch bends, but a neural transcription network is badly matched to a two-core 360 MHz device whose memory and audio-path CPU are already tightly budgeted. The AC791N does have a vector unit, but OpenPocket already uses substantial engineering to fit a tiny 1,871-weight NAM A2-Lite network; the hardware handbook reports that even that network's initial per-frame implementation took 223% of real time before extensive reorganisation. citeturn788044view5 A transcription network should therefore remain an offline oracle unless a genuinely tiny guitar-specific model is trained later.

## Implementation, licensing and acceptance gates

### Concrete integration points

For full-chord octave work, keep the change local to the existing ERB subsystem in `engine/px_dsp.c`:

- `erb_prepare()` around the band-grid definitions is where the **27-band up geometry** should become selectable. citeturn170427view3
- `erb_bank()` is the exact place for separate up/down geometry experiments, +2/−2 extension and lowest-note harmonic weighting. citeturn170427view2
- `erb_octaves2()` should remain the shared decimation/interpolation wrapper unless a hardware-STFT prototype replaces the entire analysis path. citeturn170427view2

For the new tracker:

- preserve the existing quarter-rate decimator;
- replace or add an alternative candidate backend around the current `pxt_frame()` candidate/partial stage rather than rewriting the synth layer first;
- change the 60 Hz candidate-floor assumption explicitly rather than just lowering `PXT_FMIN`: a 30.9 Hz resonator needs its own time constant and confirmation policy; the existing limit is `PXT_FMIN` in `engine/px_track.h`. citeturn314930view3
- retain or adapt current spectral-flux onset evidence from `pxt_frame()`, because repick information is useful even if the pitch candidates come from the resonator bank. citeturn314930view4
- put the per-sample resonator update in RAM alongside `pxt_run`; leave slower candidate selection/assignment outside the tight per-sample path.

`firmware/build.sh` already compiles `px_dsp.c` at `-O2`, with the AC791N hardware-FPU flags and `PX_SR=44100`; any tracker prototype should be profiled under exactly that target configuration, not inferred from x86 timing. citeturn347240view1

The device processes 128-frame audio blocks, roughly 2.9 ms at 44.1 kHz, and the hardware handbook says the maximum block time rather than average load is what predicts audible crackles. citeturn788044view3 That should define the CPU gate.

### Licensing

OpenPocket itself is **GPL-3.0-only**, subject to enumerated exceptions. citeturn314930view5

The main candidates fit that licensing model reasonably well:

- Terrarium Poly Octave: MIT. Attribution/license notice required when copying substantial code. citeturn784565search3
- Signalsmith Stretch: MIT. citeturn647084search8
- stftPitchShift: MIT. citeturn647084search1
- pbshift: MIT, with separately identified third-party components. citeturn647084search2
- BYOD: GPLv3 for the normal open-source distribution, with project-specific CLA arrangements for some contributors. citeturn647084search5
- Rubber Band: GPL open-source distribution plus a commercial licence; its GPL release can be used in a GPL-compatible project subject to its exact version terms. citeturn647084search7
- the resonator host benchmark already records that the tested upstream detector is GPLv3 and identifies the exact upstream commit. citeturn170613view2
- Zhou et al.'s paper is CC BY 2.0, which permits reimplementation with attribution but is not itself source-code licensing. citeturn784565search5

If you independently reimplement the resonator equations from the published paper, cite the paper in documentation. If you translate the upstream Rust implementation substantially line-for-line, preserve its GPL copyright/licence attribution as source provenance.

### Benchmark programme

The next programme should deliberately stop using one aggregate score for everything. Use the following matrix as the release gate:

| Test | Octave audio | Selective low octave | Synth tracker |
|---|---|---|---|
| Single notes E/B through high register | wet arrival, target energy, wobble | leakage outside selected note | pitch error, onset |
| Major/minor thirds | chord artefacts | lowest-note leakage | both-note recall |
| Whole tone / semitone | chord roughness | mask stability | **critical adjacent-note test** |
| Fifth / octave | phase stability | octave-suppression errors | genuine-octave recall |
| Unequal pick levels | spectral balance | lowest-note retention | weak-note recall |
| Bend ±2 semitones | pitch-shift continuity | mask motion | cents error, voice identity |
| Vibrato | modulation integrity | mask chatter | tracking jitter |
| Repick held note | attack preservation | retrigger leakage | retrigger accuracy |
| Individual mute | tail length | residual octave tail | note-off latency |
| Palm mute/chug | transient blur | lowest-note recognition | false onset |
| Real low E / low B | shifted pitch and onset | selection | acquisition/release |
| Six-note chords | sound/CPU | range behaviour | overload/false voices |

For candidate tracking, use **separate tuning, development and final holdout recordings**. Never tune confirmation, resonator decay or thresholds on the 24-take set you use for final reporting. Continue matching detector outputs using only audio available at each result timestamp.

Report at least:

- precision/recall/F1 per note count;
- exact all-notes-correct frame rate;
- lowest-note accuracy;
- extra-candidate rate on single notes;
- onset-to-first-correct detection CDF, not just median;
- bend RMS/95th-percentile cents error;
- release latency CDF;
- voice swaps through bends and crossing pitches;
- results grouped by interval and bass register.

For octave audio, report:

- 10%, 50% and 90% wet-response time;
- target-octave energy;
- intermodulation/non-target energy;
- attack crest reduction;
- level wobble/modulation spectrum;
- listening preference with level-matched, blind A/B files.

And on the pedal itself, I would set hard acceptance gates rather than accepting host improvements automatically:

**CPU:** start with the poly effect and Smart Gate alone. Require zero late blocks or bypasses and headroom below the 93% safety-bypass threshold in the pipelined path. Report the peak block time. Test heavier chains separately. OpenPocket's own hardware documentation explicitly recommends peak-block accounting. citeturn788044view3

**RAM:** record target `.data/.bss`, pool and RAMTEXT deltas from the actual map. The device has little margin for assumptions, and the project handbook explicitly requires target-section accounting on every change. citeturn788044view4

**Latency:** measure jack-in to jack-out/audible wet onset, not report cadence. A 2.9 ms result interval, a 22 ms resonator decay constant and a 15 ms successful-note median are different quantities, as your resonator document correctly emphasises. citeturn170613view3

**Tracker:** the first firmware resonator build should at minimum reproduce its holdout advantage without increasing single-note false voices, and should prove real low E/B, adjacent intervals, bends, repicks and mutes before it drives a production oscillator allocator.

**Octave up:** promote the 27-band bank only if blind listening does not expose the small target-energy loss and peak AC791N cost remains acceptable.

**Octave down:** do not replace HQ merely because another algorithm is theoretically more sophisticated. A challenger should beat the existing implementation on pedal latency, chord cleanliness and listening quality simultaneously.

The resulting OpenPocket architecture would therefore be:

```text
                     ┌─────────────────────────────────────┐
44.1 kHz guitar ────►│ shared causal front end / decimator │
                     └──────────────┬──────────────────────┘
                                    │
                  ┌─────────────────┴─────────────────┐
                  │                                   │
          AUDIO TRANSFORMATION                 NOTE ANALYSIS
                  │                                   │
       compact/HQ complex ERB             quarter-rate resonators
                  │                                   │
       +1 / −1 / −2 phase ops              harmonic verification
                  │                                   │
        optional lowest-note ◄──── confidence + lowest f0
          harmonic mask                     │
                  │                    fine cents tracker
                  │                     │ amplitude
                  │                     │ onset/offset
                  │                     │ confidence
                  │                     ▼
                  │                 voice allocator
                  │                     │
                  ▼                     ▼
          POG/OC-style wet          2–6 synth voices
```

That is the strongest practical direction supported by both the current OpenPocket code and the external literature: **keep spectral phase scaling for the thing it already does well, replace the weak polyphonic candidate detector with the resonator family, add continuous per-voice refinement rather than asking a semitone detector to do bend tracking, and use that same note evidence to control selective octave masks where note identity is actually useful.**