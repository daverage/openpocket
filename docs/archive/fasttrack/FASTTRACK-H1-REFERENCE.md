# H1 reference audit

H1 was read at commit `4cff9365806cae90149a59b5f9669b60491347ba`. OpenPocket was inspected at FastTrack tip `56b2eaac7a8b3308de5684ad8e9f242a4d2f496b`, a descendant of checkpoint `4d5461ff73380aea6a619945e056e7019f5ef76e`.

| H1 source | Observed behaviour |
| --- | --- |
| [Event posting, synth.c 92–110](https://github.com/jakehandy/h1/blob/4cff9365806cae90149a59b5f9669b60491347ba/core/synth.c#L92-L110) | Main loop writes packed events into a 64-slot ring with 63 usable entries. A full ring drops the incoming event, including a potential note-off. |
| [Voice allocation, synth.c 178–243](https://github.com/jakehandy/h1/blob/4cff9365806cae90149a59b5f9669b60491347ba/core/synth.c#L178-L243) | Prefer an active gated note on the same part, then an inactive slot, the oldest released slot, and the oldest active slot. Preserve the outgoing contribution across per-instrument state reset. A stored zero skips arming. |
| [Voice state, internal.h 33–59](https://github.com/jakehandy/h1/blob/4cff9365806cae90149a59b5f9669b60491347ba/core/internal.h#L33-L59) | Declick arm flag, envelope endpoints, phases, per-algorithm state, velocity low-pass and pre-pan `last` and `tail` live on the voice. |
| [Mixer, synth.c 1684–1740](https://github.com/jakehandy/h1/blob/4cff9365806cae90149a59b5f9669b60491347ba/core/synth.c#L1684-L1740) | Compute the first new envelope-scaled, velocity-filtered sample and multiply by voice gain before subtraction. Apply the resulting correction before pan. Multiply by 0.985 per sample, zero at block end below 1e-6, and clear state when release finishes. |
| [Architecture](https://github.com/jakehandy/h1/blob/4cff9365806cae90149a59b5f9669b60491347ba/docs/ARCHITECTURE.md) | The interrupt renders a part's voices separately, then sums in voice order. The event queue crosses the UI/audio boundary. OpenPocket's guitar tracker already runs on the audio thread. |

At 44.1 kHz, 0.985 corresponds to an exponential time constant of about 1.5 ms. H1 stores `s * g + tail` at the end of the block, with `tail` already decayed by one step from the value used for the last mixed sample. The prototype stores the value actually emitted. Pan changes can still change the stereo contribution in H1 because the saved value is mono and pre-pan.

H1 is MIT, Copyright (c) 2026 Jake Handy. MIT is compatible with GPLv3 and requires retaining the copyright and permission notice when distributing copies or substantial portions. This experiment independently implements the documented algorithm and imports no H1 source. OpenPocket's vendored engines retain their existing licences. [H1 licence](https://github.com/jakehandy/h1/blob/4cff9365806cae90149a59b5f9669b60491347ba/LICENSE) · [GNU licence compatibility list](https://www.gnu.org/licenses/license-list.html#Expat)
