# T357 — Tab completion reached (B057), dead spell-land hook deleted (B061), EQE1 preload gate (B014)

Class B, slot1. Three disjoint items; one branch, three commits.

## Leg A — B057 tab completion

`CommandProcessor::getCompletions` and `CommandRegistry::getCompletions` are
both implemented and tested and have ZERO production callers. Will's rule is
functionality first, and both CLAUDE.md files advertise tab completion, so
WIRE it on the path a user reaches:

1. Find the in-game chat input box's key handling (the Irrlicht UI edit box
   the player types `/commands` into) and record which registry its SUBMIT
   path dispatches through — that is the registry to complete from. If the
   two `getCompletions` are duplicated-then-drifted, say so on the board as a
   new item; do NOT merge them here.
2. On Tab while the box has focus and the text begins with `/`: one match →
   replace the partial command with it (keep the trailing space so the user
   can type arguments); several → print the candidates on one chat line (the
   same output channel `/help` uses); none → nothing. Tab must not move
   Irrlicht focus off the box (consume the event).
3. Test through the same seam the static-panel UI tests use (T54–T56 shape):
   an edit box with text `/gu`, a synthetic Tab key event, assert the text
   afterwards. Pre-fix failing.
4. Update both CLAUDE.md sentences to say where it lives; strike B057.

## Leg B — B061 `SpellManager::setOnSpellLand`

Zero setters tree-wide; the invocation at `spell_manager.cpp:1027` fires on
the animation packet, outside the `is_success_packet` gate — a wrong hook
that T348 did not use (landing is OP_Action effect_flag 4). DELETE the
typedef, setter, member and invocation, and any test that only exercises
the setter. K12 ff check first (no `pushEvent` in the removed lines). Strike
B061.

## Leg C — B014

`RaceModelLoader::preloadModelData` calls
`ConstrainedTextureCache::getOrLoad()` with an empty buffer whenever a JSON
manifest exists but no EQE1 archive is configured — one guaranteed-to-fail
call per composite name. Gate the call on EQE1 being configured (find the
existing "is EQE1 configured" predicate; do not invent a second one). Test:
manifest present, no archive → `getOrLoad` never called (count via the
cache's existing stats/seam, or a source-reader pin if none). Strike B014.
NOTE: `mesh_building.cpp` and `race_model_loader.cpp` were just touched by
T352 (a lock in `getMergedTextures`); you branch from a tip that includes it.

## Proof

Build, `ctest -j6`, the new tests named with pre-fix/post-fix runs, file
size check; Leg A and Leg C touch `src/client/graphics` → visual gate
standalone + ARM build.

STOP: the chat box has no reachable key hook without restructuring the
input bridge (then report the shape and do Legs B and C).
