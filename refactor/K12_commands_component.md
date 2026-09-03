# K12 — `CommandsComponent` (family: commands + combat_actions)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED through
K10). **Wave 4; runs alongside K11 AFTER K09 merges.**

The batch table said each `/command` body "becomes an `IActionHandler`
method or is deleted if the registry already has it". Same reasoning as
K11: extract first, mechanically; the registry-merge (T139/T223 shape) is a
separate, measured unit afterwards.

## Measured at `dfd36b06` (2026-09-03) — re-measure at your tip

Four command TUs, 26 bodies: `eq_commands.cpp`, `eq_commands_movement.cpp`,
`eq_commands_spells.cpp`, `eq_commands_train.cpp`. Plus `eq_combat_actions.cpp`
(6 bodies: `IsStunned`, `ZoneProcessAction/Consider/Damage/MoneyOnCorpse/Stun`,
member `m_stunned_until`) — small enough to ride along as the same
component's "combat actions" section; K09 already retargeted its dispatch
arms to `owner_.X()`, which this unit turns into `owner_.Commands().X()`.

Callers: `eq_action_handler*.cpp` (`Request*` — ~15), `action/command_processor_*.cpp`
and `action/action_dispatcher*.cpp` (`Request*`, `Register*Commands`),
`eq.cpp` (`RegisterCommands`), `chat_component.cpp` (`RegisterCommands` —
read it; likely a comment), `graphics/renderer_slash_commands.cpp`
(`runLoadDiagnostics`, `runPmemDiagnostics` — if these are real calls the
graphics tree changes and the visual gate is not skippable), `merchant*.cpp`
(`TrainingCost`, `SendMoveItem` — these are MERCHANT's now; the hits here
are comments or the reverse direction — verify), `eq_zone_dispatch.cpp` →
after K09, `zone/…` (the combat handlers), `spell_cast_checks.cpp`
(`IsStunned`), `spell_manager.h` / `combat.h` (declarations or comments —
read).

`ListTrainableSkills`, `ResolveCarriedItem`, `RegisterSpellCommands`: no
external callers — in-family or dead (§5 with the in-family rule).

Members: `m_has_pending_invite`, `m_report` (verify they exist and are
`EverQuest`'s), `m_stunned_until` → move. `m_id`/`m_instance` are other
classes'.

Predicted `eq.h` delta: −32 declarations −3 members +4 ≈ **−30 or better**.

## Territory

`include/client/commands/` + `src/client/commands/` (new), the four
`eq_commands*.cpp` and `eq_combat_actions.cpp` (move), `eq.h`, `eq.cpp`
(`RegisterCommands` call), root `CMakeLists.txt`, `tests/test_commands_component.cpp`
+ cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the callers
the compiler names. K11 runs alongside and edits `application*.cpp`,
`eq_bridge*.cpp` and the components at its own lines; if K11 has moved
`eq_bridge_intents.cpp` before you retarget a call in it, write the call
against the new path — check `git log fable` at dispatch.

## Verification / Review

As K05.
