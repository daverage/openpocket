# Real-time DSP for tracking only the lowest guitar note in chords and generating an octave-down bass voice

## Executive conclusion

For a conventional mono magnetic-pickup guitar signal, the best engineering strategy is **not** to treat the problem primarily as monophonic pitch detection, and it is also **not** to rely on a single low-pass filter. The most promising low-latency architecture is a **hybrid spectral-selection system**: first identify the lowest musically plausible fundamental or lowest active pitch region using a semitone/log-frequency filterbank and harmonic evidence; then generate the octave-down voice from that estimate, rather than attempting perfect waveform separation of the original string. A second, optional waveform-derived path can add guitar-like texture once the bass note has been identified.

That conclusion is strikingly consistent with Roland intellectual property filed immediately before the OC-5 appeared. BOSS publicly documents only that the OC-5 has a new tracking engine, that Poly mode can restrict the octave effect to lower notes, and that the extreme Range setting isolates the bottom note of a chord. BOSS does **not** publish the firmware algorithm. citeturn556095search0turn556095search1 However, Roland patent EP3929910B1, with priority date 26 June 2020, describes an effects pedal that divides the guitar signal into 33 pitch-oriented bands, measures their levels, suppresses bands according to energy in lower bands, and applies octave processing preferentially to the lowest active region. The OC-5 was announced later in 2020 and has exactly the guitar/bass selection and lowest-note octave behaviour described in the patent. That is powerful circumstantial evidence, but it is not proof that the production OC-5 firmware is identical to the disclosed embodiment. citeturn435704view0turn158265view0turn556095search1

The Roland approach also reveals an important design insight: **you do not necessarily have to solve full multi-pitch transcription to make a convincing lowest-note octave pedal**. You can exploit the structure of guitar spectra and the fact that you care only about the bottom voice. Roland's patent uses bands from roughly 55 Hz upwards and explicitly evaluates cumulative energy below each candidate band; a band receives full octave level when the energy beneath it is sufficiently small, then is progressively attenuated as lower-frequency evidence increases. citeturn158265view0turn483661view1 This is substantially simpler and faster than a general six-note transcription system.

My recommended implementation would therefore use three levels of evidence:

**fast path:** causal log-frequency filterbank and cumulative-low-frequency energy logic, capable of reacting within roughly 5–12 ms;

**confirmation path:** harmonic-salience lowest-F0 estimator using several harmonics and temporal continuity, typically settling within roughly 10–20 ms for standard guitar range;

**synthesis path:** an event-driven oscillator or short wavetable/sample bass voice driven by continuously estimated pitch and envelope, rather than a full-band pitch shifter.

A neural MPE model is useful as an offline benchmark and potentially as a later hybrid confidence estimator, but it would not be my first implementation for a stage pedal. Recent causal neural systems are getting impressively small, but even the 86k-parameter JIT-MPE system published in October 2026 is designed around 20 ms frames and obtains materially better onset accuracy when allowed tens of milliseconds of additional audio; the paper explicitly treats approximately 10 ms instrument-like response as outside its target regime. citeturn488079academia10turn488079search11

## What the OC-5 appears to be doing

The documented part is modest. The OC-5 manual says Poly mode supports chordal input and that the Range control narrows the octave effect towards lower frequencies; fully counter-clockwise, the effect is applied only to the lowest note of the chord. BOSS says its new tracking technology gives improved accuracy and natural response and offers separate guitar/bass optimisation. citeturn556095search7turn556095search0

The Roland patent is much more revealing. Its preferred embodiment sends the guitar signal into **33 band-pass filters**. The lowest disclosed centre frequency is A1 at 55 Hz, followed by B1 at 61.7 Hz, with contiguous overlapping bands extending to 2.217 kHz; the bands are approximately pitch-spaced rather than linearly spaced. The patent explicitly notes that overlap accommodates pitches between equal-tempered notes and bends. citeturn158265view0

For each band \(i\), Roland measures level \(L_i\). It then calculates the sum of energy in all lower bands and forms a ratio against the current band's level. Bands for which little energy exists below them are treated as candidates for the lowest sounding region. The disclosed transfer function gives full octave output for low ratios, fades between two thresholds, and blocks the band when sufficiently strong lower-frequency energy exists. citeturn483661view1turn483661view2 The patent even discusses the case where a low note is already sounding and a higher string is then played: because higher-band activity does not alter the accumulated energy *below* the low band, the low note remains selected. citeturn483661view0

