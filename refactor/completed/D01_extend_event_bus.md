# D01: Extend Existing EventBus + Consolidate Door State

## Plan

### Part 1: Add missing event types to EventBus

File: `include/client/state/event_bus.h`

1. Add new enum values to `GameEventType` for all missing event types (~45)
2. Add new data structs for each event type
3. Add all new structs to the `EventData` variant

### Part 2: Consolidate door state representations

The `eqt::state::Door` struct and `eqt::state::DoorState` class duplicate
`EQT::DoorState` and `EQT::DoorStateManager` from S06. Consolidate to single
canonical types.

4. Add missing features to `EQT::DoorStateManager` (from `eqt::state::DoorState`):
   - `getNearestDoor(x, y, z)` → returns `const DoorState*`
   - `hasDoor(doorId)` → returns bool
   - Pending click tracking: `addPendingClick()`, `removePendingClick()`,
     `isClickPending()`, `clearPendingClicks()`
   - Event callbacks: `setDoorSpawnCallback()`, `setDoorStateChangeCallback()`
     (std::function, not direct EventBus dependency)
   - Fire callbacks from `addDoor()` and `setDoorState()`

5. Update `GameState` (`include/client/state/game_state.h`, `src/client/state/game_state.cpp`):
   - Change `#include "client/state/door_state.h"` → `#include "client/door_state_manager.h"`
   - Change member `DoorState m_doorState` → `EQT::DoorStateManager m_doorManager`
   - Change accessors `doors()` return type → `EQT::DoorStateManager&`
   - In constructor: wire callbacks to fire EventBus events
   - In `resetForZoneChange()` / `clearAll()`: call `m_doorManager.clear()`

6. Update callers:
   - `src/client/action/action_dispatcher.cpp`: return types from `.doors()` now use
     `EQT::DoorState` struct (field names identical, no code changes needed)
   - `src/client/eq_action_handler.cpp`: same

7. Remove old files:
   - Delete `include/client/state/door_state.h`
   - Delete `src/client/state/door_state.cpp`
   - Remove `src/client/state/door_state.cpp` from CMakeLists.txt

8. Build and verify compilation

## Acceptance Criteria

- All new event structs are default-constructible and movable
- `EventData` variant holds all new event types
- No renderer includes in event_bus.h
- `eqt::state::Door` and `eqt::state::DoorState` no longer exist
- `GameState::doors()` returns `EQT::DoorStateManager&`
- `EQT::DoorStateManager` has no EventBus includes (uses std::function callbacks)
- Door events still fire through EventBus when doors are added/state changed
- Existing tests compile and pass
- Full build succeeds

## Review

All steps completed as planned. No deviations.

- Part 1: Added ~35 new event types to `GameEventType` enum, ~35 new data structs,
  all added to `EventData` variant (now 60 types total). Removed forward declarations
  block (no longer needed — structs defined before variant).
- Part 2: Added `getNearestDoor()`, `hasDoor()`, pending click tracking, and
  `std::function` callbacks to `EQT::DoorStateManager`. `GameState` wires callbacks
  to fire EventBus events in constructor. Old `eqt::state::Door`/`DoorState` removed.
  Callers (`action_dispatcher.cpp`, `eq_action_handler.cpp`) required no changes —
  field names are identical between old `eqt::state::Door` and `EQT::DoorState`.
- Build: clean compile, 1201/1201 tests pass.
- All acceptance criteria met.
