# T156 — Make skill activation reachable (reachability pattern #10)

**Slot: slot1. x86. NO eqemu lock, NO server, NO Pi.** T157 (rogue provisioning)
runs in parallel and touches no file in this checkout.

**Territory: `src/client/graphics/ui/static_ui_input.cpp`,
`src/client/eq_bridge.cpp` (the hotbar/skill intent arms only),
`include/client/events/renderer_intents.h`, `include/client/eq.h` (the
`HotbarButtonType` enum only), `include/client/hotbar/hotbar_model.h`,
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `tests/`, `refactor/`.**

**Do NOT touch the skill packet bodies** (`include/client/skill/skill_packet_bodies.h`,
`SkillManager::getSkillOpcode`/`sendSkillUsePacket`). T152 fixed those and T155
verified two of them on the wire. This unit is about reaching that code, nothing
else.

T155 proved on a live server that **all five of T152's skill fixes are
unreachable from anything a user or script can do.** Read T155's Review and
`/mnt/projects/fable/review/T155/skill_reachability_evidence.txt` first.

## The three defects, all confirmed by the orchestrator

1. **`SkillActivateIntent` has no producer.** One consumer (`eq_bridge.cpp:1050`),
   zero producers anywhere in `src/`, `include/` or `tests/`.
2. **`HotbarActivateIntent`'s only producer drops three of its four fields.**
   `static_ui_input.cpp:42` pushes `HotbarActivateIntent{slot}` — brace-init with
   the index alone, so `buttonType`, `id` and `emoteText` are all value-initialised.
   `buttonType` is therefore always 0 and the `case 3: // Skill` arm at
   `eq_bridge.cpp:1359` can never run.
3. **`EqActionHandler::useSkill()` is a stub** (`eq_action_handler.cpp:561`) that
   logs "not yet implemented", and `useSkill` has **no command registration at
   all** — T151 flagged this as a candidate unit; it is now the blocking one.

## ⚠ The landmine — THREE numbering schemes, none of which agree

Fixing defect 2 by "just passing the type through" will silently activate the
WRONG THING. The three:

| source | numbering |
|---|---|
| `eqt::ui::HotbarButtonType` (`eq.h:105`) | Spell=0, Skill=1, Item=2, Command=3 |
| `eqt::HotbarSlotType` (`hotbar_model.h:20`) | Empty=0, Spell=1, Skill=2, Item=3, Emote=4, Macro=5, Combat=6 |
| the `switch (i.buttonType)` (`eq_bridge.cpp:1294`) | 0=Spell, 1=Item, 2=Emote, 3=Skill |

The switch matches **neither** enum. Feed it `HotbarSlotType::Skill` (2) and you
get the Emote arm; feed it `Item` (3) and you get Skill. `static_panels.h:110`
and `event_bus.h:822` each document a fourth and fifth reading in comments.

**Pick ONE source of truth — `HotbarSlotType` is the model's and is the
strongest candidate — carry the enum type (not a raw `uint8_t`) across the
intent, and switch on named enumerators so the compiler catches the next drift.**
If `eqt::ui::HotbarButtonType` ends up with no remaining callers after that,
delete it (`eq_chat.cpp:252` uses it today — check before deleting). This is the
board's duplicated-then-drifted class; leaving two live numberings is not a fix.

## The work, in priority order

**1. A COMMAND, first and non-negotiable.** Register `/useskill <id or name>` in
`action::CommandProcessor` only (never the legacy `game` registry, T97), route it
`ActionDispatcher::useSkill` → `EqActionHandler::useSkill` → 
`SkillManager::activateSkill(skill_id, target_id)`, and **pass the current target**
— `activateSkill`'s second parameter defaults to 0 and Taunt, Disarm and
PickPocket all need a real target id.

**Why this leg outranks the UI:** every live probe in this project is driven
headlessly by `--script`. **A mouse click is not scriptable, so a hotbar-only fix
would leave T155's five probes exactly as blocked as they are now.** The command
is what converts this unit into cleared debt.

Accept a skill **name** as well as a numeric id if the skill DB makes that cheap
— `/useskill 9` is the same "technically wired, practically unreachable"
interface this arc keeps deleting. Validate the id against the skill table and
report unknown ids to the player rather than casting an unchecked int (T95).

**2. Fix the hotbar producer** to pass slot type, id and emote text from the
cached hotbar state, reconciled per the landmine section above.

**3. Decide `SkillActivateIntent`'s fate and say which you chose.** Either give it
a real producer (the skills popup already tracks `hoveredRow` in
`static_ui_input.cpp:139` and is the natural place) or **delete it with its
consumer** — an intent with no producer is T147's trap class. Do not leave it
sitting there unproduced.

## Acceptance

