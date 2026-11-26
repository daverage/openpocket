# Low-latency polyphonic pitch shifting

Start with **ERB phase doubling for the +12 layer in POLY OCTAVE**, then try the fixed +12 layer in STRINGS. For **capo and octave down**, compare peak-locked spectral shifting with the current shifter at matched latency, starting with palm mutes and fast repicking. PitchShiftOcean has a promising published latency, but its reference executable missed the expected frequencies in the low-register bench test below. Use Signalsmith Stretch and Rubber Band as desktop quality references.

Defer 12-STRING until the candidate can preserve its DETUNE control. The control raises the octave voice from +12 semitones to +12 semitones and 15 cents, while also increasing the unison copy's level and modulation depth. Exact phase doubling produces a fixed octave. An integration needs to preserve the octave offset as well as the existing unison behavior. [STRINGS and 12-STRING setup](../../engine/px_dsp.c)

## What OpenPocket does today

The chord-capable modes transpose the composite guitar waveform. They read two taps at changing delay, crossfade, and use a synchronized overlap-add (SOLA) search to choose the next tap's offset. The current search spans 64 input samples (1.45 ms at 44.1 kHz), sampling every fourth point in the coarse pass and every second in the fine pass. POLY OCTAVE's up and down voices sweep 1,000- and 1,400-sample delay ranges respectively. Those ranges are about 23 and 32 ms, and the alignment offsets add variable delay. Actual response depends on tap phase, pitch ratio, crossfade, and playing. A common splice can line up one strong partial while other parts of the chord cancel or beat. MONO SYNTH's separate zero-crossing tracker is used to drive its oscillator and a better mono tracker would leave this chord-shifting path as it is. [DSP source](../../engine/px_dsp.c) · [Audio SDK notes](../AUDIO-SDK.md)

