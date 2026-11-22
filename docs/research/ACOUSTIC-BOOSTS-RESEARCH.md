# Acoustic guitars (piezo): boosts, preamps and an overdrive — research

Question: what do pedals, preamps, EQs, VSTs and DSPs do for a real acoustic with a piezo pickup played through a PA or recorded direct, and what should OpenPocket build in the BST block? Everything below is paraphrased from the sources at the end (manufacturer pages, magazine reviews, forum threads, one ESP article, a patent). Forum numbers are anecdotes; the one calculation marked "mine" is back-of-envelope. Nothing was measured on a piezo. Nothing here is copied from any firmware.

## 1. What the piezo needs

| Problem | What products do about it |
|---|---|
| **Impedance.** A piezo is a voltage source in series with a small capacitor (ESP: at most about 12 nF, often far less; accelerometers 200 pF). With the input resistance it forms a high-pass: f = 1/(2πRC). Mine: 10 MΩ with 1 nF is 16 Hz, with 200 pF 80 Hz. Forum consensus: a 1–10 MΩ buffer first, EQ second. | Every preamp pedal has a high-impedance input. OpenPocket's input is fixed; the engine cannot change it. Nothing to build; a **low cut** is the software end of it. |
| **Quack / harsh, brittle top, boxy mids.** Anecdotes blame clipping inside the pickup's own preamp (hence 18 V preamps) and the pickup's fast, transient-heavy response. | Mid cut and treble roll-off (Tech 21 Acoustic DI: bass ±12 dB, treble ±12 dB, a mid sweeping 170 Hz–3.5 kHz at ±16 dB, mic-model soft limiting); Fender Smolder: high-pass and low-pass "pickup compensation"; TC BodyRez and Boss AD-2 "resonance"; Fishman Aura "image". |
| **Feedback on stage.** | Hand-set notch (Boss AD-2, LR Baggs Para / Venue / Session, Fishman Platinum Pro EQ, Radial PZ), phase invert, high-pass near 80 Hz (Thomann guide), automatic notches that track the frequency (patents; the pedal-size products mostly use a manual one). One patent measures the body's response to predict where it will feed back. |
| **Lacks the air and body of a microphone.** | (a) Fishman Aura: a stored "image" of a microphone's response and phase for each guitar, mixed with the pickup (manuals say too much blend sounds odd; live 2–3 of 10, recording more). It does not suit soundboard pickups. (b) UAD Wood Works, Nembrini Acoustic Voice, ToneDexter: modelled or trained mic response. Reviewers' consensus: never exactly a microphone, good enough in a mix. (c) Boss AD-2 Acoustic Resonance: "analyses the input in real time"; mixed reviews, one buyer says the resonance and notch do little. (d) Logic Match EQ against a mic recording. |
| **Too clean / thin for the PA.** | Compressor, a clean boost (Venue, Platinum Pro EQ), reverb with a damping control (Fender Acoustic Preverb, AD-2 ambience), saturation (LR Baggs Session: an analogue saturation knob that warms the signal; one reviewer says it warms but does not turn a piezo analogue). |

## 2. Overdrive and a piezo

