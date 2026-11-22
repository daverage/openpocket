# POLY SHIFTER (PITCH block, v75)

A pitch shifter for chords and any interval, on a **phase vocoder**. SHIFT −12..+12 semitones, FINE ±50 cents, MIX, ATTACK. Source: `engine/px_pv.h`; tests: `tests/engine/pv_test.c`.

## How it works

The method of J. Laroche and M. Dolson, "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects" (WASPAA 1999), written from that paper and the textbook phase vocoder (not from anyone's code):

1. every 256 samples a 1024-sample frame (Hann window) is analysed by one real FFT;
2. the **peaks** of the spectrum (the partials) are found, each with the bins around it as its region of influence;
3. each region is moved, whole, to the shifted frequency (the bins keep the window's shape around the partial), and rotated by the phase the shifted sinusoid needs to carry on from the previous frame (the partial's own frequency comes from the phase advance between frames, and it is tracked from frame to frame);
4. one inverse FFT, a window, and overlap-add.

Nothing is resampled and no note is tracked, so a chord's notes are shifted together, each one clean (the delay-line shifters, WHAMMY and HARMONIZER, give a chord a beat). A **transient** (the frame's energy jumps) drops the phase memory, so a pick keeps its attack; ATTACK is how readily it does (0: never, 100: on a rise of 1.5x).

## What to expect

* **Latency 17.4 ms** (768 samples: frame 1024, hop 256). The wet voice and the dry signal are both delayed by it, so MIX blends them without a comb filter. The delay-line shifters answer faster (the OCTAVE and the HQ octaves are faster still): this one is for harmonies, not for fast single-note lines.
* **Resolution is the limit.** A 1024-point frame has bins of 43 Hz, and a Hann window's main lobe is four bins wide: two partials closer than about **170 Hz** merge into one peak and are moved with one phase. Above that (an upper chord, a lead with its harmonics, a 5th or an octave of single notes) the voices come out clean; a low chord (an open E or A chord's low strings, partials 40..80 Hz apart) blurs, and the lowest notes can sound smeared. The test (`pv_test.c`) uses partials 200 Hz apart and checks each one lands within 2 % of the target and nothing is left at the original; the blur is a property of the frame, not a bug. A longer frame (2048) would resolve more, at twice the cost and latency and 50 KB of RAM; it is not in this build.
* **Level**: the bins move whole, so the shifted frequency sits up to half a bin off their shape; the level of a tone changes by up to about 2 dB with the interval. `engine/px_trim.h` matches the block to the guitar at noon.
* **Cost**: about 1.3 ms per second of audio on the host (`tools/research/level_audit.c`); **26 % of an audio block on a BlackBox (973), peaking at 61 % on the blocks that compute a frame** (a frame every second block), no late block in a few seconds of running. RAM: about 29 KB, part of the engine's pool. (Not yet on the FFT block: that could cut it.)
* A SHIFT of 0 with FINE 0 still runs the vocoder (it is near, not exactly, the input: correlation 0.97 on a chord); for no shift, switch the block off.

## Compared with a time-domain shifter

WHAMMY / HARMONIZER (two crossfading delay-line taps) are faster and cheaper (about 0.4 ms/s) and sound right on a single note; on a chord they add a flutter. POLY OCTAVE HQ (the ERB band bank) is the choice for octaves: no analysis window, no latency from one. POLY SHIFTER is the one for other intervals on more than one note.
