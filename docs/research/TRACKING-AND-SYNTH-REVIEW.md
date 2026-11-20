# Trackers, CPU split and the original synths: a review (Synth2, after build 916)

Asked by the user: look at the different trackers and where they can improve; at splitting tracking and processing between the CPUs; at `docs/POLY-TRACKING-EXPERIMENT.md`; and at the original code, which had no bleeps or blips, to see whether that was a better tracker or better envelopes. Host measurements only; nothing here has run on a pedal. Reproduce the tracker comparison with `sh tools/research/tracker_compare/run.sh`.

## 1. The original synths: neither a better tracker nor better envelopes

"The original" here is `origin/main` (1f4391e, the firmware before the Synth2 rebuild: v49-v69) and, under it, the v42 code before Andrzej's first commit (0bebd25). Two families:

**Signal-processing types: no pitch decision at all.** ORGAN, CHURCH, STRINGS, CHOIR, PWM, BASS, 12STR, ACOUSTIC, SITAR, BARI and (v48+) the `*_HQ` versions transform the guitar's own waveform: a delay-line pitch shifter (`syn_voice`: "ratio r = 2^(st/12) -> the read tap's delay changes by (1 - r) per sample"; ORGAN: 16' = -12 semitones, 8' = the guitar, 4' = +12, percussion +19), dividers (BASS), a comparator (PWM: "no tracking delay"), filters and a three-tap ensemble. The only thing derived from the guitar is a **level gate** (`syn_keys`): a follower (1 ms attack, 30 ms release) with a Schmitt threshold, the gate itself smoothed (3 ms attack and 20 ms release by default, 60 ms and 450 ms for CHURCH). The pitch is the guitar's, so a start cannot speak a wrong pitch; chords work; bends are exact. Costs: the sound is the guitar's timbre shifted (no oscillator, filter envelope or voice-leading), the shifter windows (700-1400 samples) colour the sound, and the intervals are fixed.

**Tracked voices (MONO at v42; TRUMPET, CELLO, CELLO SECTION, STRING MACHINE at v48+).**
* v42 MONO: a zero-crossing tracker on a steep fixed low-pass (the fundamental comes out on top), a median of three periods, the played pitch gliding to each new period's value over **12 ms**, snapping only after a silence. The gate opens on level (1.5 x threshold), with a 2 ms attack and **no restart on a new note**: a pitch change inside a ringing note is a glide.
* v48+ (`px_acoustic.h`): the tracker is today's band bank and note layer. The level is `amp += (target - amp) x k` with `k_att` a **12 ms** one-pole (about 25 ms to full) and the target `exp((expr - 1) x dyn_db x 0.115)`, **18 dB of dynamics by default (6-36 adjustable)**; an onset does not restart the level, it only picks a sample or a bow excitation, with 5-20 ms crossfades.

### Measured: the original tracker was not better
Scored on the pitch trajectory against the MIDI truth of the Guitar-TECHS DI sets (palm mutes P1, P2, 138 single notes; `tools/research/tracker_compare`):

| | hit (right pitch within 250 ms) | median / 90th percentile latency to the right pitch | first 200 ms of each note: right / wrong / silent (ms) |
|---|---|---|---|
| original v42 MONO | 62 / 62 / 91 % | 38-43 / 62-74 ms | 55-139 / 68-77 / 0 |
| today's tracker | 94 / 94 / 100 % | 7-10 / 15-26 ms | 120-205 / 0-5 / 5-9 |

The original spent a third of every note's start at the wrong pitch. It sounded smooth because it glided and never went silent or re-attacked, not because it was accurate. On the user's own recording it makes 15 note starts where today's tracker makes about 45.

