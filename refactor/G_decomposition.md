# Batch G — Method Decomposition

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
| G01 | Per-step decomposition of loadZoneSequential (2,673-line method → step methods + ZoneLoadContext) | renderer_zone_load.cpp + irrlicht_renderer.h | | | |
| G02 | Split RegisterCommands (2,568-line method → per-domain Register*Commands methods across 2–3 TUs) | eq_commands*.cpp + eq.h | | | |
| G03 | Co-locate StartCombatMovement overloads (move-only micro-unit, orchestrator-executed) | eq_movement.cpp | | | |
