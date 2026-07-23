# Batch G — Method Decomposition

**STATUS: COMPLETE (2026-07-23).** All 3 units done. No method over 408
lines remains in the decomposed files; every eq_commands file is under the
1,500-line warn ceiling.

Follow-up to Batch F. Unlike F, these units CHANGE headers (new private
helper declarations) and restructure method bodies into helpers — they are
NOT move-only. The code inside helpers still moves verbatim; the
verify_move_only.py output is expected to show signature/struct/call-site
additions only, reviewed by the orchestrator per unit.

Process per unit: same as Batch F (plan file → Sonnet executor in a slot →
build + full ctest + verifier review → orchestrator merge). Analysis tables
required in the review BEFORE editing.

| Unit | Scope | Target | Status | Slot | Commit |
|---|---|---|---|---|---|
| G01 | Per-step decomposition of loadZoneSequential (2,673-line method → 20 step methods + ZoneLoadContext; largest method now 408 lines) | renderer_zone_load.cpp + irrlicht_renderer.h | done | slot1 | `bab99d7` |
| G02 | Split RegisterCommands (120 commands → Social/Movement/Spells domains, 1,110+900+783 lines; order-critical 'who' shadowing preserved) | eq_commands.cpp, eq_commands_movement.cpp, eq_commands_spells.cpp + eq.h | done | slot2 | `9c60d77` |
| G03 | Co-locate StartCombatMovement overloads (move-only micro-unit, orchestrator-executed) | eq_movement.cpp | done | slot3 | `35d95e6` |
