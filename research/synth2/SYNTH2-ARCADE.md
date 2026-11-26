# Synth2 stage 4: ARCADE (Felucca LOFI)

Status: source inspected, host tested, target compiled and linked. **Not run on a pedal, not listened to.** The Arcade sound is the one you like: whether the character survives the guitar is for your ears.

SYNTH type 29, `ARCADE`: Felucca's LOFI chip voice (pulse, triangle, saw, noise and 4-bit wave RAM, bit-depth reduction, sample hold, pitch sweep, built-in arpeggios, stepped chip envelope) through the same adapter and the same mono tracker as ANALOG
(docs/research/synth2/SYNTH2-ANALOG.md). One note at a time; no Duo, no second tracker.

| Setting | Meaning |
|---|---|
| PATCH | ARCADE, CHIP, 8BIT BS, WAVE, STEP, ARP, ZAP, PLUCK, NOISE, MIN ARP (bands of 10, as ANALOG) |
| CHARACTER | the patch's DUTY +-63: pulse width, or which of the 16 wave tables for the WAVE patch |
| SWEEP | the pitch sweep down on each note (ZAP has one by default) +-63 |
| DECAY | on the stepped patches (STEP, PLUCK) the staircase's divider; elsewhere the envelope's decay and release x1/4..x4 |
| MIX | 0 guitar only, 50 both, 100 synth only |

Verified on the host: upstream's golden renders for the vendored LOFI engine (58 with ANALOG's, identical); pitch within 25 cents on plucked A2, E3, E4 (a chip's pitch moves in 1/16-semitone steps, a few cents: it is the engine's own resolution);
the ARP patch steps through root, major third and fifth; a stepped patch ends by itself; MIX 0 is the guitar; 810 patch/macro combinations are finite and bounded (peak 0.99); silence in gives silence out; output independent of the call size.
Levels on the loops are within about -15..-19 dBFS against the guitar's -15 (trims set on the host; to be set by ear).

Resources (stage 4 against stage 3): openpocket.bin 211 072 -> 214 368 B; RAM-code window used 59 804 -> 60 964 B, 25 040 B free; low RAM 12 036 -> 12 044 B. Host CPU: about 1.5 us per 64 frames, the same as ANALOG (tracker-dominated). Pedal CPU not measured.

Not done: the original Arcade Poly's two-voice behaviour is gone on purpose. Felucca's LOFI quantises pitch to its own steps and has no bend smoothing of its own: slow slides step by 6 cents.
