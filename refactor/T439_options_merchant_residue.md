# T439 — T434/T436 residue: absolute volume (B432), the zone-in snapshot (B444), Options popup enumerator (B434)

Class B, slot1, no server. Read B432, B444, B434 on the board; T434's
and T436's §Reviews; `AudioManager`'s volume API; `FrameComponent`'s
`optionsApplied*Step_` records; `src/client/core/everquest.cpp`'s
zone-in inventory snapshot and T436's slot-30 / bank 2000..2007
consumers; the `PopupType` enum and its census tests.

1. B432: the volume intents carry an ABSOLUTE step (0..20, the oracle's
   unit) and the audio manager publishes the applied absolute volume
   back (one event both `/music`-style commands and the Options sliders
   consume), deleting the renderer-side shadow records. Pre-fix failing
   test: a `/music` delta after a slider move leaves the slider showing
   the audio manager's real value.
2. B444: the zone-in snapshot publishes the cursor slot and the bank
   slots too (the consumers exist since T436); pre-fix failing test on
   the snapshot with a cursor item and a bank item.
3. B434: delete the unused `PopupType::Options` enumerator (T434 chose
   an independent window per the oracle's `RelativePosition false`) and
   fix every census/doc that lists it. Stale doc = defect.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
G05 in place if a pass changes, visual gate standalone + ARM (no image
golden should move), file sizes. Strike B432/B444/B434; new items B445+.
Live proof of the sliders/music is OWED to a lock unit — say so.

STOP: an image golden moves.