### What did hide the errors (and what we do now)
| Original | Today's adapter | Effect measured or expected |
|---|---|---|
| a pitch change in a ringing note glides (12 ms) | a new tracker pitch is smoothed in about 1.5 ms (a jump), and a re-detected pick restarts the envelope (a re-attack, and WHEEL's click and percussion strike) | an error becomes an audible new note instead of a scoop. **Not yet tested here.** |
| level follows the guitar: `exp((expr - 1) x 18 dB)` | `0.35 + 0.65 x vel`, times `0.6 + 0.4 x expr_r` | soft and dead starts are 14-17 dB down in the original, only 8-13 dB in ours. **Tested** (below) |
| soft amplitude attack (12 ms one-pole) on the tracked voices | Felucca's attacks, 1-2 ms on the ANALOG presets and the organs (and 160 ms on the pads) | a wrong pitch for 3-20 ms is fully audible when the note starts at full level |
| most types had no pitch decision | every sound is a decision | structural |

**Test of the level law** (scratch adapter, the old law with 18 and 24 dB of dynamics; audible blips = notes under 80 ms within 12 dB of the loud notes): P2 palm mute 32 -> 20 (18 dB) -> 13 (24 dB); your recording 5 -> 5; P1 52 -> 52. The law helps where the junk notes are soft; the short notes in the other sets are loud picks (palm-muted notes are short by nature), and for those only a correct pitch at the start helps.

## 2. Where the trackers can improve
Inventory: (a) `px_track.h` band bank (the HQ octave's ERB bank) + note layer `pxt_layer` + `px_mono.h` (today's one tracker; fix passes 3b-5 added stable-frame wait, pitch hold on a collapsing level, dead-pick rejection, harmonic-start confirmation); (b) the removed Duo / poly trackers (`px_poly_track.h`, resonator bank) in `origin/main`; (c) the original v42 zero-crossing tracker (above).
1. **Legato and glide policy (highest value, cheap, untested):** classify each onset as a new pick (re-attack) or a slur (the pitch glides over about 12 ms, no envelope restart; WHEEL strikes no percussion); glide the first pitch in from the confirmed value. The original's smoothness came from this.
2. **The level law as a per-patch DYNAMICS setting** (the old default 18 dB): measured above. Not for organs, which are on/off by nature.
3. **A soft attack floor for pads and strings** (the old voices' 12 ms): the organ keeps its 2 ms.
4. **The zero-crossing tracker as a cheap voter:** far cheaper than the band bank, 62-91 % hit, so not a replacement; its steep-low-pass fundamental trick is the "octave check on the waveform" the v49 layer already has.
5. What is left in the current tracker (about 1 start in 8 briefly at a wrong pitch; chords jump between tones) is a pitch-estimation limit; a better estimator is the PolyTrack X question.

## 3. Splitting tracking and processing between the CPUs
Today (`firmware/px_audio.h`): block N goes to **CPU1 at the end of IRQ N; CPU1 runs the whole chain**, tracker and synth included (the tracker runs before the SYNTH block in the chain); **CPU0 does the I/O interrupt, the delay and reverb "wet lanes" in parallel, the UI**, and takes over if CPU1 fails. The NAM pipelined and layer-split two-core modes were removed. The pedal's meter (`node tools/op.mjs dsp`, PING) reports **one** load for the chain with its peak, late blocks and clips; **there is no CPU0 load figure**, so a split cannot be evaluated or validated from today's instrumentation. The user's reading was 46 % steady with a full chain; the old measurement of the tracker was about 20 % of a core (docs/archive/NOTE-TRACKING.md).
Options, in order: (A) **instrument first**: a CPU0 load counter (IRQ + lanes + UI) in the same report; `tools/synth_probe.mjs` already probes per-type loads. (B) **Tracker on CPU0 in parallel with CPU1's chain, its result one hardware block late** (+2.9 ms to the note start, 14 % of today's 20 ms wait; the synth would play block N's note in block N+1): worth it only if CPU1 is the bottleneck and CPU0 has headroom, both unknown. (C) The synth's voices on CPU0 and the tracker on CPU1: the same latency, more shared state; the CHOIR and a five-voice organ are the loads that would justify it. Not proposed before (A): a split is a latency cost and a regression risk (shared state, the wet lanes already on CPU0), and the 70 % worst-case target is still unverified.

## 4. POLY-TRACKING-EXPERIMENT.md: Phase 1, preliminary
This is not the full Phase 1 literature review. What is established:
* **Assets we have:** the band-bank tracker, the removed Duo / resonator code in `origin/main` (abandoned for false harmonics, unstable chords), Guitar-TECHS chord and technique DI sets with MIDI truth (`build/datasets/guitar-techs`: P1/P2 chords, scales, techniques), the user's DI recordings (`My Audio/`), and the scoring harness above.
* **Open-source landscape** (GitHub API and a web search; licences checked): [cycfi/q](https://github.com/cycfi/q) (MIT, monophonic: already rejected for replacement, docs/research/synth2/SYNTH2-TRACKER.md), [MTG/essentia](https://github.com/MTG/essentia) (AGPL-3.0: a multi-pitch Klapuri implementation to read as a reference, not to copy into a GPL-3.0-only firmware without legal care), [spotify/basic-pitch](https://github.com/spotify/basic-pitch) (Apache-2.0, a small neural transcriber: an offline reference and labeller, not an embedded candidate), [marl/crepe](https://github.com/marl/crepe) (MIT, monophonic), [librosa/librosa](https://github.com/librosa/librosa) (ISC, analysis). The web search found **no open-source, low-latency, polyphonic guitar tracker for embedded hardware**; the closest published system is [Schramm et al., NIME 2018](https://nime.org/proc/nime2018_schramm/) (real-time spectrum factorisation of mono audio into note activations on low-cost embedded Linux; code availability not checked), plus a [Cornell ECE 4999 (2025) Pi Pico student project](https://people.ece.cornell.edu/land/courses/eceprojectsland/STUDENTPROJ/2025/mcb376/Matthew%20Baum%20-%20ECE%204999%20Final%20Report.pdf) that names the six-pitch compute bottleneck, a [JAM Origin](https://www.jamorigin.com/technology/) commercial claim (vendor claim only), and theses reporting the FFT trade-off (resolving E2 in the FFT takes about 200 ms against a 20 ms target). A repo called "polyphon" had no usable detail.
* **Proposed three candidates for the host prototype** (to confirm in a full Phase 1): (1) **harmonic-template spectrum factorisation** (NMF / sparse decomposition with a guitar dictionary: the NIME 2018 precedent, the brief's layer C); (2) **iterative harmonic-summation multi-F0 with spectral subtraction** (Klapuri-style, the brief's layers B and C, implemented from the paper); (3) **the ERB band bank plus a joint note-set scorer** (our own infrastructure: choose the set of notes that best explains the bank, with the single-note tracker as the prior and the strong-pitch case kept).
* **Two cautions from this review:** a polyphonic tracker does not by itself remove bleeps (a single note's wrong start remains a pitch-estimation and continuity problem; the voices need the legato design of section 2 whatever feeds them), and the CPU budget (section 3) is the likeliest thing to fail the brief's 70 % target.
* The `polytrack-x` branch was **not** created and nothing was pushed (the brief asks for a branch; creating and pushing it is the user's call).

## 5. Recommended next steps
1. **Done (build 917, docs/research/synth2/SYNTH2-FIXPASS2.md, fix pass 6):** the legato and glide policy and the level law as a per-patch setting; the attack floor needed nothing (the pads' attacks are 160 ms or more with Felucca's envelope). Result: audible short notes 5 -> 3 on the user's recording, 29 -> 14 on the soft palm-mute set.
3. Add the CPU0 load counter and run `synth_probe` (the user, on the pedal): CHOIR OPEN and the organ OPEN are the worst cases.
4. Confirm the three PolyTrack X candidates with a proper literature pass, then build the host reference prototype and benchmark on the chord sets.
