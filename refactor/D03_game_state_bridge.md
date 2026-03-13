# D03: Create GameStateBridge Interface and IrrlichtBridge Skeleton

## Plan

### Steps

1. Create `include/client/bridge/game_state_bridge.h`:
   - Thread-safe queues for events (game→renderer) and intents (renderer→game)
   - `pushEvent()`, `pushIntent()`, `drainEvents()`, `drainIntents()`
   - Virtual `applyEvent()` for renderer-specific translation
   - Swap-vector drain pattern: lock, swap with empty vector, unlock
   - No renderer or EverQuest includes

2. Create `include/client/bridge/irrlicht_bridge.h` and
   `src/client/bridge/irrlicht_bridge.cpp`:
   - `IrrlichtBridge : GameStateBridge`
   - `applyEvent()` visits the variant, LOG_TRACE each event type
   - No actual renderer calls yet — stubs only
   - Includes renderer headers but not eq.h

3. Add `src/client/bridge/irrlicht_bridge.cpp` to CMakeLists.txt
   (in CLIENT_CORE_SOURCES after action system sources)

4. Build and verify compilation

## Acceptance Criteria

- `GameStateBridge` has no renderer or EverQuest includes
- `IrrlichtBridge` includes bridge header but not eq.h
- Thread-safe queue: lock+swap pattern in drain methods
- Compiles and links (CMakeLists.txt updated)
- No existing code modified except CMakeLists.txt
- Build succeeds, all tests pass

## Review

All steps completed as planned. No deviations.

- `GameStateBridge`: thread-safe swap-vector queues, no renderer/EQ includes.
- `IrrlichtBridge`: complete switch over all GameEventType values with
  LOG_TRACE stubs. No renderer calls yet.
- CMakeLists updated with `src/client/bridge/irrlicht_bridge.cpp`.
- Build succeeds, 1201/1201 tests pass.
- All acceptance criteria met.
