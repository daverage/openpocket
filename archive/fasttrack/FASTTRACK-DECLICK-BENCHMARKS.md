# FastTrack declick benchmarks

**Decision: keep the current renderer for mono guitar tracking.** The H1-style correction helped a deliberately reset Felucca slot. With the current mono tracker and patch-default articulation, one frozen DI render was bit-identical and the other changed by at most one Felucca integer-output step near one onset. Forced RETRIGGER on the same events produced little change in the worst local differences. ARCADE NOISE had substantial changes in the first 2 ms of several attacks. The controlled comparison did not clear the gate for Experiment D.

## Reproduce

```sh
sh tests/archive/fasttrack/declick/run.sh
sh tests/archive/fasttrack/declick/run.sh --di-dir /path/to/fasttrack/labelling
```

Use a C compiler, Python 3, NumPy and Matplotlib. The first command runs the independent C tests, ordered delivery tests and 22 synthetic cases. The second adds the two legacy tracker captures with patch-default and forced RETRIGGER articulation. The DI directory contains `fast_play_di.mono24.wav` and `bleep3_di.mono24.wav`, the established 44.1 kHz left-channel extractions. Their SHA-256 hashes are `6ee53cffafec80019f15dd36f523d0fc45ce1fbef960f9e9d920844dac2eb296` and `d67093ad3b906181c00a9d3d2acb8d5135b7bdf59e65a068f64e4d796676b7c6`. The annotation drafts and oracle labels were unused. This comparison estimates no recall, harmonic accuracy or false-trigger rate.

Each case writes the frozen sample frames, event and block traces, float renders, a common-gain PCM16 pair and a transition plot under `build/fasttrack/declick/<case>/`. `transition_metrics.csv` includes absolute boundary step, local 4 ms peak difference, preceding 10 ms first-difference p95 and RMS, peak-to-nearby-p95 ratio, 2–8 kHz power in a 4 ms Hann window, local peak and clipping, and 2/5/10 ms attack RMS. The -60 dBFS threshold estimates the onset of sound for a cold start. A voice that is already ringing can cross that threshold immediately, so envelope and waveform evidence matter more for repicks. `summary.csv` adds whole-render peak, clipping, elapsed host block time and timing overruns. Comparisons use the same wet gain and sample origin. Large waveform slopes on bright patches can dominate the 4 ms maximum, and sample-held noise can have a preceding p95 of zero. The plots and WAVs are provided for listening assessment. The acceptance decision is based on the measurements, with no controlled listening verdict.

## Existing renderer and proposed correction

Values below use the 1.5 ms correction unless specified. Full scale is 1. A forced steal clears an occupied ensemble slot and starts it again while the other slots continue. It exercises an artificial discontinuity in the real voice renderer.

| Case | Existing OpenPocket | H1-inspired behaviour | Actual difference and evidence |
| --- | --- | --- | --- |
| Rapid lead and organ with patch FOLLOW | Keep envelope and phases, apply accent | A correction can only arm on an actual slot restart | CLASSIC and JAZZORG were bit-identical at all five time constants. |
| Bass-to-high CLASSIC with RETRIGGER | Active slot retains phase, filter and level while pitch glides | Align the first sample on the replacement boundary | A representative step fell from 0.00604 to zero, with preceding p95 of 0.124. The local peak difference went from 0.22223 to 0.22228. Phase-preserving compatibility rendering was already smooth at the boundary. |
| Same-pitch PLUCK repicks | Typically restart the algorithm while retaining the ramp | Arm correction only when the step exceeds three times the preceding block's largest within-block step plus 0.001 FS | The repeated-note waveform was bit-identical. On alternating PLUCK notes the 4 ms peak was dominated by the patch waveform and was effectively unchanged. |
| Legato plus bend and vibrato | Change pitch without a picked attack | No new correction on either event | The entire render was bit-identical at every tau. |
| DAMP then NOTE_ON | Fast release if DAMP reaches the boundary. A same-block onset clears the legacy pending release | Clear the outgoing correction on DAMP. Keep legacy articulation and block timing in A/B | At samples 8324 and 8327 the ordered model consumed both events, while the adapter rendered only the restart at sample 8352. The separated DAMP reached release at sample 21184 with no correction. All synthetic voices released by the end. |
| Live patch stress and ARCADE SPLIT | Macro/state changes can jump. Type changes in the real chain can reset the adapter | Clear correction history on patch application | The artificial live ANALOG→ARCADE switch still stepped 0.14461 FS. The real split case still stepped 0.09335 FS when crossing the threshold. Neither was corrected at the switch. |
| Forced steal, five-voice STRINGS | Slot reset adds a discontinuity to the continuing mix | Correct only the reset slot | At sample 18752, the mixed boundary step fell from 0.12759 to 0.00964 with normal nearby p95 of 0.01158. The 4 ms peak fell from 0.12759 to 0.03265 and 2–8 kHz power fell 8.0 dB. The next steal reduced the boundary step from 0.02005 to 0.00516 with little effect on the 4 ms peak. Slot 0 alone received correction. |
| ARCADE NOISE | Sample holds and deliberate waveform jumps shape the attack | Correct the full initial jump on a picked active note | At sample 8480, the 2 ms RMS fell from 0.1592 to 0.1130. At sample 19520 it rose from 0.05584 to 0.18535. The correction removed boundary steps in both cases. This is a substantial change to onset energy on a patch whose grain is intentional. |
| Frozen `fast play di.wav`, CLASSIC default | Actual tracker output and patch FOLLOW | Same frames plus correction | 23 NOTE_ON, 2 REPICK, 2 LEGATO_CHANGE and 6 NOTE_OFF including 4 DAMP. One correction affected 334 samples. Boundary step fell from 0.00001545 FS (nearby p95 0.00009347) to zero. Local 4 ms peak remained 0.012761 FS. |
| Frozen `bleep3 di.wav`, CLASSIC default | Actual tracker output and patch FOLLOW | Same frames plus correction | 22 NOTE_ON, 3 REPICK, 4 LEGATO_CHANGE and 15 NOTE_OFF including 14 DAMP. The waveform was bit-identical. |
| Frozen DI with forced RETRIGGER | Legacy frames, envelope restarts on admitted picks | Same attacks plus correction | 19 and 10 replacements corrected. The largest change to a corrected event's local 4 ms peak was 0.0000239 FS on fast play and 0.0000090 FS on bleep3. Only one of those 29 original boundary steps exceeded 3× its preceding p95. |

