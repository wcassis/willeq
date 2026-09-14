# T471 — apply T466's camera measurements: login camera, world-relative user cameras, official camera keys, pitch sign, Chase re-entry pose

Class A (match the oracle — `match-the-oracle-dont-infer-it`), **no lock
(Leg A)**, slot1. Read T466's Review (`refactor/T466_oracle_cameras_and_held_items.md`)
and `review/T466/README.md`, T461's and T464's Reviews, BOARD B530, B531, B580,
B581, B582, B585.

## What the oracle measured (T466) and what changes

1. **Login camera = First Person.** fable starts a character with no saved
   view in Chase (`kDefaultCameraMode`, T464 moved it to the identity load).
   Change the default to First Person; a saved `View=` still wins. Update the
   T464/T467 tests that pin Chase-as-default with the oracle provenance.
2. **User 1, User 2, Tether hold a WORLD direction** (0.08–0.19 match after a
   90° player turn); First Person, Overhead and Chase turn with the player.
   fable's rigs are heading-relative for all (B580). Make the three hold a
   world azimuth: on entering the mode, capture the world azimuth from the
   current player heading + rig azimuth (so the entry frame does not jump —
   verify against T466 frames if they show entry), then keep it fixed while
   the player turns; Tether additionally follows a walking player (T466). Heading
   conventions per `docs/CLAUDE.md`; the 90° player/NPC offset is load-bearing —
   never harmonise it. `/camera get` must report the azimuth in the frame it is
   stored in, and say which.
3. **Camera keys (Options > Keys > Camera, measured):** Zoom in = NPAD7 or
   INSERT; Zoom out = NPAD1 or DELETE; Raise = Alt+NPAD8 or Alt+UP; Lower =
   Alt+NPAD2 or Alt+DOWN; Tether-slide right/left = Alt+NPAD6/Alt+RIGHT,
   Alt+NPAD4/Alt+LEFT; Pitch up = Alt+NPAD9 or Alt+PGUP; Pitch down = Alt+NPAD3
   or Alt+PGDOWN; F9 = Toggle camera view (already). **The official Pitch up
   tilts the VIEW up — the opposite of fable's `CameraPitchUp`; fix the sign**
   so the action name means what the oracle's does. Bind each in the defaults
   and in `config/hotkeys.json` (T464's shipped-file test enforces both) where
   fable has the action; add Raise/Lower (look-at height nudge) and
   Tether-slide (Tether azimuth nudge) if the rig supports them cleanly. First
   census every existing fable binding on these keys (Insert/Delete, numpad,
   Alt+arrows) — a collision with a movement or UI binding is reported, not
   silently overwritten; STOP on a collision with movement keys.
   **Do NOT change F10, F11, L or Alt+C** — Will is deciding whether fable
   matches the official UI-page keymap exactly (B582).
4. **B581 — entering Chase from another camera leaves the old camera pose.**
   Confirm with a failing test; fix so Chase places its camera on entry (the
   T074/T300 reposition rule for first person is the model).
5. **FOV / Overhead** are already applied (T466). Do not re-fit anything.
   The Chase-as-rig fit (pitch 6, dist 4.0, height 7.0) is NOT applied: record
   in the Review whether fable's Chase follow camera reproduces that geometry
   at its defaults (compute it), and board the difference if it does not.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
every TU touched (`movement_order`, `renderer_frame_render` pinned; in-place
updates per G-correction 32, say so); visual gate — the gate uses a fixed
camera; if a golden moves because the default mode changed, STOP and report
(do not re-bless). ARM; file sizes. Board B650–B659; strike/narrow B580, B581,
B531.

File territory: `src/client/graphics/player/`, `include/client/graphics/player/`,
`CameraController`, `renderer_frame_preferences.cpp` camera default,
`src/client/input/hotkey_manager.cpp` camera actions, `config/hotkeys.json`
camera rows, slash `/camera`, camera tests, `include/client/graphics/ui/CLAUDE.md`
key tables. **Not** entity/equipment attachment or model yaw (T472, slot2),
**not** `door_manager*` / `application_zone_loading.cpp` (T470, slot3) —
except T464's `placeCameraForZoneIn()` call if the default change needs it
(coordinate: report, don't edit, if T470 has not merged).

STOP: a key collision with movement bindings; an image golden moves.

## Review

(executor fills)