The pedal runs at 44.1 kHz in 128-sample blocks, about 2.90 ms per callback. On the BlackBox, the published v42 USB-audio stress test reached 92% for its busiest block. The DSP bypass threshold is above 93% for eight blocks in a row. A cabinet path already uses a 128-point FFT, and NAM uses the vector hardware. Those paths establish a starting point for experiments. A larger transform still needs its own timing, RAM map, and scheduling checks with NAM running. [USB-audio cost](../USB-AUDIO.md#cost) · [Cabinet FFT](../archive/whats-new/WHATS-NEW-v38.md) · [NAM implementation](../NAM.md)

In 48 fresh simulator runs with POLY OCTAVE soloed, a 16-sample Hann pulse was placed at offsets of `4096 + 173 × run` samples. With the dry and wet controls tested individually at 100, the first sample above 1% of each run's peak and the peak itself arrived after these intervals:

| Output | First crossing | Peak arrival | Weakest peak / strongest peak |
| --- | ---: | ---: | ---: |
| Dry | 0.02 ms | 0.16 ms | — |
| +12 semitones | 0.82–6.44 ms | 8.48–14.56 ms | 85% |
| −12 semitones | 0.32–25.58 ms | 1.00–29.86 ms | 0.2% |

This transient probe exposes tap-phase sensitivity. Timing is relative to pulse start, excludes analog and hardware latency, and does not represent typical note response. The relative threshold can still register an almost inaudible pulse. [Simulator audio path](../SIMULATOR.md#the-sound)

## Candidate algorithms

| Candidate | Evidence on accuracy and latency | OpenPocket use |
| --- | --- | --- |
| **ERB-PS2 filter bank** | Thuillier evaluated octave-up doubling on real guitar against three pedals. The method doubles the phase of each complex sub-band sample. The tested design used 43 bands and a 160 Hz–8 kHz output range. The thesis reports up to roughly 3 ms in the 3–8 kHz band. Lower frequencies have greater group delay. Simulated 16-sample I/O buffers are smaller than OpenPocket's 128-sample blocks. | First octave-up experiment. Process and sum the bands in streaming C. Verify the bass end and high-note intermodulation on dense chords, and measure all filters continuously with NAM active. [Thuillier thesis, §§6.4 and 7](https://aaltodoc.aalto.fi/bitstreams/ff9e52cf-fd79-45eb-b695-93038244ec0e/download) |
| **Juillerat–Hirsbrunner short-window STFT** | Their low-latency experiment used 512 input samples (11.61 ms at 44.1 kHz), overlap by a factor of eight, and a 2,048-point inverse transform. The larger inverse transform does not require 2,048 input samples. Their examples show fewer unstable or attenuated sustained partials than an improved phase vocoder at the same short-window setting. The paper reports less than 12 ms algorithmic delay for that configuration. | Keep as a comparison. Verify low-note accuracy at each intended ratio before porting. The demodulation curve depends on the ratio, so expression-pedal sweeps need a separate transition test. [Author-posted paper, §§3–5](https://www.researchgate.net/publication/261078164_Low_latency_audio_pitch_shifting_in_the_frequency_domain) · [IEEE record](https://doi.org/10.1109/ICALIP.2010.5685027) |
| **Direct spectral peak translation with phase locking** | Laroche and Dolson move groups of neighboring bins together and adjust their phases. It can preserve relationships within a partial better than independently moving bins. At 44.1 kHz, analysis windows of 512 and 1,024 samples span 11.6 and 23.2 ms and have nominal bin spacing of 86.1 and 43.1 Hz. Phase measurements can estimate an isolated partial between bins. They still struggle when nearby fundamentals and harmonics overlap inside a short window. Nolte's guitar experiments illustrate the resulting latency and clarity trade-off. | Use as a simpler spectral comparison at 512/1,024 samples. Compare multi-voice cost using a shared analysis and one summed synthesis spectrum where possible. [Laroche and Dolson](https://www.ece.uvic.ca/~peterd/48409/Dobson1999.pdf) · [Nolte guitar thesis, §7](https://reposit.haw-hamburg.de/handle/20.500.12738/15713) |
| **Improved SOLA or WSOLA** | A longer, downsampled comparison and a preference for smaller or more consistent offsets may reduce sudden mismatches while keeping the existing buffer topology. A splice still moves every component by the same number of samples. Nolte observed that the tested WSOLA preserves attacks better on simpler material and breaks down on dense chords. | Cheap experiment and useful control. Compare the current 64-sample span to 128 and 256 with bounded correlation work and measure changes in wet onset. A longer history read alone need not delay the output. [Current DSP](../../engine/px_dsp.c) · [Nolte guitar thesis, §§4 and 7](https://reposit.haw-hamburg.de/handle/20.500.12738/15713) |

Periodic phase resets can help some spectral algorithms. The improved PVSOLA study restricts resets to sinusoidal components to reduce amplitude modulation and includes transient processing. Its listening test still favored commercial references, and its evaluated settings do not establish a short-window OpenPocket solution. Rollers shift multiple filter-bank bands by additive frequency offsets to approximate scaling. In Nolte's implementation the bands differed greatly in delay and low-frequency selectivity, making treble onset an unreliable measure of the apparent delay. Neither is a first prototype. [Kraft et al., DAFx 2012](https://www.dafx.de/paper-archive/2012/papers/dafx12_submission_26.pdf) · [Nolte guitar thesis, §§6–7](https://reposit.haw-hamburg.de/handle/20.500.12738/15713)

## What to reuse

- **Terrarium Poly Octave** is MIT-licensed Daisy Seed firmware and a useful code reference for the filter-bank experiment. It offers octave-down voices as well. Thuillier's published evaluation supports starting with +1 octave. Treat the downstream octave-down variants as separate candidates and check for discontinuities and artifacts when taking fractional phase on mixed partials. Daisy timings do not predict JieLi timings. [Terrarium source and licence](https://github.com/schult/terrarium-poly-octave) · [Thuillier thesis](https://aaltodoc.aalto.fi/bitstreams/ff9e52cf-fd79-45eb-b695-93038244ec0e/download)
- **PitchShiftOcean** is Juillerat's Java reference implementation. The source directory in the author's archive carries an MIT licence. Use the command-line reference to check low-register accuracy before considering a streaming C port. The archive lists separate third-party library terms. The author also published an erratum for the phase correction in equation 2. [Reference implementation](https://pitchtech.ch/Confs/ICALIP2010/PitchShiftOcean.7z) · [Erratum](https://pitchtech.ch/Confs/ICALIP2010/Errata.txt)
- **Signalsmith Stretch** is MIT-licensed C++ and supports custom block and hop sizes, a reported input and output latency, and distributed computation. Its standard presets use 120 ms and 100 ms blocks. Split computation adds one hop to the reported output latency. Use default and short-window configurations as separate A/B references and report the latency from both getters and a measured transient. Its STFT dependency and memory use need profiling before any firmware port. [Signalsmith algorithm and API](https://signalsmith-audio.co.uk/code/stretch/) · [Configuration source](https://github.com/Signalsmith-Audio/signalsmith-stretch/blob/main/signalsmith-stretch.h)
- **Rubber Band LiveShifter** is a host reference. The API reports 50 ms or more of delay depending on configuration and exposes the configured value through `getStartDelay()`. Its fixed-size processing calls may require additional buffering when driven by the pedal's audio callback. [LiveShifter documentation](https://www.breakfastquay.com/rubberband/code-doc/classRubberBand_1_1RubberBandLiveShifter.html)
- **KissFFT** is a BSD-3-Clause C option for host or scalar firmware FFT experiments. Its source supports floating-point and fixed-point arithmetic. Compare it to the available firmware FFT path using the actual transform sizes, scratch requirements, and worst callback durations. [KissFFT source](https://github.com/mborgerding/kissfft)

## Desktop accuracy spot check

The author's PitchShiftOcean JAR and the current OpenPocket WebAssembly engine processed a synthetic four-second chord made from equal-amplitude 82.41, 110, and 146.83 Hz sine waves at 44.1 kHz. OpenPocket ran POLY OCTAVE with only the tested wet voice active. PitchShiftOcean used `-r 2` or `-r 0.5`, `-v 8`, and `-m 4`. In the middle two seconds, a Hann-windowed spectrum measured the share of energy within ±10 cents of the three expected frequencies.

| Implementation | +12 semitones | −12 semitones |
| --- | ---: | ---: |
| OpenPocket POLY OCTAVE | 5.11% | 6.96% |
| PitchShiftOcean, 512-sample analysis | <0.01% | <0.01% |
| PitchShiftOcean, 2,048-sample analysis | 0.01% | 0.02% |

As a sanity check, the 512-sample PitchShiftOcean run on 110 Hz alone peaked at 196.15 Hz for octave up and 66.95 Hz for octave down, versus targets of 220 and 55 Hz. With 2,048 samples, the peaks were 217.69 and 56.19 Hz. This measures the tested executable and settings on synthetic low tones. It does not establish where the error arises, compare musical quality, or measure algorithmic latency. It gives a concrete reason to test cents error before committing to a port. [PitchShiftOcean reference](https://pitchtech.ch/Confs/ICALIP2010/PitchShiftOcean.7z)

## ERB octave-up prototype

Run `node tools/research/erb_octave_prototype.mjs` for the comparison. Pass an output directory to include level-matched WAVs of the bundled chord, arpeggio, and low riff. The C prototype uses 43 complex biquads and phase doubling. Its half-rate option runs the filter bank at 22.05 kHz with half-band filtering around it.

| Candidate | Drop D chord | Low E chord | Close low dyad | High chord | 82 Hz arrival | 1,500 Hz arrival |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Current POLY OCTAVE | 1.7% | 1.0% | 3.6% | 3.5% | 9.0–12.2 ms | 6.6–12.2 ms |
| POLY OCTAVE HQ, simulator | 75.4% | 67.3% | 20.5% | 73.7% | 7.7 ms | 5.8 ms |
| ERB, 1× bandwidth | 95.6% | 94.3% | 23.0% | 94.8% | 32.5 ms | 6.1 ms |
| ERB, 1.5× bandwidth | 73.6% | 64.3% | 18.4% | 71.9% | 7.5 ms | 5.1 ms |
| ERB, 1.5× bandwidth, half rate | 73.8% | 64.7% | 18.8% | 72.5% | 8.1 ms | 5.8 ms |
| ERB, 2× bandwidth | 44.1% | 15.8% | 11.7% | 24.5% | 5.9 ms | 0.9 ms |

Each percentage is the share of wet signal energy at the exact doubled frequencies of a synthetic chord. The close dyad uses 82 and 87 Hz and exposes intermodulation when two notes share a band. Arrival is the first one-output-cycle RMS crossing of half the settled RMS on a synthetic attack, timestamped at the measurement window's center, across 12 starting offsets. It measures early wet response in the simulator or host and excludes hardware latency and time to stable response.

The 1.5× half-rate configuration is available in POLY OCTAVE HQ, STRINGS HQ, CHOIR HQ, ORGAN HQ, and CHURCH ORGAN HQ. The modes use a shared implementation with separate state for PITCH and SYN, and their standard counterparts remain selectable. The filter bank uses half as many band updates as the full-rate version, plus resampling work. Substantial off-target energy remains on the close dyad. Profile the HQ modes on the pedal with one effect active and NAM off first, then measure the worst block time with additional effects. The table measures host audio and gives no on-device load figures.

A targeted flutter check found an off-target 169 Hz component in the HQ wet-only output for an 82 Hz + 87 Hz input, stronger than either expected octave at 164 and 174 Hz. The component is consistent with phase doubling two notes in the same analysis band and was also present with the dry signal added. A held 82 Hz tone alone had under 0.3% cycle-to-cycle RMS variation. Narrowing the prototype bandwidth from 1.5× to 1.2× improved synthetic low-E chord target energy from 65% to 88%, but moved its 82 Hz wet attack from about 8 to 33 ms. Keep the 1.5× settings while comparing other ways to separate nearby partials.

## Octave-down and capo prototype

Run `node tools/research/pitch_down_prototype.mjs [output-directory]` with Python, NumPy, and a C compiler. The script compares the pedal's POLY OCTAVE at −12 semitones and WHAMMY at its nearest setting to −2 semitones (−1.92) with a peak-locked spectral prototype. The spectral method translates regions around each peak, tracks their phase across 128-sample hops, and includes one analysis window of causal scheduling delay. It also tests 4× zero padding, which makes the FFT grid denser while keeping the input window the same size. The octave-down comparison also tries ERB sub-band phase halving with branch tracking.

The score is the share of steady wet energy at the exact shifted frequencies of each synthetic chord. Arrival is the first 50%-RMS wet response to an 82 Hz attack across 12 starting positions, measured over one output cycle. It does not measure settled pitch accuracy at the attack or include converter and pedal buffering delay. The optional WAVs are level-matched renders of the bundled chugging rhythm, clean riff, and chords. [Laroche and Dolson](https://www.ece.uvic.ca/~peterd/48409/Dobson1999.pdf) · [Bundled loop credits](../../assets/loops/ATTRIBUTION.md)

| Shift and method | Drop D chord | Low E chord | Close low dyad | High chord | 82 Hz arrival |
| --- | ---: | ---: | ---: | ---: | ---: |
| −12, standard POLY OCTAVE | 1.2% | 1.1% | 20.9% | 5.2% | 17.4–32.5 ms |
| −12, POLY OCTAVE HQ | 93.9% | 88.4% | 51.8% | 86.0% | 12.2–15.2 ms |
| −12, 512-sample spectral with 4× padding | 11.7% | 29.3% | 48.2% | 10.7% | 12.2 ms |
| −12, 1,024-sample spectral | 84.1% | 18.1% | 69.1% | 56.8% | 21.7–22.0 ms |
| −1.92, standard WHAMMY | 44.6% | 12.4% | 8.7% | 55.1% | 9.6–55.7 ms |
| −1.92, 512-sample spectral with 4× padding | 18.8% | 16.5% | 63.4% | 11.7% | 9.4–12.9 ms |
| −1.92, 1,024-sample spectral with 4× padding | 14.7% | 20.5% | 64.3% | 69.4% | 20.5–21.1 ms |
| −1.92, 2,048-sample spectral | 80.5% | 63.3% | 64.9% | 99.7% | 44.4–44.5 ms |

For sixteen identical short low-E repicks spaced about 109 ms apart, the energy of the weakest shifted repick was 4.6% of the strongest on standard POLY OCTAVE and 83.7% on HQ. Energy was measured over the first 100 ms after each pick. This stress probe still uses synthetic input.

The HQ modes use the same ERB analysis bank for the +12 and −12 voices. The octave-down voice tracks the sign of the complex square root at each band’s branch crossing. It is an octave-specific method. For capo, the 512-sample padded spectral result stabilized arrival but lost target energy on some chords. The 2,048-sample window improved accuracy at about 44 ms of wet delay. Keep the spectral capo experiment on the host.

## Benchmark before choosing

1. **Build a common 44.1 kHz, 128-sample streaming harness.** Feed the existing shifter, each candidate, and the host references identical input. Start with 100% wet at −12, −5, −2, +2, +7, and +12 semitones where supported. Add a dry blend for the octave layer and ramps for WHAMMY only after fixed shifts are stable.
2. **Use known answers and real playing.** Start the octave-down comparison with clean DI of palm mutes and fast repicking. Include the bundled chord, arpeggio, and riff recordings, then add six-string chords and bends against held notes. Generate low E and drop D notes (82.4 and 73.4 Hz), close dyads, overlapping harmonics, detuned unisons, and chord changes with controlled pick transients. Their known output frequencies are input frequencies multiplied by the selected ratio. [Bundled loop credits](../../assets/loops/ATTRIBUTION.md)
3. **Measure latency and stability separately.** Sweep attack position across the 128-sample block and across shifter phase. Report median, 95th percentile, and worst wet onset delay, and time until each expected pitch has stable level and frequency. Measure low, mid, and high bands, because broadband impulse detection can miss a late low fundamental. Report median and 95th-percentile absolute cents error, note or partial dropouts, spurious sideband energy, and envelope modulation on sustained input. Use cents against the known shifted frequency and score missing components separately. Include raw wet audio and a level-matched, latency-aligned blind comparison for roughness, smearing, and double attacks.
4. **Check the pedal before selecting defaults.** Measure added delay relative to the same chain bypassed, then total analog input-to-output delay. Report the actual worst audio-block time, `LATE`, safety bypass, and RAM high-water usage on both devices with a full NAM chain and USB audio where supported. Spread FFT work across callbacks only if the measured added hop still fits. Start with **10–15 ms added wet latency as an exploratory target for a fast mode** and a clear reduction in artifacts at comparable latency. Published analysis durations and host CPU figures cannot substitute for these checks. [OpenPocket USB-audio load method](../USB-AUDIO.md#cost)

True per-note synthesis, such as separate brass envelopes for every string, is a distinct problem. A multi-fundamental estimator such as Klapuri's harmonic salience method can be tested with GuitarSet's aligned annotations and separate-string recordings. Score missed and false notes, octave confusions, cents, onset and release errors. Keep that experiment off the critical path for transposition, which can process chords directly without choosing note identities. [Klapuri, ISMIR 2006](https://doi.org/10.5281/zenodo.1416740) · [GuitarSet dataset](https://zenodo.org/records/3371780)
