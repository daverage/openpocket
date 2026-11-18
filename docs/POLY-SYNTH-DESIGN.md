# Chord synth voicing and two-note tracking

## Start with the guitar signal

String Machine can reuse the shaped unshifted signal, swell, and three modulated delay taps from STRINGS. Give the octave and ensemble independent controls. At zero, each optional layer can be skipped. Keep the shaped center signal present so the sound holds together when the notes beat or the ensemble is off. The Solina reference uses three modulated delays for its ensemble colour. Its wet-only blend is a useful audition point alongside the proposed mix. [Arturia Solina V manual, pp. 51–52](https://dl.arturia.net/products/solina-v/manual/solina-v_Manual_2_10_1_EN.pdf)

For Poly Brass, keep the guitar as the excitation. Use the body filter to retain note centres, then bring in bite briefly on attacks. Moderate the drive on the summed chord. As an inference from the waveshaper, the quadratic term applied to two notes produces both difference and sum components. The saturation stage adds further intermodulation as it approaches clipping. Compare gain-matched renders with single notes, power chords, and minor-second and major-third dyads, checking that the bite adds a distinct attack without masking either note. BOSS's own guide is a precedent for a chord synth from standard guitar input. Its guide describes the playing result and controls. [BOSS SY-300 sound creation](https://articles.boss.info/sound-creation-with-the-sy-300-guitar-synthesizer/)

For Drawbar Organ, use ORGAN HQ with 8′ carrying the pitch, 4′ adding definition and 16′ adding weight. Start with subdued 16′ on dense voicings so the low octave leaves room for the chord. This is an OpenPocket preset choice based on the drawbar relationship. [Hammond SK PRO manual, Harmonic Drawbars](https://www.suzuki-music.co.jp/app/uploads/00457-40208_V160_20210519Web.pdf)

## Check candidate quality before tuning the allocator

OpenPocket already implements a form of Klapuri's iterative harmonic salience. It scores fundamentals from partials, selects one, removes the partials it explains, and repeats. In `px_track.h`, candidate frequency and salience are paired in `notes[]` and `sal[]`, sorted by increasing pitch. The first slot therefore denotes the lowest estimate, which can be spurious. The mono output additionally applies period correction and refinement. Evaluate the raw candidates separately from mono and from the allocator. Log which partials support each candidate on misses. Binary removal can discard a useful shared harmonic of the second note. A bounded comparison would retain some overlapping evidence, or score the best few candidate pairs jointly with a penalty for explaining the same peak twice. [Klapuri, ISMIR 2006](https://doi.org/10.5281/zenodo.1416740)

The repo baseline reports 61% precision and 45% recall on GuitarSet chord frames, and 47% lowest-note accuracy. Those are broad chord results. First score a two-note subset separately with one-to-one matching within 50 cents. Report note and frame precision/recall, octave errors, signed second-note acquisition delay, and any dropouts of the held note. Use mono pickup mixes as the input and time-aligned contours or per-string notes as reference. Mute and repick either note in a controlled recording so the event order and note identity are known. Include octave pairs, quiet notes under louder ones, and bent notes. [OpenPocket note tracking](NOTE-TRACKING.md) · [GuitarSet data and known annotation errors](https://zenodo.org/records/3371780)

A fresh host run of the default `track_bench.c` against all 180 GuitarSet comping pickup mixes found **both notes in 15.2% of 580,986 settled dyad frames**. Candidate precision was 49.5%, recall 45.9%, and the lowest candidate matched the bass in 54.8%. Both notes matched in 0.6% of 56,196 octave frames, 15.8% of perfect-fifth frames, and none of 1,619 semitone frames. The tracker also produced at least two candidates in 42.8% of 545,876 frames with one reference pitch. The run scored raw candidates before any allocator. It treated a pitch as active from 30 ms into its annotation until its end, counted distinct reference pitches, and matched within 50 cents without reusing a candidate. These frame-weighted results include dyads occurring within comping passages. Deliberate two-note performances and their attacks still need separate measurement.

## Two voices

At each tracker update, match existing voices to distinct candidates by pitch distance, with a bounded tolerance for drift. With just two slots, compare the two assignments directly. Give a new candidate a short confirmation window. Hold an established note across brief misses and release individually after a longer absence. Keep oscillator phase continuous through release and calculate increment from the accepted frequency. OpenPocket's existing pulse helper advances a quarter-rate phase and scales the phase and step together for a fundamental pulse. Reuse that convention or use a full-rate phase consistently with the saw helper. Try Pulse Poly first. Use the same allocator for Saw Poly and Arcade Poly once the note handling and pedal load are acceptable.

The current onset is global, so it cannot tell which of two held notes has been repicked. A repick estimate should combine the global attack with a local rise in evidence around the matched pitch and its relatively unshared harmonics. Ambiguous attacks should leave both envelopes alone. A unison or an octave pair with dominant shared partials can be truly ambiguous at the single mixed pickup: one onset cannot reliably identify which string changed. Continuous per-note contours and string/fret inference are useful research comparisons, with different complexity and runtime assumptions from this embedded allocator. [FretNet paper](https://arxiv.org/abs/2212.03023)

For bass follow, treat choosing the lowest validated note as a separate evaluation. Hold the previous choice briefly across a dropout and reduce the shifted output when confidence is low. Measure incorrect bass substitutions before comparing the result on the pedal against fixed RANGE. Keep all of these tracker experiments separate from judging the input-derived chord voices, which can be auditioned without pitch estimates.

## First implementation pass

STRING MACHINE uses the STRINGS HQ swell and tone filters. The direct voice supplies the base tone, OCTAVE adds the
+12 layer, and ENSEMBLE crossfades into the three modulated taps. POLY BRASS keeps the audio-derived BRASS path, with
a shorter envelope, lower waveshaping drive, and a separate BITE amount. ORGAN HQ already provides the drawbar patch.
These are starting voicings for a pedal listening pass, especially on close dyads and fast stabs.

To audition the host oscillator prototype, compile `tools/sim/track_bench.c` as described in its header and run
`build/track_bench input.wav notes.csv 1 pulse.f32`. Use a 44.1 kHz mono DI. CSV columns `v1` and `v2` show the assigned pitches, and `pick1` and `pick2` mark voice starts or retriggers. The render is mono 44.1 kHz native float32. The allocator acquires candidates after
five agreeing frames, holds a missing voice for eight frames, and uses a rise in that candidate's salience as
additional evidence for a repick. A synthetic 110/196 Hz pair demonstrated independent arrival and muting, with
brief lingering tracker candidates after the final mute. Real guitar repicks, close dyads, octave pairs, and the
processor's peak block time remain acceptance checks. Saw and arcade variants can share the allocator once it
reliably tracks two notes.

A second synthetic run added independent repicks while both notes decayed. It produced a false second voice on
the first attack, lost an assigned voice around the second onset, and did not reliably retrigger the intended
voice. Keep the allocator on the host. Compare candidate extraction, pitch-specific onset evidence and assignment
separately before trying another oscillator preset.
