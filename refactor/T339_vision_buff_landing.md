# T339 — A vision buff LANDING publishes nothing (B055): add the apply-side hook

Class A, found by T334. Slot3. The fade side works (T334 fixed the
consumer): `EverQuest::InitializeSpellSystem` registers a `BuffManager`
FADE callback that recomputes vision from the remaining buffs and
publishes `VisionChanged`. `BuffManager::applyBuff()` has no apply-side
callback, so casting Ultravision/Infravision never reaches the renderer.

## Fix

Mirror the fade hook: a `BuffManager` apply-side callback (`setOnBuffApplied`
or whatever the fade one is named, same signature shape), registered in
the same place with the same recompute-and-publish body (factor the
recompute into one function called from both hooks, so vision is always
"the best of the active buffs, else racial base"). This IS a state-module
change (the `BuffManager` API gains one callback) — T334 stopped on it
by contract; this unit is the contract for it. Keep the change to: one
callback member + setter on `BuffManager`, one invocation at the end of
`applyBuff()` (after the buff is in the list), the registration, the
shared recompute.

## Proof

`tests/test_vision_buff_fade_wire.cpp` gains the landing cases: apply
Ultravision → the renderer-side vision (through the bridge consumer T334
fixed) is Ultravision; apply Infravision on top → Ultravision still
(best wins — read how the recompute ranks them; match the official
client's precedence if `docs/` records it, else best-vision-wins and say
so); fade Ultravision → Infravision; fade both → base. Pre-fix failing
run pasted. `test_buff_*`, `test_spell_*` and the reachability sweep pass
with assertions untouched (a new publisher of an existing event is fine;
the sweep counts consumers).

Territory: `include/client/spell/buff_manager.h`, `src/client/spell/buff_manager.cpp`,
`src/client/core/everquest.cpp` (`InitializeSpellSystem`), the test,
`refactor/BOARD.md`. No graphics files: gate + ARM skipped with evidence.

STOP: the fade hook's signature cannot be mirrored without changing its
callers; the recompute needs state outside `BuffManager`.
