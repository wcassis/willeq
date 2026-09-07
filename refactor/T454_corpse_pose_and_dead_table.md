# T454 — player corpses lie down (B473); the equipment_textures table nobody reads (B474)

Class A/B, slot2, no server. Read B473 (T431: the official client lays a
player corpse flat; fable spawns it as an upright standing entity —
captures `review/T431/probe/T431_sit_dwarf_sitting_h32.png` vs
`review/T431/sweeps/human_eye/eyeHeightFractionOfSkull_0.65_/BEST_frame_000051.png`),
T346's §Review (the death sequence matched to the oracle: which
animation the official client plays on death and what the last frame
is), the corpse spawn path (a corpse arrives as a spawn with the corpse
flag / `NPC type` — read `ZoneProcessNewSpawn` and how `isCorpse` is
carried), the animation code tables (`d05` death animations per race;
`getSkillAnimationCode`), `AnimatedMeshSceneNode`'s frozen-frame
support if any; and B474 (T452: `equipment_textures`, 2,623 rows, no
reader), T450's db recipe.

1. B473: a corpse entity (player or NPC) renders in the death pose's
   final frame from spawn — the same pose the death sequence (T346)
   ends in, held. Read what the official client does for an NPC corpse
   too (T346's captures) and treat both the same unless the captures
   differ (say which). Pre-fix failing test: a corpse spawn packet →
   the entity's animation state is the held death frame, not idle.
   Headless render proof if the panel/scene capture rig allows
   (`review/T454/`); the live A/B is OWED to the lock (add to the T449
   follow-up list in the Review).
2. B474: delete the `equipment_textures` writer, the table, and the
   docs naming it; rebuild and re-pin the db under T450's recipe (one
   commit: db + sha256; row diff = the table only). `DbReproducible`
   green. Check `metadata.total_entries` / any count that included it.
3. Doc nets green; CLAUDE.md's table list re-measured.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
visual gate standalone + ARM (no corpse is in a gate frame — if an image
golden moves, STOP with the sheet), file sizes. Strike B473/B474; new
items B476+ (T449 allocates from B474+ — collisions renumbered at merge;
say which you used).

STOP: an image golden moves; the death pose cannot be identified from
T346's evidence (then board the capture and do B474 only).
