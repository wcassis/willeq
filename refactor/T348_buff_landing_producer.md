# T348 — The buff-landing producer (B055 reopened) and the racial vision floor (B056)

Class A, both measured live by T345. Slot2.

## B055 — measured cause

`ZoneProcessBuff` calls `setPlayerBuff()` (the only production caller
that fires `BuffManager`'s apply hook) only on an `OP_Buff` with
`fade != 1`; eqemu never sends that shape (server-side the only
`bufffade = 0` sender is gated on `zone->BuffTimersSuspended()`). The
route that DOES deliver a landed self-buff on this server is the one
T345 saw: `#castspell` produced two `SpellAction` packets and the buff
appears in the next zone-in PlayerProfile → `setPlayerBuffs()` (plural),
which does not fire the callback.

Measure first, from T345's logs (`review/T345/`) and a packet read of
`src/client/player/player_buffs.cpp` / `spell_manager.cpp`: on a
self-cast that lands, which packets arrive in order (`OP_Action`,
`OP_Buff` with `fade=1`?, `OP_ManaChange`, …) and which handler updates
the buff list. The producer goes where the buff list is actually
updated on landing: if that is `setPlayerBuffs()` (plural, zone-in and
possibly the post-cast refresh), it fires the apply hook per buff added
(diff old vs new lists); if landing is delivered through `OP_Action`'s
buff-effect arm, that arm calls `setPlayerBuff()`. Match what the
official client would do: it shows the buff icon the moment the cast
lands, so whichever packet carries the landing is the trigger.

Test: replay the T345 cast log's packets (`zonetest::ReplayZone`) and
assert `VisionChanged` publishes Ultravision after the landing packet;
pre-fix failure pasted. T339's tests keep passing.

## B056 — the racial vision floor

`PlayerCameraComponent::setPlayerRace()` logs at INFO on every call and
that line appears in none of T345's five session logs; its only call site
is `IrrlichtRenderer::createEntity()` while the live path builds entities
through `src/client/graphics/zone_load/renderer_zone_load_entities.cpp`.
Measure: is `setPlayerRace` reached on the live path at all (add the call
where the player's own entity is created on the zone-load path if not),
and what the racial floor should be (the race table in `renderer_camera.cpp`).
Test: after a zone-in with a Dark Elf / Human, `getVisionType()` is the
racial floor (Ultravision / Normal) before any buff. Pre-fix failure.

Live proof: owed — a night session after both fixes, `#castspell 46`
with `invulnerable=0` (T345 found an invulnerable account refuses its
own cast), before/after frames; `review/T348/` if the lock is free.

Territory: `src/client/player/player_buffs.cpp`, `src/client/spell/`
(the landing handler), `src/client/graphics/zone_load/renderer_zone_load_entities.cpp`
(one call), `src/client/graphics/player/renderer_camera.cpp` (the race
table only if wrong), tests, `refactor/BOARD.md`. A graphics file
changes: visual gate + ARM NOT skippable.

STOP: the landing packet cannot be identified from the logs (then a
capture unit with the official client comes first — say so); a
state-module change beyond a publish.

## Review

Branch `task/T348-buff-landing-producer`, cut from `fable` at `5ae2e587`.
Both items closed. No STOP triggered.

### The measured landing sequence

From `review/T345/logs/live_session_vision.log`, a self-cast of Ultravision
(`/say #castspell 46`) against eqemu, in wire order. First cast, nothing up:

    18:20:06.876  Chat submit: '/say #castspell 46'
    18:20:06.942  [SPELL] ManaChange: mana=0, stamina=243, spell=0
    18:20:06.943  [SPELL] SpellAction: caster=287, target=287, spell='Ultravision' (46), effect_flag=0
    18:20:06.946  [SPELL] SpellAction: caster=287, target=287, spell='Ultravision' (46), effect_flag=4

Re-cast at 18:20:35, with the first copy still up — the server fades the old
one BETWEEN the two action packets:

    18:20:35.433  [SPELL] SpellAction: ... effect_flag=0
    18:20:35.433  [SPELL] Buff: entity=287, spell=46,         slot=0, duration=0, fade=1, effect_type=2
    18:20:35.433  [SPELL] Buff: entity=287, spell=4294967295, slot=0, duration=0, fade=1, effect_type=2
    18:20:35.433  [SPELL] SpellAction: ... effect_flag=4

and `#nukebuffs` at 18:21:46 produces the same fade pair and nothing else.

**There is no `OP_Buff` with `fade=0` anywhere in the session.** Every `OP_Buff`
in all five of T345's logs carries `fade=1`. That confirms T345's read of the
server: `MakeBuffFadePacket` is the only `OP_Buff` eqemu sends a Titanium client
on a normal zone, because the only sender of `bufffade = 0`
(`Client::SendBuffDurationPacket`) is gated on `zone->BuffTimersSuspended()`.

So the buff list is NOT updated on landing anywhere at this tip — not by
`setPlayerBuff()` (plural or singular) and not by anything else. The landing is
carried by the SECOND `OP_Action` of the pair, `effect_flag == 4`, and nothing
consumed it. `setPlayerBuffs()` (plural, the PlayerProfile at zone-in) is a
LATER, separate delivery of the same fact, not the landing; a re-log is not what
the official client responds to, which shows the icon the moment the cast lands.
The producer therefore goes on the `OP_Action` arm.

### B055 — the producer

`PlayerComponent::NoteBuffLandedOnPlayer(spell_id, caster_id, caster_level)`,
new in `src/client/player/player_buffs.cpp` (declared in
`include/client/player/player_component.h`), called from
`CommandsComponent::ZoneProcessAction` (`src/client/commands/combat_actions.cpp`)
when `type == 231 && effect_flag == 4 && target == MySpawnId()`.

Three decisions, each stated in the code:

* **`applyBuff()`, not `setPlayerBuff()`.** The Action packet carries no slot, so
  the slot cannot be supplied; `applyBuff()` picks a free one, drops an earlier
  copy of the same spell, derives the effect type from the spell data and fires
  the apply hook. It is existing public `BuffManager` API — **no state-module
  change** was made, which is what the contract STOPped on.
* **Duration is computed, not received.** The Action packet has none either, so
  the client runs `SpellData::calculateDuration(caster_level)` — the same formula
  the server used. Spell 46 is formula 3 (`level * 30`) capped at 360 ticks.
* **Gated on `isBuffSpell()` (beneficial AND with a duration).** `effect_flag == 4`
  is also sent for a RESISTED detrimental spell (resists arrive on a separate
  message packet — see `SpellManager::handleAction`'s own comment), so admitting
  detrimental spells here would post debuffs that never landed. A detrimental
  landing that is never recorded is still safe: its later fade packet's
  `removeBuffBySlot()` is a no-op.

The publish reuses `PublishPlayerBuffUpdated()`, the same body
`ZoneProcessBuff()` and the zone-in PlayerProfile use, reading the slot back out
of `BuffManager` so the bar and the model cannot disagree.

Test: `tests/test_buff_landing_producer.cpp` (7 tests, new target in
`tests/cmake/spells.cmake`). It replays the packets above through a real
`EverQuest` — `MakeEq()`, `InitializeSpellSystem()`, `Test_SetMySpawnIdForSeam(287)`,
the real `ReplayZonePacket` dispatch — and observes the far end of the whole
chain: a recording `GameStateBridge` catching the `VisionChanged` event that
T339's REAL registered hook publishes. Nothing is hand-wired; the assertion is on
what the renderer would have been told.

**Pre-fix failure** (production changes reverted via an exported patch, tests
kept — 4 of 7 passed, which is correct: the three negative tests pass trivially
when nothing lands):

    [ RUN      ] BuffLandingProducer.TheLandingPacketRecordsTheBuffAndFiresTheApplyHook
    tests/test_buff_landing_producer.cpp:183: Failure
    Value of: h.Buffs()->hasPlayerBuff(kUltravision)
      Actual: false
    Expected: true
    tests/test_buff_landing_producer.cpp:184: Failure
    Expected equality of these values:
      h.Vision().visionEvents
        Which is: 0
      1
    the landing must publish VisionChanged
    tests/test_buff_landing_producer.cpp:185: Failure
    Expected equality of these values:
      h.Vision().lastVision
        Which is: '\0'
      2
    Ultravision
    tests/test_buff_landing_producer.cpp:188: Failure
    Expected: (landed) != (nullptr), actual: NULL vs (nullptr)
    [  FAILED  ] BuffLandingProducer.TheLandingPacketRecordsTheBuffAndFiresTheApplyHook
    [  FAILED  ] BuffLandingProducer.ARecastRefreshesRatherThanStacking
    [  FAILED  ] BuffLandingProducer.TwoDifferentVisionBuffsBothLandAndBestWins
     3 FAILED TESTS

Post-fix: 7/7. T339's `test_vision_buff_fade_wire` is untouched and still 11/11.

### B056 — the racial vision floor

**PROVEN, not just a lead: `IrrlichtRenderer::createEntity()` has ZERO
production callers tree-wide.** Grepped every `createEntity` reference in
`src/`, `include/` and `tests/`: the only calls are to `EntityRenderer::createEntity`
(a different method, from `entity_lifecycle.cpp` and three tests). The live zone
load registers every entity through `IrrlichtRenderer::registerEntity()`
(`src/client/application_zone_loading.cpp:242` threaded and `:352` inline) and
post-load spawns arrive on the `EntitySpawned` bridge arm
(`src/client/bridge/irrlicht_bridge.cpp:64`), which also calls `registerEntity()`.
That is why `Player race N -> base vision: X` appears in none of T345's five
logs while `[ZONE-IN] setPlayerSpawnId` — set from the same `isPlayer` arm at
both call sites — appears in all of them.

Fix: one call, in `IrrlichtRenderer::registerEntity()`
(`src/client/graphics/renderer/renderer_entities.cpp`), the same shape
`createEntity()` already had:

    if (result && isPlayer && playerCamera_) {
        playerCamera_->setPlayerRace(raceId);
    }

**Deviation from the contract's territory line, stated:** the contract named
`zone_load/renderer_zone_load_entities.cpp` for this call. That file is step 8 of
the load — it PREPARES and builds meshes for entities already in
`entityRenderer_`'s map; it never creates one and never sees `isPlayer` as a
parameter. `registerEntity()` in `renderer_entities.cpp` (also named in the
contract, at `:29`) is where the player's own entity is actually created on the
live path, and it covers both the zone load and post-load spawns in one place.
`renderer_zone_load_entities.cpp` was not modified.

The race table in `renderer_camera.cpp` was NOT changed — the contract said "only
if wrong", and I have no oracle for it. Recorded as an open question below.

Test: three tests appended to `tests/test_vision_buff_fade_wire.cpp`. The floor
itself is driven at runtime (Dark Elf race 6 -> Ultravision, Human race 1 ->
Normal, and an untold renderer -> Normal, which is why the miss was invisible).
The reachability half has no runtime seam — `IrrlichtRenderer::registerEntity()`
returns early without an `entityRenderer_`, which needs an Irrlicht device — so
it is pinned by reading the source, the same idiom as T339's registration pin and
`test_command_registration_order.cpp`. A second test pins that the two live call
sites still use `registerEntity()` and never `->createEntity(`, so the call moves
if the path ever does.

**Pre-fix failure:**

    [ RUN      ] RacialVisionFloorTest.RegisterEntitySetsTheRaceBecauseCreateEntityIsNeverCalled
    tests/test_vision_buff_fade_wire.cpp:380: Failure
    Expected: (src.find("setPlayerRace", reg)) != (std::string::npos), actual: 18446744073709551615 vs 18446744073709551615
    tests/test_vision_buff_fade_wire.cpp:381: Failure
    Expected: (src.find("setPlayerRace", reg)) < (upd), actual: 18446744073709551615 vs 3704
    B056: the racial vision floor must be set in registerEntity(), the only
    entity-creation entry point the live zone load reaches
    [  FAILED  ] RacialVisionFloorTest.RegisterEntitySetsTheRaceBecauseCreateEntityIsNeverCalled
     1 FAILED TEST

Post-fix: 14/14 in that binary.

### Live proof — OWED, and why

**The eqemu lock was NOT free.** `/mnt/projects/fable/.eqemu-lock` reads
`T350 2026-09-04` (checked after the code was green, and again at commit time).
Per the protocol that means another session owns the server: STOP, never
overwrite. No live session was run and `review/T348/` was not created.

What is owed: a night session (`#zone` to a dark spot or forced 22:00),
`/say #set invulnerable off` first (T345: an invulnerable account refuses its own
`#castspell`) and restored after, `#castspell 46`, before/after frames showing
the zone light change; plus a Dark Elf zone-in showing the racial floor with no
buff up. The rig notes on B055 still apply unchanged.

The two fixes are proven correct end-to-end in a test that drives the real
production chain from the wire packet to the `VisionChanged` event, so what
remains owed is the pixel confirmation, not the mechanism.

### Open question, recorded not decided

`PlayerCameraComponent::setPlayerRace()`'s race table and B056's own board text
disagree: the table puts Troll (9) on Ultravision and Ogre (10) on Infravision;
the board entry says "Dark Elf Ultravision, Dwarf/Troll/Ogre Infravision".
`grep -rni 'ultravision|infravision' docs/` returns nothing, so there is no
recorded oracle either way. Not changed by argument (match the oracle, don't
infer it). Boarded as **B057** (class C — needs Will or a capture).

A second finding, boarded as **B058** (class B): `SpellManager::setOnSpellLand()`
has zero production callers — `m_on_spell_land` is invoked at
`spell_manager.cpp:1027` and nothing ever sets it. It was NOT used as T348's seam,
because that invocation sits OUTSIDE the `is_success_packet` gate and therefore
fires on the animation packet too, which is wrong for anything meaning "landed".

### Verification

| step | result |
| --- | --- |
| `cmake --build build -j24` | **exit 0**. First attempt hit the 600 s tool ceiling (exit 143); protocol recovery applied (6 zero-byte binaries under `build/bin` deleted, `ar t` member counts checked: `libwilleq_core.a` 321, `libEtcLib.a` 15), re-run exit 0 |
| `ctest -j6 --output-on-failure` | **3832 / 3833 passed**; the sole failure is the sanctioned `AudioManagerTest.PreloadCommonSounds` (ALSA) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` | **PASS**, `=== VISUAL GATE: PASS ===`; `git status --porcelain` carries no golden |
| `scripts/build-arm-noble.sh` | **exit 0**. Also hit the 600 s ceiling once; zero-byte sweep removed only CMake ExternalProject stamp files (etc2comp's source tree intact), re-run in background to completion |
| `scripts/refactor/check_file_sizes.sh --all` | **879 ok, 0 warn, 0 FAIL** |

Suite grew by 10 (7 new in `test_buff_landing_producer`, 3 appended to
`test_vision_buff_fade_wire`).

### STOPs

Neither of the contract's two STOPs triggered: the landing packet WAS identifiable
from T345's logs (no capture unit needed), and no state-module change was made —
`BuffManager` is untouched, the producer uses its existing public `applyBuff()`.
The eqemu lock being held is a blocked live proof, not a STOP.