* Straight into a normal overdrive a piezo gives woofy bass and top-end fizz; several sources say to avoid it, others make it work with EQ both sides of the clipper.
* The fixes the dedicated products use are the same: **shape before the clipper** (tame the lows and the piezo's top, Fender's two filters), **keep the dry signal** (the Smolder's Blend, Trickfish and Voodoo Lab "clean" controls, Neural DSP users), **moderate drive** ("just breaking up" with a little blended in), and a **cab / PA voicing** on the way out.
* The brief says an overdrive "especially for acoustics": a clip that adds sustain and bite without losing the guitar's attack, not a rock drive.

## 3. What this firmware has already

| Need | Already in OpenPocket |
|---|---|
| Clean boost | EP BOOST, ENHANCE (BETTER + LOUDER 0..+12 dB), MAXIMIZER (BBE process, which brightens and adds low end) |
| Notch | HUM FILTER (EQ block): a true notch at 50/60 Hz; the same biquad can sweep |
| EQ | 5 BAND, GRAPHIC 5, PARAMETRIC (EQ block), plus the global input high-pass and output EQ |
| Compression, gate | four compressors (incl. SLOW SWELL), smart gate with hush |
| Reverb | HALL, PLATE, SPRING, AMBIENT... (a damped small-room AMBIENT is the Preverb idea) |
| What is missing | one block that does the **piezo-specific shaping** together (low cut + pickup compensation + boxiness cut + feedback notch + boost) with sensible ranges, and an **acoustic-voiced overdrive with a dry blend**. |

## 4. Proposal (my synthesis; nothing built yet)

Two new BST types, both cheap (no Newton solver):

**ACOUSTIC PREAMP** — 5 controls, in order of the signal:
1. LOW CUT: 20–200 Hz high-pass (rumble, body feedback; about 80 Hz default).
2. PICKUP COMP: one knob that blends a gentle 2.5–4 kHz presence dip, a treble roll-off above about 6 kHz and a small de-quack mid notch — the Smolder / BodyRez idea, our own curves.
3. BOX: a mid cut with a swept centre (170 Hz–3.5 kHz, ±12 dB; the SOS review of the Tech 21 gives that range), a bell for scooping the boxiness, or a boost for presence.
4. NOTCH: a narrow notch sweeping 60 Hz–4 kHz (the feedback control; Thomann: most feedback moves with room and gain, so manual only, with an optional ring-out helper later).
5. BOOST: 0..+12 dB, with a soft peak limiter after it so a clean boost cannot clip the PA.
Phase reverse could replace one of the above, or become a second page/preset flag. A body "resonance" (the Aura / AD-2 / BodyRez idea) is a later step: a short decorrelating all-pass plus a fixed EQ complement would be *our own* guess at what those do, and the sources are clear it will not turn a piezo into a microphone. It needs a real piezo recording to tune: ask for a DI of the user's guitar and a mic'd take of the same part (Match-EQ style) before building it.

**ACOUSTIC DRIVE** — controls: DRIVE, BLEND (dry to wet, the key control), PICKUP COMP (the pre-clip high/low shaping), TONE, LEVEL.
* Pre-clip: high-pass about 150 Hz and a top roll-off (the "tame before the clipper" rule), so the clipper sees the part of the signal that sounds good clipped.
* Clipper: a soft, slightly asymmetric tanh-like curve, 2x oversampled (a piezo's top end aliases audibly), with a low maximum gain: from "tape-like warmth" (the Session's saturation) up to a gritty crunch, not a rock drive.
* Post: low-pass and the guitar's own attack through BLEND, plus unity gain at BLEND 50.
* Cost estimate: well under RANGEMASTER (about 12 % of a core on the pedal); the clipper is a polynomial, not a solver.

Order of work, if you agree: (1) ACOUSTIC PREAMP, (2) ACOUSTIC DRIVE, both with a host test (curves, level to unity, no clicks), pedal CPU reading, docs and one or two factory presets; (3) only then the resonance experiment, with a real recording.

## 5. Limits of this research

* Product descriptions are marketing copy; no independent measurements of any of these pedals were found. The Aura's and ToneDexter's actual algorithms were not found (a 2007 review says digital EQ and phase adjustment from a mic-vs-pickup comparison).
* No source gave notch or de-quack frequencies for a piezo: the ranges above (the Tech 21 mid sweep, the 80 Hz high-pass, the 6 kHz roll-off) are starting points to be set by ear.
* The OpenPocket input stage cannot change the pickup's loading; if the sound is thin on the pedal's own input, that is hardware.

## Sources

* Premier Guitar, 9 best acoustic preamps: https://www.premierguitar.com/pro-advice/tools-for-the-task/9-best-acoustic-preamps
* Sound On Sound, Tech 21 SansAmp Acoustic DI review: https://www.soundonsound.com/reviews/tech-21-sansamp-acoustic-di
* Fender Smolder Acoustic Overdrive (page and reviews): https://www.fender.com/effects-pedals/acoustic/smolder-acoustic-overdrive/0234550000.html , https://guitarplayer.com/reviews/fender-smolder-acoustic-overdrive-review
* Overdrive for acoustic guitar (guides): https://www.guitarchalk.com/overdrive-pedal-acoustic-guitar/ , https://delicious-audio.com/how-to-use-overdrive-with-acoustic-guitar/
* Thomann, feedback elimination: https://www.thomannmusic.com/onlineexpert_page_acoustic_guitar_amps_feedback_elimination.html
* Patent on body-response-based feedback prediction: https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11501745
* Fishman Aura manuals and SOS review: https://www.soundonsound.com/reviews/fishman-aura-imaging-pedals , https://www.manualsdir.com/manuals/103200/fishman-aura.html?page=20
* Boss AD-2: https://www.boss.info/UK/products/ad-2/ , https://www.musicradar.com/reviews/boss-ad-2-acoustic-preamp
* UAD Wood Works, Nembrini Acoustic Voice: https://www.uaudio.com/uad-plugins/guitar-bass/sound-machine-wood-works.html , https://mixdownmag.com.au/news/nembrini-audios-acoustic-voice-makes-your-piezo-pickups-sound-real/
* Piezo impedance: Elliott Sound Products project 202 https://sound-au.com/project202.htm ; forum threads https://www.talkbass.com/threads/piezo-bridge-impedance-matching.1666152/ , https://www.tdpri.com/threads/getting-rid-of-the-dreaded-piezo-quack.305502