Conceptually, the patent is doing this:

```text
                    ┌─ BPF 1 ─ level L1 ─ octave ─ gain C1 ─┐
                    ├─ BPF 2 ─ level L2 ─ octave ─ gain C2 ─┤
Guitar ─ ADC ───────┼─ BPF 3 ─ level L3 ─ octave ─ gain C3 ─┼─ sum ─ wet
                    │                    ...                  │
                    └─ BPF N ─ level LN ─ octave ─ gain CN ─┘
                                  │
                 cumulative energy below each band
                                  │
                         lowest-band weighting
```

The patent allows octave conversion before or after the lowest-band weighting, and even describes summing the selected bands first and sending the result through a single octave processor. citeturn158265view1 That flexibility matters because it means the key invention is not a magical polyphonic pitch tracker. It is a **frequency-band selection rule that favours the bottom component before octave generation**.

This also clarifies why simple low-pass filtering is related to the right idea but insufficient. A static LPF can remove upper strings only when their fundamentals lie clearly above the cutoff. In a normal guitar chord, the lowest string contributes many harmonics well above the fundamental while upper strings may have fundamentals below or near any useful cutoff. A fixed cutoff therefore either removes useful low-note harmonics or admits upper notes. Roland's approach is effectively a much more intelligent, adaptive version of low-frequency selection. citeturn158265view0turn483661view2

Older Roland work shows the evolutionary path. A 2003 Roland patent describes splitting the input into frequency bands and creating an octave-lower signal separately in each band so that chords can be processed. citeturn509256view1 A later Roland patent explicitly describes the earlier system as dividing the chord with band-pass filters, deriving octave-down signals from individual bands, and notes that merely using the fundamental of each filtered component produced a relatively sine-like timbre. citeturn509256view2 The 2020-priority patent then adds the specific mechanism for favouring the lowest active region. citeturn435704view0

## Algorithm families and where they fail

The fundamental distinction is between **detecting which fundamental is lowest** and **recovering that string's waveform**. The first is a recognition problem. The second is a blind source-separation problem. With one magnetic pickup, two strings can have partials at almost identical frequencies, and those components are linearly summed before the ADC. Once two partials overlap in a single channel, there is generally no unique way to attribute the shared sinusoidal energy back to one string without a model or prior. Guitar pickup location and plucking location also create comb-like spectral attenuation, so individual harmonics may be heavily weakened or absent. Studies of electric-guitar spectra explicitly model both pickup position and pluck position as comb filtering, and guitar-string partials exhibit measurable inharmonicity. citeturn590255search9turn590255search10turn590255search6

**Adaptive low-pass / filterbank selection.** This is the strongest choice when latency is paramount. Use 24–48 logarithmically spaced IIR bands, or a small constant-Q-like bank, measure short-time energy, and locate the lowest band or harmonic family that is plausibly active. The Roland patent demonstrates that this class of solution is practical for exactly the requested effect. citeturn158265view0turn483661view1 The weakness is that raw band energy does not distinguish a fundamental from a harmonic. If an E2 fundamental is weak but its E3 harmonic is strong, while another string produces a note around E3, simple band gating can confuse ownership.

**Harmonic salience / harmonic summation.** This is the best classical confirmation stage. For every plausible guitar \(f_0\), evaluate energy near \(f_0,2f_0,3f_0,\ldots\), preferably after spectral whitening, and select the lowest high-confidence candidate. Klapuri's multiple-F0 work formalises this as a weighted sum of harmonic amplitudes and shows that iterative salience and cancellation can support polyphonic estimation. citeturn556095search9turn556095search8 For this application, full iterative cancellation is probably unnecessary; you only need the lowest credible \(f_0\). Weighting several upper harmonics is particularly useful when the fundamental is weak.

A useful score is

\[
S(f)=\sum_{h=1}^{H} w_h\,P(hf)\,R(h,f)-\lambda\,A(f/2)
\]

where \(P\) is locally whitened spectral energy, \(w_h\) decays with harmonic number, \(R\) allows a small inharmonicity tolerance, and the final term suppresses subharmonic or octave errors. Harmonic-salience methods specifically use local spectral contrast to reduce half-frequency and multiple-frequency mistakes. citeturn556095search6

