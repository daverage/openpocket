# What's new in OpenPocket v50: a playable BRASS SYNTH

## BRASS SYNTH

BRASS now follows one played note and drives a band-limited saw/pulse oscillator through an envelope-opened resonant
filter. ATTACK, RELEASE, BRIGHT and BITE shape the voice; DRY adds the guitar. It no longer compresses and clips the
guitar waveform to create its main tone. It is monophonic, like the other tracked instrument voices.

The former TRUMPET MODEL is removed as an independent voice. Its saved-preset type slot is retained for compatibility
and aliases the sampled TRUMPET. CELLO is now named BOWED CELLO, and CELLO SECTION is CELLO ENSEMBLE.

## Verification status

The engine source passed a host C syntax check. A firmware package was not built because Docker is unavailable in this
environment. The new BRASS voice and adaptive gate still need pedal listening and DSP-load checks before release.
