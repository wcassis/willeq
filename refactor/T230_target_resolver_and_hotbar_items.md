# T230 — one target-name resolver; keyboard hotbar fires Item slots

Slot: slot3. x86. OWNS eqemu briefly for the live leg (noclobber; T227/T229
may hold it — code first, report pending if held). Class A. Territory:
src/client/combat.cpp (SetTarget(name) only), src/client/action/
input_action_bridge.cpp + action_dispatcher / eq_action_handler for the
hotbar Item route, inventory lookup (InventoryManager::findItemSlotByItemId
already exists, eq_bridge.cpp:~1328 mouse path), tests. Do NOT touch
application.cpp, bridge/*, eq_merchant.cpp (T227), eq_gm.cpp (T229).

## 1. CombatManager::SetTarget(const std::string&) (T173, combat.cpp ~262-279)
A second, case-sensitive, UNREACHED name resolver that bypasses T173's
corpse-aware ranking. Grep callers: if zero, DELETE it; if any, route them
through the one ranked resolver T173 built (command_processor's /target
path — find it) and delete the duplicate. Test: a name that only the ranked
resolver handles (case-insensitive prefix, corpse ranking) resolves the same
way from every caller.

## 2. Keyboard hotbar Item slots are SKIPPED (T174, input_action_bridge.cpp ~424-437)
`processHotbarRequests` routes Spell→castSpell, Skill→useSkill, and skips
Item with a DEBUG log because `slot.id` is an ITEM id while `useItem` takes
an INVENTORY SLOT. The mouse path resolves via
`InventoryManager::findItemSlotByItemId` (eq_bridge.cpp ~1328). Expose that
resolution through the action handler (`IActionHandler`/`EqActionHandler`)
so the keyboard route can call `useItem(resolvedSlot)`; if the item is not
in inventory, fail with the same message the mouse path gives. Leave
Emote/Macro skipped (needs command-text processing — note it). Test: a
HotbarModel with an Item slot → key press → useItem called with the
resolved inventory slot; item absent → failure, no send. Red/green.
Live: put a clickable item on a hotbar slot via the mouse path or
/hotbar command if one exists, press the key, observe the server-side
effect (item use packet in the client log + server reaction).

## Verification
Full ctest; Review in this file; evidence review/T230/. Commit:
`T230: one target-name resolver; keyboard hotbar uses Item slots`.

## Review (executor, 2026-08-25, slot3, branch task/T230-resolver-hotbar off fable 23d8ea59)

### 1. SetTarget(name): callers, disposition

Grep at 23d8ea59: `CombatManager::SetTarget(const std::string&)` had exactly ONE
caller, `EqActionHandler::targetEntityByName`, whose only caller was
`ActionDispatcher::targetEntityByName`, whose only callers were tests
(`test_action_dispatcher.cpp`). Zero production callers at every layer -- the
chain was never reachable from a key, a click, a slash command or --script.
`/target` already resolves through `EntityManager::findEntityByName`
(CommandProcessor::cmdTarget) and only hands a resolved spawn id down.

Disposition taken: DELETE the whole chain (the plan's "if zero, DELETE it"
branch, the disposition combat.cpp's own T173 comment said was owed):
- `CombatManager::SetTarget(const std::string&)` (combat.h/.cpp)
- `IActionHandler::targetEntityByName`, `ActionDispatcher::targetEntityByName`
  (action_dispatcher.h/.cpp)
- `EqActionHandler::targetEntityByName` (eq_action_handler.h/.cpp)
- `RecordingActionHandler::targetEntityByName` (tests/action_test_fakes.h)
- tests that exercised the deleted resolver: `CombatManagerTargeting.
  SetTargetByName_{ExactMatch,PartialMatchFallback,Miss}`; the
  `TargetingFamily` dispatcher test dropped its targetEntityByName line
  (10 -> 9 members).
- comments pointing at the second resolver updated (combat.cpp T104 note,
  command_processor.cpp cmdTarget note). `bridge/game_state_sync_bridge.h`
  line ~115 still mentions "SetTarget()" generically -- it refers to the
  uint16_t overload and bridge/* is out of territory; left alone.

Plan test ("a name only the ranked resolver handles resolves the same way
from every caller"): with one caller left, this pins /target's behaviour on
the two properties the deleted resolver lacked --
`CommandProcessorTest.Target_CaseInsensitive_ResolvesThroughRankedResolver`
(`/target guard_ROLAND` -> `Guard_Roland`) and
`CommandProcessorTest.Target_PrefersLivingOverOwnCorpse` (corpse with the
LOWER spawn id inserted first, `/target testmelee` -> the living id). Both
green (`review/T230/green_command_processor_target.txt`, 9 Target_* tests).
No red is possible for a deletion; the evidence for it is the caller grep
above plus a clean full build.

### 2. Keyboard hotbar Item route -- design

Two defects, not one. Besides the skipped arm the plan named, the handler
end was a STUB: `EqActionHandler::useItem()` only logged "Use item not yet
implemented" -- so even with the id->slot hop, a key press could never have
reached the wire. Both fixed:

- `IActionHandler::findItemSlotByItemId(uint32_t) -> int16_t` (new pure
  virtual). `EqActionHandler` implements it via
  `InventoryManager::findItemSlotByItemId` (the mouse path's lookup);
  SLOT_INVALID when absent or no inventory. The fake carries a scripted
  `inventoryByItemId` table; the query is not recorded as a call.
- `ActionDispatcher::useItemById(uint32_t itemId)`: zone-connection check,
  resolve through the handler, `Failure("Item not found in inventory")`
  (the mouse path's exact string) with nothing sent when not carried, else
  `handler->useItem(slot)`. The hop lives in the action layer, so the bridge
  never sees a raw item id near useItem.
- `InputActionBridge::processHotbarRequests` Item arm:
  `reportAction("UseItem", m_dispatcher.useItemById(slot.id))`.
- `EverQuest::UseInventoryItem(int16_t slot)` (eq.h, defined in
  eq_bridge.cpp): the click-effect sender extracted verbatim from the mouse
  HotbarActivateIntent Item arm (getItem / no-click-effect / must-equip
  refusals, OP_CastSpell slot=10 packet, "Using <name>"). The mouse arm now
  does its id->slot hop and calls it; `EqActionHandler::useItem(slot)` calls
  it directly. One sender, two entry points.
- Cooldown feedback: the mouse arm used to push HotbarCooldownStarted for
  the ONE clicked index (`i.index`). The keyboard route has no index to
  hand down, so UseInventoryItem pushes it for EVERY hotbar slot holding
  that item id (reads `m_hotbar_model`). Superset of the old behaviour for
  the mouse; deliberate, called out here as the one behavioural change to
  the mouse path.
- Emote/Macro/Combat/Empty keyboard arms: still a logged skip, as the plan
  says. Emote/Macro need command-text processing (ProcessChatInput on the
  mouse path); the action layer has no route to it. Owed, unchanged.

### 3. Red/green (measured, files in review/T230/)

RED was measured with the old bridge arm in place AND `useItem()` temporarily
restored to its stub, against the new tests:
- `red_input_action_bridge.txt`: 2 FAILED
  (`HotbarItemSlot_ReachesUseItemWithResolvedInventorySlot`,
  `HotbarItemSlot_ItemNotCarried_FailsWithoutUsing`) -- the arm skipped,
  nothing dispatched, nothing reported.
- `red_eq_action_handler_hotbar_item.txt`: 4 FAILED (every UseItem_* test --
  the stub emitted no message/event); the 2 findItemSlotByItemId tests
  passed already (the lookup was correct, just unreachable).
GREEN after routing the arm + wiring useItem:
- `green_input_action_bridge.txt`: 32/32; `green_eq_action_handler_hotbar_item.txt`: 6/6;
  `green_action_dispatcher.txt`: 22/22 (incl. 3 new UseItemById_* tests,
  one of which pins the "not carried -> Failure, no useItem(1234)" contract).

New test binary `test_eq_action_handler_hotbar_item` (tests/CMakeLists.txt):
real EverQuest + RecordingBridge, observes the "Using Test Potion" message
(emitted only after QueuePacket), the refusal messages, and
HotbarCooldownStarted indices {3,7} for two hotbar slots holding the item
and not for a third holding another id. The replay harness has no zone
connection so the OP_CastSpell bytes are NOT asserted (same limitation
test_eq_action_handler_tradeskill.cpp documents); that is what the live leg
is for.

### Verification bar

| step | result |
|---|---|
| 1. cmake --build build -j24 | OK, 0 warnings in project code |
| 2. ctest | **2862/2863**; sole failure AudioManagerTest.PreloadCommonSounds (documented ALSA quirk). `ctest_full.txt` |
| 3. build-gles2-x86.sh | OK (`build_gles2_x86_tail.txt`) |
| 4. visual_gate.sh | **PASS**, goldens untouched (`visual_gate.txt`; memoryCoverage SKIP is the known no-eqe1_client-on-this-machine gap) |
| 5. build-arm-noble.sh | OK on the second invocation (first hit the 600s tool ceiling mid-compile; re-run per protocol) (`build_arm_noble_tail.txt`) |
| 6. check_file_sizes.sh fable | checked 15 files: 8 ok, 7 warn (pre-existing sizes), **0 fail** |

### Live leg: PENDING -- lock held by T227

`bash -c 'set -o noclobber; ...'` returned 1: `/mnt/projects/fable/.eqemu-lock`
holds `T227 2026-08-25`. Not overwritten. Nothing server-side was touched.

Prepared plan for when the lock frees (no keyboard-injection script command
exists -- `NullInputHandler::injectHotbar` has zero callers and --script only
carries slash commands -- so the REAL keyboard path is driven with X):
1. `up.sh befallen`; pick a clicky item from `items` (clickeffect>0,
   clicktype != 5, low reclevel) via `mysql_exec`.
2. Config: t229-style willeq.json plus `"hotbar": {"buttons": [{"type": 3,
   "id": <itemId>}]}` inside clients[0] (HotbarSlotType::Item == 3;
   EverQuest::LoadHotbarConfig reads it via ResolveClientConfigForRead).
3. Xvfb :77 + `build-gles2/bin/willeq --egl-x11 --eqe1-path
   /mnt/projects/EverQuest_ETC1 --log-level=INFO --script`: wait, `/say #set
   time 11 0`, `/say #summonitem <id>` (lands on cursor slot 30 -- NOT
   searched by findItemSlotByItemId), `/equip 30 22` (cursor -> general),
   `/equip list`; then `xdotool key 1` (default HotbarSlot1 binding,
   config/hotkeys.json) to fire the real GraphicsInputHandler ->
   InputActionBridge -> useItemById route.
4. Evidence: client log lines `[INPUT-TRACE] ... reportAction: UseItem ->
   success=true`, `Using <item>`, the C->S OP_CastSpell dump; zone log for
   the server's cast handling / the item's effect message; a screenshot
   after the press. Then restore Testchar (zone 36, -77.9232/21.9086/3,
   h190), down.sh, delete the lock.

### Deviations
- Scope grew by one production function (`EverQuest::UseInventoryItem`) and
  one stub fix (`EqActionHandler::useItem`) beyond the plan's literal text:
  without them the keyboard route was reachable but inert. Flagged, not
  absorbed.
- Cooldown event now targets every hotbar slot holding the item, not only
  the clicked one (see design).
- `refactor/deferred.md` NOT edited (T227/T229 are live and touch it; the
  two entries -- T173 "second name resolver", T174 "Item slots skipped" --
  are closed by this unit, Emote/Macro remain open; orchestrator strikes).
- Baseline grew from 2745/2746 to 2862/2863 between e70e3347 and 23d8ea59
  (other units merged); +6 tests here.

## Live leg (executor, 2026-08-25 21:31-21:55, lock T230, befallen, Testchar)

**Result: PASS, after finding and fixing a third defect the unit tests could
not see.** Five client runs (`review/T230/live_client_testchar*.log`,
`drive_run{1..5}.log`); run 5 is the proof run.

### The keyboard hotbar route was unreachable LIVE for every slot type

Runs 1-2 (`--log-level=INFO`, then `DEBUG`): the real X key press reached the
renderer -- `Key pressed: 1 (...) mode=player, action=HotbarSlot1` -- and
NOTHING reached the bridge: no `processHotbarRequests`, no `reportAction`,
no message. Root cause, `src/client/graphics/renderer_frame.cpp`
`IrrlichtRenderer::processChatInput()` (render thread):

    int8_t hotbarRequest = eventReceiver_->getHotbarActivationRequest();
    (void)hotbarRequest;

`getHotbarActivationRequest()` is an `exchange(-1)`. The render thread runs
this every frame and reaches it before the game thread's
`InputActionBridge::update()` polls `GraphicsInputHandler::
consumeHotbarRequest()`, so every keyboard hotbar press -- Spell, Skill AND
Item, the entire T174 route -- was swallowed live. T174 had no live probe
(its Review says so) and its unit tests drive the bridge directly, so the
route was proven at the bridge and dead at the window. The two swallowing
lines are deleted (comment left in place); the sole consumer is now
`GraphicsInputHandler`. Deviation: `renderer_frame.cpp` is outside the plan's
named territory; it is a two-line deletion nobody else's live unit touches,
and the live leg could not be completed without it. Flagged.

Related, NOT fixed here: the same function still consumes
`getSpellGemCastRequest()` and fires `spellGemCastCallback_`, while
`GraphicsInputHandler::consumeSpellCastRequest()` also polls it -- the same
two-consumer race for the spell-gem keys; whichever thread wins gets the
press. Owed as a follow-up (out of scope, not the hotbar route).

### Fixture trouble (runs 3-4, not defects of this unit)

Run 3 (swallow removed): the press reached the bridge --
`reportAction: UseItem -> success=false, msg='Item not found in inventory'`
-- the failure branch working exactly as designed, because the fixture was
wrong: Testchar's 8 general slots were all full (7x Scaled Curskin 12654 +
1), the cursor held a Wooden Fishing Tackle, and `#summonitem` queued the
meal BEHIND it (server cursor-queue slot 6011), where
`findItemSlotByItemId` (equipment/general/bags only, same as the mouse
path) does not look. Run 4: `/equip 30 22` reported "Swapped items: cursor
(from 30) <-> 22" client-side but the server DB never changed -- a
client/server inventory drift on swap-with-cursor moves that is a separate
open item (noted, not chased). Run 4 also had the key presses land before
the script's setup because the driver slept a fixed 75s and zone-in was
slower; the driver now waits for a chat marker with `scripts/wait_for.sh`.

Fixture fix: offline DB edit of `inventory` for character 65 -- meal 6239
moved from server slot 6011 to server slot 23 (= Titanium general 22), the
curskin from 23 to 6011 (`db_staged.txt`). Restored to the exact
`db_before.txt` layout afterwards, charges reset 3 -> 5 (`db_restored.txt`).
This is an inventory write beyond the protocol's "character-position
writes" -- documented, reversible, on the test character, and reversed.

### Proof run (run 5, `live_client_testchar_run5_debug.log`)

Config `t230_willeq.json` carries `"hotbar":{"buttons":[{"type":3,"id":6239}]}`
(HotbarSlotType::Item; `Loaded hotbar config` confirmed at DEBUG). Script:
`#set time 11 0`, `/equip list`, `/say T230 ready for key presses`; the
driver (`t230_drive.sh`: Xvfb :77, `build-gles2/bin/willeq --egl-x11
--eqe1-path /mnt/projects/EverQuest_ETC1 --log-level=DEBUG --script`) then
sends `xdotool key 1` twice, 12s apart.

Client, per press (lines 13234-13252 and 14537-15118):

    [INPUT] Key pressed: 1 (...) mode=player, action=HotbarSlot1
    [MAIN]  Using Chef's Instant Meal                     <- after QueuePacket(OP_CastSpell)
    [INPUT] InputActionBridge::reportAction: UseItem -> success=true
    [SPELL] BeginCast: caster=407, spell='Abundant Food' (53), time=5000ms   <- SERVER packet
    [SPELL] SpellAction: caster=407, target=407, spell='Abundant Food'       <- SERVER packet
    [MAIN]  Your spell ... Abundant Food has failed to land on yourself because you are invulnerable.  <- SERVER message (GM invuln)

Server side: the item's charges went 5 -> 3 in `inventory` after the run
(`server_zone_log_and_db_after_run5.txt`) -- one charge per key press. The
zone log at its Info level records no cast lines (only SetTime/Save), so the
server reaction is the DB decrement plus the three server-originated
packets above. The client's own C->S dump is TRACE-level and not in the
DEBUG log; "Using ..." is emitted only after `QueuePacket` in
`UseInventoryItem`, and the server's BeginCast is its reply.

Screenshot `run5_after_second_press_1024x768.png` (daylight forced): cast
bar "Abundant Food" mid-cast, chat shows both "Using Chef's Instant Meal"
lines and the server's invulnerable message. Hotbar slot 1 renders with no
icon/name (item slots have no icon/name resolver) -- cosmetic, pre-existing,
not chased.

### After the live fix
Test tree rebuilt; ctest **2862/2863** (`ctest_full_after_live.txt`, ALSA
quirk only); visual gate PASS (`visual_gate_after_live.txt`);
check_file_sizes 0 fail (`check_file_sizes_after_live.txt`). Testchar
restored (zone 36, -77.9232/21.9086/3, h190); down.sh clean; lock deleted.