**STFT/CQT/VQT and spectral masks.** An STFT is cheap and familiar, but low-frequency resolution and latency conflict directly: a short FFT gives poor bin spacing, while a long FFT delays the decision. A guitar-effects comparison showed that a 1024-point FFT at 44.1 kHz is far too coarse for low guitar notes unless phase information or interpolation is exploited. citeturn336208view0 A logarithmic CQT/VQT matches musical pitch spacing better, but long low-frequency kernels remain an issue. Cached, streamable VQT implementations can reduce computation, as demonstrated by the 2025 PESTO system, though PESTO itself is a monophonic pitch estimator rather than a chord-bottom extractor. citeturn488079search3

**Autocorrelation, YIN, pYIN and MPM.** These are excellent when the input itself is monophonic, but a chord violates their core single-periodicity model. YIN is efficient and robust for single guitar notes; a DAFx comparison found it attractive for real-time guitar pitch tracking and measured about 13.4 ms algorithmic delay without smoothing in its particular test configuration. citeturn893641search6turn336208view0 pYIN improves temporal stability by retaining multiple YIN candidates and selecting a path probabilistically, but it remains fundamentally a monophonic pitch-track model. citeturn893641search7 MPM likewise targets continuous monophonic pitch and provides a useful clarity measure, but a chord can create correlation maxima corresponding to common divisors or subharmonic relationships rather than any actual played string. citeturn893641search8

This means YIN or MPM becomes much more useful **after** a low-band selection stage. Run it on the candidate bass-band reconstruction rather than on the original chord.

**Harmonic product spectrum.** HPS is actually a spectral technique rather than a time-domain one. It compresses harmonic structure towards the fundamental by downsampling and multiplying spectra. It is cheap, but multiplication makes it vulnerable to a missing or deeply notched harmonic and can amplify subharmonic mistakes in polyphonic material. For a pedal I would use weighted additive harmonic salience instead.

**NMF / PLCA / sinusoidal separation.** Harmonic NMF can represent simultaneous notes with constrained harmonic templates and has long been used for multi-pitch analysis. citeturn660934search8 A guitar-specific system combining multipitch estimation, partial tracking and instrument constraints reported very high frame-level accuracy on its own evaluation material. citeturn590255search3 These methods are attractive for offline analysis, but iterative optimisation and template maintenance buy considerably more capability than this effect requires. They are also awkward at note attacks because the transient is not well modelled by steady harmonic templates.

**Sinusoidal modelling and masking.** Once the lowest \(f_0\) is known, you can assign spectral peaks near \(hf_0\) to the bass voice and resynthesise them at \(hf_0/2\). This preserves more of the original guitar timbre than a plain oscillator, but partial ownership is ambiguous wherever another chord tone overlaps the same bins. Guitar pickup comb filtering and inharmonicity mean the harmonic pattern is not perfectly regular either. citeturn590255search9turn590255search11 It is viable as a coloration layer, not as the sole detector.

**Neural MPE.** Spotify's Basic Pitch is a lightweight polyphonic transcription model and explicitly supports guitar and pitch bends, but it is a transcription-oriented system rather than a sub-10-ms control loop. citeturn408689search0turn408689academia32 FretNet is more guitar-specific and estimates continuous pitch contours tied to strings/frets, again mainly as a transcription system. citeturn660934academia6 JIT-MPE is much more relevant to live work: it is causal, only about 86k parameters, processes 20 ms frames, and can successively refine estimates with extra lookahead. Its results also demonstrate the central difficulty here: the first 20 ms around note onset are precisely where pitch evidence is weakest. citeturn488079academia10

Neural inference therefore makes sense as a **later confidence layer** or desktop research benchmark. For a dedicated pedal, the classical filterbank/harmonic hybrid offers tighter worst-case latency, deterministic behaviour and much easier failure analysis.

Distorted input should be treated as a separate operating regime. Nonlinear guitar processing adds harmonics and, for chords, intermodulation products that were not present in the pickup signal; that destroys the clean harmonic ownership assumption. Guitar-effects research treats distortion as a materially different signal-processing problem. citeturn289521search21turn289521search35 The detector should therefore sit **before distortion whenever possible**. If the unit must accept distorted guitar, thresholds and continuity constraints should become more conservative.

## Recommended architecture

The most practical version is a **dual-path detector with synthesis**, not full separation:

