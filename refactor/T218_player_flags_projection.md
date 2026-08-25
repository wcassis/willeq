# T218 — project player AFK/anon flags (and settle the mouse-look heading read)

Slot: slot1. x86. OWNS eqemu (two-client probe). Decision: docs/decisions.md 2026-08-25 GameState projection.

## Defect
`Application::m_gameState->player()` never receives `setAFK`/`setAnonymous`/
`setRoleplay` — only `EverQuest::m_game_state.player()` does
(`eq_player_info.cpp` ~787-870). So `ActionDispatcher::toggleAFK`
(`action_dispatcher.cpp:809`) and `CommandProcessor` `/anon`
(`command_processor.cpp:1144`) read a permanently-false flag and compute
`newState = true` on every call: the toggles cannot turn OFF via those paths.

## Scope
1. Add a `PlayerFlagsChanged` (or reuse an existing appearance event if one
   already carries AFK/anon/roleplay — check `event_bus.h` first) projection
   case in `GameStateSyncBridge` → `m_state.player().setAFK/...`. Publisher
   side: wherever `EverQuest` already sets its own flags (the SpawnAppearance
   handlers), publish through `m_bridge` like the other projected domains.
2. Test in `tests/` that (a) the projection lands and (b) fails when the
   case is removed — MEASURE red/green.
3. Live: two clients. Testchar `/afk` → second client's `/who` or spawn
   appearance shows AFK; Testchar `/afk` again → clears. Same for `/anon`.
4. Heading: `input_action_bridge.cpp:220,254` reads `m_state.player().heading()`
   which is never projected (always 0). Determine at RUNTIME whether
   `InputActionBridge::processMouseInput` is reached in the graphical build
   (a LOG_DEBUG or breakpoint during mouse-look). If reached: project
   `PlayerMoved` heading too and note it. If never reached: do NOT fix — record
   it in deferred.md as dead-path candidate for a delete unit. Either way,
   report the answer with evidence.

