# B1081 — no character-select flash on a zone-change world reconnect (#401)

Status: complete (this branch). Live leg: `zone_change_no_flash` scenario,
`scripts/scenario_configs/zone_change_no_flash.json`, lane 1.

## The bug

A `--character-select` run keeps `m_select_screen` ON after its first Enter
World (the screen only goes down for good once `OP_EnterWorld` is out — the
service's `HasSentEnterWorld()` early-return in `tick()`). A zone change then
tears the world session down and `ReconnectWorld()` resets
`m_enter_world_sent` to false (login_world_char_select.cpp:60). The zone-change
world pass does NOT re-send the character list — eqemu's world skips
`OP_SendCharInfo` when the zoning byte is 1 (akk-stack world/client.cpp:516) —
but the stale list is still held, and for the frames of the reconnect
`CharacterSelectService::tick()` re-activated the channel from it: the
character-select screen flashed up on top of the loading screen. Measured on
the XU4 (issue #401).

## The fix

One gate, no new state: `LoginWorldComponent::IsZoningReconnect()` is
`ctx_.Zone().ZoneToZoneInProgress() && !m_enter_world_sent` — true from the
latch (set in `ProcessDeferredZoneChange`, zone_component.cpp:646, latched
across the teardown) until the zoning pass's `OP_EnterWorld` goes out, false
for a fresh login, the B1005 camp return and the B1072 give-up (both clear
the latch before their reconnect). `CharacterSelectService::tick()` returns
early while it is true.

The derivation is the fix. No `m_select_screen` mutation was added (the
earlier `SuppressCharacterSelectScreenForZoneChange()` was removed — it could
mask the B1005 gate if the latch were ever wrongly true); no new packet
parsing, no new storage.

`IsZoningReconnect()` is defined in login_world_component.cpp, not the
header: the zone header forward-declares `class EverQuest;`, and including it
from the login-world header before eq.h's definition makes the two
declarations disagree in every TU.

`CharacterSelectService::Activations()` counts inactive→active channel
transitions — the game-thread equivalent of the render thread's
`[CHARSELECT] screen up` log line, so a headless `--scenario` run can assert
the screen never came up a second time.

## The scenario

`zone_change_no_flash` (src/client/scenario/scenario_zone_change_no_flash.cpp):
a GM character, the screen opted in for the first time via
`IScenario::WantsCharacterSelectScreen()` (initScenario creates the
`CharacterSelectChannel` and turns the screen on), the pre-flight wait
skipped via `IScenario::WantsPreFlight() = false` (the client never
auto-zones-in while the screen is up — the scenario presses its own Enter
World). It lists, enters world, `/gmzones` to `--scenario-zone`
(scenario.sh's `--zone`, default 303 — must differ from the character's zone),
and PASSES only if `CharacterSelectActivations()` stayed at 1. Exit code 0 =
the assertion held; 1 = the flash recurred; 2/3 per the usual taxonomy.

Live leg: `bash scripts/scenario.sh zone_change_no_flash --timeout 120
--zone 303 --unit B1081` against a running lane (the config pins lane 1's
host, the GM character Testchar, zone 2). B1005's
`camp_to_charselect` must stay green: a camp return IS a character-select
stage (Activations there goes 1 → 2, and that is asserted by the unit test
and by B1005's own live scenario).

The live leg names its character through `IScenarioWorld::CharacterSelectName()`
(scenario.h) — the select screen's cursor entry
(`LoginWorldComponent::GetCharacterSelectIndex()`) or the run's configured
character (`Application::ConfiguredCharacter()`) when the list is still owed.
Game-state's player name is empty before the first zone-in, so `PlayerName()`
is the wrong source at the select screen; the first live run failed at
t=1.4 s with an empty `Enter World as  was refused` until the scenario read
the character this way. The gain of the production caller demotes
`GetCharacterSelectIndex()` out of check_zero_callers.py's `EXEMPT_TESTS`
table (tests-only → reached in production); the fix commit removes the row.

## The /gmzone confirm (live leg, lane 4, 2026-10-17)

The first lane-4 live run showed the GATE working end to end — `Enter World
Testchar`, the screen up exactly once (Activations == 1) — but the zone
change itself never happened: the server answered `OP_GMZoneRequest` with
success=1 ("GM zone request approved for zone 303") and then nothing zoned,
and the scenario timed out on its transition deadline.

The oracle wire shows why. `OP_GMZoneRequest` (0x1306) is only the QUERY
half: eqemu's `Handle_OP_GMZoneRequest` (akk-stack zone/client_packet.cpp
7066-7130) looks up the target zone's safe coords and replies with them
plus a success flag. The CHANGE is a second, one-word packet — the client
sends `OP_GMZoneRequest2` (0x244c, `patch_Titanium.conf` line 138) carrying
the uint32 zone id, and eqemu's `Handle_OP_GMZoneRequest2`
(client_packet.cpp 7132-7143) is what performs it: `GoToSafeCoords(zonereq,
0)` (zoning.cpp 1074) → `MovePC(..., ZoneToSafeCoords)`. fable's
`ZoneProcessGMZoneRequest` only logged the approval, so a live
`/gmzone <char> <other_zone>` was a silent no-op for every cross-zone
move — and `/gmzone` is the only on-demand cross-zone trigger, so the
scenario could never reach its transition arm (and therefore the flash
window) on any lane.

Fix: on an approved reply, `ZoneProcessGMZoneRequest` (gm_component.cpp)
builds and queues `OP_GMZoneRequest2` with the reply's zone id; a denied
reply (success=0) sends nothing and keeps the "Zone request denied."
system message. The new `HC_OP_GMZoneRequest2 = 0x244c` in eq_opcodes.h
cites both server halves. `Test_GetLastGMZoneRequest2` is the T217/T229
seam (opcode + zone word + raw bytes), classified in
check_zero_callers.py's EXEMPT as "seam".

Lane 4 detail: its eqemu DB ships only zones 1 and 2, so the lane-4 leg
targets `--zone 2` (qeynos) from Testchar's home 1 (freporte) — a genuine
cross-zone change, which is what the scenario requires (a same-zone move
relocates in place and has no world reconnect to gate).

## The tests

tests/test_zone_change_no_flash.cpp — the gate over the real EverQuest rig
(five: the four states of the derivation; the fresh login at exactly one
screen-up; the zone-change reconnect at zero; the camp return and the B1072
give-up each still at +1) and the scenario over a fake world (four: the
opt-in/target validation, the pass, the fail on a second screen-up, the fail
when the zone change never completes). The reconnect's flag reset is modeled
with `Test_EnterWorldSentForSeam(false)` — `ReconnectWorld` itself needs a
stored world host the rig never gets, and its other side effects (a fresh
socket) are irrelevant to the gate.

`Test_SetZoneToZoneInProgressForSeam` (zone_component.h) already existed and
is reused; the new `Test_EnterWorldSentForSeam` is the only new seam,
classified in check_zero_callers.py's EXEMPT.

tests/test_gm_component.cpp gains the /gmzone-confirm quartet over the same
rig: the approved reply builds `OP_GMZoneRequest2` with the exact oracle
opcode (0x244c) and the reply's zone word; a denied reply sends nothing but
still delivers "Zone request denied."; a short reply is ignored.
