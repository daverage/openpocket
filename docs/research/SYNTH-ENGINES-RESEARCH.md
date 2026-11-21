# Open-source synth engines for tracked voices

For a **realistic trumpet**, build a **spectral-interpolation voice** in the style of Dannenberg and Derenyi. Store compact harmonic-amplitude tables, analysed on the host from the University of Iowa non-vibrato trumpet, and index them by pitch and loudness. Render them with Mutable Instruments Plaits' MIT-licensed Chebyshev `HarmonicOscillator`. Optionally, splice a short recorded attack onto the front. For the **cello**, keep the STK-style bowed waveguide that the owner likes. Improve its bow noise and body rather than replacing it. Mutable Instruments' Elements and the Karoryfer CC0 cello are the references for that work. For a **broader synth palette**, port single Plaits engines: virtual analog with filter, waveshaping, 2-op FM, formant/grain, string machine, Karplus-Strong string, modal resonator, and the 6-operator DX7 voice. All of Plaits' STM32 code is MIT, written in float, and sized for a 72 MHz Cortex-M4. Most of these engines need only a few hundred bytes of state and no sample data. Avoid engines that carry large tables (wavetables, LPC speech) or large delay memories (full Elements, Rings polyphony).

Labels used below. **Published** means stated in the cited source code, manual, paper, or licence. **Measured (OpenPocket)** means a project measurement. **Estimate** means arithmetic in this note, which still needs a timing run on the pedal with NAM active.

## Budget and assumptions

- **Target (from the task brief).** A synth voice may use at most ~10–20% of one ~240 MHz core at 44.1 kHz with 128-sample blocks. That is 24–48 M cycles per second, or about **540–1,090 cycles per output sample**. The [hardware handbook](../HARDWARE-HANDBOOK.md) records a measured core clock of 320–360 MHz, so the 240 MHz figures here are conservative. The core has a hardware single-precision FPU but no hardware square root, and libm is in software ([HARDWARE-HANDBOOK.md](../HARDWARE-HANDBOOK.md), [`firmware/px_math.c`](../../firmware/px_math.c)).
- **Memory.** About 42 KB of flash is free, or about 190 KB if the v48 sample banks are dropped. Each voice can use a few KB of RAM.
- **Cost estimates.** Each estimate counts float operations per output sample and assumes about 2 cycles per operation, including loads, stores, and loop overhead. The cycles are then converted to a percentage of 240 MHz. The ratio between engines is more reliable than the absolute numbers.
- **Measured (OpenPocket, host).** The current waveguides cost 25.9 ns/sample (trumpet) and 17.0 ns/sample (cello) on the host at -O2. Their delay lines use 2,152 B and 2,696 B ([REALISTIC-INSTRUMENT-PROTOTYPE.md](REALISTIC-INSTRUMENT-PROTOTYPE.md)).
- **Licences.** OpenPocket is GPL-3.0-only ([NOTICE.md](../../NOTICE.md)). MIT, BSD, Apache-2.0, and LGPL-2.1-or-later code can be combined with it. GPL-3.0 code (Surge XT, Vital, Dexed) can also be used, but every file then stays GPL-3.0. Every MIT/STK-style port must carry the original copyright and permission notice and get a row in `NOTICE.md`, as `engine/px_acoustic.h` already does for STK.

## Comparison

"Target" is the hardware the code was written for. "Our cost" is an estimate for one voice at 44.1 kHz. Flash covers code plus tables; RAM covers per-voice state.

