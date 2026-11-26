# ENHANCE research: what the NE570 can do, how real enhancers work, and a design that is audible and usable

Status: research and an offline prototype. Nothing here is firmware. Written after two pedal tests (builds 924 and 926) in which the effect was inaudible. Sources are listed at the end; claims I could not verify are marked.

## 1. The research package against the NE570 datasheet (onsemi ne570-d.pdf, read in full)
| claim in the package | datasheet | verdict |
|---|---|---|
| a 1 mV offset gives 0.34 % second harmonic at 0 dBm | "At a nominal operating level of 0 dBm, a 1.0 mV offset will yield 0.34 % of second harmonic distortion" (Figure 13, % THD against input level and offset voltage) | confirmed |
| gain-cell saturation | "Saturation of the gain cell occurs at a +8.0 dBm level" | confirmed |
| the distortion is a parabola from transistor mismatch | "If the transistors are not perfectly matched, a parabolic non-linearity is generated"; odd harmonics are cancelled by design, "the only distortion which remains is even harmonics ... because of internal offset voltages" | confirmed |
| rectifier ripple modulates the gain and gives third harmonic | "Any ripple on the gain control signal will modulate the signal passing through the variable gain cell. In an expander or compressor application, this would lead to third harmonic distortion" | confirmed, and note its context: a compander's **gain control** |
| rectifier time constant 10 kOhm x C | the step-response equation carries the 10 k term | consistent (not worked through) |
| the effect can be trimmed | "THD trim" pin; untrimmed gain-cell distortion typically 0.3 %, at most 1.0 %; trimmed 0.05 % | not in the package, important |
| a 5 mV equivalent offset | not a datasheet value: the package's choice. Typical untrimmed 0.3 % at 0 dBm is about 1 mV | **5x the typical chip** |
What this means: the NE570's own distortion is small by design. Even a bad chip (5 mV) at its saturation level (+8 dBm) gives only a few per cent second harmonic. Our x1 is already five times a typical chip; x6 is thirty times, far outside anything the chip does. **Whatever makes the real Better Box audible, it cannot be the gain cell's parabolic distortion alone.**
The package also made one deliberate choice with large consequences: it subtracts the slow envelope from the rectifier "to avoid creating an uncompensated expander". That removed the NE570's actual function, a **dynamic gain stage** (compressor/expander). The manufacturer's own claims for the pedal, as quoted in reviews, are "a more even frequency response and a punchier, tighter pick attack" and "brings out the high end without behaving like an EQ": those are dynamics and tone statements, not statements about a few per cent of second harmonic. That the pedal uses an NE570 at all rests on the product description only; I found no teardown or schematic.

## 2. Why the current ENHANCE cannot be heard
Measured on the owner's guitar (single notes, "off" take of `enhances2.wav`): the guitar's own second harmonic is **-19 dB re its fundamental**. The flashed build (x6, voicing C, HIGH, BETTER 100) adds a second harmonic at -31 dB: the second harmonic rises **+1.9 dB**. Turning the strength up does not help: x12 +3.0, x24 +3.1 dB, because the gain cell's input saturates. The effect adds a few per cent of distortion **in the 100-1000 Hz region, where the guitar already has plenty of energy**, so the change is small compared with what is already there, and it is far below what a listener perceives as a different tone.

## 3. How the well-known enhancers work (primary sources where I could get them)
| device | mechanism | numbers |
|---|---|---|
| **Aphex Aural Exciter** (US 4,150,253) | the input is split: the main path is untouched; a side path goes through a **high-pass** (2-pole Butterworth, 4 kHz in the solid-state version; 1 kHz or 4 kHz variants), a variable-gain stage, then a **diode soft-clip on one side only, set so only transients clip**, which makes low-order odd and even harmonics; a tube version adds a frequency-dependent phase shift (about 360 degrees from 100 Hz to 22 kHz, zero near 2 kHz). The harmonic level follows the high-pass output level. | the exciter path is **attenuated to 20-70 % of the main path** (-14 to -3 dB), but only in the band above the corner, where the source has little energy; the patent says it cannot be said with certainty which element does what |
| **Sound On Sound: how enhancers work** | Aphex: a side-chain high-pass into a harmonics generator, processed dynamically, a small amount mixed back; "Tune" sets the frequency above which harmonics are generated; sounds "minuscule" and subtle; harsh if overdone; best on material that lacks highs | no dB figures given |
| **BBE Sonic Maximizer** | adds **no harmonics**: three bands with different delays (below 150 Hz about 2.5 ms, 150-1200 Hz about 0.5 ms, above 1200 Hz none) and **dynamic level control** above 1.2 kHz (compress or expand); Lo-Contour -12 to +10 dB below 200 Hz; little or no boost on low-level signals. An independent measurement of the related 282iR: the HF boost depends on the Process knob **and the RMS level of the mid band**; group delay about 2.5 ms LF, 0.5 ms MF | |
| **SPL Vitalizer** | a side-chain whose frequency response is shaped additively and subtractively (low-frequency EQ that cancels lower-mid energy, lifting bass without mud); a Harmonics section pulling out transient detail; fourth-order filters create harmonics as a by-product | |
| **Dolby Spectral Enhancer** | a bank of complex input-dependent filters, heavily compressed, added back | |
| **Dynamic EQ** | no deliberate distortion; transients raised; less harsh at high amounts, cannot synthesise missing highs | |
| **Xotic EP Booster** (Echoplex EP-3 preamp) | not an enhancer circuit: a discrete FET gain stage of about 20 dB with a gentle high-frequency roll-off and selectable EQ; the character is gain plus coloration; "at low settings just a little harmonic edge" | secondary sources (reviews, retailers) only |
| **Transient-discriminate harmonics generator** (US 5,424,488) | more harmonics on transients, fewer on steady signals; a full-wave rectifier is named as the simple harmonic generator | |
What they share: (1) **the harmonics are generated and added where the source is weak** (high band), not on top of what is already strong; (2) the amount **depends on level and on transients**; (3) the effect is **tonal and dynamic** as much as harmonic (phase, delay, dynamic EQ); (4) every source says: use in moderation, it is subtle; the quoted amounts are 20-70 % of the main path in the processed band.