```text
                                    ┌────────────────────────────┐
Guitar ─ preamp ─ ADC ─ HP/DC ─────┤ transient / envelope path  ├───────┐
               │                    └────────────────────────────┘       │
               │                                                        │
               │     ┌───────────────────────────────┐                  │
               ├────►│ log-frequency IIR filterbank │                  │
               │     │ ~36-48 bands, 50 Hz–2.5 kHz  │                  │
               │     └──────────────┬────────────────┘                  │
               │                    │ energies                         │
               │                    ▼                                  │
               │        cumulative-lower-band test                     │
               │                    │                                  │
               │                    ▼                                  │
               │           coarse lowest region                        │
               │                    │                                  │
               │        ┌───────────▼─────────────┐                    │
               └───────►│ harmonic-salience test  │                    │
                        │ candidate ± neighbours   │                    │
                        └───────────┬─────────────┘                    │
                                    │ f0 + confidence                  │
                                    ▼                                  │
                         causal state tracker                           │
                   hysteresis / bends / legato logic                    │
                                    │                                  │
                         f0/2, envelope, attack                         │
                                    ▼                                  │
                     bandlimited bass synthesiser ◄─────────────────────┘
                                    │
                     optional harmonic/residual colour
                                    │
                                    ▼
                              octave output
```

The first-stage filterbank should be IIR rather than a long constant-Q FIR implementation. Thirty-six to forty-eight biquad bands is entirely feasible on Cortex-M7 hardware. Use roughly one band per semitone, optionally half-semitone spacing in the lowest two octaves. Roland's disclosed 33-band arrangement demonstrates that semitone-oriented filtering is a sensible scale for this job. citeturn158265view0

Do **not** declare the lowest non-zero band to be the pitch. Instead compute a Roland-like cumulative lower-energy statistic and then evaluate harmonic consistency around the best few candidates. A robust version is:

```text
for each audio block:
    update onset/envelope detector

    for band i:
        y[i] = bandpass_i(x)
        E[i] = fast_RMS_or_abs_envelope(y[i])

    lower = 0
    for i from low to high:
        ratio[i] = lower / (E[i] + eps)
        lower += E[i]

    candidates = low-frequency bands with:
        E[i] > noise_floor
        ratio[i] < ratio_max

    for candidate f:
        S[f] =
            w1 * local_energy(f)
          + w2 * local_energy(2f)
          + w3 * local_energy(3f)
          + w4 * local_energy(4f)
          - subharmonic_penalty(f/2)
          - inharmonicity_penalty
          + continuity_bonus(previous_f0, f)

    f_new, confidence = lowest candidate with S > threshold

    if onset:
        allow rapid state change
    else:
        require hysteresis before changing bass note

    if bend/slide:
        interpolate f0 continuously
    else:
        track smoothly but do not quantise unless "stable-note" mode enabled

    bass_pitch = 0.5 * f0
    bass_amp = mapped_input_envelope
    output = synth(bass_pitch, bass_amp)
```

For the filterbank energy detector I would begin with attack/release time constants around **1–3 ms attack and 20–60 ms release**. Those are design starting points, not values documented by BOSS. A slightly slower release helps prevent higher strings from taking over while the lowest string is decaying. A separate onset detector should temporarily shorten hysteresis so that a genuine bass-note change is accepted quickly.

The harmonic-confirmation stage does not need a full 2048/4096-point FFT every frame. One option is Goertzel/heterodyne measurements at candidate harmonic frequencies; another is a small 1024-point phase-refined FFT updated every 32–64 samples. Since the coarse filterbank has already narrowed the candidate region, evaluating perhaps 5 candidates × 6 harmonics is cheap.

The tracker should keep continuous frequency rather than immediately mapping to MIDI notes. Guitar bends and vibrato are part of the signal; Roland explicitly designed overlapping filter bands so intermediate pitches and bends remain represented. citeturn158265view0 Quantisation should be optional and used only for a deliberately synth-like mode.

For lowest-note switching, I would use asymmetric hysteresis. Allow an immediately detected **lower** pitch to take control quickly. Require stronger or sustained evidence before switching **upward**, because the most common musically annoying failure is the octave bass suddenly jumping to an upper chord tone when the low string decays.

Alternate tunings should require no fundamental algorithm change. The candidate range can simply extend down to approximately 45–50 Hz for unusually low guitars. The trade-off is detection time: as frequency falls, a complete waveform cycle becomes longer. The DAFx guitar comparison explicitly notes that minimum pitch constrains frame length and used 13.3 ms merely to observe one period at 75 Hz. citeturn336208view0 The filterbank/harmonic method can respond earlier than a strict one-period time-domain estimator because upper harmonics arrive immediately, but confidence should be lower during the first few milliseconds.