## Territory
`src/client/bridge/game_state_sync_bridge.cpp`, `include/client/state/event_bus.h`
(additive only), the `eq_player_info.cpp` flag setters (publish only), tests.
Do NOT touch doors, `action_dispatcher.cpp` door methods, or `eq_entities.cpp`
(T219's territory).

## STOP
If the flags turn out to be projected by some path this plan missed, stop
and report the path — do not add a duplicate.

## Review (executor, 2026-08-25, slot1, branch `task/T218-player-flags` off fable `3f5eaf30`)

Evidence: `/mnt/projects/fable/review/T218/` (README there).

### What was wrong (confirmed live BEFORE the fix, `before_prefix_actor_*`)

Actor = pre-fix binary (`/mnt/projects/fable/main/build/bin/willeq`), observer
= Oraclerogue on the fixed binary, arena, both headless:

| step | actor CMD outcome | observer (`Entity 33 ...`) |
|---|---|---|
| `/afk` #1 | `AFK enabled` | `AFK set to true` |
| `/afk` #2 | **`AFK enabled`** (again) | **nothing** — never cleared before despawn |
| `/anon` #1 | `Anonymous enabled` | `anon_status set to 1` |
| `/anon` #2 | **`Anonymous enabled`** (again) | **nothing** |
| `/roleplay on` / `off` | enabled / disabled | `anon_status 2` then `0` (explicit-arg path, unaffected) |

Exactly the plan's diagnosis: `toggleAFK()`/`/anon` read
`Application::m_gameState->player()`, which nothing ever wrote, so
`!false == true` every time and `EverQuest::SetAFK(true)` early-returned on
the second call (no SpawnAppearance sent — the observer is the proof).

### Fix (scope 1)

- `include/client/state/event_bus.h`: new `PlayerFlagsChanged` enumerator +
  `PlayerFlagsChangedData{afk, anonymous, roleplay}` (full set every time,
  T81 republish-the-struct pattern). Additive only. `event_bus.h` did not
  already carry AFK/anon/roleplay on any event (checked:
  `EntityAppearanceChangedData` is race/gender/face/equipment only).
- `src/client/eq_player_info.cpp`: file-static `PublishPlayerFlagsChanged()`
  called from `SetAFK`/`SetAnonymous`/`SetRoleplay` — the only writers of
  `m_is_afk`/`m_is_anonymous`/`m_is_roleplay` — right after each sets
  `m_game_state` and before the SpawnAppearance send.
- `src/client/bridge/game_state_sync_bridge.cpp`: `PlayerFlagsChanged` case ->
  `m_state.player().setAFK/setAnonymous/setRoleplay`.
- STOP condition checked: `grep setAFK(|setAnonymous(|setRoleplay(` across
  src/include finds only the `EverQuest::m_game_state` writes in
  `eq_player_info.cpp` and the `PlayerState` definitions — no other path
  projected these flags. No duplicate added.
- Not touched: `eq_entities.cpp` (T219's; the inbound `AT_AFK`/`AT_ANONYMOUS`
  SpawnAppearance cases there update entity flags, not the player's), doors,
  `action_dispatcher.cpp`.
- `IrrlichtBridge`'s switch has no case for the new enumerator (it has no
  `default:`; -Wswitch is not -Werror, no warning appeared in
  `build_full.log`) — same status as `AutoAttackChanged`/`EntityStateSnapshot`
  per CLAUDE.md. `include/client/state/CLAUDE.md`'s "101 event types" is now
  102; not edited (docs-tracker territory, T215-style refresh).

### Test (scope 2) — measured red/green

`tests/test_game_state_sync_bridge.cpp`, 4 new cases. With ONLY the
projection case removed from `game_state_sync_bridge.cpp` (publisher, event
and tests kept): **3/4 FAIL** — `PlayerFlagsChangedProjectsAfkOn`,
`PlayerFlagsChangedAfkOnThenOffRoundTrips`,
`PlayerFlagsChangedProjectsAnonymousAndRoleplayIndependently`
(`Value of: state->player().isAFK() Actual: false Expected: true`);
the 4th (`...StillDelegatesToWrappedBridge`) passes either way by design.
Restored: 4/4, file suite 62/62.
`unit_test_red_with_projection_case_removed.txt` /
`unit_test_green_with_projection.txt` / `unit_test_green_full_suite.txt`.

### Live (scope 3) — two clients, eqemu lock held T218, arena

Actor = fixed binary (`after_fixed_actor_*`), same scripts, same observer:

| step | actor CMD outcome | observer (`Entity 35 ...`) |
|---|---|---|
| `/afk` #1 | `AFK enabled` | `AFK set to true` |
| `/afk` #2 | **`AFK disabled`** | **`AFK set to false`** |
| `/anon` #1 | `Anonymous enabled` | `anon_status set to 1` |
| `/anon` #2 | **`Anonymous disabled`** | **`anon_status set to 0`** |
| `/roleplay on` / `off` | enabled / disabled | `anon_status 2` then `0` |

PASS. Observer evidence is the server's rebroadcast SpawnAppearance, not the
actor's belief. `/who` was not used for the observer read: willeq's
`ZoneProcessWhoAllResponse` prints only the header/count and never parses
entries (pre-existing, `eq_player_info.cpp` ~1140), so it cannot show AFK/ANON
flags — the SpawnAppearance log line is the stronger signal anyway.

### Heading (scope 4) — runtime answer: NOT reached; NOT fixed; recorded in `deferred.md`

Probe: fixed binary, `--egl-x11 -r 1024 768` under `Xvfb :97`, Testchar,
`--log-level=DEBUG`, a `[T218]` LOG_DEBUG inside the `deltaX != 0` branch of
`InputActionBridge::processMouseInput()`; after "Fully connected to zone!" +
40 s, six xdotool left-button drags of 400 px across the window (120
MotionNotify events). Result (`heading_probe_A_testchar_eglx11.log`,
`heading_probe_driver.txt`):

- renderer mouse-look worked: 12 `[ROT-CAMERA] setFollowPosition: heading=`
  changes, +100 per drag (80 -> 180 -> 280 -> 380 -> 480 -> 68 ...).
- `[T218]` line count: **0**.
- `processMouseInput()` itself IS called every game tick (no
  `InputActionBridge::update: SKIPPED` line; `GraphicsInputHandler` was
  connected at 15:57:33). The branch is unreached because both threads
  consume the same destructive `RendererEventReceiver::getMouseDeltaX()`;
  the render thread pumps X events and reads the delta in the same
  `processFrame()`, leaving a microsecond window the game thread never hit
  in 120 events.
- Bonus finding: the OTHER `heading()` read (line ~220) is in
  `processContinuousInput()`, which has **zero callers** (`update()` dropped
  it deliberately). Structurally dead.

Per the plan: no `PlayerMoved` projection added. Entry appended to
`refactor/deferred.md` ("`InputActionBridge` heading path is dead ...") as a
delete-unit candidate. The `[T218]` LOG_DEBUG is left in place (DEBUG-gated,
one line) so the delete unit can re-run the same probe.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | clean (`build_full.log`) |
| ctest (GLES2 tree) | **2758/2759** — sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA). 2755 -> 2759 = the 4 new cases (`ctest_full.log`) |
| `build-gles2-x86.sh` | OK (`build_gles2_x86.log`) |
| visual gate | **PASS**, goldens untouched (`visual_gate.log`, `git status` shows only the 5 source/test files + this plan + deferred.md) |
| `build-arm-noble.sh` | OK (`build_arm_noble.log`) |
| `check_file_sizes.sh fable` | 5 files checked, 0 FAIL; `eq_player_info.cpp` WARN pre-existing (2104 -> 2125 lines) |

### Server state / deviations

- Testchar restored to zone 36 (-77.9232, 21.9086, 3, h190) gm=0 anon=0;
  Oraclerogue to zone 2 (167, 30, -29.25, h384); `db_before/staged/restored.txt`.
  eqemu down (`eqemu_down.log`), lock deleted.
- Deviation: the "before" actor is `main/build/bin/willeq` (a pre-fix build
  of the same tree family, dated 12:02 today) rather than a rebuilt pre-fix
  slot1 binary; the observer in both runs is the fixed binary — the observer
  code path (`AT_AFK` entity handling) is untouched by this unit.
- Deviation: three live legs used `/roleplay on|off` in addition to the
  plan's `/afk` and `/anon`; roleplay is explicit-arg and was never broken,
  included only to show the projection covers all three flags.
- `data/captures/t218_*` (configs, scripts, drivers) live behind the
  `data/captures` symlink, outside the repo; copies are in `review/T218/`.