| Engine | Sound families | Licence (SPDX) | Target CPU and published load | Our cost / voice (estimate) | Flash / RAM (estimate unless noted) | Port effort | Source |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Plaits** (16 + 8 engines) | VA, VA+VCF, waveshaper/folder, 2-op FM, 6-op DX7, formant/grain, additive, wavetable, chords, string machine, chiptune, phase distortion, wave terrain, speech, swarm, noise, particles, KS string, modal, 808-style drums | MIT | STM32F373 at 72 MHz, 48 kHz, 12-sample blocks, 16 KB shared engine buffer. No CPU figure is published. | 1–6% of a core per engine (see below) | Per engine: 2–10 KB code. Wavetables 50.7 KB; three DX7 banks 4 KB each. RAM: < 1 KB for most engines; 5 KB for the string; 4 KB for the string machine ensemble | Low–medium: C++ classes with stmlib helpers, float | [eurorack/plaits](https://github.com/pichenettes/eurorack/tree/master/plaits) |
| **Elements** | Bowed, blown, and struck exciters into a modal resonator, string, and tube | MIT | STM32F405 at 168 MHz, 32 kHz, 16-sample blocks. Published: exciter plus reverb take about 40% of the CPU (Rings vs Elements note). | Full voice: > 30% (estimate). Bow exciter plus 8 bowed modes: 5–10%. | Published RAM sizes: 8 bowed delays × 1,024 floats (32 KB), tube 2,048 floats (8 KB), string 3,072 floats (12 KB) | High as a whole; parts are easy | [eurorack/elements](https://github.com/pichenettes/eurorack/tree/master/elements) |
| **Rings** | Modal, sympathetic strings, KS with dispersion, FM voice, string synth | MIT | STM32F405 at 168 MHz, 48 kHz, ≤ 24-sample blocks. 64 SVF modes, split 1/2/4 ways for polyphony. | 64 modes: 15–25%. 15 modes: 4–6%. | String delays: 2,048 + 1,024 floats per string (12 KB) | Medium | [eurorack/rings](https://github.com/pichenettes/eurorack/tree/master/rings) |
| **Braids** | 47 macro-oscillator shapes, including plucked, bowed, blown, fluted, bell, drum, vowel/FOF | MIT | STM32F103 (Cortex-M3, no FPU) at 72 MHz, 96 kHz, 24-sample blocks | 1–5% | Waveguide buffers in int8/int16 (bore 2,048 × int16) | Medium: fixed-point code; Plaits supersedes most of it | [eurorack/braids](https://github.com/pichenettes/eurorack/tree/master/braids) |
| **DaisySP** | Plaits/Rings ports (StringVoice, ModalVoice, Resonator, FormantOsc, HarmonicOsc, VOSIM, Z-osc, drums), Fm2, polyBLEP oscillators, SVF, ladder | MIT; `Pluck`, `MoogLadder` and others moved to DaisySP-LGPL (LGPL-2.1) | STM32H750 (Daisy Seed). No load table is published. | Same as Plaits (same code) | Same as Plaits; KarplusString 1,024 + 256 floats | Low: standalone classes | [electro-smith/DaisySP](https://github.com/electro-smith/DaisySP) |
| **STK** | Bowed, Brass, Clarinet, Flute, Saxofony, BlowHole, Recorder, Sitar, StifKarp, Mandolin, ModalBar, FM (Rhodey…), VoicForm | MIT-style (STK; `LicenseRef-STK`) | Desktop C++, double precision | Measured (host): Bowed 17 ns/sample, Brass 25.9 ns/sample. Estimate on the pedal: 3–8%. | Delay lines sized from the lowest frequency (2–3 KB); Mandolin and ModalBar need sample files | Low per class (already done twice) | [thestk/stk](https://github.com/thestk/stk) |
| **Faust `physmodels.lib`** / `instruments.lib` | Violin, brass, clarinet, flute, guitar models; Faust-STK ports | LGPL-2.1-or-later with an exception: the generated code may use any licence. `instruments.lib` is STK-licensed. | Compiler output (`-lang c -single`) | Depends on the model: 3–15% | Generated C, fixed sample rate per build | Medium: generated code needs hand-trimming | [grame-cncm/faustlibraries](https://github.com/grame-cncm/faustlibraries) |
| **MSFA** (DX7 core used by Dexed) | 6-op DX7 | Apache-2.0 (MSFA); Dexed is GPL-3.0 | Android ARM, int32, NEON kernels, 64-sample blocks | 4–8% (estimate) | 1,024-entry sine table, 128 B per patch | Medium; Plaits `fm/` is the easier float DX7 | [google/music-synthesizer-for-android](https://github.com/google/music-synthesizer-for-android) |
| **Spectral interpolation** (Dannenberg/Derenyi) | Realistic brass and winds from analysed spectra | Method from paper; render with Plaits `HarmonicOscillator` (MIT) | Paper: 5–20 spectra per second | 24 harmonics: 3–5% | Tables: 1–3 KB; optional attacks 4–8 KB | Medium: host analysis tool plus a ~200-line voice | [CSIS 98 paper](https://www.cs.cmu.edu/~rbd/papers/csis98/csis98.htm) |
| **Surge XT** oscillators | Classic (BLIT), Modern (DPW), Wavetable, Window, FM2/3, String, Twist (= Plaits) | GPL-3.0 | Desktop, SSE/SIMD, unison | 10–40% | Large | High | [surge-synthesizer/surge](https://github.com/surge-synthesizer/surge) |
| **Vital** | Wavetable, spectral warps | GPL-3.0 | Desktop, SIMD, JUCE | Too heavy | Large | Very high | [mtytel/vital](https://github.com/mtytel/vital) |
| **Soundpipe** | Csound-derived modules: `pluck`, `bar`, `fof`, `blsaw`, `wpkorg35`, `voc` (vocal tract), `padsynth` | MIT for the project, but several modules are extracted from Csound (LGPL upstream) | Desktop C | 1–10% | Small | Low; check provenance per file | [PaulBatchelor/Soundpipe](https://github.com/PaulBatchelor/Soundpipe) |
| **Teensy Audio Library** | Waveform, PWM, Karplus-Strong, simple drum, SF2-style wavetable | MIT (with a funding notice) | Teensy 3/4 (Cortex-M4/M7), int16, 128-sample blocks | 1–3% | Small | Low, but the voices are basic | [PaulStoffregen/Audio](https://github.com/PaulStoffregen/Audio) |

## Mutable Instruments (pichenettes/eurorack)

**Licence (Published).** The repository README says "Code (STM32F projects): MIT license", "Code (AVR projects): GPL3.0", and hardware is CC-BY-SA-3.0. It also asks that the name "Mutable Instruments" not be used on derivative works. The source files carry the MIT text and "Copyright 2016 Emilie Gillet" headers. Each module's documentation page also says "source code is available under the MIT licence". Plaits, Rings, Elements, and Braids are all STM32 projects, so all four are MIT. The repository was checked at `master` `f4cfa76` (2026-10-12). [README](https://github.com/pichenettes/eurorack/blob/master/README.md) · [plaits/dsp/voice.cc header](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/voice.cc) · [Plaits open-source page](https://github.com/pichenettes/mutable-instruments-documentation/blob/master/docs/modules/plaits/open_source.md)

**Plaits targets (Published).** STM32F373CCT6 at 72 MHz (`F_CPU = 72000000L`, family `f37x`). It runs at 48 kHz (47,872.34 Hz after the I2S divider) with `kBlockSize = 12` and `kMaxBlockSize = 24`. Processing is 32-bit float. All engines share one 16,384-byte buffer (`shared_buffer[16384]`), allocated at `Init`. The docs say Plaits uses band-limited synthesis "almost everywhere" at 48 kHz, unlike Braids' 96 kHz naive oversampling. No per-engine CPU figures are published. The open-source pages say only that the code works under "tight CPU and code size limits". [dsp.h](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/dsp.h) · [makefile](https://github.com/pichenettes/eurorack/blob/master/plaits/makefile) · [plaits.cc](https://github.com/pichenettes/eurorack/blob/master/plaits/plaits.cc) · [Plaits docs](https://github.com/pichenettes/mutable-instruments-documentation/blob/master/docs/modules/plaits/index.md)

**Plaits engines (Published, from `voice.cc` and the docs).** v1.0 has 16 engines:

- `VirtualAnalogEngine`: two detuned variable-shape oscillators.
- `WaveshapingEngine`: triangle into a waveshaper and wavefolder.
- `FMEngine`: 2-op with a feedback path, computed at 4× oversampling with a FIR downsampler.
- `GrainEngine`: two formant/grainlet oscillators plus a Z-oscillator (VOSIM, pulsar, CZ-style resonance).
- `AdditiveEngine`: 36 harmonics in batched Chebyshev oscillators. The docs say "24-harmonic".
- `WavetableEngine`: four 8×8 banks.
- `ChordEngine`: divide-down organ/string or wavetables.
- `SpeechEngine`: formant, SAM, and LPC.
- `SwarmEngine`: 8 voices.
- `NoiseEngine`: clocked noise into a resonant filter.
- `ParticleEngine`: dust into resonators.
- `StringEngine`: extended Karplus-Strong, "Rings' red mode".
- `ModalEngine`: "Rings' green mode", 24 SVF modes in batches of 4.
- Bass drum, snare, and hi-hat engines.

Firmware v1.2 (final) adds 8 engines:

- `VirtualAnalogVCFEngine`: classic waveshapes with a 12/24 dB filter.
- `PhaseDistortionEngine`.
- Three banks of `SixOpEngine`: "2-voice, 6-operator FM synth with 32 presets", DX7 SysEx compatible.
- `WaveTerrainEngine`.
- `StringMachineEngine`: 4 divide-down voices, stereo filter, and chorus.
- `ChiptuneEngine`: four square voices with an arpeggiator and an NES triangle.

[voice.cc](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/voice.cc) · [engine/](https://github.com/pichenettes/eurorack/tree/master/plaits/dsp/engine) · [engine2/](https://github.com/pichenettes/eurorack/tree/master/plaits/dsp/engine2) · [Plaits firmware notes](https://github.com/pichenettes/mutable-instruments-documentation/blob/master/docs/modules/plaits/firmware.md)

**Plaits memory (Published, from code).**

- `wav_integrated_waves` has 25,344 int16 entries, or **50,688 B**. That alone is more than today's free flash.
- The DX7 banks are 4,096 B each, which is 128 B per patch.
- `lut_sine` has 641 floats (2.5 KB). The other LUTs are 65–641 entries each.
- The LPC word bank source file is 71,725 B of text.
- `String` allocates `kDelayLineSize = 1024` plus 256 floats, which is **5 KB**.
- The string machine's `Ensemble` uses a 1,024-float delay (4 KB).
- The DX7 engine runs **2 voices with staggered rendering**: each block renders one voice for twice the block length. *Inference:* on a 72 MHz M4 at 48 kHz, the budget was about one full DX7 voice of compute.

[resources.h](https://github.com/pichenettes/eurorack/blob/master/plaits/resources.h) · [string.h](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/physical_modelling/string.h) · [six_op_engine.cc](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/engine2/six_op_engine.cc) · [string_machine_engine.cc](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/engine2/string_machine_engine.cc)

**Plaits cost per engine (Estimate, 44.1 kHz, 240 MHz).**

| Engine | Cost | Notes |
| --- | --- | --- |
| VA / VA+VCF | 2–4% | |
| Waveshaping | 1–2% | |
| 2-op FM | 3–5% | 4× oversampling plus the FIR |
| Grain/formant | 2–3% | |
| Additive (36 harmonics) | 5–7% | Each harmonic costs one multiply-add for the recurrence and one for its amplitude, with per-sample amplitude interpolation |
| String machine | 4–6% | 4 voices, 2 SVFs, chorus |
| KS string | 2–3% | |
| Modal (24 modes) | 5–7% | |
| One DX7 voice | 4–8% | 6 sine lookups plus envelopes |

Each engine is driven by `EngineParameters`: `trigger`, `note` (pitch), `timbre`, `morph`, `harmonics`, and `accent`. The tracker's onset maps to `trigger`, its pitch to `note`, and its pick strength to `accent`. Expression can drive `timbre` or `morph`, or an output VCA, since loudness is applied outside the engine. [engine.h](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/engine/engine.h)

**The harmonic oscillator (Published).** `HarmonicOscillator<n>` is "based on Chebyshev polynomials". It uses the recurrence `current = 2·cos(x)·current − previous`, so each harmonic costs one multiply-add after a single sine lookup per sample. It ramps each harmonic's amplitude linearly across the block and attenuates harmonics near Nyquist. DaisySP ships the same class as `HarmonicOscillator<16>`. This is the cheapest open renderer for spectral-interpolation brass. [harmonic_oscillator.h](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/oscillator/harmonic_oscillator.h) · [DaisySP harmonic_osc.h](https://github.com/electro-smith/DaisySP/blob/master/Source/Synthesis/harmonic_osc.h)

**Elements (Published).** STM32F405RGT6 at 168 MHz, "Audio acquisition and restitution: 16-bit, 32kHz", 32-bit float, `kMaxBlockSize = 16`.

- **Exciter.** Three generators: "bowing noise" (`EXCITER_MODEL_FLOW`, particle-like scratching noise into a 2-pole LP), "blowing noise" (a granular sample player with timbre scanning), and percussive impulses (sampled sticks and mallets, plectrum, particles).
- **Blown tube.** A short `Tube` waveguide with a reed-like nonlinearity (`kTubeDelaySize = 2048` floats) is mixed in at high blow levels.
- **Resonator.** It "internally uses 64 zero-delay feedback state variable filters" (`kMaxModes = 64`). The **first 8 modes are also banded waveguides for bowing** (`kMaxBowedModes = 8`, each with a ≤ 1,024-float delay and a band-pass, fed back through `BowTable`). This is a cheap bowed sound that is not a string waveguide.
- **CPU.** The Rings design note says that Rings can split the 60 filters for polyphony "because 40% of the CPU is freed by the lack of exciter or reverb".

[Elements docs](https://github.com/pichenettes/mutable-instruments-documentation/blob/master/docs/modules/elements/index.md) · [exciter.h](https://github.com/pichenettes/eurorack/blob/master/elements/dsp/exciter.h) · [resonator.h](https://github.com/pichenettes/eurorack/blob/master/elements/dsp/resonator.h) · [resonator.cc](https://github.com/pichenettes/eurorack/blob/master/elements/dsp/resonator.cc) · [tube.cc](https://github.com/pichenettes/eurorack/blob/master/elements/dsp/tube.cc) · [voice.cc](https://github.com/pichenettes/eurorack/blob/master/elements/dsp/voice.cc) · [Rings docs](https://github.com/pichenettes/mutable-instruments-documentation/blob/master/docs/modules/rings/index.md)

*Estimate.* Full Elements is about 168 MHz × 32 kHz worth of work. At 44.1 kHz on this core it would take well over the budget, and its delays alone need about 52 KB of RAM. What is worth taking is the **flow (bow-noise) exciter** (a few dozen operations) and, for experiments, the **8 bowed banded-waveguide modes**. With delays held in int16 and capped at the cello range, those need roughly 8–16 KB.

**Rings (Published).** STM32F405 at 168 MHz, 48 kHz, `kMaxBlockSize = 24`. Its models are modal, sympathetic string, string, FM voice, quantized sympathetic string, and string-and-reverb. With polyphony 1, 2, or 4, the resolution is `64 / polyphony − 4` modes per voice. Strings use 2,048 + 1,024-float delays. [part.cc](https://github.com/pichenettes/eurorack/blob/master/rings/dsp/part.cc) · [string.h](https://github.com/pichenettes/eurorack/blob/master/rings/dsp/string.h) · [Rings docs](https://github.com/pichenettes/mutable-instruments-documentation/blob/master/docs/modules/rings/index.md)

**Braids (Published).** STM32F103CBT6 at 72 MHz, which is a Cortex-M3 without an FPU. "96kHz, 16-bit audio processing (some algorithms are 2x or 4x oversampled)." `kBlockSize = 24`. It has 47 shapes, including `BOWED`, `BLOWN`, `FLUTED`, `PLUCKED`, `STRUCK_BELL`, and `VOWEL_FOF`, all fixed-point. The waveguide memories are int8/int16 (`kWGBoreLength = 2048`, `kWGNeckLength = 4096`). Plaits re-implements most of Braids' sounds in float, so port from Plaits. [Braids docs](https://github.com/pichenettes/mutable-instruments-documentation/blob/master/docs/modules/braids/index.md) · [digital_oscillator.h](https://github.com/pichenettes/eurorack/blob/master/braids/digital_oscillator.h) · [settings.h](https://github.com/pichenettes/eurorack/blob/master/braids/settings.h)

## DaisySP

**Licence (Published).** "DaisySP, copyright (c) 2020 Electrosmith, Corp. Published under the MIT license". Modules that came from Csound were moved to **DaisySP-LGPL** (LGPL-2.1): `Pluck`/`PolyPluck`, `MoogLadder`, `Blosc`, `Biquad`, `Comb`, `Allpass`, `Mode`, `NlFilt`, `Tone`/`ATone`. LGPL-2.1 code can be used in a GPL-3.0 program. Its headers name their sources. `StringVoice` is "Ported from pichenettes/eurorack/plaits/dsp/physical_modelling/string_voice" ("Extended Karplus-Strong, with all the niceties from Rings"). `Resonator` is ported from Plaits' `resonator.h` with `kMaxNumModes = 24`. `FormantOscillator` and `HarmonicOscillator` are also from Plaits. `KarplusString` uses 1,024 + 256-float delays. `Fm2` is two `Oscillator`s. Its target is the Daisy Seed's STM32H750, and no CPU table is published. **Conclusion:** DaisySP's physical models are standalone repackagings of Plaits. They are a convenient MIT starting point, but they add no new synthesis. [LICENSE](https://github.com/electro-smith/DaisySP/blob/master/LICENSE) · [PhysicalModeling/](https://github.com/electro-smith/DaisySP/tree/master/Source/PhysicalModeling) · [stringvoice.h](https://github.com/electro-smith/DaisySP/blob/master/Source/PhysicalModeling/stringvoice.h) · [resonator.h](https://github.com/electro-smith/DaisySP/blob/master/Source/PhysicalModeling/resonator.h) · [DaisySP-LGPL](https://github.com/electro-smith/DaisySP-LGPL)

## Synthesis ToolKit (STK)

**Licence (Published).** The licence is MIT text: "Copyright (c) 1995-2023 Perry R. Cook and Gary P. Scavone". It adds a non-binding request to send modifications back. Each waveguide header says it "is a digital waveguide model, making its use possibly subject to patents held by Stanford University, Yamaha, and others". *Estimate, not legal advice:* those headers date from the 1990s, and patents filed then would be past a 20-year term. [LICENSE](https://github.com/thestk/stk/blob/master/LICENSE) · [Brass.h](https://github.com/thestk/stk/blob/master/include/Brass.h)

**Classes (Published, from headers).**

- `Bowed`: after Smith 1986 and McIntyre-Schumacher-Woodhouse 1983, with "Contributions by Esteban Maestre, 2011". It has two `DelayL`s, a `BowTable`, and 6 body `BiQuad`s.
- `Brass`: "a la Cook (TBone, HosePlayer)", built from one `DelayA`, a `BiQuad` lip filter, and a DC block.
- `Clarinet`: one `DelayL` and a `ReedTable`.
- `Flute`: jet and bore delays with a `JetTable` polynomial.
- `Saxofony`: a "blowed string" with two delays and a reed table. The header says that near the bridge it sounds closer to a saxophone.
- `BlowHole`: a clarinet with a register hole and a tonehole, after Scavone and Cook 1998.
- `Recorder`: after Verge, Hirschberg, and Caussé 1997, added in 2023.

`Mandolin` and `ModalBar` load sample files (`mand1–12.raw`, `marmstk1.raw`), so they are not self-contained. [Bowed.h](https://github.com/thestk/stk/blob/master/include/Bowed.h) · [Clarinet.h](https://github.com/thestk/stk/blob/master/include/Clarinet.h) · [Saxofony.h](https://github.com/thestk/stk/blob/master/include/Saxofony.h) · [Recorder.h](https://github.com/thestk/stk/blob/master/include/Recorder.h) · [Mandolin.cpp](https://github.com/thestk/stk/blob/master/src/Mandolin.cpp)

**What works (Measured, OpenPocket).** The bowed model was in tune within 1 cent from C2 to C4 and came closer to the cello recordings than STRINGS (7.4 dB against 9.8 dB). The brass model needed three changes to speak and stayed 17 dB from the trumpet recordings ([REALISTIC-INSTRUMENT-PROTOTYPE.md](REALISTIC-INSTRUMENT-PROTOTYPE.md)). *Estimate:* `Clarinet` and `Saxofony` cost about the same as `Brass` (one delay, one table, 1–2 filters, 3–5%). They are the cheapest woodwind additions, because a reed table speaks far more reliably than STK's lip model.

## Faust libraries

**Licence (Published).** `physmodels.lib` is LGPL-2.1-or-later with an "EXCEPTION TO THE LGPL LICENSE": the compiled code generated by the Faust compiler may be distributed "under your own copyright and license". `instruments.lib` is the Faust-STK import, under `LicenseRef-STK-4.3` ("MIT style license"). [physmodels.lib](https://github.com/grame-cncm/faustlibraries/blob/master/physmodels.lib) · [instruments.lib](https://github.com/grame-cncm/faustlibraries/blob/master/instruments.lib)

**Models (Published).** `violinModel` (bow table, nut, bridge, body), `brassModel` with `brassLips`, `clarinetModel`, `fluteModel`, and the `guitar`/`nylonGuitar`/`elecGuitar` models. The compiler emits C with `-lang c`. It uses single precision by default (`-single`), and offers `-os` (one-sample computation), `-mem` (custom memory manager), and `-ftz` (flush-to-zero on recursive signals). [physmodels.lib](https://github.com/grame-cncm/faustlibraries/blob/master/physmodels.lib) · [Faust compiler options](https://faustdoc.grame.fr/manual/options/)

*Estimate.* The generated C is usable, but it is a flat `compute()` with fixed-size arrays sized from the maximum string or tube length. Faust's `physmodels` brass uses the same Cook lip filter as STK, so it is not expected to sound better. Its main use is as a quick host A/B reference for the violin and clarinet before hand-porting.

## FM engines

**MSFA (Published).** Apache-2.0, "Copyright 2012 Google Inc.". It uses an int32 fixed-point operator kernel with optional NEON assembly (`neon_fm_kernel.s`), a 1,024-entry sine table with deltas (`SIN_LG_N_SAMPLES 10`), and 64-sample blocks (`LG_N 6`). Dexed builds on MSFA but is GPL-3.0. [fm_op_kernel.cc](https://github.com/google/music-synthesizer-for-android/blob/master/app/src/main/jni/fm_op_kernel.cc) · [sin.h](https://github.com/google/music-synthesizer-for-android/blob/master/app/src/main/jni/sin.h) · [synth.h](https://github.com/google/music-synthesizer-for-android/blob/master/app/src/main/jni/synth.h) · [Dexed](https://github.com/asb2m10/dexed)

**Plaits `fm/` (Published).** This is a float "DX7 voice" with `algorithms`, `envelope`, `lfo`, `operator`, and `patch` files. A comment notes that one modulation path uses "a more complex formula involving an exponential … to match Dexed's output". It loads 32-patch DX7 SysEx banks. Because it is MIT and float, it is the easier DX7 for OpenPocket. A 4-op design would be the same code with `Algorithms<4>`. *Estimate:* about two thirds of the 6-op cost. [plaits/dsp/fm](https://github.com/pichenettes/eurorack/tree/master/plaits/dsp/fm) · [fm/voice.h](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/fm/voice.h)

## Spectral interpolation for realistic brass

**Method (Published).** Dannenberg and Derenyi resynthesise the trumpet by "interpolating between … wave tables", using "only 5 to 20 spectra per second". "The spectrum itself is stored as an array of the relative amplitudes of the harmonics", indexed by overall RMS amplitude and pitch. "We do not store the phase information", so interpolation causes no phase cancellation. Because "attacks carry significant perceptual cues", they "simply splice sampled attacks onto synthesized sounds", and "30 ms attacks work well with the trumpet". Horner, Beauchamp, and Haken fit a tone with a few fixed wavetables under time-varying weights, found by genetic algorithm or PCA. They report that 3–5 tables often give a good replica, with trumpet among the tested tones. Serra's SMS adds a stochastic residual (breath noise) on top of the sinusoids. [Derenyi & Dannenberg, CSIS 98](https://www.cs.cmu.edu/~rbd/papers/csis98/csis98.htm) · [Horner et al., JAES 41(5) 1993](https://www.aes.org/e-lib/browse.cfm?elib=7003) · [Serra & Smith, SMS](https://mtg.upf.edu/node/251)

**Open analysis code.** `MTG/sms-tools` has `harmonicModel.py` and `hpsModel.py` (harmonic plus stochastic), and is **AGPL-3.0**. Use it only as a host tool, or rewrite the harmonic peak-picking in `tools/` with numpy, which the project already uses in `tools/legacy-synth/acoustic_prep.py`. Tables produced from recordings are data, not code. [sms-tools](https://github.com/MTG/sms-tools)

**Recordings (Published, verified).**

- **University of Iowa MIS.** "Since 1997, these recordings have been freely available on this website and may be downloaded and used for any projects, without restrictions." The Bb trumpet (Bach Stradivarius, anechoic chamber, "16-bit, 44.1 kHz, mono") has `Trumpet.novib.{pp,mf,ff}.{E3B3,C4B4,C5B5,C6D6/Eb6}.aiff`. [MIS home](https://theremin.music.uiowa.edu/MIS.html) · [Bb trumpet page](https://theremin.music.uiowa.edu/MISBbtrumpet.html)
- **Karoryfer x bigcat cello.** The GitHub licence is CC0-1.0, and the readme says: "This instrument is open source, and you can do whatever you want with it". It contains `Samples/sus` (136 files, e.g. `A1_f_d.wav`), staccato, pizzicato, and noises, played by Kamila Borowiak. [Repository](https://github.com/sfzinstruments/karoryfer-bigcat.cello) · [readme.txt](https://github.com/sfzinstruments/karoryfer-bigcat.cello/blob/master/readme.txt)

*Estimate: table size.* Use 6 pitch regions × 8 loudness steps × 24 harmonics, with one byte per amplitude (dB, 0.5 dB steps). That is **1,152 B**. Adding 30 ms attacks for 6 regions at 22.05 kHz in 8-bit mu-law adds about **4 KB**. Today's TRUMPET bank holds two looped dynamics. This table spans ff to pp by interpolating, so soft notes keep the measured upper harmonics instead of near-sine soft takes. The flute-like sound comes from those soft takes. Extrapolate below the quietest analysed level with a spectral-tilt rule rather than using pp recordings.

## Other candidates

- **Surge XT (GPL-3.0).** Its oscillators are SIMD and unison-oriented. `ModernOscillator` documents DPW (differentiated polynomial waveforms). `TwistOscillator` embeds `plaits::Voice` directly. The `Classic` oscillator is BLIT-based with SSE. Nothing in it is cheaper or more portable than the Plaits originals. [oscillators/](https://github.com/surge-synthesizer/surge/tree/main/src/common/dsp/oscillators) · [ModernOscillator.cpp](https://github.com/surge-synthesizer/surge/blob/main/src/common/dsp/oscillators/ModernOscillator.cpp) · [TwistOscillator.h](https://github.com/surge-synthesizer/surge/blob/main/src/common/dsp/oscillators/TwistOscillator.h)
- **Vital (GPL-3.0).** It is desktop-only, built on JUCE and SIMD. It is not portable within the budget. [mtytel/vital](https://github.com/mtytel/vital)
- **Soundpipe (MIT).** `pluck.c` says it "has been extracted from the Csound opcode", by Vercoe and ffitch, 1991. Electrosmith moved code with that kind of origin into an LGPL repository. Check provenance before copying any module. `voc` (vocal tract) and `padsynth` are interesting, but they are not needed first. [modules/](https://github.com/PaulBatchelor/Soundpipe/tree/master/modules) · [pluck.c](https://github.com/PaulBatchelor/Soundpipe/blob/master/modules/pluck.c)
- **Teensy Audio (MIT, with a development-funding notice to keep).** Its synths are basic waveforms, PWM, Karplus-Strong, a simple drum, and a sample wavetable. They are cheap but not better than what OpenPocket has. [synth_waveform.cpp](https://github.com/PaulStoffregen/Audio/blob/master/synth_waveform.cpp)

## Ranked recommendations

### (a) Realistic trumpet and brass

1. **Spectral-interpolation trumpet.** Analyse Iowa `novib` mf and ff into harmonic tables indexed by pitch and loudness, and drive loudness from the tracker's expression. Render 20–24 harmonics with a C port of Plaits' `HarmonicOscillator`. Add an SMS-style band-passed breath residual, plus optional 30 ms ff attack splices that pick strength scales. *Estimate:* 3–5% of a core, 1–5 KB flash, < 0.5 KB RAM per voice. It replaces 190 KB of PCM16 (Measured, prototype) or the v48 mu-law bank. The same tool can later produce trombone, horn, and the saxophone and clarinet tables from Iowa.
2. **DX7 brass as a "synth brass" voice.** Port Plaits `fm/` with one patch (128 B). *Estimate:* 4–8%. It is recognisably brassy but not realistic.
3. **Drop the STK brass waveguide** unless listening says otherwise (17 dB from the recordings; Measured).

### (b) Realistic bowed strings and cello

1. **Keep `px_acoustic.h`'s bowed waveguide.** Replace its simple noise term with Elements' `EXCITER_MODEL_FLOW` bow-scratch generator, which is a few operations per sample. Fit the six body biquads to the Karoryfer CC0 cello's long-term spectrum (sustain, f and p) with the existing `acoustic_compare.py`. *Estimate:* +1% CPU, no flash.
2. **Optional attack realism.** Splice a 40–60 ms Karoryfer bow onset in mu-law, a few KB per region, in the same way as the trumpet attacks.
3. **Experiment: Elements' 8 bowed banded-waveguide modes** as a "section" or "ensemble" body. *Estimate:* 5–10% and 8–16 KB RAM, so only if (1) falls short.
4. Faust `violinModel` as a host A/B only.

### (c) Cheap tracked synth palette (Plaits ports, MIT)

Ranked by value per cost. Each costs 1–6% of a core (estimate) with no sample data:

1. `VirtualAnalogVCFEngine`: lead/bass with filter.
2. `StringMachineEngine`: a one-note-to-chord string pad. It needs 4 KB RAM.
3. `StringEngine` (KS): plucked string and harp. It needs 5 KB RAM, or 2.5 KB in int16.
4. `ModalEngine`: mallets and bells, ~5–7%.
5. `FMEngine` (2-op) and `WaveshapingEngine`: bell and fold timbres.
6. `GrainEngine`: vocal-formant leads.
7. `ChiptuneEngine`: square arpeggios, which the tracked chord list can drive.
8. One `SixOpEngine` voice with a single small bank.

Skip `WavetableEngine` (50.7 KB), LPC speech (large word tables), and Swarm and Rings polyphony (CPU). For woodwinds, port STK `Clarinet` and `Saxofony` (3–5% each, ~2 KB RAM), or reuse the spectral tool with Iowa clarinet and saxophone recordings.

### (d) Concrete first port

**Port Plaits' harmonic oscillator and build the spectral-interpolation trumpet on it.**

- **Code.** Port `plaits/dsp/oscillator/harmonic_oscillator.h` (the `HarmonicOscillator` template) and the `ParameterInterpolator` it uses from `stmlib/dsp/parameter_interpolator.h`. Write C in `engine/px_harm.h`, with `Sine()`/`SineNoWrap()` mapped to OpenPocket's own sine table so that `lut_sine` (2.5 KB) is not copied.
- **Host tool.** Write `tools/mk_trumpet_si.py` (numpy only) to produce `engine/px_trumpet_si.h` from the Iowa files, recording their URLs and SHA-256 sums the way `bank.json` does.
- **Licence obligations.** Keep the MIT notice "Copyright 2016 Emilie Gillet" plus the permission text in the file header. Add a `NOTICE.md` row. Do not use the "Mutable Instruments" name for the voice. The Iowa data has no licence obligation ("without restrictions"), but record its provenance.
- **Expected cost (estimate).** Under 2 KB of code, 1–5 KB of tables, under 0.5 KB RAM per voice, and 3–5% of one core for 24 harmonics, with one sine lookup per sample.
- **Before shipping.** Time it on both pedals with NAM active (worst block, `LATE`, RAM high-water). Score it with `acoustic_compare.py` against the Iowa notes that were held out of the analysis. Only a person can judge whether it sounds like a trumpet.
