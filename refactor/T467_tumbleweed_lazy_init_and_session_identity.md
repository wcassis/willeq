# T467 — the lazily-created TumbleweedManager exits the process (B550); a second character inherits the first one's UI state (B560)

Class A (both unconfirmed live — confirm with a failing test first), **no
lock, no Pi**, slot3.

## B550 — `OverlaysComponent::applyEnvironmentalDisplaySettings()`

T463: it lazily creates a `TumbleweedManager` without
`setConstrainedTextureCache()`, and `TumbleweedManager::init()` then takes the
path whose pack miss is FATAL `exit(1)` (EQE1 all-or-nothing — the exit itself
is correct behaviour; reaching it through a missing wiring call is the defect).
Reachable from Step 11 of every zone load when `rollingObjects` was off at
renderer init and turned on later. Compare with the init-time construction of
the same manager and of every sibling environment manager in
`applyEnvironmentalDisplaySettings()` (boids, storm clouds, …): census each
lazy construction site for the SAME missing wiring (cache, protect list from
T463, eviction guards registered at init) and fix all of them through ONE
construction path so init-time and lazy creation cannot diverge again. Pre-fix
failing test: toggle the setting on after init on an EDT_NULL device with the
real cache and pack; the manager is wired and nothing exits (death test for the
pre-fix exit is acceptable as the red).

## B560 — second character in the same session inherits the first one's state

T464: with the Chase default moved to the identity load, window open states and
user-camera settings still carry over from character A to character B in one
process (camp → character select → B) when B has no ini or an ini missing
those keys. Establish from `FrameComponent::syncUiPreferences()` /
`setUiPreferencesIdentity()` and `ui_preferences.cpp` exactly which fields
carry over. Rule (docs/decisions.md 2026-09-07; T432): an ABSENT key keeps the
DEFAULT, i.e. `defaults.ini`'s value — not the previous character's. Fix: on an
identity change, reset every persisted field to its default before reading the
new file. Pre-fix failing test: A opens windows + sets User 1 rig values; switch
identity to B with no file → B has defaults and writes nothing.

## Proof

Standard verification bar (EXECUTOR_PROTOCOL): background builds with exit
files, `ctest -j6`, G-batch goldens for every TU touched
(`renderer_frame_render` is pinned), visual gate (no golden moves), ARM, file
sizes. Board B590–B599; strike B550/B560 with evidence.

File territory: `src/client/graphics/overlays/`, environment manager
construction, `src/client/graphics/frame/renderer_frame_preferences.cpp`,
`src/client/graphics/ui/ui_preferences.*`, tests. **Not**
`include/client/graphics/player/camera_rig.h`, camera tests,
`config/hotkeys.json`/`hotkey_manager.cpp` (T466, slot1), **not** the
HeadlessShutdownTest and its fixture (T465, slot2).

STOP: B560's fix requires changing what `defaults.ini` or the ini format means
(report); an image golden moves.

## Review

(executor fills)