## 4. What this says about a usable ENHANCE
Our design adds a small amount of energy everywhere the guitar already has energy. A usable enhancer adds a controlled amount of energy **in the regions the guitar lacks**, and does it dynamically:
- **PRESENCE (Aphex-style):** high-pass the side path around 2-3 kHz (4 kHz is for full-range program; a DI guitar has almost nothing above 5 kHz), soft-clip one side with a threshold relative to the running level so only the louder peaks and pick transients generate harmonics, low-pass around 10 kHz, mix in. On the owner's clips the 4.8-9.6 kHz band holds only about -39 dB of the energy, so a small total addition is a large lift in that band.
- **WARMTH (even harmonics, level-independent of saturation):** band-pass 80-400 Hz, **full-wave rectifier** (the simple even-harmonic generator, with no limiter to saturate), remove the DC, low-pass about 1.2 kHz, mix in. Unlike our parabola, its size is set by a mix gain, not by how hard a cell is driven.
- **Dynamics (the NE570's real job):** optionally a gentle envelope-controlled gain on the side paths (more on transients, less on sustain), which is what the manufacturer's "tighter pick attack" suggests and what the BBE's dynamic HF control does.
- **Controls:** keep the original pedal's two (BETTER, LOUDER) and, if wanted, add one (CHARACTER: warm to bright) in the free SFX slot.
- **Cost:** about 6 biquads, a rectifier, an envelope follower and a soft clip per sample: far below the CPU of the synth engines; no buffers. The presence clip creates harmonics above 10 kHz that alias at 44.1 kHz: low-pass before the clip, or oversample the clip 2x.

## 5. Offline prototype (`tests/archive/enhance/enhance_proto.py`), measured on the owner's playing
| setting | added re dry | 2.4-4.8 kHz | 4.8-9.6 kHz | second harmonic of single notes |
|---|---|---|---|---|
| dry | | | | -19.1 dB |
| presence 0.1 / 0.25 / 0.5 | -38 / -30 / -24 dB | +0.5 / +1.2 / +2.3 | +0.8 / +1.8 / +3.4 | -19.1 / -19.2 / -19.3 |
| warmth 0.2 / 0.5 | -22 / -14 dB | 0 | 0 | **-13.8 / -9.0 dB** (+5 / +10 dB) |
| both 0.15 + 0.3 | -18 dB | +0.8 | +1.2 | -11.8 dB (+7 dB) |
| flashed build 926 for comparison | -19 dB (as measured on its own scale) | ~0 | ~0 | -17.2 dB (+1.9 dB) |
The warmth path moves the guitar's second harmonic by 5-10 dB with a plain mix gain and no saturation; the presence path lifts the top band by a few dB. Both scale smoothly. Whether that **sounds** like "presence and warmth without distorting" is the open question: it needs ears. Level-matched audition renders of the three DI clips (dry, presence x3, warmth x2, both): `build/enhance4/matched/`.

## 6. Open questions and next steps
1. Audition `build/enhance4/matched/`: which of presence, warmth or both is the effect you want; how much is too much.
2. The presence clip here is a simple one-sided soft clip; the Aphex's character comes from the threshold-on-transients behaviour, so it is worth trying the transient-discriminate variant (more harmonics on attacks, fewer on sustain) once the direction is chosen.
3. Decide whether dynamics (envelope-controlled side-path gain) belong in v1.
4. A real Better Box measurement (DI through a unit, bypassed and engaged) would settle what that particular pedal adds; until then these are inferences from the datasheet, the patents and reviews.
5. Only then port a chosen design to `engine/px_enhance.h` (fixed-point/float biquads are already in the engine), replacing the NE570 parabola.

## Sources
- onsemi NE570 datasheet: https://www.onsemi.com/download/data-sheet/pdf/ne570-d.pdf (read in full; offset, saturation and ripple statements quoted above)
- Aphex exciter patent US 4,150,253: https://patents.google.com/patent/US4150253A (Knoppel; filter, clipper, attenuation and phase figures quoted)
- Sound On Sound, "How enhancers work": https://www.soundonsound.com/techniques/how-enhancers-work (Aphex, BBE, SPL Vitalizer, Dolby, dynamic EQ)
- Mikhail Naganov, BBE 282i Sonic Maximizer measurements: https://mnaganov.github.io/2018/01/bbe-282i-sonic-maximizer-measurements.html (as summarised by the search; not read in full)
- Transient discriminate harmonics generator, US 5,424,488: https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5424488 (as summarised by the search; not read in full)
- Premier Guitar, TWA BB-01 Better Box: https://www.premierguitar.com/videos/pg-plays/twa-bb-01-better-box (manufacturer claims as reported; no schematic)
- Premier Guitar and retailer reviews of the Xotic EP Booster (secondary; no schematic analysis found)