- `/useskill` reaches `SkillManager::activateSkill` with the right skill id and
  the current target, proven by tests.
- The hotbar Skill arm is reachable, proven by a test that drives the producer's
  slot state through to the consumer — **not** by a test that constructs the
  intent by hand, which is exactly the fake-substitution trap T93 catalogued.
- A test that would FAIL against today's numbering mismatch.

**Not claimed by this unit: any wire behaviour.** T157 is provisioning a Rogue in
parallel; the live re-probe of T152's five opcodes is a follow-up that needs both
units merged. Name it as owed.

## Verification

Full protocol bar. Baseline **2542/2543** at tip `0aba3292`, sole expected failure
the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

⚠ `tests/test_command_processor.cpp` is **3168 lines, already past the 3000-line
FAIL ceiling** — T153 hit this. Put new tests in a new file rather than growing it.

## STOP conditions

- Reconciling the numbering would require touching the renderer's panel state
  beyond the hotbar arm (report it; scope a follow-up).
- `SkillManager::activateSkill` turns out to need state the action layer cannot
  reach (T136's bridge-intent gap).

## Review

Executed in `/mnt/projects/fable/wt/slot1`, branch `task/T156-skill-reachability`
off `fable` tip `0aba3292`. No eqemu, no lock, no server, no Pi. `main` and the
sibling slots were never touched.

### Verification

| gate | result |
|---|---|
| `cmake --build build -j24` | clean, unpiped, no warnings introduced |
| `ctest --output-on-failure` | **2570/2571**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. Baseline was 2542/2543; +28 (27 in the new file, +1 in `test_static_ui_panels.cpp`) |
| `scripts/refactor/check_file_sizes.sh fable` | 17 files, **13 ok, 4 warn, 0 fail**. The four WARNs (`eq.h`, `command_processor.cpp`, `eq_action_handler.cpp`, `eq_bridge.cpp`) all pre-date this unit; `eq.h` and `eq_chat.cpp` got *smaller* |

### Leg 1 — `/useskill`, the leg that outranks the UI

Registered in `action::CommandProcessor` **only** (T97), which
`docs/decisions.md` records as the one registry `--script` dispatches to. The
legacy `game` registry was not touched and has no `skill`/`useskill` entry.

| layer | what it does |
|---|---|
| `CommandProcessor::cmdUseSkill` (`command_processor.cpp`) | `/useskill <id\|name>`, alias `/skill`, category Combat. Resolves a name via a file-local `findSkillIdByName()` over `EQ::getSkillName()`'s table, normalised to lowercase alphanumerics so `taunt`, `Bind Wound`, `bind-wound` and `PICK POCKET` all resolve. Numeric ids are range-checked against `EQ::MAX_SKILL_ID` **and** required to have a name, then checked against `EQ::isActivatableSkill()`. Nothing is dispatched on any failure path |
| `ActionDispatcher::useSkill` | unchanged — it already existed, `checkZoneConnection()` then `m_handler->useSkill(id)` |
| `EqActionHandler::useSkill` | **was the `"not yet implemented"` stub.** Now: null-checks `GetSkillManager()`, re-validates the id, resolves the **current target** from `CombatManager` (`HasTarget()`/`GetTargetId()`, the shape `taunt()`/`shieldTarget()` already use), calls `SkillManager::activateSkill(id, targetId)`, and reports `getSkillUseError()` to the player when it returns false |
| `SkillManager::activateSkill` | **not touched.** T152's packet bodies, `getSkillOpcode` and `sendSkillUsePacket` are untouched by this unit |

The target is passed explicitly rather than relying on `activateSkill`'s own
`target_id == 0` fallback, because that fallback only fires for
`requires_target` skills and leaves the caller's intent invisible and
untestable.

All five of T152's skills are now reachable by name — pinned by
`AllFiveT152SkillsAreNowReachableByName`: apply poison (6), bind wound (9),
disarm (16), pick pocket (48), taunt (73).

### The numbering: `eqt::HotbarSlotType` is the source of truth, and why

The plan named it the strongest candidate. Reading the code makes it the only
candidate: it is **the only numbering any real data has ever carried**. The live
path is `HotbarModel::assignSlot` → `HotbarSlotAssignedData` → `HotbarPanelState`,
and every hop of it is already in `HotbarSlotType`'s numbering. The
`eq_bridge.cpp` switch's numbering (0=Spell, 1=Item, 2=Emote, 3=Skill) had
**zero producers** — it matched neither enum and nothing ever fed it — and
`eqt::ui::HotbarButtonType`'s numbering reached only a dead struct.

What landed:

* **New header `include/client/hotbar/hotbar_slot_type.h`** — the enum alone,
  plus `hotbarSlotTypeFromRaw()` (validated, T95) and `hotbarSlotTypeName()`.
  Zero dependencies beyond `<cstdint>`, so `renderer_intents.h` (whose contract
  forbids renderer/EverQuest includes), `event_bus.h`, `static_panels.h` and
  `hotbar_model.h` can all share the **type**, not a raw `uint8_t`.
* **The type is now carried, not the number**, at every hop:
  `HotbarSlot.type`, `HotbarSlotAssignedData.type`, `HotbarSlotState.type`,
  `HotbarActivateIntent.type`.
* **`eq_bridge.cpp` switches on named enumerators with no `default:`**, so
  `-Wswitch` catches the next drift. Every enumerator is listed; `Empty`,
  `Macro` and `Combat` fall through to an explicit no-op.
* **`eqt::ui::HotbarButtonType` is deleted.** The plan said to check
  `eq_chat.cpp:252` first. The check found something stronger than "one caller":
  `HotbarButtonType`'s only user was `eqt::ui::PendingHotbarButton`, whose four
  `EverQuest` accessors (`AddPendingHotbarButton`, `GetPendingHotbarButtons`,
  `ClearPendingHotbarButtons`, `GetPendingHotbarButtonCount`) have **zero callers
  anywhere in `src/`, `include/` or `tests/`** — the reachability pattern again,
  inside the very defect this unit exists to fix. The enum, the struct, the four
  methods, the `m_pending_hotbar_buttons` member and the `eq_chat.cpp` block are
  all gone. **One numbering now exists in the tree.**
* **`HotbarModel::loadFromJson` no longer casts an unvalidated `int` to the
  enum** (T95). An out-of-range `type` in the legacy hotbar JSON config now
  logs a warning and clears the slot instead of manufacturing a bogus enumerator.

### Leg 2 — the hotbar producer, and the fields it dropped

`static_ui_input.cpp:42` pushed `HotbarActivateIntent{slot}`. Three of four
fields were value-initialised, so *every* click on *every* slot looked like
"activate spell id 0". Two of the dropped fields had nowhere to come from:
`HotbarSlotState` carried only `type`/`name`/`iconId` — **no `id` and no
`emoteText`** — and `HotbarSlotAssignedData` had no `emoteText` either, so
`HotbarModel::publishSlot` was silently discarding it. Both were added, and the
producer now fills all four fields from the cached slot, pushing nothing at all
for an `Empty` slot (which previously produced the bogus "spell 0").

`IrrlichtBridge`'s inline `HotbarSlotAssigned` copy was extracted to
`EQT::Graphics::applyHotbarSlotAssigned()` in `static_panels.cpp`. That is the
only reason the test below can be honest: production and the test now call the
*same* function for that hop, so nothing is reconstructed by hand.

### Leg 3 — `SkillActivateIntent`: **produced, not deleted**

It now has a real producer. Clicking a row in the skills popup
(`static_ui_input.cpp`, the `skills.hoveredRow` block the plan pointed at)
pushes `SkillActivateIntent{skills.skills[scrollOffset + row].skillId}`, with
the same row geometry `renderSkillsPopup()` draws and a bounds check against the
skill vector. The consumer in `eq_bridge.cpp` now also passes the current target
(it previously called the one-argument `activateSkill`, so Taunt/Disarm/
PickPocket would have gone out with target 0 even once reachable).

Deleting it was the alternative, and was rejected: the popup already exists,
already renders per-skill rows with ids, and already tracks the hovered row — it
was a producer that had simply never been written, not a design with no home.

### The test that fails against the old numbering

`tests/test_skill_reachability.cpp` — **a new file**, because
`tests/test_command_processor.cpp` is 3168 lines, past the FAIL ceiling. 27
tests, all passing.

`HotbarReachabilityTest` drives the real chain with **nothing copied by hand**
at any step (the T93 fake-substitution trap):

```
HotbarModel::assignSlot()        real producer
  -> GameStateBridge queue       real transport (pushEvent/drainEvents)
  -> applyHotbarSlotAssigned()   the real renderer-side hop
  -> StaticUIInput::processInput() real intent producer, real hit-test geometry
  -> drainIntents()              exactly what eq_bridge.cpp consumes
```

`SkillSlotProducesASkillIntentCarryingTheSkillId` asserts
`type == HotbarSlotType::Skill` and `id == 73`. It fails against the old code
three separate ways: `id` was 0, `buttonType` was 0 (the Spell arm), and even a
naive "pass the raw type through" would have handed the old consumer a 2, which
its numbering read as **Emote**. `ItemSlotIsNotMistakenForASkill` is the mirror
— `Item` is 3, which was the old consumer's *Skill* case.

**Measured, not asserted.** The producer was temporarily reverted to
`pushIntent(HotbarActivateIntent{slot})` and the suite re-run: **6 of the 9
`HotbarReachabilityTest` cases failed**, including all four intent-content
cases. The producer was then restored and the full build/suite re-run.

`UseSkillCommandTest` (13 cases) drives the real `CommandProcessor` over the
real `ActionDispatcher` into `RecordingActionHandler`, covering registration,
numeric ids, name resolution in four spellings, the alias, all five T152 skills,
out-of-range ids, in-range-but-unnamed ids (75–99), unknown names,
non-activatable skills, missing arguments and the zone-connection guard.
`SkillsPopupProducerTest` (5 cases) covers the new `SkillActivateIntent`
producer including scroll offset, past-the-end rows, a closed popup and
hover-without-click.

### Deviations

1. **Four files outside the stated territory were touched**, all strictly on the
   hotbar activation path, because the plan's leg 2 is not completable without
   them: `include/client/graphics/ui/static_panels.h` +`.cpp` (`HotbarSlotState`
   had no `id`/`emoteText` for the producer to read, and
   `applyHotbarSlotAssigned` lives here),
   `include/client/state/event_bus.h` (`HotbarSlotAssignedData` had no
   `emoteText` and carried a raw `uint8_t` type), `src/client/hotbar/hotbar_model.cpp`
   and `src/client/bridge/irrlicht_bridge.cpp` (one `case` arm). Judged **not**
   the first STOP condition: that condition names renderer panel state *beyond
   the hotbar arm*, and nothing outside the hotbar's own struct and its own
   event case was changed. No other panel was touched.
2. **`src/client/eq_chat.cpp` and more of `include/client/eq.h` than "the
   `HotbarButtonType` enum only"** — the dead `PendingHotbarButton` feature went
   with the enum. Rationale above; leaving the enum alive inside a zero-caller
   feature would have left the plan's "two live numberings is not a fix" only
   half-answered.
3. **A new header** (`hotbar_slot_type.h`) rather than sharing
   `hotbar_model.h`, which pulls in `json/json.h` — unacceptable weight for
   `renderer_intents.h` and `event_bus.h`.
4. **`tests/test_static_ui_panels.cpp`'s `HotbarClickPushesActivateIntentWithSlotIndex`
   was updated**, not deleted: it clicked an *empty* slot and asserted an intent
   came out, which is precisely the bogus "spell 0" behaviour this unit removes.
   It now assigns real slot content first, and a new sibling case
   (`HotbarClickOnEmptySlotPushesNothing`) pins the new empty-slot rule.
5. **The dispatcher signature was left id-only.** Target resolution lives in
   `EqActionHandler`, which is where the live `CombatManager` is; adding a
   target parameter to `IActionHandler::useSkill` would have forced every fake
   and every caller to invent one. Same shape as `taunt()`.
6. **No live probe.** Correct for this unit — the plan explicitly claims no wire
   behaviour, and no eqemu was involved.

### STOP conditions

Neither was hit. `SkillManager::activateSkill` needed no state the action layer
cannot reach (T136's gap did not appear — `EverQuest::GetSkillManager()` and
`GetCombatManager()` are both plain accessors the handler already holds). The
numbering reconciliation stayed inside the hotbar arm, per deviation 1.

### Findings outside this unit's scope

1. **`InputActionBridge::processHotbarRequests` (`input_action_bridge.cpp:388`)
   is a SECOND, divergent hotbar activation path.** It converts every keyboard
   hotbar request into `castSpell(slot + 1)` with the comment "For now, we treat
   all hotbar slots as spell gems (1-indexed)". So the keyboard hotbar and the
   mouse hotbar do genuinely different things, and the keyboard one cannot
   activate a skill, an item or an emote no matter what is in the slot. It has
   no access to `HotbarModel` at all, which is why it was not fixed here — the
   fix is a plumbing change through the action layer, its own small unit.
2. **The `PendingHotbarButton` feature was reachability pattern #11** and is
   recorded as such above; deleted here rather than logged.

### Still owed

1. **The live re-probe of T152's five opcodes** — ApplyPoison (6), BindWound (9),
   Disarm (16), PickPocket (48), Taunt (73) — over `/useskill`, which is now
   scriptable. This unblocks T155's probes 5, 7 and 9. PickPocket additionally
   needs the Rogue T157 is provisioning, so the probe wants **both units merged**.
   BindWound and Taunt already have VERIFIED wire evidence from T155 via
   `/bandage` and `/taunt`; `/useskill bind wound` and `/useskill taunt` are the
   cheap controls that prove the new route reaches the same senders.
2. **The keyboard hotbar path** (finding 1).
3. **`HotbarSlotType::Macro` and `::Combat` have no consumer arm.** Nothing
   produces them either, so this is latent rather than broken; the explicit
   no-op cases in `eq_bridge.cpp` name it rather than hiding it under a
   `default:`.
