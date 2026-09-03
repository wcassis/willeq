# K09 — `ZoneComponent` (family: zone — connection lifecycle, handshake, entry, `DispatchZonePacket`)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED). **Runs
ALONE after K08 and K10 merge** — it is the family every other component's
handlers are dispatched from, and it owns the zone flags the extracted
components currently reach as `owner_.m_*`.

## Measured at `feb6b10b` (2026-09-03) — re-measure at your tip

Four TUs, 35 bodies: `eq_zone.cpp`, `eq_zone_dispatch.cpp` (`DispatchZonePacket`,
880 lines, moves WHOLE — Batch G decides its decomposition, not this unit),
`eq_zone_entry.cpp`, `eq_zone_handshake.cpp`. 103 members referenced, 32
single-family.

**What this unit is really doing:** after K01–K08 and K10, `DispatchZonePacket`
is a switch whose arms call `m_<family>->ZoneProcess*(p)`. Moving it into
`ZoneComponent` turns those into `owner_.<Family>().ZoneProcess*(p)` — a
mechanical rewrite of ~100 arms, and the last big block of `eq.h`
declarations goes with it.

Members:
- **move** (single-family, 32): every `m_*_sent` / `m_*_received` handshake
  flag, `m_zone_sequence`, `m_zone_archive`, `m_zone_wld`, `m_maps_path`,
  `m_safe_x/y/z`, `m_gm_mode` (+ `GetGMMode()` accessor — 4 external readers,
  retarget), `m_test_force_zone_out_handshake`,
  `m_test_last_zone_change_request` (+ `Test_` accessors), `m_zoning_enabled`.
  Some names in the single-family list are OTHER classes' members appearing
  in lambdas (`m_id`, `m_instance`, `m_pos`, `m_Position`, `m_speed`,
  `m_spells`, `m_bank`, `m_mode`, `m_packet_type`, `m_offset_annotated`,
  `m_zone`, `m_profile_appearance`, `m_spawn_id`, `m_structs`) — verify each
  against `eq.h`; only `EverQuest` members move.
- **move, and retarget the components that read them as `owner_.m_*`**: the
  zone flags K06/K07 left behind (`m_req_new_zone_sent`, `m_server_filter_sent`,
  `m_zone_entry_sent`, `m_weather_received`, `m_player_graphics_entity_pending`,
  `m_send_exp_zonein_received`, `m_exp_zonein_sent`, `m_zone_out_*`,
  `m_zone_to_zone_in_progress`, `m_zone_lines`, `m_movement_history`,
  `m_last_movement_history_send`, `m_replay_suppressed_sends`, `m_exp`,
  `m_exp_aa`, `m_aa_table_count`, `m_tribute_count`, `m_guild_tribute_count`,
  the scribe trio `m_pending_scribe_*`, `m_navmesh_path`). Each becomes a
  small public getter/setter on `ZoneComponent` or a public member — prefer
  the accessor; record the choice per member. Readers: `entities/`,
  `movement/`, `merchant/`, `player/`, `login_world/` components, `eq_bridge*.cpp`,
  `eq.cpp`.