For inversions, the output should follow the **actual bottom note**, not the theoretical chord root. That is what the effect behaviour implies and what the Roland patent's bottom-frequency logic naturally implements. The patent occasionally uses root terminology, but the disclosed algorithm is based on lowest spectral/pitch region rather than harmonic-function analysis. citeturn158265view1

### Synthesis choice

For the main production mode I would choose **pitch detection plus synthesised bass**, not a conventional pitch shifter.

A synthesised bass can start as soon as the tracker has enough evidence and does not need to buffer a long stretch of the original waveform for time-scale modification. It also avoids contamination from upper chord tones. The tone can be much more convincing than a plain sine if you use a bandlimited waveform with controlled harmonic evolution: for example, sine + 15–25% second harmonic + smaller third harmonic, shaped by the input envelope and mildly saturated after synthesis.

The envelope should come primarily from the low candidate band, not the full chord, otherwise upper-string attacks will retrigger or pump the bass. A parallel full-band transient detector can nevertheless provide a short attack impulse so the bass does not feel late.

A practical bass oscillator can use:

\[
y(t)=A(t)\left[
\sin\phi +
a_2\sin(2\phi+\theta_2)+
a_3\sin(3\phi+\theta_3)
\right]
\]

with \(\phi[n]=\phi[n-1]+2\pi(f_0[n]/2)/f_s\). Because frequency is updated continuously, bends and glissandi follow naturally.

A wavetable or single-cycle sampled bass can replace the additive oscillator if you want more personality. A multisample engine is unnecessary unless the product specifically needs realistic bass-guitar articulation.

Pitch-shifting the isolated low-band waveform is worth adding only as a secondary timbre mode. It can retain pick attack and string character, but it cannot undo the fact that the original mono pickup has mixed overlapping partials. The Roland patent itself allows selected bands to be combined and then sent through octave processing, which is a sensible compromise. citeturn158265view1

A useful product architecture is therefore:

**Synth mode:** most stable, lowest contamination, fastest.

**Hybrid mode:** synth fundamental plus pitch-shifted or resynthesised harmonics from selected guitar bands.

**Natural mode:** selected-band waveform octave processing, closest conceptually to the Roland architecture but potentially glitchier.

## Latency and embedded implementation

A modern Cortex-M7 is comfortably powerful enough for the recommended classical pipeline. ST's STM32H750 runs up to 480 MHz with hardware floating-point support and cache. citeturn893641search4 The Daisy Seed platform uses the STM32H750 family and libDaisy processes audio in 32-bit floating point; its normal small-block audio buffering adds around or below a millisecond of software latency. citeturn893641search5

The relevant limitation is therefore not CPU throughput but **information latency**. You cannot robustly identify an 82 Hz fundamental from an arbitrarily short waveform because its period is about 12 ms; the problem gets worse below standard tuning. The important trick is to make a provisional musical decision from harmonics and filterbank energy before a full fundamental period has elapsed, then refine the state without producing audible discontinuities. The guitar-effects literature makes exactly this latency-versus-lowest-frequency trade-off explicit. citeturn336208view0

Reasonable engineering targets are:

| Stage | Practical target |
|---|---:|
| ADC + audio block | 0.2–1 ms |
| filterbank-energy reaction | 2–5 ms |
| provisional lowest-note decision | 5–10 ms |
| high-confidence standard-tuning decision | 10–18 ms |
| very low/drop-tuned note confirmation | 15–25 ms |
| oscillator generation | effectively immediate after estimate |
| total perceptual bass onset | approximately 7–15 ms typical |

Those numbers are design estimates rather than published OC-5 measurements. The lower end is achievable only if you allow provisional decisions from partials and onset context; a strict period-based detector naturally trends towards one or more periods of latency. The historical DAFx comparison measured 13.4 ms for unsmoothed YIN in its own test configuration and approximately 23 ms for some competing approaches. citeturn336208view0

Memory use is negligible for the classical system. Forty-eight biquads, envelope states and candidate scores amount to kilobytes rather than megabytes. Even a short 1024–2048-sample spectral analysis buffer is trivial on STM32H7-class hardware. Daisy's hardware and library ecosystem therefore make it a good prototype platform. citeturn893641search4turn893641search5

