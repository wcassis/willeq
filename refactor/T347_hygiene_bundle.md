# T347 — Five small board items in one unit (B008, B012, B013, B029, B051)

Class B hygiene, each a few lines, disjoint files. Slot1. One commit per
item so a revert is surgical.

| item | file | change | proof |
|---|---|---|---|
| B008 | `src/common/net/daybreak_connection_send.cpp` `ValidateCRC` `default:` | a `LOG_WARN` naming the `crc_bytes` value before `return false` (or an assert if the caller can only pass 2/4 — measure the callers; if provably unreachable, `assert` + comment and say so) | a test feeding `crc_bytes = 3` sees the log (or the assert) |
| B012 | `src/client/action/command_processor_registration.cpp` | register a `/cleartarget` command reaching `ActionDispatcher::clearTarget` in the family whose banner fits (⚠ G03's order golden `command_registration_order_g03.txt` learns the new registration IN PLACE at its position, both faults re-planted) | the merged-help test lists it; a scripted `--script` run clears a target (headless test via the command path) |
| B013 | same file | register `/newskills` in the game registry (the renderer-side toggle stays; the registry entry routes to it the way other renderer-facing commands do — read how `/skills` routes) or rename+merge with `/skills` if they are the same feature — measure which is the working one (T143 said `/newskills` is the ONLY working toggle) | `/help` lists it; the merged-help test |
| B029 | `src/client/movement/movement_state.cpp` `m_water_surface_z` | zero readers → delete the member and its writes (§5; check `pushEvent` first) | build; the movement tests |
| B051 | `src/client/application.cpp` `initConstrainedRendererConfig` `customMemorySpec` | write-only → delete, or read it where the preset is applied if that was the intent (read the surrounding code; if the preset path ignores it by design, delete) — ⚠ G04's order golden learns the change in place | the init-order test |

Each: pre-fix evidence (the grep/test that shows the gap), the change,
the test, the board strike. Territory as listed plus the two goldens (in
place only), `refactor/BOARD.md`. No graphics files: gate + ARM skipped
with evidence.

STOP: an item needs more than a few lines or a design choice (B013's
merge question if `/skills` and `/newskills` differ in behaviour) —
leave it and say so.

## Review

Five items, five commits, one per item. Full build exit 0; `ctest -j6`
3827/3828 pass with only `AudioManagerTest.PreloadCommonSounds` failing (the
sanctioned ALSA flake) and 17 pre-existing skips; `check_file_sizes.sh --all`
878 ok / 0 warn / 0 FAIL. `git diff --name-only $(git merge-base fable HEAD)..HEAD`
lists no file under `src/client/graphics/`, no shader and no ARM/EGL path, so
the gate and ARM runs are skipped on that evidence. (The visual gate ran anyway
as part of ctest and passed, 26.3 s.)

### B008 — `ValidateCRC`'s `default:` arm

**Gap.** `grep -rn 'm_crc_bytes' src/ include/` showed the arm was NOT
provably unreachable: `daybreak_connection_decode.cpp:482` assigns
`m_crc_bytes = reply.crc_bytes;` straight off the wire in the
`OP_SessionResponse` handler, and `crc_bytes` is a plain `uint8_t` the SERVER
picks (`include/common/net/daybreak_structs.h:52`). A server negotiating any
width but 2 or 4 therefore made every subsequent packet fail validation with
nothing in the log. So: a log line, not an assert.

**Change.** `LOG_WARN(MOD_NET, "Session [{}] rejecting packet: unsupported
crc_bytes={} (only 2 and 4 are implemented)", ...)` before the `return false`,
plus a comment recording why the arm is reachable.

**Test.** `DaybreakCrcCaptureTest.UnsupportedCrcBytesIsLoggedNotSilent`
(`tests/test_daybreak_crc_capture.cpp`). Brings up the session through the
REAL recorded `OP_SessionResponse` and the production `ProcessPacket()`, then
(a) validates a recorded packet at the negotiated width and asserts nothing
warns, (b) forces `m_crc_bytes = 3` and asserts the rejection names
`crc_bytes=3`. Log capture follows the `test_constrained_texture_cache.cpp`
idiom (`CaptureStdout` + `FlushThreadLog` + poll for the 50 ms drain thread).
**Planted fault:** rewording the message to "unsupported width {}" — the test
fails. No golden.

### B012 — `/cleartarget`

**Gap.** `grep -rn 'clearTarget' src/ include/` returned exactly three
production sites: the `IActionHandler` implementation
(`eq_action_handler.cpp:246`), the dispatcher
(`action_dispatcher.cpp:241`), and ONE caller —
`input_action_bridge.cpp:90`, the `ClearTarget` keybinding. Nothing in
`command_processor_registration.cpp`, so `--script` (and piped stdin, and the
chat box) could set a target via `/target` and never drop one.

**Change.** `/cleartarget` (no alias — `ct` is unclaimed, but short aliases are
what T097/T098's shadowing audit was about; category `Combat`) registered in
`registerCombatCommands()` immediately after `/target`, with
`CommandProcessor::cmdClearTarget` forwarding to
`m_dispatcher.clearTarget()` — the same entry point the keybinding uses, so
the handler check and the "Target cleared" message are not duplicated.

**Tests.** `CommandProcessorTest.T347_ClearTargetIsRegisteredInTheCombatCategory`
and `.T347_ClearTargetDispatchesToTheHandler`
(`tests/test_command_processor_combat_guild.cpp`) — the second targets an
entity, clears the recorder, runs `/cleartarget` and asserts
`calledOnceWith("clearTarget()")`. `test_merged_help` (24 cases) still green.

**Golden learn (G03, in place).**
`tests/goldens/command_registration_order_g03.txt`: entry `022 cleartarget`
INSERTED after `021 target`; old 022–142 renumbered 023–143; count 142 → 143.
Named in the golden's own header under a `LEARNED EDITS` block and in the
`kGoldenSize` comment in `tests/test_command_registration_order.cpp`. NOT
regenerated. **Both faults re-planted:** (1) delete the new registration →
`RegistrationOrderMatchesThePreG03Golden` fails with "registration count
changed: 143 -> 142" and `TheFamiliesPartitionTheGolden` fails too; (2) swap
`registerCombatCommands()` with `registerSocialCommands()` in
`registerBuiltinCommands()` → the position clause fails at 21 ("golden:
target, cleartarget, attack, stopattack / actual: trade, accept,
tradedecline, canceltrade"). Guard unblunted.

### B013 — `/newskills` — LEFT OPEN (the entry itself closed as stale)

No code change; the plan's STOP applies. Three claims measured:

1. *"invisible to `/help`"* — **false since T223.** Registry 3's static
   catalog carries it: `src/client/help/renderer_command_catalog.cpp:62`,
   `{"newskills", {}, "Toggle the skills window"}`, and
   `MergedHelpTest.T223_NewSkillsIsFoundByHelp` /
   `.T223_OverviewListsGraphicsCategoryWithTag` already pin that `/help
   newskills` resolves and that the overview lists it.
2. *"invisible to tab completion"* — a **phantom**. `getCompletions()` has
   ZERO production callers in either registry (`grep -rn getCompletions
   src/ include/` returns the two definitions and one declaration, nothing
   else), so NOTHING in the client is tab-completable. Boarded separately as
   **B057**.
3. *"reachable only by a user who knows the word"* — **false.** `--script`
   and piped stdin both run `Application::DispatchSlashCommand`, whose
   registry-3 stage is the renderer bridge (T29,
   `application_commands.cpp:290`), so `/newskills` is scriptable today.

What is genuinely left is the plan's named STOP: the merge. `/newskills`
(`renderer_slash_commands.cpp:422`) and the bare `skills` keyword piped stdin
recognises (`console_input_handler.cpp:432` → `InputAction::ToggleSkills` →
`renderer_frame_input.cpp:222`) toggle the SAME `skillsPopupState_.isOpen`,
but they are **not equivalent**: `/newskills` additionally closes the
spellbook and the inventory popup and echoes to chat; the keyword does
neither and only `LOG_INFO`s. And `/skills` typed WITH a slash is unknown to
all three registries, so the real client's name for the window does nothing.
Picking the name and picking which body survives is a product call, so B013
is struck as stale-with-measurement and the choice is boarded as **B054 (class
C)** for Will.

### B029 — `MovementComponent::m_water_surface_z`

**Gap.** `grep -rn 'water_surface\|waterSurfaceZ' src/ include/ tests/` gave
four occurrences: the declaration
(`movement_component.h:170`), the local `waterSurfaceZ` (`movement_state.cpp:588`),
its one assignment in `UpdateWaterState()`'s OnSurface arm (`:651`), and the
store on state change (`:662`). Zero reads. `pushEvent` checked first as the
entry required: the `SwimmingStateChanged` event this same function publishes
carries `SwimmingStateChangedData{isSwimming, swimSpeed, isLevitating}`
(`include/client/state/event_bus.h:702`) and no height, so the value reached
no consumer by that route either.

**Change.** Member, local and store deleted. The OnSurface arm keeps a comment
naming the estimate that was there (`PosZ + HEAD_HEIGHT_OFFSET/2`) and saying
a reader comes before the estimate comes back.

**Tests.** A deleted member has no runtime handle, so the net is two source
guards in `tests/test_movement_component.cpp`, each with a non-vacuity
assertion first: `MovementWaterStateGuard.NoUnreadWaterSurfaceHeightIsStored`
(the header declares no `m_water_surface_z`; the TU computes no
`waterSurfaceZ`) and `.SwimmingEventCarriesNoSurfaceHeight` (the payload
struct's body, read from `event_bus.h` rather than guessed from `sizeof`,
which padding makes a lie: no "urface" and exactly three members). **Planted
fault:** re-declare `float m_water_surface_z = 0.0f;` in the header — the
first guard fails. No golden (G06's `movement_order_g06.txt` pins
`renderer_player_physics.cpp`, not this TU).

### B051 — `customMemorySpec`

**Gap.** `grep -rn 'customMemorySpec' src/ include/ tests/` gave exactly two
production occurrences — `application.cpp:503` (`bool customMemorySpec =
false;`) and `:507` (`customMemorySpec = true;`) — and no read.

**Change.** Deleted, not read. The plan allowed "read it where the preset is
applied if that was the intent"; the surrounding code says there is no such
intent — BOTH arms of the enclosing `if/else` call
`builtConfig.loadJsonOverrides(config.constrainedPreset,
"config/constrained_presets.json")` identically afterwards, so the flag
distinguishes nothing, and each arm already `LOG_DEBUG`s which path it took. A
comment at the declaration site records that.

**Test.** The four `ApplicationInitOrderTest` cases.

**Golden learn (G04, in place).**
`tests/goldens/application_init_order_g04.txt`: entries `163 bool
customMemorySpec = false;` and `166 customMemorySpec = true;` DELETED, 164–316
renumbered, count 316 → 314. Named in the golden's own `LEARNED EDITS` header
block and in the `kGoldenSize` comment in
`tests/test_application_init_order.cpp`. NOT regenerated. **Both faults
re-planted:** (1) drop `m_graphicsInitialized = false;` from `initGraphics()`
→ "startup statement count changed: 314 -> 313"; (2) swap
`initConstrainedRendererConfig(config)` with `initRendererAndGraphics(config)`
→ "startup order diverges at statement 162". Guard unblunted.

## Left open

* **B013's merge** — boarded as **B058 (class C)**, needs Will: rename
  `/newskills` to `/skills` and keep one body, or keep both and say why. The
  two bodies differ (popup closing + chat echo), so they cannot be merged
  silently.
* **B057** (new, class B) — `getCompletions()` in both registries: implemented,
  tested, zero production callers. Either an input path calls one or both go,
  with their tests. Not in this unit's territory.
