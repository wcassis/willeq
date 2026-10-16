# B1088 (#408) — loading screen shows "Entering <zone long name>..."

**Date:** 2026-10-16 · **Author:** dev (hermes-ext) · **Issue:** GitLab #408
**Spec:** the issue body is the specification; the card `t_0272d995`.

## What changed

The client's zone-loading screen now shows the zone's human-readable **long
name**, centred above the existing loading/stage text and the progress bar:

```
        EverQuest              <- fixed title (as before)
   Entering North Qeynos...    <- B1088: the zone's long name (new)
   [============      ] 50%    <- progress bar (as before)
           Loading...          <- stage text (as before)
```

An empty name (a zone the era table does not know and the server does not
name) draws **no line** — never `"Zone <id>"`, never the raw short name.
The name is **verbatim** — a leading "The" is kept, not stripped (Will's
correction).

### One value source, read at draw time

`eqt::ZoneDisplayNames` (`include/client/zone/zone_display_name.h`,
`src/client/zone/zone_display_name.cpp`) is the single lookup, device-free
(no client, no network, no display):

- **Primary:** the era table's long name via the ONE existing parser
  `eqt::login_world::LoadEraZoneLongNames()` (`character_select.cpp`) —
  `data/era_zones.tsv`, col 4. No second TSV loader.
- **Override:** the server's `OP_NewZone zone_long_name[278]` (packet
  offset 96, after `zone_short_name`; parsed in `ZoneProcessNewZone()`),
  stored via `noteNewZone()`. It is authoritative for that zone and is the
  only source that knows the ids the era table omits (`tutorialb`). An
  empty field leaves the era table's answer in place.
- Unknown id → `""` (empty). The screen draws nothing for it.

The line the screen shows — `"Entering <name>..."` or `""` — is **stored
once and read at draw time** (mutex-guarded):

| Writer (game thread) | Trigger | Reader (render + loading thread) |
|---|---|---|
| `refresh(zoneId)` | the target zone id is known — the entering character's zone (character-select list), every server-approved zone change, the zone-line trigger | `entryText()` behind the mutex |
| `noteNewZone(zoneId, name)` | `OP_NewZone` — the new zone's own long name | (re-derives + logs if it's the shown zone) |
| `clearEntry()` | `hideLoadingScreen()` — zone ready | the stale name is forgotten before the next load |

The game thread → render/loading-thread carry is the mutex, not `GameState`
(the render thread never reads `GameState` for this — the card's step 2
constraint).

Each write logs one INFO line with the **exact string the screen draws**
(`[LOADING] Entering North Qeynos...`) — that line is the scenario arm's
assertion, greppable from a plain log without pixels.

### One layout, both draw bodies

`EQT::Graphics::drawLoadingFrame` (`include/client/graphics/overlays/
loading_frame_layout.h`, `.../loading_frame_layout.cpp`) is the ONE body
both loading screens draw through, so they cannot drift:

- `LoadingThread::drawLoadingFrame` (render thread, 0–50 % network phase,
  `application_frame.cpp`) — delegates to it.
- `OverlaysComponent::drawLoadingScreen` (loading thread, `overlays_hud_env.cpp`)
  — delegates to it.

The zone-name line sits at `barY - 24` — below the title (`barY - 40`) and
above the bar's top border (`barY - 2`) — centred horizontally, and is not
drawn when empty.

## Files

| file | change |
|---|---|
| `include/client/zone/zone_display_name.h` / `src/client/zone/zone_display_name.cpp` | NEW — the value source |
| `include/client/graphics/overlays/loading_frame_layout.h` / `.cpp` | NEW — the shared layout body |
| `include/client/graphics/loading_thread.h` / `src/client/graphics/loading_thread.cpp` | `drawLoadingFrame` gains `entryText`; body delegates |
| `src/client/graphics/overlays/overlays_hud_env.cpp` | `drawLoadingScreen` delegates to the shared body |
| `src/client/graphics/renderer/renderer_init.cpp` | `hideLoadingScreen` clears the stored line |
| `src/client/zone/zone_entry.cpp` | `OP_NewZone` → `noteNewZone()` |
| `src/client/zone/zone_component.cpp` | approved zone change + zone-line trigger → `refresh()` |
| `src/client/login_world/login_world_component.cpp` | the entering character's zone → `refresh()` |
| `CMakeLists.txt` | registers the two new TUs + the scenario TU |
| `tests/test_zone_display_name.cpp` (+ `tests/cmake/ui_rendering.cmake`) | NEW — lookup unit + pixel-geometry tests |
| `src/client/scenario/scenario_loading_name.cpp` (+ registry, `scenario.h`/`scenarios.h`, `application.{h,cpp}`, `scenario_application_wiring.cpp`, `tests/cmake/core_utils.cmake`) | NEW — the `loading_name` scenario arm |
| `scripts/scenario_configs/loading_name.{json,post.sh}` | NEW — the lane rig |
| `docs/module_map.md` | rows for the two new TUs |

## Tests

- **Lookup unit + pixel geometry** — `tests/test_zone_display_name.cpp`
  (`test_zone_display_name`): unknown id → empty; entry line format; the
  leading "The" kept verbatim; the `OP_NewZone` override shadows the era
  table (and an empty override clears it); `refresh()`/`noteNewZone()`/
  `clearEntry()` store + read back the shown line; `noteNewZone` re-derives
  only for the shown zone; the era table's `North Qeynos` row (self-skips
  if the TSV does not resolve). The pixel leg draws a frame with and without
  a name on the surfaceless GLES2 path and asserts the name line is centred
  (|left − right margin| ≤ 2 px), above the bar, and the ONLY difference from
  the empty frame. Self-skips without a device.
- **Scenario arm** — `scripts/scenario.sh loading_name` (lane 1–4): zone in,
  then a GM zone request to zone 2; `loading_name.post.sh` asserts BOTH
  loading-screen INFO lines made it to the log.

## Not verified / left open

- **Red-at-the-parent / green-at-the-head:** the unit + pixel tests are new,
  so they cannot be red at the parent commit — they simply do not exist there.
  Their green at this head is what CI's `build-test` proves. (The card's
  "red at the parent" is satisfied only in the sense that the parent has no
  such tests / no line drawn.)
- **The pixel leg's exact geometry** is asserted at the test's own 400×300
  window (not the 1024×768 gate size) — centring + "above the bar" hold at
  any size; the absolute `barY` is size-relative.
- **The live lane run** (`loading_name` on a lane) was NOT executed from
  this hermes-ext box: the eqemu lane stack must be brought up (a lane 1–4
  lock), which the machine's not in this is not available from here. The
  arm is wired end-to-end (scenario + rig + post-check) and is what a lane
  runs. Will/ops runs it with:
  `SCENARIO_CLIENT_ARGS="--scenario-zone-id 2 --scenario-zone-name qeynos2"
  EQEMU_LANE=1 bash scripts/scenario.sh loading_name --timeout 300 --unit B1088`
  (the character in `loading_name.json` must be a GM for the zone request to
  be approved — a non-GM is refused and the scenario times out to a loud
  FAIL, by design).
- **`data/era_zones.tsv` col 4 = the long name** (id in col 2, short in col
  3) — confirmed against the character-select loader's parse; `LoadEraZoneLongNames()`
  is the one parser reused, not a second copy.
