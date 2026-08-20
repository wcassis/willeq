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

(executor fills this in)
