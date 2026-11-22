# FastTrack voice transitions

OpenPocket's mono tracker drives one musical note. ENSEMBLE and CHOIR can assign that note to several Felucca slots. A live pick can update an occupied slot, but the adapter has no polyphonic stealing allocator.

| Transition | Compatibility renderer and sample domain |
| --- | --- |
| Active pitch change, bend or vibrato | `px_s2_block` glides MIDI pitch toward the target with a 6 ms time constant. Pitch is updated in Felucca pitch16 plus fine units where supported. Oscillator phase persists through ordinary frequency updates. |
| Repick or picked new pitch | `px_s2_noteon` restarts attack from current adapter envelope for active voices. It saves oscillator phases and selected algorithm state as dictated by the engine mask, excluding slots 2 and 3. Algorithm-specific state may still restart. |
| Envelope restart | `px_s2_voice` combines envelope shape, expression, stack level and accent, then passes Q15 amplitude endpoints to `voice_amp` for a ramp within the block. The envelope also modulates filter cutoff. This preserves gain continuity across many retriggers. |
| FOLLOW | On patches using FOLLOW, a pending pick on an attacking or sustaining voice produces a velocity-scaled accent decaying over about 18 ms. The existing envelope stage continues. Picks in release can restart the voice. |
| DAMP | A surviving release flag switches the adapter envelope to release with at least the configured 40 ms damping rate. An onset in the same control block can clear the pending release and damp flags. |
| Patch and algorithm change | `px_s2_apply` maps patch macros to time, gain and oscillator parameters and deactivates removed stack slots. The chain resets the adapter on certain type changes. A live patch edit is distinct from a confirmed note replacement. Algorithm state cannot in general be reinterpreted safely across engine types. |
| Ensembles | Each slot has its own ramp, pitch offsets and drift. Felucca render calls add integer contributions to the same accumulator. Block output scales the sum by `level * PX_S2_OUT / 32768`. The chain adds dry guitar downstream. |

The experimental overlay intercepts each additive voice call. It renders into reusable zeroed integer scratch, adds the original integer contribution in original order, and accumulates just the floating correction after voice and patch gain in normalized wet synth units. A bypassed comparison build uses the original rendering path. Correction state is per slot and the adjacent slot's contribution has no dependency on it. Slot termination, DAMP and patch changes clear state. The correction uses actual emitted history for back-to-back replacements, with a small flush threshold and a per-voice representable-range guard. There is no final bus limiter.

The sample loop detects and delivers an event before reading the current ring entry. It calls `px_s2_block` after reading entry 31, at sample `d | 31` for event sample `d`. The new block is first heard at `(d | 31) + 1`, a 1–32 sample wait at 44.1 kHz. The new correction is calculated as the replacement's first sample is rendered. It becomes audible with that same ring entry. There is no added lookahead or control interval.

| Event | Articulation contract |
| --- | --- |
| NOTE_ON | Start the admitted note. An already active slot may need output correction. |
| REPICK | The event reaches the adapter regardless of policy. RETRIGGER restarts the envelope from its current level, and FOLLOW permits a continuous envelope and accent. Existing patch FOLLOW is the compatibility default. |
| LEGATO_CHANGE | Set target pitch without a picked envelope restart. |
| PITCH_UPDATE | Set the continuous target, including bends and vibrato. |
| NOTE_OFF | Release every slot for the monophonic note. |
| DAMP | Apply fast release and cancel correction history. A new note remains eligible immediately. |

Legacy pending flags are a compatibility snapshot. For example, DAMP and NOTE_ON in one block can collapse to NOTE_ON, and two picks can collapse to one restart. A separate host-only ordered delivery model consumes events synchronously in sample order and keeps event delivery independent of the adapter snapshot. Multiple events before one control boundary can be delivered to a state machine while still sharing one audible render boundary. Firmware adoption would require an explicit articulation scheduling decision for that case.

Frozen legacy captures keep exact per-sample frequency, velocity and expression. Legacy retriggers lack a same-note semantic label, so the host labels them REPICK when the new frequency is within roughly half a semitone of the preceding frequency. The labels only choose the candidate's repick correction check, while native and disabled overlay renders verify the same legacy audio.
