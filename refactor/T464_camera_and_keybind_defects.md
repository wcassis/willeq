# T464 — camera and key-binding live defects: zone load resets the camera (B532), hotkeys.json wipes defaults (B535), the ceiling clamp latches (B507)

Class A, **no lock, no Pi**, slot1. Read T461's Review
(`refactor/T461_camera_modes_orbit_f9.md`) — it built the camera set these
defects live in — and T459's B507 evidence (`review/T459/README.md`).

## B532 — every zone load forces Chase, and the preference save persists it

T461: `application_zone_loading.cpp` calls `setCameraMode(Chase)` on each
zone load (two sites), after which `FrameComponent::syncUiPreferences()`
sees a changed value and writes it over the player's saved `[Camera] View=`.
Confirm with a pre-fix failing test (load a preference of e.g. User 1 →
simulate zone load → the mode and the written ini are still User 1). Decide
where the saved camera is applied relative to the forced reset — the saved
choice must win at zone-in, including first person (T074/T300 reposition
rules). Do not change what a character with NO saved view gets (Chase,
pending the oracle's login-camera measurement B530).

## B535 — `config/hotkeys.json` clears the default bindings

T461: loading the shipped `config/hotkeys.json` clears the compiled-in
defaults, and the file has no entries for Alt+C, L, F10/F11, Shift+F10/F11,
so those keys do nothing in production (tests that use the defaults stay
green). Establish the intended contract (file overrides per action vs file
replaces all) from `src/client/input/CLAUDE.md`, the loader and git history;
then make production reach every default binding: either the file carries
them all (a test that every `HotkeyAction` default is present in the shipped
file) or the loader merges. **F11 is bound to both a volume action and
hide-UI** — do NOT pick a winner: keep today's effective production
behaviour for F11, board the conflict as oracle-owed (the official keymap,
T461 Leg B), and say which one the file currently yields.

## B507 — the Follow/Chase BSP ceiling clamp latches the camera low

T459: `CameraController::setFollowPosition()` clamps height to
`regionBoundsMap_[bspPlayerRegion_].MaxEdge.Y`; in nro that region's AABB
top is −172 with the player at z −11..+29, so the camera latched at
`eyeHeight*0.5` (3.0 instead of 10.0) for the rest of the run; reproduces on
x86 (`/move` along the same path near (2617,−183)). `bspPlayerRegion_` is fed
a frame late and only on region change. Find why the region's bounds are
below the player (wrong region id? a leaf AABB in a different coordinate
frame — z-up vs y-up, the T332/T437 z history? stale region after a
transition?) and fix the root, not the clamp's symptom. Pre-fix failing test
from real nro BSP data if the loader can run headless (the zone BSP is in the
EQ files at `/mnt/projects/EverQuestP1999`), else a constructed region map
reproducing the measured numbers. Also check whether this clamp can ALTERNATE
(region flipping at a boundary) — Will's still-unreproduced "camera jerks
between two perspectives running forward in nro at 1280x720" (B508) is in the
same zone; report whether the code allows a two-state flip and under what
condition, even if you cannot reproduce it.

## Proof

Standard verification bar; G-batch goldens for every TU touched (G06
`movement_order` and `renderer_frame_render` are pinned — reviewed edits update
goldens in place per G-correction 32, and say so); visual gate (no image golden
moves); ARM; file sizes. Board B560–B569; strike B532/B535/B507 with evidence
(or mark B507 narrowed).

File territory: `src/client/graphics/player/`, `include/client/graphics/player/`,
`CameraController`, `src/client/input/` (hotkeys), `config/hotkeys.json`,
`application_zone_loading.cpp` camera calls, `renderer_frame_preferences.cpp`,
tests. **Not** `src/client/graphics/ui/` or frame timing (T462, slot3), **not**
`src/client/graphics/environment/` or the texture cache (T463, slot2).

STOP: the B507 root cause is in the BSP loader shared with game-state water/
zone-line detection (report before changing it); an image golden moves.

## Review

(executor fills)