- **stay** (bridge/renderer-facing zone environment: `m_zone_map`,
  `m_zone_bsp_tree`, `m_zone_bsp_tree_zone_name`, `m_zone_fog_*`,
  `m_zone_sky_type`, `m_zone_type`, `m_eq_client_path`, `m_zone_audio_manager`,
  `m_hotbar_model`, `m_chat`, `m_guild`, `m_gm` (component pointers),
  `m_loot_items`, `m_pending_loot_slots`, `m_guild_id`, `m_guild_rank`,
  `m_world_*` (K10 moved or kept them — read K10's Review). These go to
  K11 (bridge) or K13.

Callers by compiler. `DispatchZonePacket` has external callers in `eq.cpp`
(the receive callback) and `eq_player_social.cpp`; `CleanupZone` /
`LoadZoneSources` from `application*.cpp` and `eq_bridge.cpp`; `FixZ` /
`CheckZoneLine` / `ProcessDeferredZoneChange` / `BeginGracefulZoneOut` /
`DisconnectFromZone` from the movement and player components (`owner_.` →
`owner_.Zone().`); `GetGMMode` from the action layer.

Text readers: `test_reachability_sweep.cpp` and `test_duplicate_state_stores.cpp`
scan `eq.h`; `test_zone_connection_flow.cpp` (1,367 lines) drives the
handshake through `EverQuest` `Test_*` seams — those seams move with their
members and the test's call sites are retargeted (assertions untouched).
Grep `tests/` for `eq_zone` as a string.

Predicted `eq.h` delta: −35 declarations −(32 + ~20) members −accessors
−banners +4 ≈ **−90 or better**. After K09 `eq.h` should be near 1,400.

## Territory

`include/client/zone/` + `src/client/zone/` (new), the four `eq_zone*.cpp`
(move), `eq.h`, `eq.cpp`, root `CMakeLists.txt`, `tests/test_zone_component.cpp`
+ cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in every
caller the compiler names, including inside `src/client/*/` components and
`application*.cpp`. Runs alone: no collision matrix.

## STOP conditions

- `DispatchZonePacket` needs any edit beyond `m_x->` → `owner_.X().` and the
  standard substitutions → STOP with the arm named.
- `test_zone_connection_flow` needs an assertion change → STOP.

## Verification / Review

Executed in `wt/slot3` on `task/K09-zone-component`, branched from `fable` at
`cfadb78e`. Ran ALONE. §Mechanism REVISED followed as written, including every
correction block K03–K10. **Neither of this plan's two STOP conditions fired**
— `DispatchZonePacket` needed nothing beyond the standard substitutions, and
`test_zone_connection_flow` kept every assertion. One §Mechanism 7 test
file-list edit WAS needed and is reported below.

### The three `eq.h` numbers

| | lines |
|---|---|
| at dispatch (`fable` `cfadb78e`) | **1,441** |
| plan's pre-dispatch estimate | −90 or better, "near 1,400" |
| **predicted** before any edit (23 explicit line ranges) | **1,223** (−218) |
| **measured** after | **1,226** (**−215**) |

`git diff --numstat -M` on the single file: **224 deletions, 9 insertions =
net −215**. The three-line miss is comment, not code: the note left in place of
the deleted T322 seam block (pointing the reader at `Zone()`) is three lines,
where the arithmetic assumed one. The plan's own estimate is beaten by a factor
of two and a bit, for the same reason K08's was — the plan counted declarations
and members, not the ~90 banner/comment lines that travel with them.

Where the 224 deleted lines came from:

| block | lines | contents |
|---|---|---|
| T231 test seam | 10 | the 4-line note, `struct TestZoneChangeRequest`, `Test_GetLastZoneChangeRequest()` |
| T322 test seams | 20 | the 9-line note + `Test_BeginGracefulZoneOutForSeam` / `Test_DisconnectFromZoneForSeam` / `Test_IsZoneOut*` / `Test_*ZoneToZoneInProgress`. **`Test_IsPendingLogoutDisconnect()` STAYED** — `m_pending_logout_disconnect` is movement's and player's, not zone's |
| `SetZoningEnabled` / `IsZoningEnabled` | 2 | |
| `SetNavmeshPath` / `SetMapsPath` | 2 | |
| the T148 GM-mode block | 29 | 28 comment lines + `GetGMMode()`. The single largest comment block in the header |
| `SetPlayerGraphicsEntityPending` | 1 | was inside the `EQT_HAS_GRAPHICS` block; the member never was |
| `m_zone_archive` / `m_zone_wld` + banner | 4 | |
| the private zone-stream block | 31 | `DispatchZonePacket` + its comment, 14 `ZoneSend*`, 6 `ZoneProcess*`, the K01/K02/K06 dispatch banners |
| the "Zone transition methods" block | 21 | banner, `RequestZoneChange` + its 8-line T322 note, `BeginGracefulZoneOut` + note, `RelocateWithinZone` + note, `CleanupZone`, `DisconnectFromZone`, `ProcessDeferredZoneChange` |
| the zone-state flag block | 20 | 19 members + the stale "Time of day (from server)" banner. `// Zone state` and `m_zone_session_established` stay |
| `m_player_graphics_entity_pending` | 1 | |
| the T286 character-stats block | 7 | 4 comment lines + `m_exp` / `m_exp_aa` |
| `m_safe_x/y/z` | 1 | |
| `m_navmesh_path` | 1 | |
| zone lines + zoning-enabled | 6 | 2 banners + `m_zone_lines` + `m_zoning_enabled` |
| the T322 zone-out state block | 23 | 15 comment lines, 3 flags, the deadline, `kZoneOutHandshakeTimeoutMs`, `m_test_force_zone_out_handshake` |
| the T322 zone-to-zone block | 8 | 6 comment lines + `m_zone_to_zone_in_progress` |
| pending scribe state | 5 | banner + 3 members |
| movement history | 9 | banner, `struct MovementHistoryEntry`, `m_movement_history`, `m_last_movement_history_send`, `m_maps_path` |
| the T148 `m_gm_mode` member + note | 5 | |
| `m_test_last_zone_change_request` + note | 3 | |
| the map / zone-source / zone-line / flow-helper method block | 15 | 4 banners + `LoadZoneMap`, `GetBestZ`, `FixZ`, `LoadZoneSources`, `LoadZoneLines`, `CheckZoneLine`, `CheckZoneRequestPhaseComplete` |
| **added (9)** | | `#include "client/zone/zone_component.h"` (+1), `Zone()` + 2 comment lines (+3), `friend class eqt::zone::ZoneComponent` (+1), `std::unique_ptr<...> m_zone` (+1), the 3-line note left where the T322 seams were (+3) |

`scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry lowered
**1,441 → 1,226**. `check_file_sizes.sh --all`: **819 ok, 0 warn, 0 FAIL**.

### THE CUT: 34 bodies, 32 moved, 2 deleted

`grep -nE '^[A-Za-z_].*EverQuest::'` over the four TUs at this tip returns
**34**, not the plan's 35: `zone_handshake` 14, `zone_component` 14,
`zone_entry` 5, `zone_dispatch` 1. Every one of the 32 survivors moved; nothing
in this family is a facade, so unlike K03/K08 there is no stay-behind set.

**The zone CONNECTION stays on `EverQuest`, and that is the whole shape of this
unit.** `ConnectToZone()`, the four `ZoneOn*` callbacks, `ReplayZonePacket()`
and `SafeQueueZonePacket()` are `eq.cpp` bodies, and `SafeQueueZonePacket` in
particular is called from `guild/`, `merchant/`, `movement/` and `player/` —
it is a shared service, not zone-family code. So this unit deliberately did
NOT take K10's correction-2 route of pulling a family's wiring out of `eq.cpp`:
the socket, its callbacks and the replay seam are the boundary the rest of the
tree still uses, and `DispatchZonePacket` plus the three senders those
callbacks drive are simply **public** on the component instead (K10 correction
1 — friendship runs `EverQuest → component`, so anything `EverQuest` still
calls must be public).

**Two zero-caller senders were DELETED** (§Mechanism 5, K03 correction (a)
applied — zero callers *including in-family*):

- **`ZoneSendSessionReady()`** — 12 lines. Sends `HC_OP_SessionReady` on the
  zone stream. Its only occurrences tree-wide were its own declaration and its
  own definition. It is the exact shape K10 found on the world stream
  (`WorldSendSessionReady`), one protocol layer down.
- **`ZoneSendSpawnAppearance()`** — 12 lines. Sends `HC_OP_SpawnAppearance`
  with a hardcoded `type=14, value=0`. Same: declaration + definition only.

Both dereference `m_zone_connection` **unguarded** — every live sender in this
family carries the `if (m_zone_connection) … else ++m_replay_suppressed_sends;`
gate the replay harness needs. They would have crashed in a replay run. That
they never did is the proof they are unreachable.

Deleting `ZoneSendSessionReady` left **`m_zone_sequence` with zero references
tree-wide**, so it was NOT moved (§Mechanism 3's dead-member rule, K01's
`m_guild_name` precedent). Boarded below.

### Compiler-derived caller list

The whole retarget was computed mechanically BEFORE the first build (K08
correction 4, script reused): parse `eq.h` for the class's declarations and
members, mask comments and string literals per line, classify every `m_*` into
core / other-component / mover / stays-behind, scan the movers for bare
`identifier(` against the declaration set, and emit the substitution list.
`eq.h`'s declarations and members were deleted and every call site retargeted
in ONE pass. **The build then failed with exactly ONE error** —
`EverQuest::kZoneOutHandshakeTimeoutMs` in `movement_update.cpp` (K08
correction (v): the constant travels with the family, and no member grep
finds it) — and went clean on the second compile. A clean build is the
compiler's proof that no caller was missed.

| file | sites | receiver expression | change |
|---|---|---|---|
| `src/client/eq.cpp` | 9 | bare, inside `ZoneOn*` / `ReplayZonePacket` / `CleanupZone`-adjacent code | `DispatchZonePacket` ×2, `ZoneSendStreamIdentify`, `ZoneSendAckPacket`, `ZoneSendZoneEntry` → `m_zone->`; `m_movement_history`, `m_zone_to_zone_in_progress`, `m_zone_entry_sent`, `m_exp` → `m_zone-><Accessor>()` |
| `src/client/movement/movement_update.cpp` | 15 | `owner_.` | `DisconnectFromZone`, `BeginGracefulZoneOut`, `ProcessDeferredZoneChange` ×2, `CheckZoneLine` ×3, `FixZ`, the three zone-out flags, `m_movement_history`, `m_last_movement_history_send` ×2, `kZoneOutHandshakeTimeoutMs`, `MovementHistoryEntry` |
| `src/client/movement/movement_state.cpp` | 11 | `owner_.` | `FixZ`, `m_navmesh_path`, `m_movement_history` ×8, `sizeof(MovementHistoryEntry)` |
| `src/client/entities/entities_component.cpp` | 9 | `owner_.` | `ZoneSendSetServerFilter`, `ZoneSendClientReady`, `m_server_filter_sent` ×2, `m_send_exp_zonein_received`, `m_zone_entry_sent`, `m_zone_lines` ×2, `m_player_graphics_entity_pending` |
| `src/client/entities/entities_component_updates.cpp` | 10 | `owner_.` | `ZoneSendReqNewZone`, `m_req_new_zone_sent`, `m_weather_received`, `m_exp` ×3, `m_exp_aa` ×2, `m_player_graphics_entity_pending` ×2 |
| `src/client/player/player_info.cpp` | 10 | `owner_.` | `CheckZoneRequestPhaseComplete` ×4, `ZoneSendSendExpZonein`, `m_aa_table_count`, `m_tribute_count`, `m_guild_tribute_count`, `m_send_exp_zonein_received`, `m_exp_zonein_sent` |
| `src/client/eq_bridge.cpp` | 5 | bare | `CheckZoneLine`, `m_zone_lines` ×4 (inside `CreateZoneLoadSnapshot`) |
| `src/client/eq_action_handler_extended.cpp` | 4 | `m_eq.` | `GetGMMode`, `IsZoningEnabled`, `SetZoningEnabled` ×2 |
| `src/client/player/player_component.cpp` | 3 | `owner_.` | `m_zone_out_handshake_pending`, `m_zone_out_acknowledged`, `m_zone_to_zone_in_progress` (the `OP_LogoutReply` arm) |
| `src/client/login_world/login_world_component.cpp` | 3 | `owner_.` | `DisconnectFromZone`, `m_zone_to_zone_in_progress` ×2 |
| `src/client/merchant/merchant.cpp` | 3 | `owner_.` | the pending-scribe trio |
| `src/client/application.cpp` | 2 | `m_eqClient->` | `SetNavmeshPath`, `SetMapsPath` |
| `src/client/application_zone_loading.cpp` | 2 | `m_eqClient->` | `SetPlayerGraphicsEntityPending` ×2 |
| `src/client/eq_bridge_intents.cpp` | 1 | bare | `SetZoningEnabled` (the `ZoningToggleIntent` arm) |
| `src/client/player/player_state.cpp` | 1 | `owner_.` | `RequestZoneChange` (the bind/death path) |
| `tests/test_zone_out_handshake.cpp` | 21 | `eq.` | the six T322 seams. **Assertions untouched** |
| `tests/test_zone_connection_flow.cpp` | 8 | `eq.` | `GetGMMode`. **Assertions untouched** |
| `tests/test_zone_position_convention.cpp` | 4 | `eq.` | `Test_GetLastZoneChangeRequest` |
| `tests/test_position_store_coherence.cpp` | 3 | `eq.` | `CleanupZone` |
| `tests/test_eq_action_handler_movement_utility.cpp` | 3 | `eq.` | `IsZoningEnabled` |
| `tests/test_guild_wire_rank.cpp` | 2 | `eq.` | `GetGMMode` |
| `tests/test_scribe_scroll.cpp` | 2 | `h.eq.` | `m_pending_scribe_spell_id` → `h.eq.Zone().PendingScribeSpellId()`. Reached through `#define private public`; still reachable, so **no STOP** |
| `tests/test_zoning_integration.cpp`, `test_zoning_graphics_integration.cpp`, `test_beneficial_spell_casting.cpp` | 2 each | `eq_->` | `SetMapsPath`, `SetNavmeshPath` |
| `tests/test_group_packet_shapes.cpp` | 1 | `eq.` | `CleanupZone` |
| `tests/test_eq_action_handler_transforms_real.cpp` | 1 | `h.eq.` | `ZoneSendSetServerFilter` |

**Total: 141 real call sites in 25 files** (14 production, 11 test). Every
retarget in an already-extracted component was written `owner_.Zone().Foo()`
per K06/K07 correction (r).

**Inside `DispatchZonePacket` itself** the arms became
`owner_.<Family>().ZoneProcess*(p)` for `Player()` (49), `Merchant()` (18),
`Entities()` (18), `Group()` (8), `Movement()` (7), `Chat()` (5), `Guild()`
(4), `Gm()` (3), `LoginWorld()` (11 across the family) — and stayed
`owner_.ZoneProcess*(p)` for the five combat handlers K12 still owns
(`Consider`, `Action`, `Damage`, `Stun`, `MoneyOnCorpse`). No arm needed
anything else. **STOP condition 1 did not fire.**

**Leads that resolved to nothing.** The plan named `application*.cpp` and
`eq_bridge.cpp` as callers of `CleanupZone` / `LoadZoneSources`. **Neither
calls either.** All five hits (`application.cpp:253`,
`application_zone_loading.cpp:170/187`, `eq_bridge.cpp:384/390/398/429`,
`eq.cpp:940`, `login_world_component.cpp:562/975`) are prose comments —
K04 correction (g) again. `CleanupZone`'s only production caller is
in-family (`DisconnectFromZone`); `LoadZoneSources`'s is in-family
(`ZoneProcessNewZone`), so `LoadZoneSources`, `LoadZoneMap`, `LoadZoneLines`,
`GetBestZ`, `RelocateWithinZone`, the five in-family `ZoneSend*` and the six
`ZoneProcess*` are all **private** on the component.

**Dead declarations (K10 correction s):** grepping `eq.h` for this family's
name prefixes (`Zone*`, `*ZoneChange`, `FixZ`, `GetBestZ`, `Deferred`, `Safe`)
turned up **74 names, every one either defined in the tree or an inline
accessor**. K10's two-dead-promise finding does not repeat here.

### Members

**41 members moved**, plus two nested structs (`MovementHistoryEntry`,
`TestZoneChangeRequest`) and one constant (`kZoneOutHandshakeTimeoutMs`, made
public because `MovementComponent::UpdateMovement` names it in the timeout
WARN). One member was deleted rather than moved.

#### Per-member accessor table (the flags the other components now reach through `Zone()`)

Every shared mover got a **reference-returning** getter, so each retargeted
call site is a name substitution and nothing else — no read/write asymmetry to
get wrong, and `owner_.m_x = v` / `owner_.m_x++` / `if (owner_.m_x)` all
survive unchanged in shape. The plan asked for "a small public getter/setter or
a public member — prefer the accessor"; this is the accessor form, uniformly.

| member | accessor on `ZoneComponent` | who reaches it, after |
|---|---|---|
| `m_zone_entry_sent` | `bool& ZoneEntrySent()` | `entities_component.cpp`, `eq.cpp` |
| `m_weather_received` | `bool& WeatherReceived()` | `entities_component_updates.cpp` |
| `m_req_new_zone_sent` | `bool& ReqNewZoneSent()` | `entities_component_updates.cpp` |
| `m_server_filter_sent` | `bool& ServerFilterSent()` | `entities_component.cpp` (2) |
| `m_exp_zonein_sent` | `bool& ExpZoneinSent()` | `player_info.cpp` |
| `m_send_exp_zonein_received` | `bool& SendExpZoneinReceived()` | `entities_component.cpp`, `player_info.cpp` |
| `m_player_graphics_entity_pending` | `bool& PlayerGraphicsEntityPending()` + `SetPlayerGraphicsEntityPending()` | `entities_component*.cpp` (3), `application_zone_loading.cpp` (2, via the setter) |
| `m_zone_out_handshake_pending` | `bool& ZoneOutHandshakePending()` | `movement_update.cpp`, `player_component.cpp` |
| `m_zone_out_acknowledged` | `bool& ZoneOutAcknowledged()` | `movement_update.cpp` (2), `player_component.cpp` |
| `m_zone_out_handshake_deadline` | `std::chrono::steady_clock::time_point& ZoneOutHandshakeDeadline()` | `movement_update.cpp` |
| `m_zone_to_zone_in_progress` | `bool& ZoneToZoneInProgress()` | `eq.cpp`, `login_world_component.cpp` (2), `player_component.cpp` |
| `m_zone_lines` | `std::unique_ptr<EQT::ZoneLines>& ZoneLinesPtr()` | `entities_component.cpp` (2), `eq_bridge.cpp` (4) |
| `m_movement_history` | `std::deque<MovementHistoryEntry>& MovementHistory()` | `movement_state.cpp` (8), `movement_update.cpp`, `eq.cpp` |
| `m_last_movement_history_send` | `uint32_t& LastMovementHistorySend()` | `movement_update.cpp` (2) |
| `m_navmesh_path` | `std::string& NavmeshPath()` + `SetNavmeshPath()` | `movement_state.cpp`; `application.cpp` + 3 tests via the setter |
| `m_maps_path` | `SetMapsPath()` only | `application.cpp` + 3 tests |
| `m_exp` | `uint32_t& Exp()` | `entities_component_updates.cpp` (3), `eq.cpp` |
| `m_exp_aa` | `uint32_t& ExpAA()` | `entities_component_updates.cpp` (2) |
| `m_aa_table_count` | `int& AATableCount()` | `player_info.cpp` |
| `m_tribute_count` | `int& TributeCount()` | `player_info.cpp` |
| `m_guild_tribute_count` | `int& GuildTributeCount()` | `player_info.cpp` |
| `m_pending_scribe_spell_id` | `uint32_t& PendingScribeSpellId()` | `merchant.cpp`, `test_scribe_scroll.cpp` (2) |
| `m_pending_scribe_book_slot` | `uint16_t& PendingScribeBookSlot()` | `merchant.cpp` |
| `m_pending_scribe_source_slot` | `int16_t& PendingScribeSourceSlot()` | `merchant.cpp` |
| `m_gm_mode` | `bool GetGMMode() const` (moved verbatim, with its 28-line T148 contract) | `eq_action_handler_extended.cpp`, `test_zone_connection_flow.cpp` (8), `test_guild_wire_rank.cpp` (2) |
| `m_zoning_enabled` | `SetZoningEnabled()` / `IsZoningEnabled()` (moved verbatim) | `eq_action_handler_extended.cpp` (3), `eq_bridge_intents.cpp`, `test_eq_action_handler_movement_utility.cpp` (3) |
| `m_test_last_zone_change_request` | `Test_GetLastZoneChangeRequest()` + `struct TestZoneChangeRequest` | `test_zone_position_convention.cpp` (4) |
| `m_test_force_zone_out_handshake`, and the three zone-out flags | `Test_BeginGracefulZoneOutForSeam` / `Test_DisconnectFromZoneForSeam` / `Test_IsZoneOut*` / `Test_*ZoneToZoneInProgress` (moved verbatim) | `test_zone_out_handshake.cpp` (21) |

The remaining 14 movers (`m_zone_archive`, `m_zone_wld`, `m_new_zone_received`,
`m_aa_table_sent`, `m_update_aa_sent`, `m_tributes_sent`,
`m_guild_tributes_sent`, `m_req_client_spawn_sent`, `m_spawn_appearance_sent`,
`m_client_spawned`, `m_safe_x/y/z`) are single-family and need no accessor.

#### Members the plan named that did NOT move

| member | plan's decision | measured | outcome |
|---|---|---|---|
| **`m_replay_suppressed_sends`** | move, with a retarget | **not zone state.** It is the replay harness's global suppressed-send counter: **12 writes in `login_world_component.cpp`**, 11 in this family, 1 in `player_buffs.cpp`, and its public `EverQuest::GetReplaySuppressedSendCount()` carries **49 call sites across 6 test files** (`test_login_world_flow` 15, `test_zone_connection_flow` 15, `test_login_world_seam` 8, `test_buff_click_off` 5, `test_combat_manager` 2, `test_session_replay` 2, `test_login_world_component` 2) | **⚠ STAYS — deviates from the plan.** §Mechanism 3's shared rule wins: login_world outweighs zone, and the accessor is an `EverQuest`-level replay-harness API, not a zone one. `owner_.m_replay_suppressed_sends`. Same class as K08's `m_inspect_message` and K10's `m_zone_server_*`. K13's job |
| `m_zone_session_established` | not named in either list | 1 use here, **2 in `eq.cpp`** — it is written by `ZoneOnStatusChangeReconnectEnabled`/`Disabled`, i.e. by the zone SOCKET, which stays | **stays**, `owner_.m_zone_session_established` |
| `m_zone_sequence` | move | **dead after `ZoneSendSessionReady` was deleted** — it was that body's only reader | **DELETED, boarded** (§Mechanism 3's zero-reference rule) |
| `m_id`, `m_instance`, `m_pos`, `m_Position`, `m_speed`, `m_spells`, `m_bank`, `m_mode`, `m_packet_type`, `m_offset_annotated`, `m_zone`, `m_profile_appearance`, `m_spawn_id`, `m_structs` | "verify each against `eq.h`; only `EverQuest` members move" | verified: **none of them is an `EverQuest` member reference in this family** except `m_profile_appearance`, which is one read in `ZoneProcessPlayerProfile` of a member the player/bridge families own | `owner_.m_profile_appearance`; the other 13 were never `EverQuest`'s |
| `m_pending_logout_disconnect` | not named | zero references in this family; `movement_update.cpp` + `player_component.cpp` only | **stays**, and so does `Test_IsPendingLogoutDisconnect()` — the one seam in the T322 block that did not move |
| `m_zone_map`, `m_zone_bsp_tree`, `m_zone_bsp_tree_zone_name`, `m_zone_fog_*`, `m_zone_sky_type`, `m_zone_type`, `m_eq_client_path`, `m_zone_audio_manager`, `m_loot_items`, `m_pending_loot_slots`, `m_guild_id`, `m_guild_rank` | stay (bridge/renderer-facing) | confirmed | `owner_.m_*`; K11/K13 |
| core | `ctx_.` | 12 of the 15-member core used: `m_game_state` (78), `m_bridge` (35), `m_zone_connection` (30), `m_entities` (15), `m_trade_manager` (14), `m_spell_manager` (8), `m_inventory_manager` (8), `m_character` (6), `m_zone_connection_manager` (5), `m_skill_manager` (5), `m_buff_manager` (4), `m_audio_manager` (2) | `ctx_.` |

**Inline `eq.h` accessors on a moving member (K04 correction f): fourteen** —
`GetGMMode`, `SetZoningEnabled`, `IsZoningEnabled`, `SetNavmeshPath`,
`SetMapsPath`, `SetPlayerGraphicsEntityPending`, `Test_GetLastZoneChangeRequest`,
`Test_BeginGracefulZoneOutForSeam`, `Test_DisconnectFromZoneForSeam`,
`Test_IsZoneOutHandshakePending`, `Test_IsZoneOutAcknowledged`,
`Test_IsZoneToZoneInProgress`, `Test_SetZoneToZoneInProgressForSeam`, and the
`struct TestZoneChangeRequest` one of them returns. **None is an `EverQuest::`
body, so no method count sees them, and between them they carried 47 of the 141
call sites.**

### Dependencies no lead table named

Found by cross-referencing every bare `identifier(` in the moving files against
`eq.h`'s declaration set before building: **28 stays-behind methods, 152 call
sites**, all rewritten `owner_.Foo()` — `PosZ` (23), `PosY` (19), `PosX` (19),
`MySpawnId` (15), `CurrentZoneName` (8), `HeadingDeg` (8), `RequestQuit` (6),
`SetLoadingPhase` (5), `AddChatSystemMessage` (4), `CurrentZoneId` (4),
`IsFullyZonedIn` (2), `QueuePacket` (2), `SetHeading` (2), `SetPosition` (2),
and one each of `CloseLootWindow`, `IsVendorWindowOpen`, `OnPetRemoved`,
`SafeQueueZonePacket`, `SetMoving`, `SetMySpawnId`, `SetPosLocal`,
`StopUpdateLoop`, `UpdateInventoryStats`, plus the five combat
`ZoneProcess*`.

`EverQuest`-scoped names needing the third substitution: **`s_debug_level`
(29 sites — more than K08's 23, the most of any K unit), `DumpPacket` (14),
`GetClassName` (2), `GetOpcodeName`, `GetRaceName`, `GetDeityName`.**

**`this` → `&owner_` (K06/K07 correction n): 2 sites**, both in
`ProcessDeferredZoneChange`/`DisconnectFromZone` —
`std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, this, …)`. The
bound method stayed on `EverQuest`, so the member-function pointer is
unchanged and only the instance moves. Friendship makes naming the private
handler legal from the component.

**K10's leftover, closed:** `ProcessDeferredZoneChange`'s world-connection-manager
rebuild — which K10 deliberately left in `eq_zone.cpp` reachable through
`LoginWorld()`'s public getters — moved with the body and needed **no edit at
all**. The three `std::bind(&eqt::login_world::LoginWorldComponent::WorldOn*,
owner_.m_login_world.get(), …)` lines and the five getter reads
(`WorldManager()`, `WorldConnection()`, `WorldServerHost()`, `WorldReady()`,
`EnterWorldSent()`) are byte-identical after the `m_login_world` →
`owner_.m_login_world` substitution. K10's decision to make those three
handlers public was exactly right and is now used from the component that
inherited the block.

### Multiset residual (§Mechanism 8)

Normalisation applied to both sides, longest-name-first (K04 correction i):
`owner_.<Family>().` → `m_<family>->` for the nine component accessors;
`ctx_.<core>` → `m_<core>` for the twelve core members used;
`eqt::zone::ZoneComponent::` → `EverQuest::`; then un-qualify the six
scope-only forms (`EverQuest::s_debug_level`, `EverQuest::DumpPacket`,
`EverQuest::GetOpcodeName`, `EverQuest::GetClassName`, `EverQuest::GetRaceName`,
`EverQuest::GetDeityName` — K06 correction 6); `&owner_` → `this`;
`owner_.m_` → `m_`; `owner_.` → ``; trailing `//` comments stripped (K08
correction w); comment-only, blank and preprocessor lines filtered; both sides
sorted.

Old side: the four `eq_zone*.cpp` at `cfadb78e` — **2,105 code lines**. New
side: the four `src/client/zone/*.cpp` — **2,081 code lines**.

**Residual: 0 additions, 24 deletions, ZERO modifications.** The 24 are exactly
the two zero-caller senders' 12 code lines each (signature, braces, packet
build, `DumpPacket`, `QueuePacket`, flag write). **Every other line is
byte-identical between the two sorted multisets.** For a 2,100-line move
including an 880-line switch whose every arm was rewritten, the diff is empty.
The move is verbatim.

### Text readers of the moved TUs (K03 correction c)

`grep -rn 'eq_zone' tests/ scripts/ docs/ refactor/BOARD.md` BEFORE moving
anything: **26 hits, every one a prose comment naming the old filename.** No
test reads any of the four TUs as data, the way `test_chat_channel_names.cpp`
read `eq_chat.cpp`. Repointed in-unit:

- `docs/module_map.md` rows 28/29/30/42 and the family row 78 → full new
  repo-relative paths, no "(was …)" prose (K05 correction l).
- `docs/CLAUDE.md:159` — the load-bearing heading-flip citation,
  `EverQuest::ZoneProcessPlayerProfile` in `src/client/eq_zone_entry.cpp` →
  `eqt::zone::ZoneComponent::ZoneProcessPlayerProfile` in
  `src/client/zone/zone_entry.cpp`. This is a `DocCitations` target.
- `refactor/BOARD.md:198` (B025, the eight oversized methods) — `eq.cpp` was
  already stale for `DispatchZonePacket` (J02 moved it); repointed to
  `src/client/zone/zone_dispatch.cpp`.
- `CMakeLists.txt:334`'s comment naming `eq_zone.cpp`.

`DocCitations` passes.

### ⚠ §Mechanism 7 test file-list edit: `test_duplicate_state_stores.cpp`

**This is the edit §Mechanism 7 predicted ("K01 needed no file-list edit, but a
later family might") and it landed here.** After 41 members left `eq.h`, the
guard's own sanity floor tripped:

```
Expected: (eqMembers.size()) > (100u), actual: 71 vs 100
eq.h member parse collapsed -- the regex, not the code, is wrong
```

The regex had not collapsed; the header had. The fix is **the same widening
`ReachabilitySweep.EverySenderHasACaller` took at K05** (correction k): the
sweep's INPUT set is now `include/client/eq.h` PLUS every
`include/client/**/*_component.h`, so it keeps watching the members Batch K
moves out instead of silently shrinking as the batch proceeds. **No assertion
changed** — `kKnownRemaining`, `kNotDuplicates` and both directions of the
overlap check are untouched, and the widened sweep still reports exactly the
one documented exception (`m_character` ↔ `PlayerState::m_name`). It is
strictly a stronger guard than before: the nine extracted components' members
are now swept for duplicate state stores too, and none is.

Reported rather than silently absorbed, per §Mechanism 7. Every unit from K11
on inherits the widened form and needs no further edit.

### Files

| file | lines |
|---|---|
| `include/client/eq.h` | 1,441 → **1,226** (−215) |
| `src/client/eq_zone.cpp` → `src/client/zone/zone_component.cpp` | 1,092 (unchanged count) |
| `src/client/eq_zone_entry.cpp` → `src/client/zone/zone_entry.cpp` | 1,057 (unchanged count) |
| `src/client/eq_zone_dispatch.cpp` → `src/client/zone/zone_dispatch.cpp` | 958 (unchanged count) |
| `src/client/eq_zone_handshake.cpp` → `src/client/zone/zone_handshake.cpp` | 349 → **320** (the two dead senders) |
| `include/client/zone/zone_component.h` | **347 (new)** |
| `tests/test_zone_component.cpp` | **128 (new)** |
| `src/client/eq.cpp` | 1,079 → **1,081** (+2 ctor lines) |

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. Three attempts, none timed out (no exit 143, so no truncation scan was needed): (1) the one-pass declaration-deletion + precomputed retarget, exit 2, **exactly ONE error** (`EverQuest::kZoneOutHandshakeTimeoutMs`); (2) exit 0; (3) exit 0 with `test_zone_component` added |
| `ctest --test-dir build -j6 --output-on-failure` | **3,571 / 3,572 passed**, 17 skipped, 119 s. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling`, `DocCitations`, all 5 `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` (Test #3567, 21.20 s, inside this same run) all **PASS**. One intermediate run had a second failure, `DuplicateStateStores` on the member floor above; fixed in-unit and re-run green |
| `tests/test_zone_component.cpp` (new) | **9/9 pass**: `ZoneReturnsTheSameComponentEveryTime`, `FreshClientHasNoZoneHandshakeState`, `GmModeStartsFalseAndItsAccessorMoved`, `ZoningEnabledRoundTripsThroughTheComponent`, `ZoneOutSeamsReadTheComponentsOwnFlags`, `ZoneChangeRequestSeamMovedWithItsStruct`, `TransitionGettersAreReferencesIntoTheComponent`, `PathSettersMovedWithTheirMembers`, `TheReplayCounterAndLogoutSeamStayedOnEverQuest` |
| retargeted suites | `test_zone_connection_flow` **35**, `test_eq_action_handler_transforms_real` **28**, `test_scribe_scroll` **21**, `test_eq_action_handler_movement_utility` **16**, `test_zone_position_convention` **15**, `test_guild_wire_rank` **10**, `test_position_store_coherence` **9**, `test_group_packet_shapes` **9**, `test_zone_out_handshake` **8** — **all pass, every assertion untouched.** STOP condition 2 did not fire |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate (standalone) / `bash scripts/build-arm-noble.sh` | **SKIPPED, with evidence**: `git diff --cached --name-only -M -- src/client/graphics include/client/graphics` is **empty** — this unit changed no graphics file. Goldens untouched. `visual_gate` nonetheless ran and PASSED inside the full ctest run above |
| `bash scripts/refactor/check_file_sizes.sh --all` | **819 ok, 0 warn, 0 FAIL** with the ratchet lowered to 1,226 |

### Corrections to §Mechanism

1. **A family can own its PACKET HANDLING without owning its SOCKET, and
   §Mechanism should name that as a third cut shape.** K03 established "some
   bodies stay behind by role"; K08 generalised it to a property. K09 is the
   first family where the stay-behind set is not bodies at all but a
   *boundary*: `ConnectToZone`, the four `ZoneOn*` callbacks, `ReplayZonePacket`
   and `SafeQueueZonePacket` stay on `EverQuest` because four other families'
   senders call `SafeQueueZonePacket` and because the callbacks are what the
   `DaybreakConnectionManager` was handed. The component owns everything the
   socket *delivers*. K10 took the opposite decision for the same shape (it
   pulled `ConnectToLogin`/`ConnectToWorld` in) and both are right: **the test
   is whether the wiring is used by ONE family or by many.** login/world
   sockets are login_world's alone; the zone socket is the whole client's.

2. **The `this` → `&owner_` substitution (K06/K07 correction n) has a second,
   different case the correction does not describe: a `std::bind` whose bound
   METHOD stayed behind.** Movement's twenty sites were function-static
   `std::map<EverQuest*,…>` throttle tables — `this` as an identity key. K09's
   two are `std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, this, …)`,
   where `&EverQuest::Method` is correct and unchanged and only the instance
   argument rebases. Mechanically identical, semantically not; a unit that
   greps for the identity-key shape will miss it. State it as: **every bare
   `this` in a moved body becomes `&owner_`, whatever it is doing.**

3. **Deleting a zero-caller method can strand a member, and the two rules have
   to be applied in that order.** `ZoneSendSessionReady` was the sole reader of
   `m_zone_sequence`. Had the member table been settled first (as §Mechanism 3
   suggests, before §Mechanism 5's deletes), `m_zone_sequence` would have moved
   into the component as dead state and nothing would ever have noticed.
   §Mechanism should say: **resolve zero-caller deletions BEFORE the member
   single/shared decision, then re-measure.**

4. **An unguarded wire send is a reachability tell.** Both deleted senders
   dereference `m_zone_connection` with no null check, while every live sender
   in the same file carries the replay guard. That asymmetry — not the caller
   grep — is the cheap signal, and it is checkable by inspection in a family
   that has a replay harness. Worth a one-line sweep in a later unit: any
   `Send*` that touches a connection without the guard has probably never run
   under `ReplayZonePacket`.

5. **§Mechanism 7's predicted test file-list edit finally landed** — see the
   section above. The fix is K05 correction (k) applied to the second text
   guard; both now read `eq.h` PLUS `include/client/**/*_component.h`, and no
   later unit needs to touch either.

6. **No other STOP condition was hit.** No moved body needed an edit beyond
   the four substitutions §Mechanism 4 allows; no test asserted on a member by
   name that could not be repointed; the predicted delta was −218 before any
   edit and the measured delta is −215.

### Left for the board

- **`EverQuest::ZoneSendSessionReady()` and `EverQuest::ZoneSendSpawnAppearance()`
  deleted** — zero callers tree-wide, in-family or out, and both would have
  segfaulted under the replay harness (no null-connection guard). The
  reachability pattern's next two instances, and the zone-stream twin of K10's
  `WorldSendSessionReady`.
- **`m_zone_sequence` is gone** — the zone stream's outbound sequence counter,
  read only by the dead `ZoneSendSessionReady`. Nothing else in the client
  sequences zone packets at the application layer (Daybreak does it below).
- **`m_spawn_appearance_sent` and `m_client_spawned` are WRITE-ONLY** — each
  now has exactly one reference, a `= false` in `CleanupZone`. `m_client_spawned`
  was already in that state before this unit; `m_spawn_appearance_sent` became
  so when its only reader was deleted. Both moved (they are referenced, so
  §Mechanism 3's zero-reference rule does not reach them) and are marked in
  `zone_component.h`. Deleting them is a behaviour-neutral edit outside a
  structural unit's licence.
- **`m_replay_suppressed_sends` is the replay harness's, not zone's.** Twelve
  of its writes are login_world's, and its public accessor has 49 test call
  sites. It stays on `EverQuest` (deviation, above). It is the best remaining
  candidate for a `ClientContext` service in K13, alongside K03's `AddChat*`
  trio and K08's player-state facade.
- **`m_movement_history` / `m_last_movement_history_send` now live on
  `ZoneComponent` but are read 11 times by `movement/` and 3 times here.** The
  plan put them in this unit because zone is the last family to extract, and
  §Mechanism 3's rule is mechanical. K13 should consider re-homing them into
  `MovementComponent`, where `SendMovementHistory` lives; the only zone use is
  `ZoneSendZoneEntry` seeding the first entry.
- **`m_exp` / `m_exp_aa` are player experience on a zone component**, for the
  same mechanical reason. `entities_component_updates.cpp` (OP_ExpUpdate) writes
  them and `EverQuest::GetLoadingProgress` reads `m_exp`. K13.