The sweep includes 8, 12.5, 16, 20 and 24 notes/s, large jumps from MIDI 28 to 96, and STRINGS replacements during attack, decay, near steady sustain and release. It also covers SOFTPAD, a five-singer CHOIR, stacked STRINGS and the split ARCADE range. Across 264 plotted or measured NOTE_ON, REPICK and forced slot events, 140 boundary steps decreased by more than 1e-7 FS at every tau. At 1.5 ms, the 4 ms peak difference decreased on 117 events and increased on 21 at that numerical tolerance. All five constants changed early attack energy in both directions. No test added clipping. The highest output magnitude anywhere in the sweep was 0.632 FS.

Raw per-block Felucca mix hashes, voice counts, envelope levels, starts, releases and block timing matched between A and B. The disabled overlay was bit-identical to native, and the default frozen-event A renders were bit-identical against direct `px_synth_run`. Across 52 cold starts with a threshold crossing at 1.5 ms, detected event to threshold was 5–52 samples (0.11–1.18 ms), unchanged in B. One already-ringing synthetic event crossed the threshold one sample earlier in B. The underlying ring delay remains 1–32 samples (0.023–0.726 ms). Example for the first forced steal: event and adapter receipt at 18742, render at 18751, first corrected output at 18752. Event counts here describe detector output. They do not certify that the guitar intended each event.

## Listening renders

Each listed directory holds `a.wav`, `b.wav` at 1.5 ms and `transitions.png`. Plots mark event receipt with a dotted line and first ring output with a solid one.

| Phrase | Directory |
| --- | --- |
| Rapid alternate-picked lead, patch default and forced retrigger | `rapid_lead`, `rapid_retrigger` |
| Hard mute and immediate restart | `mute_restart` |
| Repeated notes and legato | `repeated`, `legato` |
| Bass to high note | `bass_high` |
| Organ and plucked instrument | `organ`, `plucked` |
| Choir and string ensemble | `choir`, `strings` |
| Deliberately reset slot and sampled noise | `forced_steal`, `noise` |
| Frozen mono guitar recordings | `legacy_fast_play_di`, `legacy_bleep3_di` |

## Embedded feasibility and regression checks

The independent state is 12 bytes per slot. The host overlay adds 420 bytes of fixed static data at nine slots, including per-slot state and recent-step measurements, two 32-sample scratch arrays, coefficient and flags. There are no renderer allocations or waits. The cost includes an additional 32-sample accumulation and correction loop per active voice. On the arm64 Mac host, isolated 32-frame render p99 for the 1.5 ms run was about 0.21→0.33 µs for CLASSIC, 0.67→1.79 µs for five-voice STRINGS and 2.04→2.92 µs for five-singer CHOIR. Two host timing measurements at 3 ms exceeded the 726 µs slot, with a 3.79 ms maximum in that run. Host timings include OS preemption and exclude the pitch tracker, the rest of the chain and interrupt scheduling on the pedal. BlackBox callback load, late blocks and available SRAM for this exact build remain unverified. Historical linker figures do not establish current headroom.

FastTrack's existing `regress.sh` matched the saved Guitar-TECHS baseline. The protected-effect comparator was bit-identical against checkpoint `4d5461f` for 936 protected renders and 408 other-block renders once its temporary reference archive included the vendored renderer required by that checkpoint. These results compare the production tree on this branch to the checkpoint. `make test` stopped because Docker was unavailable. The native suite passed the synth, mono, wet-lane and all 58 vendored Felucca golden-render checks. Its pre-existing sweep and long-run flags, and protected-case differences against the older `2f3519b` reference, remain in the baseline log. Logs are under `build/fasttrack/declick/baseline/`.

The ordered delivery model retains DAMP and NOTE_ON in sequence with no queue overflow path. It still feeds an independent host trace while the compatibility renderer uses its existing block flags. Its result identifies a specific same-block mute and repick case worth distinguishing from pitch recognition in future mono work. Experiment D and firmware integration were gated on the controlled A/B results. The controller's note admission and the production renderer remain at the base revision.