A neural prototype belongs on desktop first. Basic Pitch is open source and polyphonic, so it is an excellent accuracy reference. citeturn408689search0 The much newer JIT-MPE is also open source and explicitly streaming, with about 86k parameters and approximately 2.4 ms compute per 20 ms frame on the CPU used by its authors. citeturn488079search0 That is useful evidence that lightweight causal neural MPE is now computationally plausible, but its 20 ms frame granularity and benefit from later revisions mean it is not obviously superior to purpose-built DSP for this effect.

The neural path becomes interesting if you train specifically on **DI electric guitar**, use only the lowest-note output, and distil the model into perhaps 10–30k parameters. That removes a great deal of unnecessary general-MPE capacity. I would not commit to that until the classical baseline has been measured because the Roland-style method may already be sufficient.

## Test protocol and development roadmap

The benchmark must be musical, not merely frame-wise pitch accuracy. A system can have excellent average pitch F1 and still feel unusable because of a 30 ms bass-note takeover error during chord changes.

Create a DI corpus with isolated string recordings and scripted chord performances, and retain string-level ground truth wherever possible. GuitarSet is an established guitar dataset intended for transcription research and provides a useful external benchmark, although for pedal development you should make a dedicated DI dataset matching your pickup and playing conditions. citeturn660934search9 Include several single coils, humbuckers, bridge/neck positions and pickup combinations because electric-guitar spectra materially depend on pickup placement and spectral comb filtering. citeturn590255search9turn590255search4

The adversarial test set should contain: open-position chords; close intervals on adjacent strings; inversions; deliberately weak bass strings beneath hard-struck upper notes; low string struck first then high strings added; high chord followed by a late low note; ringing previous bass note through a chord change; palm-muted bass notes; harmonics; slides; whole-tone bends; vibrato; hammer-ons/pull-offs; drop D/C/B/A ranges; detuned strings; dead strings; neck and bridge pickups; humbucker and single coil; dual-pickup combinations; pick and finger attacks; compression; strong EQ; and progressively increasing overdrive/distortion.

The most important metrics are:

- **lowest-note frame accuracy**, with a tolerance such as ±25 or ±50 cents;
- **lowest-note onset acquisition time**, measured from physical/annotated onset to stable correct estimate;
- **wrong-upward-switch rate**, because these errors are especially objectionable in accompaniment;
- **subharmonic/octave-error rate**;
- **false bass rate during silence or upper-string-only playing**;
- **bend tracking error in cents versus time**;
- **dropout duration** during sustained notes;
- **bass-envelope correlation** with the true lowest string;
- **note-change latency** separately for downward and upward transitions;
- **audio glitch count** and peak discontinuity at estimator changes;
- **CPU load, worst-case execution time and RAM**;
- and a blinded musician preference test against OC-5 Lowest mode.

The MPE literature generally evaluates precision, recall and F-measure, but those should be supplemented here with deadline-aware and onset-specific measures because recent real-time work shows that pitch-estimation quality changes sharply during the first tens of milliseconds after note onset. citeturn488079academia10

The first prototype should be desktop C++ or Python and should implement three systems side by side: a Roland-inspired 33–48-band cumulative-energy baseline; that same system plus harmonic-salience confirmation; and a general MPE reference such as Basic Pitch or JIT-MPE. Feed identical DI recordings to all three and measure not only pitch correctness but the actual synthesised bass output. citeturn408689search0turn488079search0

The second prototype should move the hybrid classical algorithm onto Daisy/STM32H7. At that stage I would tune four things before adding sophistication: lowest-band false positives, upward-switch hysteresis, attack latency and bass-envelope behaviour. Only after those are stable would I spend time on partial resynthesis or neural inference.

The central engineering judgement is therefore fairly clear. **Pure low-pass filtering is too crude. Full polyphonic source separation is unnecessarily hard. Full six-note transcription is more capability than the effect needs.** The best starting point is an adaptive log-frequency filterbank that finds the lowest active region, strengthened by harmonic-salience evidence and temporal state, followed by a bass synthesiser. That architecture is computationally light, causal, explainable, resistant to overlapping chord harmonics, naturally supports bends, and is closely aligned with the strongest public technical evidence we have about how Roland approached the same musical problem. citeturn158265view0turn483661view2turn435704view0
