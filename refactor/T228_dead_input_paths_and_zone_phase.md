# T228 — delete the dead InputActionBridge input paths; make ZoneProcessNewZone complete its request phase

Slot: slot3. x86. NO eqemu. Class A. Do NOT touch application.cpp,
include/client/bridge/*, src/client/bridge/*, eq_merchant.cpp, tests/
test_game_state_sync_bridge.cpp, tests/test_game_state_source_writes.cpp
(T227), or any graphics loader (T226).

## 1. Dead input paths (T218 evidence, review/T218/, deferred.md T218 entry)
`InputActionBridge::processMouseInput`'s heading branch reads
`m_state.player().heading()` (never projected → 0) and fired 0 times in 120
mouse-motion events because the render thread consumes the same destructive
`getMouseDeltaX()` first; `processContinuousInput()` has ZERO callers. Had the
branch ever fired it would have sent a position update from heading 0.
Delete both paths (and the T218 DEBUG probe left in place), any now-unused
members/includes, and their tests if they only tested the dead code
(tests/test_input_action_bridge.cpp — check each case). Keep everything
else in the class. Confirm with grep that no caller remains.

## 2. ZoneProcessNewZone never calls CheckZoneRequestPhaseComplete (T04)
`eq.cpp` ~1918 defines it; the only callers are in eq_player_info.cpp
(:57,:65,:74,:83). NewZone is one of the phase inputs but the handler never
calls the check, so completion rests on the other packets arriving after
NewZone (true today by server causality). Add the call at the end of
`ZoneProcessNewZone` (idempotent — verify the function is), and a test in
tests/test_zone_connection_flow.cpp that NewZone arriving LAST still
completes the phase. Red/green measured.

## Verification
Full ctest; Review in this file; evidence review/T228/. Commit:
`T228: dead InputActionBridge paths deleted; NewZone completes the zone-request phase`.
