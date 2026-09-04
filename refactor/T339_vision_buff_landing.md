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

## Review

**The contracted "one callback member + setter" was already there and the
premise is half-refuted.** `BuffManager` has had a `BuffApplyCallback` typedef,
a `setBuffApplyCallback()` setter, an `m_on_buff_apply` member, and FOUR
invocations of it (`applyBuff()` at the end, after `push_back`;
`setPlayerBuff()`; the entity path; the pet path) since long before this unit.
The only thing missing was the REGISTRATION: `EverQuest::InitializeSpellSystem()`
called `setBuffFadeCallback()` and nothing else, so the apply callback was a
live, fired, permanently-null hook. Tree-wide, the only callers of
`setBuffApplyCallback()` were in `tests/test_buff_manager.cpp`. So the change is
smaller than the contract expected: no new member, no new setter, no new
invocation inside `BuffManager`.

### The hook

`src/client/core/everquest.cpp`, `InitializeSpellSystem()`. The fade lambda was
replaced by one shared body, `publishVisionForBuffChange(entity_id, spell_id,
exclude_spell_id)`, and BOTH callbacks are now thin adapters over it:

    m_buff_manager->setBuffApplyCallback([publishVisionForBuffChange](uint16_t e, uint32_t s) {
        publishVisionForBuffChange(e, s, EQ::BuffManager::NO_EXCLUDED_SPELL);
    });
    m_buff_manager->setBuffFadeCallback([publishVisionForBuffChange](uint16_t e, uint32_t s) {
        publishVisionForBuffChange(e, s, s);
    });

The body is the old fade body with the recompute lifted out: player buffs only,
return unless the spell carries `UltraVision` or `InfraVision`, then recompute
and push one absolute `VisionChanged`. The racial base is still the consumer's
floor (`PlayerCameraComponent`, T334) and is never consulted here.

### The recompute

`BuffManager::computeVisionType(uint32_t exclude_spell_id = NO_EXCLUDED_SPELL)`
(`include/client/spell/buff_manager.h`, `src/client/spell/buff_manager.cpp`) —
the old inline loop, moved onto the class it was already reading. It needs
nothing outside `BuffManager` (`m_player_buffs` + `m_spell_db`), so the "the
recompute needs state outside BuffManager" STOP did not trigger. It is a const
query; no `BuffManager` behaviour changed.

**The exclusion is a real fix, not decoration.** Three of the four fade paths
(`checkBuffExpiration()`, `removeBuff()`, `removeBuffBySlot()`) fire the fade
callback BEFORE erasing the buff; only `removePlayerBuffBySlot()` erases first.
The old inline fade loop walked `getPlayerBuffs()` unconditionally, so on those
three paths it counted the DEPARTING buff as still up and republished the value
that was already showing. T334's tests never caught it because they fed the
bridge hand-built events and never ran the publisher. Excluding the fading spell
is correct on all four paths (on the fourth it is already gone, so a no-op).

### Precedence decision: BEST VISION WINS, and it is ours

`grep -rni "ultravision\|infravision" docs/` returns NOTHING, so the official
client's precedence is not recorded and could not be matched. The stated rule is
therefore **Ultravision (2) > Infravision (1) > no vision buff (0)**, chosen
because it is what the pre-existing fade loop already did (short-circuit on the
first Ultravision, remember Infravision otherwise) — keeping it means a landing
and a fade cannot disagree. Recorded in the header comment on
`computeVisionType()`. If Will ever wants the real client's rule it is an
A/B measurement against the oracle, not an argument.

### Pre-fix failure

The behavioural half of the proof cannot fail pre-fix: `EverQuest` is not
constructible in a unit test, so the harness installs the callbacks itself and
would pass against either registration. The registration — which is the whole of
B055 — is pinned by reading the source, the `test_command_registration_order.cpp`
idiom. Run with `src/client/core/everquest.cpp` reverted to `03239def` (the
`buff_manager` half kept so it still compiles):

    [ RUN      ] VisionBuffLandingWireTest.InitializeSpellSystemRegistersBothHooksOnOneRecompute
    tests/test_vision_buff_fade_wire.cpp:299: Failure
    Expected: (src.find("setBuffApplyCallback")) != (std::string::npos), actual: 18446744073709551615 vs 18446744073709551615
    B055: a vision buff LANDING publishes nothing without this hook

    tests/test_vision_buff_fade_wire.cpp:306: Failure
    Expected: (first) != (std::string::npos), actual: 18446744073709551615 vs 18446744073709551615

    [  FAILED  ] VisionBuffLandingWireTest.InitializeSpellSystemRegistersBothHooksOnOneRecompute

It passes after, and asserts three things: both setters are called; the shared
body is named at least four times (definition, two captures, two call sites);
and `->computeVisionType(` appears EXACTLY ONCE in the file, so a future edit
cannot quietly give the two hooks two different recomputes.

### Test

`tests/test_vision_buff_fade_wire.cpp` gains 8 tests (3 → 11). Seven are
behavioural, driving a real `BuffManager` over the real `data/willeq.db`
(spell 46 Ultravision, effect 66; spell 276 Serpent Sight, effect 65) through a
real `IrrlichtBridge` into the never-`Init()`'d `IrrlichtRenderer` the T334
tests already use: Ultravision landing raises a Human to Ultravision; an
Infravision buff landing on top does NOT downgrade him; fading the better buff
falls back to Infravision and then to Normal; a Dwarf never falls below his own
Infravision; a non-vision buff (spell 3, Summon Corpse) publishes nothing; plus
`computeVisionType()`'s ranking and exclusion on their own, and its
no-spell-DB-returns-0 guard. The file now owns its `main()` (it opens the spell
DB once, `test_buff_manager.cpp` precedent), so `tests/cmake/renderer_guards.cmake`
links `GTest::gtest` instead of `GTest::gtest_main` and passes `WILLEQ_DB_PATH`.
DB-gated tests `GTEST_SKIP` if `willeq.db` is absent; the three T334 bridge-arm
tests need no DB and run regardless.

The reachability sweep is untouched and passes — a new PUBLISHER of an existing
event is invisible to it (it counts consumers), exactly as the contract said.

### Steps

| step | result |
| --- | --- |
| `cmake --build build -j24` | exit 0 |
| `ctest -j6 --output-on-failure` | **3795 / 3796**, 17 skipped. Only failure is the sanctioned `AudioManagerTest.PreloadCommonSounds`. BuffManager 34/34, VisionBuff 11/11, PlayerCameraComponentTest 9/9, Reachability 14/14, SpellManager 93/93 — all with assertions untouched. |
| `scripts/refactor/check_file_sizes.sh --all` | `873 ok, 0 warn, 0 fail` |
| gles2 / visual gate / ARM | **SKIPPED, no graphics files.** `git diff --name-only` = `include/client/spell/buff_manager.h`, `src/client/core/everquest.cpp`, `src/client/spell/buff_manager.cpp`, `tests/cmake/renderer_guards.cmake`, `tests/test_vision_buff_fade_wire.cpp`. |

Live check: not run. slot1 holds the eqemu lock, and the path is pinned from the
buff list to the renderer's `VisionType`. A live capture of the zone-light change
on casting Ultravision is the one thing still owed if Will wants it.

### STOPs

Neither contract STOP triggered. The fade hook's signature was mirrored with no
caller change (the apply callback already had the identical
`void(uint16_t, uint32_t)` shape), and the recompute reads only `BuffManager`'s
own state.
